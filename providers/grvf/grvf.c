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
#include <rdma/ib_user_ioctl_cmds.h>
#include <rdma/grvf_user_ioctl_cmds.h>
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
#include "grvf_bare.h"
#include "grvfdv.h"

#define READ_CQ(entry, member, cq)                                             \
	(entry.member) =                                                       \
		mmio_read64(&((cq)->dev[((cq)->cons << CQ_ENTRY_STRIDE_BITS) + \
					offsetof(typeof(entry), member)]))

#define WRITE_CQ(entry, member, cq, val)                                       \
	mmio_write64(&((cq)->dev[((cq)->cons << CQ_ENTRY_STRIDE_BITS) +        \
				 offsetof(typeof(entry), member)]),            \
		     (val))

static void grvf_async_event(struct ibv_context *context,
			     struct ibv_async_event *event)
{
	struct grvf_context *ctx;

	switch (event->event_type) {
	case IBV_EVENT_DEVICE_FATAL:
		ctx = to_gctx(context);
		ctx->ibv_active = false;
		break;
	default:
		break;
	}
}

static struct ibv_dm *grvf_alloc_dm(struct ibv_context *context,
				    struct ibv_alloc_dm_attr *attr)
{
	DECLARE_COMMAND_BUFFER(cmdb, UVERBS_OBJECT_DM, UVERBS_METHOD_DM_ALLOC,
			       0);
	struct grvf_dm *dm;
	int ret;

	dm = calloc(1, sizeof(*dm));
	if (!dm)
		return NULL;

	ret = ibv_cmd_alloc_dm(context, attr, &dm->vdm, cmdb);
	if (ret) {
		verbs_err(verbs_get_ctx(context), "Failed to allocate DM\n");
		errno = ret;
		free(dm);
		return NULL;
	}

	dm->length = attr->length;
	return &dm->vdm.dm;
}

static struct ibv_pd *grvf_alloc_pd(struct ibv_context *context)
{
	struct grvf_pd *pd;
	struct ibv_alloc_pd cmd;
	struct grvf_alloc_pd_resp resp;
	int ret;

	pd = calloc(1, sizeof(*pd));
	if (!pd)
		return NULL;

	ret = ibv_cmd_alloc_pd(context, &pd->ibvpd, &cmd, sizeof(cmd),
			       &resp.ibv_resp, sizeof(resp));
	if (ret) {
		verbs_err(verbs_get_ctx(context), "Failed to allocate PD\n");
		errno = ret;
		free(pd);
		return NULL;
	}

	pd->index = resp.index;

	return &pd->ibvpd;
}

static int grvf_post_send(struct ibv_qp *qp, struct ibv_send_wr *wr,
			  struct ibv_send_wr **bad_wr);

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

	memcpy(ah->av.smac, resp.smac, sizeof(resp.smac));
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

	cq->cons = 0;
	cq->size_mask = cq->vcq.cq.cqe - 1;

	if (upper_power_of_two(cq->size_mask) != cq->vcq.cq.cqe) {
		verbs_err(verbs_get_ctx(context), "Internal error\n");
		errno = EINVAL;
		ibv_cmd_destroy_cq(&cq->vcq.cq);
		free(cq);
		return NULL;
	}

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

static inline void wc_dev_to_uverbs_wc(struct cq_entry_t *wc_dev,
				       struct ib_uverbs_wc *wc)
{
	wc->wr_id = wc_dev->wc.wr_id;
	wc->status = (enum ibv_wc_status)wc_dev->wc.status;
	wc->opcode = (enum ibv_wc_opcode)wc_dev->wc.opcode;
	wc->vendor_err = wc_dev->wc.verndor_err;
	wc->byte_len = wc_dev->wc.byte_len;

	//this moves inv_key as well
	wc->ex.imm_data = wc_dev->wc.imm_data;

	wc->qp_num = wc_dev->wc.qp_num;
	wc->src_qp = wc_dev->wc.src_qp;
	wc->wc_flags = wc_dev->wc.wc_flags;
	wc->pkey_index = wc_dev->wc.pkey_index;
}

static int cq_start_poll(struct ibv_cq_ex *current,
			 struct ibv_poll_cq_attr *attr)
{
	struct grvf_cq *cq = container_of(current, struct grvf_cq, vcq.cq_ex);
	struct cq_entry_t wc_dev;

	pthread_spin_lock(&cq->lock);

	READ_CQ(wc_dev, raw.qw4, cq);
	if (!(wc_dev.raw.flag & CQ_FLAG_RDY)) {
		pthread_spin_unlock(&cq->lock);
		errno = ENOENT;
		return errno;
	}

	READ_CQ(wc_dev, raw.qw0, cq);
	READ_CQ(wc_dev, raw.qw1, cq);
	READ_CQ(wc_dev, raw.qw2, cq);
	READ_CQ(wc_dev, raw.qw3, cq);

	wc_dev_to_uverbs_wc(&wc_dev, &cq->wc);
	cq->vcq.cq_ex.status = (enum ibv_wc_status)wc_dev.wc.status;
	cq->vcq.cq_ex.wr_id = wc_dev.wc.wr_id;

	WRITE_CQ(wc_dev, raw.qw4, cq, 0);

	return 0;
}

static int cq_next_poll(struct ibv_cq_ex *current)
{
	struct grvf_cq *cq = container_of(current, struct grvf_cq, vcq.cq_ex);
	struct cq_entry_t wc_dev;

	++cq->cons;
	cq->cons &= cq->size_mask;

	READ_CQ(wc_dev, raw.qw4, cq);
	if (!(wc_dev.raw.flag & CQ_FLAG_RDY)) {
		pthread_spin_unlock(&cq->lock);
		errno = ENOENT;
		return errno;
	}

	READ_CQ(wc_dev, raw.qw0, cq);
	READ_CQ(wc_dev, raw.qw1, cq);
	READ_CQ(wc_dev, raw.qw2, cq);
	READ_CQ(wc_dev, raw.qw3, cq);

	wc_dev_to_uverbs_wc(&wc_dev, &cq->wc);
	cq->vcq.cq_ex.status = (enum ibv_wc_status)wc_dev.wc.status;
	cq->vcq.cq_ex.wr_id = wc_dev.wc.wr_id;

	WRITE_CQ(wc_dev, raw.qw4, cq, 0);

	return 0;
}

static void cq_end_poll(struct ibv_cq_ex *current)
{
	struct grvf_cq *cq = container_of(current, struct grvf_cq, vcq.cq_ex);

	++cq->cons;
	cq->cons &= cq->size_mask;
	pthread_spin_unlock(&cq->lock);
}

static enum ibv_wc_opcode cq_read_opcode(struct ibv_cq_ex *current)
{
	struct grvf_cq *cq = container_of(current, struct grvf_cq, vcq.cq_ex);

	return cq->wc.opcode;
}

static uint32_t cq_read_vendor_err(struct ibv_cq_ex *current)
{
	struct grvf_cq *cq = container_of(current, struct grvf_cq, vcq.cq_ex);

	return cq->wc.vendor_err;
}

static uint32_t cq_read_byte_len(struct ibv_cq_ex *current)
{
	struct grvf_cq *cq = container_of(current, struct grvf_cq, vcq.cq_ex);

	return cq->wc.byte_len;
}

static __be32 cq_read_imm_data(struct ibv_cq_ex *current)
{
	struct grvf_cq *cq = container_of(current, struct grvf_cq, vcq.cq_ex);

	return cq->wc.ex.imm_data;
}

static uint32_t cq_read_qp_num(struct ibv_cq_ex *current)
{
	struct grvf_cq *cq = container_of(current, struct grvf_cq, vcq.cq_ex);

	return cq->wc.qp_num;
}

static uint32_t cq_read_src_qp(struct ibv_cq_ex *current)
{
	struct grvf_cq *cq = container_of(current, struct grvf_cq, vcq.cq_ex);

	return cq->wc.src_qp;
}

static unsigned int cq_read_wc_flags(struct ibv_cq_ex *current)
{
	struct grvf_cq *cq = container_of(current, struct grvf_cq, vcq.cq_ex);

	return cq->wc.wc_flags;
}

static uint32_t cq_read_slid(struct ibv_cq_ex *current)
{
	struct grvf_cq *cq = container_of(current, struct grvf_cq, vcq.cq_ex);

	return cq->wc.slid;
}

static uint8_t cq_read_sl(struct ibv_cq_ex *current)
{
	struct grvf_cq *cq = container_of(current, struct grvf_cq, vcq.cq_ex);

	return cq->wc.sl;
}

static struct ibv_cq_ex *grvf_create_cq_ex(struct ibv_context *context,
					   struct ibv_cq_init_attr_ex *attr)
{
	struct grvf_cq *cq;
	struct grvf_create_cq_ex_resp resp;
	int ret;

	if (attr->wc_flags & ~(IBV_WC_EX_WITH_BYTE_LEN | IBV_WC_EX_WITH_IMM |
			       IBV_WC_EX_WITH_QP_NUM | IBV_WC_EX_WITH_SRC_QP |
			       IBV_WC_EX_WITH_SLID | IBV_WC_EX_WITH_SL)) {
		verbs_err(verbs_get_ctx(context), "Flags are not supported\n");
		errno = EOPNOTSUPP;
		goto err;
	}

	cq = calloc(1, sizeof(*cq));
	if (!cq)
		return NULL;

	ret = ibv_cmd_create_cq_ex(context, attr, &cq->vcq, NULL, 0,
				   &resp.ibv_resp, sizeof(resp), 0);
	if (ret) {
		verbs_err(verbs_get_ctx(context),
			  "Failed to create extended CQ\n");
		errno = ret;
		goto err_free;
	}

	cq->cons = 0;
	cq->size_mask = cq->vcq.cq_ex.cqe - 1;

	if (upper_power_of_two(cq->size_mask) != cq->vcq.cq_ex.cqe) {
		verbs_err(verbs_get_ctx(context), "Internal error\n");
		errno = EINVAL;
		goto err_destroy;
	}

	cq->dev = mmap(NULL, resp.mi.size, PROT_READ | PROT_WRITE, MAP_SHARED,
		       context->cmd_fd, resp.mi.offset);
	if ((void *)cq->dev == MAP_FAILED) {
		verbs_err(verbs_get_ctx(context),
			  "Failed mmap for create CQ\n");
		goto err_destroy;
	}

	cq->mmap_info = resp.mi;
	pthread_spin_init(&cq->lock, PTHREAD_PROCESS_PRIVATE);

	cq->vcq.cq_ex.start_poll = cq_start_poll;
	cq->vcq.cq_ex.next_poll = cq_next_poll;
	cq->vcq.cq_ex.end_poll = cq_end_poll;
	cq->vcq.cq_ex.read_opcode = cq_read_opcode;
	cq->vcq.cq_ex.read_vendor_err = cq_read_vendor_err;
	cq->vcq.cq_ex.read_wc_flags = cq_read_wc_flags;

	if (attr->wc_flags & IBV_WC_EX_WITH_BYTE_LEN)
		cq->vcq.cq_ex.read_byte_len = cq_read_byte_len;

	if (attr->wc_flags & IBV_WC_EX_WITH_IMM)
		cq->vcq.cq_ex.read_imm_data = cq_read_imm_data;

	if (attr->wc_flags & IBV_WC_EX_WITH_QP_NUM)
		cq->vcq.cq_ex.read_qp_num = cq_read_qp_num;

	if (attr->wc_flags & IBV_WC_EX_WITH_SRC_QP)
		cq->vcq.cq_ex.read_src_qp = cq_read_src_qp;

	if (attr->wc_flags & IBV_WC_EX_WITH_SLID)
		cq->vcq.cq_ex.read_slid = cq_read_slid;

	if (attr->wc_flags & IBV_WC_EX_WITH_SL)
		cq->vcq.cq_ex.read_sl = cq_read_sl;

	return &cq->vcq.cq_ex;

err_destroy:
	ibv_cmd_destroy_cq(&cq->vcq.cq);
err_free:
	free(cq);
err:

	return NULL;
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

static struct ibv_qp *grvf_create_qp(struct ibv_pd *ibvpd,
				     struct ibv_qp_init_attr *attr)
{
	struct grvf_qp *qp;
	struct grvf_pd *pd = container_of(ibvpd, struct grvf_pd, ibvpd);
	struct ibv_create_qp cmd;
	struct grvf_create_qp_resp resp;
	int ret;

	qp = calloc(1, sizeof(*qp));
	if (!qp)
		return NULL;

	qp->sq.wr_dmp = calloc(0x40, sizeof(union sq_entry_t));
	if (!qp->sq.wr_dmp) {
		free(qp);
		return NULL;
	}

	ret = ibv_cmd_create_qp(ibvpd, &qp->vqp.qp, attr, &cmd, sizeof(cmd),
				&resp.ibv_resp, sizeof(resp));
	if (ret) {
		verbs_err(verbs_get_ctx(ibvpd->context),
			  "Failed to create QP\n");
		errno = ret;
		free(qp);
		return NULL;
	}

	ret = map_queue_pair(ibvpd->context->cmd_fd, qp, attr,
			     &resp.drv_payload);
	if (ret) {
		verbs_err(verbs_get_ctx(ibvpd->context), "Failed mapping QP\n");
		ibv_cmd_destroy_qp(&qp->vqp.qp);
		free(qp);
		return NULL;
	}

	qp->pd = pd;

	return &qp->vqp.qp;
}

static struct ibv_qp *grvf_create_qp_ex(struct ibv_context *context,
					struct ibv_qp_init_attr_ex *attr)
{
	int ret;
	struct grvf_qp *qp;
	struct ibv_create_qp_ex cmd = {};
	struct grvf_create_qp_ex_resp resp = {};

	qp = calloc(1, sizeof(*qp));
	if (!qp)
		return NULL;

	qp->sq.wr_dmp = calloc(0x40, sizeof(union sq_entry_t));
	if (!qp->sq.wr_dmp) {
		goto err_free;
	}

	if (attr->comp_mask & IBV_QP_INIT_ATTR_SEND_OPS_FLAGS) {
		set_qp_send_ops(qp, attr->send_ops_flags);
	}

	ret = ibv_cmd_create_qp_ex2(context, &qp->vqp, attr, &cmd, sizeof(cmd),
				    &resp.ibv_resp, sizeof(resp));
	if (ret) {
		verbs_err(verbs_get_ctx(context), "Failed to create EX QP\n");
		goto err_sq_free;
	}

	ret = map_queue_pair(context->cmd_fd, qp,
			     (struct ibv_qp_init_attr *)attr,
			     &resp.drv_payload);
	if (ret) {
		verbs_err(verbs_get_ctx(context), "Failed mapping QP\n");
		goto err_destroy;
	}

	qp->pd = container_of(attr->pd, struct grvf_pd, ibvpd);
	qp->vqp.comp_mask |= VERBS_QP_EX;

	return &qp->vqp.qp;

err_destroy:
	ibv_cmd_destroy_qp(&qp->vqp.qp);
err_sq_free:
	free(qp->sq.wr_dmp);
err_free:
	free(qp);
	return NULL;
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

static int grvf_dealloc_pd(struct ibv_pd *ibvpd)
{
	struct grvf_pd *pd = container_of(ibvpd, struct grvf_pd, ibvpd);
	struct verbs_context *ctx = verbs_get_ctx(ibvpd->context);
	int ret;

	ret = ibv_cmd_dealloc_pd(&pd->ibvpd);
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

	free(p->sq.wr_dmp);
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
	struct grvf_context *ctx = to_gctx(context);

	verbs_uninit_context(&ctx->ibv_ctx);
	free(ctx);
}

static int grvf_free_dm(struct ibv_dm *dm)
{
	struct grvf_dm *p = container_of(dm, struct grvf_dm, vdm.dm);
	int ret;

	ret = ibv_cmd_free_dm(&p->vdm);
	if (ret) {
		verbs_err(verbs_get_ctx(dm->context), "Failed to free DM\n");
		return ret;
	}

	free(p);
	return 0;
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

int grvfdv_modify_qp_custom(struct ibv_qp *ibvqp,
			    struct grvfdv_qp_custom_attr *attr, int attr_mask)
{
	int ret;

	DECLARE_COMMAND_BUFFER(cmd, UVERBS_OBJECT_QP,
			       GRVF_IB_METHOD_MODIFY_QP_CUSTOM, 3);

	fill_attr_in_obj(cmd, GRVF_IB_ATTR_MODIFY_QP_CUSTOM_HANDLE,
			 ibvqp->handle);
	fill_attr_in(cmd, GRVF_IB_ATTR_MODIFY_QP_ATTRS, attr,
		     sizeof(struct grvfdv_qp_custom_attr));
	fill_attr_in(cmd, GRVF_IB_ATTR_MODIFY_QP_MASK, &attr_mask,
		     sizeof(attr_mask));

	ret = execute_ioctl(ibvqp->context, cmd);
	if (ret) {
		verbs_err(verbs_get_ctx(ibvqp->context),
			  "Failed to modify QP custom attributes\n");
		return ret;
	}

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

static int grvf_poll_cq(struct ibv_cq *cq, int num_entries, struct ibv_wc *wc)
{
	struct grvf_context *ctx = to_gctx(cq->context);
	struct grvf_cq *q = container_of(cq, struct grvf_cq, vcq.cq);
	struct cq_entry_t wc_dev;
	int npolled;

	if (!ctx->ibv_active)
		return -EAGAIN;

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
	struct grvf_context *ctx = to_gctx(qp->context);
	struct grvf_qp *q = container_of(qp, struct grvf_qp, vqp.qp);
	int ret = 0;
	int nreq;

	if (!ctx->ibv_active)
		return -EAGAIN;

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
	struct grvf_context *ctx = to_gctx(qp->context);
	struct grvf_qp *q = container_of(qp, struct grvf_qp, vqp.qp);
	int ret = 0;
	int nreq;

	if (!ctx->ibv_active)
		return -EAGAIN;

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
	struct grvf_context *ctx = to_gctx(srq->context);
	struct grvf_srq *q = container_of(srq, struct grvf_srq, vsrq.srq);
	int ret = 0;
	int nreq;

	if (!ctx->ibv_active)
		return -EAGAIN;

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

static struct ibv_mr *grvf_reg_dm_mr(struct ibv_pd *pd, struct ibv_dm *dm,
				     uint64_t dm_offset, size_t length,
				     unsigned int access)
{
	struct grvf_dm *p = container_of(dm, struct grvf_dm, vdm.dm);
	struct verbs_mr *vmr;
	int ret;

	if (access & ~(IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE |
		       IBV_ACCESS_REMOTE_READ | IBV_ACCESS_ZERO_BASED)) {
		errno = EINVAL;
		return NULL;
	}

	vmr = calloc(1, sizeof(*vmr));
	if (!vmr)
		return NULL;

	ret = ibv_cmd_reg_dm_mr(pd, &p->vdm, dm_offset, length, access, vmr,
				NULL);
	if (ret) {
		verbs_err(verbs_get_ctx(dm->context),
			  "Failed to register MR\n");
		errno = ret;
		free(vmr);
		return NULL;
	}

	return &vmr->ibv_mr;
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

static int grvfdv_query_dbg_mon(struct ibv_context *context, uint16_t method_id,
				uint16_t type_id, uint16_t counter_id,
				uint32_t event_type, uint32_t *counter)
{
	int ret;

	DECLARE_COMMAND_BUFFER(cmd, GRVF_IB_OBJECT_DBG_MON, method_id, 2);

	fill_attr_in(cmd, type_id, &event_type, sizeof(event_type));
	fill_attr_out(cmd, counter_id, counter, sizeof(uint32_t));

	ret = execute_ioctl(context, cmd);
	if (ret) {
		verbs_err(verbs_get_ctx(context), "Failed to query dbg mon\n");
		return ret;
	}

	return 0;
}

int grvfdv_query_tx_dbg_mon(struct ibv_context *context,
			    enum grvfdv_dbg_mon_tx_events_type event_type,
			    uint32_t *counter)
{
	return grvfdv_query_dbg_mon(context, GRVF_IB_METHOD_QUERY_TX_DBG_MON,
				    GRVF_IB_ATTR_QUERY_TX_DBG_MON_TYPE,
				    GRVF_IB_ATTR_QUERY_TX_DBG_MON_COUNTER,
				    event_type, counter);
}

int grvfdv_query_rx_dbg_mon(struct ibv_context *context,
			    enum grvfdv_dbg_mon_rx_events_type event_type,
			    uint32_t *counter)
{
	return grvfdv_query_dbg_mon(context, GRVF_IB_METHOD_QUERY_RX_DBG_MON,
				    GRVF_IB_ATTR_QUERY_RX_DBG_MON_TYPE,
				    GRVF_IB_ATTR_QUERY_RX_DBG_MON_COUNTER,
				    event_type, counter);
}

int grvfdv_query_global_dbg_mon(
	struct ibv_context *context,
	enum grvfdv_dbg_mon_global_events_type event_type, uint32_t *counter)
{
	return grvfdv_query_dbg_mon(context,
				    GRVF_IB_METHOD_QUERY_GLOBAL_DBG_MON,
				    GRVF_IB_ATTR_QUERY_GLOBAL_DBG_MON_TYPE,
				    GRVF_IB_ATTR_QUERY_GLOBAL_DBG_MON_COUNTER,
				    event_type, counter);
}

int grvfdv_query_cnp(struct ibv_qp *ibvqp,
		     struct grvfdv_congestion_cnp_attrs *attr)
{
	int ret;

	DECLARE_COMMAND_BUFFER(cmd, UVERBS_OBJECT_QP, GRVF_IB_METHOD_QUERY_CNP,
			       2);

	fill_attr_in_obj(cmd, GRVF_IB_ATTR_QUERY_CPN_HANDLE, ibvqp->handle);
	fill_attr_out(cmd, GRVF_IB_ATTR_QUERY_CPN_INFO_ATTRS, attr,
		      sizeof(struct grvfdv_congestion_cnp_attrs));

	ret = execute_ioctl(ibvqp->context, cmd);
	if (ret) {
		verbs_err(verbs_get_ctx(ibvqp->context),
			  "Failed to query CNP\n");
		return ret;
	}

	return 0;
}

static const struct verbs_context_ops grvf_ctx_ops = {
	.async_event = grvf_async_event,
	.alloc_dm = grvf_alloc_dm,
	.alloc_pd = grvf_alloc_pd,
	.attach_mcast = ibv_cmd_attach_mcast,
	.create_ah = grvf_create_ah,
	.create_cq = grvf_create_cq,
	.create_cq_ex = grvf_create_cq_ex,
	.create_qp = grvf_create_qp,
	.create_qp_ex = grvf_create_qp_ex,
	.create_srq = grvf_create_srq,
	.dealloc_pd = grvf_dealloc_pd,
	.dereg_mr = grvf_dereg_mr,
	.destroy_ah = grvf_destroy_ah,
	.destroy_cq = grvf_destroy_cq,
	.destroy_qp = grvf_destroy_qp,
	.destroy_srq = grvf_destroy_srq,
	.detach_mcast = ibv_cmd_detach_mcast,
	.free_context = grvf_free_context,
	.free_dm = grvf_free_dm,
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
	.reg_dm_mr = grvf_reg_dm_mr,
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

	ret = ibv_cmd_get_context(&context->ibv_ctx, &cmd, sizeof(cmd), &resp,
				  sizeof(resp));
	if (ret) {
		verbs_err(&context->ibv_ctx,
			  "Failed to get ucontext, err: %d\n", ret);
		errno = ret;
		verbs_uninit_context(&context->ibv_ctx);
		free(context);
		return NULL;
	}

	verbs_set_ops(&context->ibv_ctx, &grvf_ctx_ops);
	context->ibv_active = true;

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
