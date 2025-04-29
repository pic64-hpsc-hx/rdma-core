// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2023-2024, Grovf, Inc. All rights reserved.
 */

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/param.h> /* For MAX macro */
#include <infiniband/driver.h>
#include <util/mmio.h>
#if 1
/* FIXME */
#include <netinet/in.h>
#define _LINUX_IN_H
#define _LINUX_IN6_H
#include "grvf-abi.h"
#endif
#include "grvf.h"
#include "grvf_util.h"
#include "grvf_ip.h"
#include "grvf_post.h"
#include "grvfdv.h"

static struct ibv_mw *grvf_alloc_mw(struct ibv_pd *pd, enum ibv_mw_type type)
{
	struct ibv_mw *mw;
	struct ibv_alloc_mw cmd;
	struct ib_uverbs_alloc_mw_resp resp;
	int ret;

	mw = calloc(1, sizeof(*mw));
	if (!mw)
		return NULL;

	ret = ibv_cmd_alloc_mw(pd, type, mw, &cmd, sizeof(cmd), &resp,
			       sizeof(resp));
	if (ret) {
		verbs_err(verbs_get_ctx(pd->context),
			  "Failed to allocate MW\n");
		errno = ret;
		free(mw);
		return NULL;
	}
	return mw;
}

static struct ibv_pd *grvf_alloc_pd(struct ibv_context *context)
{
	struct ibv_pd *pd;
	struct ibv_alloc_pd cmd;
	struct ib_uverbs_alloc_pd_resp resp;
	int ret;

	pd = calloc(1, sizeof(*pd));
	if (!pd)
		return NULL;

	ret = ibv_cmd_alloc_pd(context, pd, &cmd, sizeof(cmd), &resp,
			       sizeof(resp));
	if (ret) {
		verbs_err(verbs_get_ctx(context), "Failed to allocate PD\n");
		errno = ret;
		free(pd);
		return NULL;
	}
	return pd;
}

static int grvf_post_send(struct ibv_qp *qp, struct ibv_send_wr *wr,
			  struct ibv_send_wr **bad_wr);

static int grvf_bind_mw(struct ibv_qp *qp, struct ibv_mw *mw,
			struct ibv_mw_bind *mw_bind)
{
	struct ibv_mw_bind_info *bind_info = &mw_bind->bind_info;
	struct ibv_send_wr wr = {};
	struct ibv_send_wr *bad_wr = NULL;
	int ret;

	if (bind_info->mw_access_flags & IBV_ACCESS_ZERO_BASED) {
		errno = EINVAL;
		return errno;
	}

	if (bind_info->mr) {
		if (verbs_get_mr(bind_info->mr)->mr_type != IBV_MR_TYPE_MR) {
			errno = ENOTSUP;
			return errno;
		}
#if 0
		// TODO:
		if (bind_info->mr->alloc_flags & IBV_ACCESS_ZERO_BASED) {
			errno = EINVAL;
			return errno;
		}
#endif
	}

	wr.opcode = IBV_WR_BIND_MW;
	wr.next = NULL;
	wr.wr_id = mw_bind->wr_id;
	wr.send_flags = mw_bind->send_flags;
	wr.bind_mw.bind_info = mw_bind->bind_info;
	wr.bind_mw.mw = mw;
	wr.bind_mw.rkey = ibv_inc_rkey(mw->rkey);

	ret = grvf_post_send(qp, &wr, &bad_wr);
	if (ret)
		return ret;

	mw->rkey = wr.bind_mw.rkey;
	return 0;
}

static struct ibv_ah *grvf_create_ah(struct ibv_pd *pd,
				     struct ibv_ah_attr *attr)
{
	struct grvf_ah *ah;
	struct grvf_create_ah_resp resp;
	union ibv_gid sgid;
	int ret;

	ah = calloc(1, sizeof(*ah));
	if (!ah)
		return NULL;

	ret = ibv_cmd_create_ah(pd, &ah->ibv_ah, attr, &resp.ibv_resp,
				sizeof(resp));
	if (ret) {
		verbs_err(verbs_get_ctx(pd->context), "Failed to create AH\n");
		errno = ret;
		free(ah);
		return NULL;
	}

	ret = ibv_query_gid(pd->context, attr->port_num, attr->grh.sgid_index,
			    &sgid);
	if (ret) {
		verbs_err(verbs_get_ctx(pd->context), "Failed to query GID\n");
		errno = ret;
		free(ah);
		return NULL;
	}

	ah->av.port_num = attr->port_num;
	ah->av.network_type = IN6_IS_ADDR_V4MAPPED(attr->grh.dgid.raw) ?
				      GRVF_NETWORK_TYPE_IPV4 :
				      GRVF_NETWORK_TYPE_IPV6;
	memcpy(&ah->av.grh, &attr->grh, sizeof(attr->grh));
	rdma_gid2ip(&ah->av.sgid_addr, &sgid);
	rdma_gid2ip(&ah->av.dgid_addr, &attr->grh.dgid);

	ret = ibv_resolve_eth_l2_from_gid(pd->context, attr, ah->av.dmac, NULL);
	if (ret) {
		verbs_err(verbs_get_ctx(pd->context),
			  "Failed to resolve eth address\n");
		errno = ret;
		free(ah);
		return NULL;
	}
	return &ah->ibv_ah;
}

static struct ibv_cq *grvf_create_cq(struct ibv_context *context, int cqe,
				     struct ibv_comp_channel *channel,
				     int comp_vector)
{
	struct grvf_cq *cq;
	struct grvf_create_cq_resp resp;
	int ret;

	cq = calloc(1, sizeof(*cq));
	if (!cq)
		return NULL;

	ret = ibv_cmd_create_cq(context, cqe, channel, comp_vector, &cq->vcq.cq,
				NULL, 0, &resp.ibv_resp, sizeof(resp));
	if (ret) {
		verbs_err(verbs_get_ctx(context), "Failed to create CQ\n");
		errno = ret;
		free(cq);
		return NULL;
	}

	cq->size_mask = upper_power_of_two(cqe) - 1;

	cq->dev = mmap(NULL, resp.mi.size, PROT_READ | PROT_WRITE, MAP_SHARED,
		       context->cmd_fd, resp.mi.offset);
	if ((void *)cq->dev == MAP_FAILED) {
		verbs_err(verbs_get_ctx(context),
			  "Failed mmap for create CQ\n");
		ibv_cmd_destroy_cq(&cq->vcq.cq);
		free(cq);
		return NULL;
	}

	cq->mmap_info = resp.mi;
	pthread_spin_init(&cq->lock, PTHREAD_PROCESS_PRIVATE);

	return &cq->vcq.cq;
}

static int map_queue_pair(int cmd_fd, struct grvf_qp *qp,
			  struct ibv_qp_init_attr *attr,
			  struct grvf_ib_create_qp_resp *resp)
{
	if (attr->srq) {
		qp->rq.wq.max_sge = 0;
		qp->rq.wq.dev = NULL;
		qp->rq.wq.db_dev = NULL;
		qp->rq.mmap_info.size = 0;
	} else {
		qp->rq.wq.max_sge = attr->cap.max_recv_sge;
		qp->rq.wq.dev =
			mmap(NULL, resp->rq.mi.size, PROT_READ | PROT_WRITE,
			     MAP_SHARED, cmd_fd, resp->rq.mi.offset);
		if ((void *)qp->rq.wq.dev == MAP_FAILED)
			return errno;

		qp->rq.mmap_info = resp->rq.mi;
		qp->rq.wq.db_dev =
			mmap(NULL, resp->rq.db_mi.size, PROT_READ | PROT_WRITE,
			     MAP_SHARED, cmd_fd, resp->rq.db_mi.offset);
		if ((void *)qp->rq.wq.db_dev == MAP_FAILED) {
			munmap(qp->rq.wq.dev, qp->rq.mmap_info.size);
			return errno;
		}
		qp->rq.db_mmap_info = resp->rq.db_mi;

		pthread_spin_init(&qp->rq.wq.lock, PTHREAD_PROCESS_PRIVATE);
	}
	qp->sq.wq.max_sge = attr->cap.max_send_sge;
	qp->sq.wq.max_inline = attr->cap.max_inline_data;
	qp->sq.wq.dev = mmap(NULL, resp->sq.mi.size, PROT_READ | PROT_WRITE,
			     MAP_SHARED, cmd_fd, resp->sq.mi.offset);
	if ((void *)qp->sq.wq.dev == MAP_FAILED) {
		if (qp->rq.mmap_info.size) {
			munmap(qp->rq.wq.dev, qp->rq.mmap_info.size);
			munmap(qp->rq.wq.db_dev, qp->rq.db_mmap_info.size);
		}
		return errno;
	}

	qp->sq.mmap_info = resp->sq.mi;
	qp->sq.wq.db_dev =
		mmap(NULL, resp->sq.db_mi.size, PROT_READ | PROT_WRITE,
		     MAP_SHARED, cmd_fd, resp->sq.db_mi.offset);
	if ((void *)qp->sq.wq.db_dev == MAP_FAILED) {
		if (qp->rq.mmap_info.size) {
			munmap(qp->rq.wq.dev, qp->rq.mmap_info.size);
			munmap(qp->rq.wq.db_dev, qp->rq.db_mmap_info.size);
		}
		munmap(qp->sq.wq.dev, qp->sq.mmap_info.size);
		return errno;
	}

	qp->sq.db_mmap_info = resp->sq.db_mi;
	qp->sq.num = resp->sq.num;
	qp->rq.num = resp->rq.num;

	pthread_spin_init(&qp->sq.wq.lock, PTHREAD_PROCESS_PRIVATE);

	qp->cur_qp_state = IBV_QPS_RESET;
	qp->qp_type = attr->qp_type;
	qp->sq.prod = 0;
	qp->sq.wr_hdr_size = (attr->qp_type == IBV_QPT_UD) ? 5 : 3;
	qp->sq.wr_count_mask =
		upper_power_of_two(MAX(2, attr->cap.max_send_wr)) - 1;
	qp->sq.wr_size_elements =
		upper_power_of_two(qp->sq.wq.max_sge + qp->sq.wr_hdr_size);
	qp->sq.wr_size = sizeof(union sq_entry_t) * qp->sq.wr_size_elements;
	qp->rq.prod = 0;
	qp->rq.wr_size =
		sizeof(union rq_entry_t) * (attr->cap.max_recv_sge + 1);
	qp->rq.wr_count_mask = upper_power_of_two(attr->cap.max_recv_wr) - 1;
	qp->path_mtu = IBV_MTU_1024;

	return 0;
}

static struct ibv_qp *grvf_create_qp(struct ibv_pd *pd,
				     struct ibv_qp_init_attr *attr)
{
	struct grvf_qp *qp;
	struct ibv_create_qp cmd;
	struct grvf_create_qp_resp resp;
	int ret;

	qp = calloc(1, sizeof(*qp));
	if (!qp)
		return NULL;

	ret = ibv_cmd_create_qp(pd, &qp->vqp.qp, attr, &cmd, sizeof(cmd),
				&resp.ibv_resp, sizeof(resp));
	if (ret) {
		verbs_err(verbs_get_ctx(pd->context), "Failed to create QP\n");
		errno = ret;
		free(qp);
		return NULL;
	}

	ret = map_queue_pair(pd->context->cmd_fd, qp, attr, &resp.drv_payload);
	if (ret) {
		verbs_err(verbs_get_ctx(pd->context), "Failed mapping QP\n");
		ibv_cmd_destroy_qp(&qp->vqp.qp);
		free(qp);
		return NULL;
	}

	qp->inline_buf = malloc(attr->cap.max_inline_data);
	if (!qp->inline_buf) {
		ibv_cmd_destroy_qp(&qp->vqp.qp);
		free(qp);
		return NULL;
	}

	return &qp->vqp.qp;
}

static struct ibv_srq *grvf_create_srq(struct ibv_pd *pd,
				       struct ibv_srq_init_attr *srq_init_attr)
{
	struct grvf_srq *srq;
	struct ibv_create_srq cmd;
	struct grvf_create_srq_resp resp;
	int ret;

	srq = calloc(1, sizeof(*srq));
	if (!srq)
		return NULL;

	ret = ibv_cmd_create_srq(pd, &srq->vsrq.srq, srq_init_attr, &cmd,
				 sizeof(cmd), &resp.ibv_resp, sizeof(resp));
	if (ret) {
		verbs_err(verbs_get_ctx(pd->context), "Failed to create SRQ\n");
		errno = ret;
		free(srq);
		return NULL;
	}

	srq->rq.mmap_info = resp.mi;
	srq->rq.wq.dev =
		mmap(NULL, srq->rq.mmap_info.size, PROT_READ | PROT_WRITE,
		     MAP_SHARED, pd->context->cmd_fd, srq->rq.mmap_info.offset);
	if ((void *)srq->rq.wq.dev == MAP_FAILED) {
		verbs_err(verbs_get_ctx(pd->context),
			  "Failed mmap for create SRQ\n");
		ibv_cmd_destroy_srq(&srq->vsrq.srq);
		free(srq);
		return NULL;
	}

	srq->rq.db_mmap_info = resp.db_mi;
	srq->rq.wq.db_dev =
		mmap(NULL, srq->rq.db_mmap_info.size, PROT_READ | PROT_WRITE,
		     MAP_SHARED, pd->context->cmd_fd,
		     srq->rq.db_mmap_info.offset);
	if ((void *)srq->rq.wq.db_dev == MAP_FAILED) {
		verbs_err(verbs_get_ctx(pd->context),
			  "Failed mmap for create SRQ (DB)\n");
		munmap(srq->rq.wq.dev, srq->rq.mmap_info.size);
		ibv_cmd_destroy_srq(&srq->vsrq.srq);
		free(srq);
		return NULL;
	}

	srq->rq.num = resp.num;
	srq->rq.wq.max_sge = srq_init_attr->attr.max_sge;
	pthread_spin_init(&srq->rq.wq.lock, PTHREAD_PROCESS_PRIVATE);
	srq->rq.prod = 0;
	srq->rq.wr_size =
		sizeof(union rq_entry_t) * (srq_init_attr->attr.max_sge + 1);
	srq->rq.wr_count_mask =
		upper_power_of_two(srq_init_attr->attr.max_wr) - 1;
	return &srq->vsrq.srq;
}

static int grvf_dealloc_mw(struct ibv_mw *mw)
{
	struct verbs_context *ctx = verbs_get_ctx(mw->context);
	int ret;

	ret = ibv_cmd_dealloc_mw(mw);
	if (ret)
		verbs_err(ctx, "Failed to deallocate MW\n");
	else
		free(mw);

	return ret;
}

static int grvf_dealloc_pd(struct ibv_pd *pd)
{
	struct verbs_context *ctx = verbs_get_ctx(pd->context);
	int ret;

	ret = ibv_cmd_dealloc_pd(pd);
	if (ret)
		verbs_err(ctx, "Failed to deallocate PD\n");
	else
		free(pd);

	return ret;
}

static int grvf_dereg_mr(struct verbs_mr *vmr)
{
	struct verbs_context *ctx = verbs_get_ctx(vmr->ibv_mr.context);
	int ret;

	ret = ibv_cmd_dereg_mr(vmr);
	if (ret)
		verbs_err(ctx, "Failed to deregister MR\n");
	else
		free(vmr);

	return ret;
}

static int grvf_destroy_ah(struct ibv_ah *ah)
{
	struct grvf_ah *p = container_of(ah, struct grvf_ah, ibv_ah);
	struct verbs_context *ctx = verbs_get_ctx(ah->context);
	int ret;

	ret = ibv_cmd_destroy_ah(ah);
	if (ret)
		verbs_err(ctx, "Failed to destroy AH\n");
	else
		free(p);

	return ret;
}

static int grvf_destroy_cq(struct ibv_cq *cq)
{
	struct grvf_cq *p = container_of(cq, struct grvf_cq, vcq.cq);
	struct verbs_context *ctx = verbs_get_ctx(cq->context);
	int ret;

	ret = ibv_cmd_destroy_cq(cq);
	if (ret) {
		verbs_err(ctx, "Failed to destroy CQ\n");
		return ret;
	}

	if (p->mmap_info.size)
		munmap(p->dev, p->mmap_info.size);
	free(p);
	return 0;
}

static int grvf_destroy_qp(struct ibv_qp *qp)
{
	struct grvf_qp *p = container_of(qp, struct grvf_qp, vqp.qp);
	struct verbs_context *ctx = verbs_get_ctx(qp->context);
	int ret;

	ret = ibv_cmd_destroy_qp(qp);
	if (ret) {
		verbs_err(ctx, "Failed to destroy QP\n");
		return ret;
	}

	if (p->rq.mmap_info.size)
		munmap(p->rq.wq.dev, p->rq.mmap_info.size);
	if (p->rq.db_mmap_info.size)
		munmap(p->rq.wq.db_dev, p->rq.db_mmap_info.size);
	if (p->sq.mmap_info.size)
		munmap(p->sq.wq.dev, p->sq.mmap_info.size);
	if (p->sq.db_mmap_info.size)
		munmap(p->sq.wq.db_dev, p->sq.db_mmap_info.size);

	free(p);
	return 0;
}

static int grvf_destroy_srq(struct ibv_srq *srq)
{
	struct grvf_srq *p = container_of(srq, struct grvf_srq, vsrq.srq);
	struct verbs_context *ctx = verbs_get_ctx(srq->context);
	int ret;

	ret = ibv_cmd_destroy_srq(srq);
	if (ret) {
		verbs_err(ctx, "Failed to destroy SRQ\n");
		return ret;
	}

	if (p->rq.mmap_info.size)
		munmap(p->rq.wq.dev, p->rq.mmap_info.size);
	if (p->rq.db_mmap_info.size)
		munmap(p->rq.wq.db_dev, p->rq.db_mmap_info.size);

	free(p);
	return 0;
}

static void grvf_free_context(struct ibv_context *context)
{
	struct grvf_context *ctx =
		container_of(context, struct grvf_context, ibv_ctx.context);

	verbs_uninit_context(&ctx->ibv_ctx);
	free(ctx);
}

static int grvf_modify_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr,
			  int attr_mask)
{
	struct grvf_qp *p = container_of(qp, struct grvf_qp, vqp.qp);
	struct verbs_context *ctx = verbs_get_ctx(qp->context);
	struct ibv_modify_qp cmd;
	int ret;

	ret = ibv_cmd_modify_qp(qp, attr, attr_mask, &cmd, sizeof(cmd));
	if (ret) {
		verbs_err(ctx, "Failed to modify QP\n");
		return ret;
	}

	if (attr_mask & IBV_QP_PATH_MTU) {
		p->path_mtu = attr->path_mtu;
	}

	p->cur_qp_state = attr->qp_state;
	p->sq.psn = attr->sq_psn;

	return 0;
}

static int grvf_modify_srq(struct ibv_srq *srq, struct ibv_srq_attr *srq_attr,
			   int srq_attr_mask)
{
	struct grvf_srq *p = container_of(srq, struct grvf_srq, vsrq.srq);
	struct verbs_context *ctx = verbs_get_ctx(srq->context);
	struct grvf_modify_srq cmd;
	struct grvf_mminfo mi = {};
	int ret;

	if (srq_attr_mask & IBV_SRQ_MAX_WR)
		pthread_spin_lock(&p->rq.wq.lock);

	cmd.mmap_info_addr = (uintptr_t)&mi;
	ret = ibv_cmd_modify_srq(srq, srq_attr, srq_attr_mask, &cmd.ibv_cmd,
				 sizeof(cmd));
	if (ret)
		verbs_err(ctx, "Failed to modify SRQ\n");

	if (srq_attr_mask & IBV_SRQ_MAX_WR)
		pthread_spin_unlock(&p->rq.wq.lock);
	return ret;
}

#define READ_CQ(entry, member, cq)                                             \
	(entry.member) =                                                       \
		mmio_read64(&((cq)->dev[((cq)->cons << CQ_ENTRY_STRIDE_BITS) + \
					offsetof(typeof(entry), member)]))

#define WRITE_CQ(entry, member, cq, val)                                       \
	mmio_write64(&((cq)->dev[((cq)->cons << CQ_ENTRY_STRIDE_BITS) +        \
				 offsetof(typeof(entry), member)]),            \
		     (val))

static int grvf_poll_cq(struct ibv_cq *cq, int num_entries, struct ibv_wc *wc)
{
	struct grvf_cq *q = container_of(cq, struct grvf_cq, vcq.cq);
	struct cq_entry_t wc_dev;
	int npolled;

	pthread_spin_lock(&q->lock);

	for (npolled = 0; npolled < num_entries; ++npolled, ++wc) {
		READ_CQ(wc_dev, raw.qw4, q);
		if (!(wc_dev.raw.flag & CQ_FLAG_RDY))
			break;

		READ_CQ(wc_dev, raw.qw0, q);
		READ_CQ(wc_dev, raw.qw1, q);
		READ_CQ(wc_dev, raw.qw2, q);
		READ_CQ(wc_dev, raw.qw3, q);

		wc->wr_id = wc_dev.wc.wr_id;
		wc->status = (enum ibv_wc_status)wc_dev.wc.status;
		wc->opcode = (enum ibv_wc_opcode)wc_dev.wc.opcode;
		wc->vendor_err = wc_dev.wc.verndor_err;
		wc->byte_len = wc_dev.wc.byte_len;

		//this moves inv_key as well
		wc->imm_data = wc_dev.wc.imm_data;

		wc->qp_num = wc_dev.wc.qp_num;
		wc->src_qp = wc_dev.wc.src_qp;
		wc->wc_flags = wc_dev.wc.wc_flags;
		wc->pkey_index = wc_dev.wc.pkey_index;

		WRITE_CQ(wc_dev, raw.qw4, q, 0);

		++q->cons;
		q->cons &= q->size_mask;
	}

	pthread_spin_unlock(&q->lock);

	return npolled;
}

static int grvf_post_recv(struct ibv_qp *qp, struct ibv_recv_wr *wr,
			  struct ibv_recv_wr **bad_wr)
{
	struct grvf_qp *q = container_of(qp, struct grvf_qp, vqp.qp);
	int ret = 0;
	int nreq;

	pthread_spin_lock(&q->rq.wq.lock);

	for (nreq = 0; wr; ++nreq, wr = wr->next) {
		ret = grvf_post_one_recv(&q->rq, wr);
		if (ret) {
			ret = EINVAL;
			*bad_wr = wr;
			break;
		}
	}

	pthread_spin_unlock(&q->rq.wq.lock);

	grvf_post_recv_db(&q->rq, nreq);
	return ret;
}

static int grvf_post_send(struct ibv_qp *qp, struct ibv_send_wr *wr,
			  struct ibv_send_wr **bad_wr)
{
	struct grvf_qp *q = container_of(qp, struct grvf_qp, vqp.qp);
	int ret = 0;
	int nreq;

	pthread_spin_lock(&q->sq.wq.lock);

	for (nreq = 0; wr; ++nreq, wr = wr->next) {
		ret = grvf_post_one_send(q, wr);
		if (ret) {
			ret = EINVAL;
			*bad_wr = wr;
			break;
		}
	}

	pthread_spin_unlock(&q->sq.wq.lock);

	grvf_post_send_db(&q->sq, nreq);
	return ret;
}

static int grvf_post_srq_recv(struct ibv_srq *srq, struct ibv_recv_wr *recv_wr,
			      struct ibv_recv_wr **bad_recv_wr)
{
	struct grvf_srq *q = container_of(srq, struct grvf_srq, vsrq.srq);
	int ret = 0;
	int nreq;

	pthread_spin_lock(&q->rq.wq.lock);

	for (nreq = 0; recv_wr; ++nreq, recv_wr = recv_wr->next) {
		ret = grvf_post_one_recv(&q->rq, recv_wr);
		if (ret) {
			ret = EINVAL;
			*bad_recv_wr = recv_wr;
			break;
		}
	}

	pthread_spin_unlock(&q->rq.wq.lock);

	grvf_post_recv_db(&q->rq, nreq);
	return ret;
}

static int grvf_query_device_ex(struct ibv_context *context,
				const struct ibv_query_device_ex_input *input,
				struct ibv_device_attr_ex *attr,
				size_t attr_size)
{
	struct ib_uverbs_ex_query_device_resp resp;
	size_t resp_size = sizeof(resp);
	uint64_t raw_fw_ver;
	unsigned int major, minor, sub_minor;
	int ret;

	ret = ibv_cmd_query_device_any(context, input, attr, attr_size, &resp,
				       &resp_size);
	if (ret) {
		verbs_err(verbs_get_ctx(context), "Failed to query device\n");
		return ret;
	}

	raw_fw_ver = resp.base.fw_ver;
	major = (raw_fw_ver >> 32) & 0xffff;
	minor = (raw_fw_ver >> 16) & 0xffff;
	sub_minor = raw_fw_ver & 0xffff;

	snprintf(attr->orig_attr.fw_ver, sizeof(attr->orig_attr.fw_ver),
		 "%d.%d.%d", major, minor, sub_minor);
	return 0;
}

static int grvf_query_port(struct ibv_context *context, uint8_t port_num,
			   struct ibv_port_attr *port_attr)
{
	struct ibv_query_port cmd;

	return ibv_cmd_query_port(context, port_num, port_attr, &cmd,
				  sizeof(cmd));
}

static int grvf_query_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr,
			 int attr_mask, struct ibv_qp_init_attr *init_attr)
{
	struct ibv_query_qp cmd;

	return ibv_cmd_query_qp(qp, attr, attr_mask, init_attr, &cmd,
				sizeof(cmd));
}

static int grvf_query_srq(struct ibv_srq *srq, struct ibv_srq_attr *srq_attr)
{
	struct ibv_query_srq cmd;

	return ibv_cmd_query_srq(srq, srq_attr, &cmd, sizeof(cmd));
}

static struct ibv_mr *grvf_reg_mr(struct ibv_pd *pd, void *addr, size_t length,
				  uint64_t hca_va, int access)
{
	struct verbs_mr *vmr;
	struct ibv_reg_mr cmd;
	struct ib_uverbs_reg_mr_resp resp;
	int ret;

	vmr = calloc(1, sizeof(*vmr));
	if (!vmr)
		return NULL;

	ret = ibv_cmd_reg_mr(pd, addr, length, hca_va, access, vmr, &cmd,
			     sizeof(cmd), &resp, sizeof(resp));
	if (ret) {
		verbs_err(verbs_get_ctx(pd->context),
			  "Failed to register MR\n");
		errno = ret;
		free(vmr);
		return NULL;
	}

	return &vmr->ibv_mr;
}

static const struct verbs_context_ops grvf_ctx_ops = {
	.alloc_mw = grvf_alloc_mw,
	.alloc_pd = grvf_alloc_pd,
	.attach_mcast = ibv_cmd_attach_mcast,
	.bind_mw = grvf_bind_mw,
	.create_ah = grvf_create_ah,
	.create_cq = grvf_create_cq,
	.create_qp = grvf_create_qp,
	.create_srq = grvf_create_srq,
	.dealloc_mw = grvf_dealloc_mw,
	.dealloc_pd = grvf_dealloc_pd,
	.dereg_mr = grvf_dereg_mr,
	.destroy_ah = grvf_destroy_ah,
	.destroy_cq = grvf_destroy_cq,
	.destroy_qp = grvf_destroy_qp,
	.destroy_srq = grvf_destroy_srq,
	.detach_mcast = ibv_cmd_detach_mcast,
	.free_context = grvf_free_context,
	.modify_qp = grvf_modify_qp,
	.modify_srq = grvf_modify_srq,
	.poll_cq = grvf_poll_cq,
	.post_recv = grvf_post_recv,
	.post_send = grvf_post_send,
	.post_srq_recv = grvf_post_srq_recv,
	.query_device_ex = grvf_query_device_ex,
	.query_port = grvf_query_port,
	.query_qp = grvf_query_qp,
	.query_srq = grvf_query_srq,
	.reg_mr = grvf_reg_mr,
	.req_notify_cq = ibv_cmd_req_notify_cq,
};

static struct verbs_context *grvf_alloc_context(struct ibv_device *device,
						int cmd_fd, void *private_data)
{
	int ret;
	struct grvf_context *context;
	struct ibv_get_context cmd;
	struct ib_uverbs_get_context_resp resp;

	context = verbs_init_and_alloc_context(device, cmd_fd, context, ibv_ctx,
					       RDMA_DRIVER_GRVF);
	if (!context)
		return NULL;

	ret = ibv_cmd_get_context(&context->ibv_ctx, &cmd, sizeof(cmd), NULL,
				  &resp, sizeof(resp));
	if (ret) {
		verbs_err(&context->ibv_ctx,
			  "Failed to get ucontext, err: %d\n", ret);
		errno = ret;
		verbs_uninit_context(&context->ibv_ctx);
		free(context);
		return NULL;
	}

	verbs_set_ops(&context->ibv_ctx, &grvf_ctx_ops);

	verbs_debug(&context->ibv_ctx, "Allocate context successful\n", ret);
	return &context->ibv_ctx;
}

static struct verbs_device *grvf_alloc_device(struct verbs_sysfs_dev *sysfs_dev)
{
	struct grvf_device *dev;

	dev = calloc(1, sizeof(*dev));
	if (!dev)
		return NULL;

	dev->driver_abi_ver = sysfs_dev->abi_ver;
	return &dev->verbs_dev;
}

static void grvf_uninit_device(struct verbs_device *device)
{
	struct grvf_device *dev =
		container_of(device, struct grvf_device, verbs_dev);
	free(dev);
}

static const struct verbs_match_ent hca_table[] = {
	VERBS_DRIVER_ID(RDMA_DRIVER_GRVF),
	VERBS_MODALIAS_MATCH("rdma_device:Ngrvf_*", NULL),
	{},
};

static const struct verbs_device_ops grvf_dev_ops = {
	.name = "grvf",
	.match_min_abi_version = GRVF_ABI_VERSION,
	.match_max_abi_version = GRVF_ABI_VERSION,
	.match_table = hca_table,
	.alloc_device = grvf_alloc_device,
	.uninit_device = grvf_uninit_device,
	.alloc_context = grvf_alloc_context,
};

PROVIDER_DRIVER(grvf, grvf_dev_ops);
