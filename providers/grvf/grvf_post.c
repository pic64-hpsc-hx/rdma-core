// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2023-2024, Grovf, Inc. All rights reserved.
 */

#include <stddef.h>
#include <stdint.h>
#include <util/mmio.h>
#include <netinet/in.h>
#include <linux/if_ether.h> /* For ETH_ALEN. */
#include "grvf.h"
#include "grvf_post.h"
#include "grvf_bare.h"
#include "grvf_ip.h"

#define QPN_MCAST 0xFFFFFF
#define IPV4_MCAST_MAC 0x0000005E0001
#define IPV6_MCAST_MAC 0x000000003333

static int recv_wr_to_dev(struct grvf_rq *rq, struct ibv_recv_wr *ibwr)
{
	union rq_entry_t wr_hdr;
	union rq_entry_t wr_sge;
	uint64_t *rqe;
	size_t i;

	rqe = (uint64_t *)rq->wq.dev + ((rq->prod * rq->wr_size) >> 3);

	wr_hdr.wqe.wr_id = ibwr->wr_id;
	wr_hdr.wqe.num_sge = ibwr->num_sge;

	if (ibwr->num_sge > rq->wq.max_sge)
		return EINVAL;

	rqe[0] = wr_hdr.raw.qw0;
	rqe[1] = wr_hdr.raw.qw1;

	for (i = 0; i < ibwr->num_sge; ++i) {
		wr_sge.sge.lkey = ibwr->sg_list[i].lkey;
		wr_sge.sge.size = ibwr->sg_list[i].length;
		wr_sge.sge.vaddr = ibwr->sg_list[i].addr;

		rqe[(i * 2) + 2] = wr_sge.raw.qw0;
		rqe[(i * 2) + 3] = wr_sge.raw.qw1;
	}

	rq->prod = (rq->prod + 1) & rq->wr_count_mask;
	return 0;
}

int grvf_post_one_recv(struct grvf_rq *rq, struct ibv_recv_wr *recv_wr)
{
	if (recv_wr->num_sge > rq->wq.max_sge)
		return EINVAL;

	return recv_wr_to_dev(rq, recv_wr);
}

void grvf_post_recv_db(struct grvf_rq *rq, uint32_t wr_count)
{
	struct cmd64_t cmd;

	cmd.hdr.cmd_id = cmd_rx_rq_doorbell_id;
	cmd.hdr.rq_id_hi = 0;
	cmd.hdr.rq_id_lo = rq->num;
	cmd.bdy.cmd_rx_rq_doorbell.value = wr_count;
	mmio_write64(rq->wq.db_dev, cmd.cmd_raw);
}

static inline void init_wr(union sq_entry_t *wr_dmp, uint64_t wr_id,
			   enum ibv_wr_opcode optcode, unsigned int wr_flags,
			   uint32_t psn)
{
	wr_dmp[0].wr.wr_id = wr_id;
	wr_dmp[0].wr.opcoode = optcode;
	wr_dmp[0].wr.flags = wr_flags;
	wr_dmp[0].wr.num_sge = 0;
	wr_dmp[0].wr.psn_last = (psn + 0xFFFFFF) & 0xFFFFFF;
}

static inline void advance_sq(struct grvf_qp *qp)
{
	union sq_entry_t *wr_dmp = qp->sq.wr_dmp;
	size_t size =
		(((qp->qp_type == IBV_QPT_UD) ? 5 : 3) + wr_dmp[0].wr.num_sge)
		<< 4;
	uint8_t *wr_dev = qp->sq.wq.dev + qp->sq.prod_tmp * qp->sq.wr_size;

	if (qp->sq.wr_count > 0) {
		memcpy((void *)wr_dev, (void *)wr_dmp, size);
		qp->sq.prod_tmp = (qp->sq.prod_tmp + 1) & qp->sq.wr_count_mask;
	}
}

static void rdma_read_write_common(struct grvf_qp *qp, uint64_t wr_id,
				   enum ibv_wr_opcode optcode,
				   unsigned int wr_flags, uint32_t psn,
				   uint32_t rkey, uint64_t remote_addr,
				   __be32 imm_data)
{
	union sq_entry_t *wr_dmp = qp->sq.wr_dmp;

	init_wr(wr_dmp, wr_id, optcode, wr_flags, psn + 1);

	wr_dmp[1].impl.tx_sideband_tag = 0;
	wr_dmp[1].impl.imm_data = imm_data;
	wr_dmp[1].impl.rc.psn_first = psn;

	wr_dmp[2].wr_rdma_ud_mr.rdma.length = 0xFFFFFFFF;
	wr_dmp[2].wr_rdma_ud_mr.rdma.rkey = rkey;
	wr_dmp[2].wr_rdma_ud_mr.rdma.vaddr = remote_addr;
}

static void rdma_read_opt(struct grvf_qp *qp, uint64_t wr_id,
			  unsigned int wr_flags, uint32_t psn, uint32_t rkey,
			  uint64_t remote_addr)
{
	rdma_read_write_common(qp, wr_id, IBV_WR_RDMA_READ, wr_flags, psn + 1,
			       rkey, remote_addr, 0);
}

static void wr_rdma_read(struct ibv_qp_ex *ibvqp, uint32_t rkey,
			 uint64_t remote_addr)
{
	struct grvf_qp *qp = container_of(ibvqp, struct grvf_qp, vqp.qp_ex);

	advance_sq(qp);
	rdma_read_opt(qp, qp->vqp.qp_ex.wr_id, qp->vqp.qp_ex.wr_flags,
		      qp->sq.psn_tmp, rkey, remote_addr);
	qp->sq.psn_tmp++;
	qp->sq.wr_count++;
}

static void rdma_write_opt(struct grvf_qp *qp, uint64_t wr_id,
			   unsigned int wr_flags, uint32_t psn, uint32_t rkey,
			   uint64_t remote_addr)
{
	rdma_read_write_common(qp, wr_id, IBV_WR_RDMA_WRITE, wr_flags, psn + 1,
			       rkey, remote_addr, 0);
}

static void wr_rdma_write(struct ibv_qp_ex *ibvqp, uint32_t rkey,
			  uint64_t remote_addr)
{
	struct grvf_qp *qp = container_of(ibvqp, struct grvf_qp, vqp.qp_ex);

	advance_sq(qp);
	rdma_write_opt(qp, qp->vqp.qp_ex.wr_id, qp->vqp.qp_ex.wr_flags,
		       qp->sq.psn_tmp, rkey, remote_addr);
	qp->sq.psn_tmp++;
	qp->sq.wr_count++;
}

static void rdma_write_imm_opt(struct grvf_qp *qp, uint64_t wr_id,
			       unsigned int wr_flags, uint32_t psn,
			       uint32_t rkey, uint64_t remote_addr,
			       __be32 imm_data)
{
	rdma_read_write_common(qp, wr_id, IBV_WR_RDMA_WRITE_WITH_IMM, wr_flags,
			       psn + 1, rkey, remote_addr, imm_data);
}

static void wr_rdma_write_imm(struct ibv_qp_ex *ibvqp, uint32_t rkey,
			      uint64_t remote_addr, __be32 imm_data)
{
	struct grvf_qp *qp = container_of(ibvqp, struct grvf_qp, vqp.qp_ex);

	advance_sq(qp);
	rdma_write_imm_opt(qp, qp->vqp.qp_ex.wr_id, qp->vqp.qp_ex.wr_flags,
			   qp->sq.psn_tmp, rkey, remote_addr, imm_data);
	qp->sq.psn_tmp++;
	qp->sq.wr_count++;
}

static void send_common(struct grvf_qp *qp, uint64_t wr_id,
			enum ibv_wr_opcode optcode, unsigned int wr_flags,
			uint32_t psn, __be32 imm_data)
{
	union sq_entry_t *wr_dmp = qp->sq.wr_dmp;

	init_wr(wr_dmp, wr_id, optcode, wr_flags, psn + 1);

	wr_dmp[1].impl.tx_sideband_tag = 0;
	wr_dmp[1].impl.imm_data = imm_data;

	if (qp->qp_type != IBV_QPT_UD) {
		wr_dmp[1].impl.rc.psn_first = psn;
		wr_dmp[2].wr_rdma_ud_mr.rdma.length = 0;
	}
}

static void send_opt(struct grvf_qp *qp, uint64_t wr_id, unsigned int wr_flags,
		     uint32_t psn)
{
	send_common(qp, wr_id, IBV_WR_SEND, wr_flags, psn, 0);
}

static void wr_send(struct ibv_qp_ex *ibvqp)
{
	struct grvf_qp *qp = container_of(ibvqp, struct grvf_qp, vqp.qp_ex);

	advance_sq(qp);
	send_opt(qp, qp->vqp.qp_ex.wr_id, qp->vqp.qp_ex.wr_flags,
		 qp->sq.psn_tmp);
	qp->sq.psn_tmp++;
	qp->sq.wr_count++;
}

static void send_imm_opt(struct grvf_qp *qp, uint64_t wr_id,
			 unsigned int wr_flags, uint32_t psn, __be32 imm_data)
{
	send_common(qp, wr_id, IBV_WR_SEND_WITH_IMM, wr_flags, psn, imm_data);
}

static void wr_send_imm(struct ibv_qp_ex *ibvqp, __be32 imm_data)
{
	struct grvf_qp *qp = container_of(ibvqp, struct grvf_qp, vqp.qp_ex);

	advance_sq(qp);
	send_imm_opt(qp, qp->vqp.qp_ex.wr_id, qp->vqp.qp_ex.wr_flags,
		     qp->sq.psn_tmp, imm_data);
	qp->sq.psn_tmp++;
	qp->sq.wr_count++;
}

static void send_inv_opt(struct grvf_qp *qp, uint64_t wr_id,
			 unsigned int wr_flags, uint32_t psn,
			 uint32_t invalidate_rkey)
{
	send_common(qp, wr_id, IBV_WR_SEND_WITH_INV, wr_flags, psn,
		    invalidate_rkey);
}

static void wr_send_inv(struct ibv_qp_ex *ibvqp, uint32_t invalidate_rkey)
{
	struct grvf_qp *qp = container_of(ibvqp, struct grvf_qp, vqp.qp_ex);

	advance_sq(qp);
	send_inv_opt(qp, qp->vqp.qp_ex.wr_id, qp->vqp.qp_ex.wr_flags,
		     qp->sq.psn_tmp, invalidate_rkey);
	qp->sq.psn_tmp++;
	qp->sq.wr_count++;
}

static void set_ud_addr(struct grvf_qp *qp, struct ibv_ah *ibvah,
			uint32_t remote_qpn, uint32_t remote_qkey)
{
	union sq_entry_t *wr_dmp = qp->sq.wr_dmp;
	struct grvf_ah *ah = container_of(ibvah, struct grvf_ah, ibv_ah);

	wr_dmp[1].impl.ud.remote_qkey = remote_qkey;
	wr_dmp[1].impl.ud.remote_qpn = remote_qpn;

	if (ah->av.network_type == GRVF_NETWORK_TYPE_IPV4) {
		wr_dmp[2].wr_rdma_ud_mr.ah.gid[3] = 0;
		wr_dmp[2].wr_rdma_ud_mr.ah.gid[2] = 0;
		wr_dmp[2].wr_rdma_ud_mr.ah.gid[1] = 0xffff0000;
		wr_dmp[2].wr_rdma_ud_mr.ah.gid[0] =
			ah->av.dgid_addr._sockaddr_in.sin_addr.s_addr;

		wr_dmp[3].wr_rdma_ud_mr.ah.gid[3] = 0;
		wr_dmp[3].wr_rdma_ud_mr.ah.gid[2] = 0;
		wr_dmp[3].wr_rdma_ud_mr.ah.gid[1] = 0xffff0000;
		wr_dmp[3].wr_rdma_ud_mr.ah.gid[0] =
			ah->av.sgid_addr._sockaddr_in.sin_addr.s_addr;
	} else {
		wr_dmp[2].wr_rdma_ud_mr.ah.gid[3] =
			ah->av.dgid_addr._sockaddr_in6.sin6_addr.__in6_u
				.__u6_addr32[0];
		wr_dmp[2].wr_rdma_ud_mr.ah.gid[2] =
			ah->av.dgid_addr._sockaddr_in6.sin6_addr.__in6_u
				.__u6_addr32[1];
		wr_dmp[2].wr_rdma_ud_mr.ah.gid[1] =
			ah->av.dgid_addr._sockaddr_in6.sin6_addr.__in6_u
				.__u6_addr32[2];
		wr_dmp[2].wr_rdma_ud_mr.ah.gid[0] =
			ah->av.dgid_addr._sockaddr_in6.sin6_addr.__in6_u
				.__u6_addr32[3];

		wr_dmp[3].wr_rdma_ud_mr.ah.gid[3] =
			ah->av.sgid_addr._sockaddr_in6.sin6_addr.__in6_u
				.__u6_addr32[0];
		wr_dmp[3].wr_rdma_ud_mr.ah.gid[2] =
			ah->av.sgid_addr._sockaddr_in6.sin6_addr.__in6_u
				.__u6_addr32[1];
		wr_dmp[3].wr_rdma_ud_mr.ah.gid[1] =
			ah->av.sgid_addr._sockaddr_in6.sin6_addr.__in6_u
				.__u6_addr32[2];
		wr_dmp[3].wr_rdma_ud_mr.ah.gid[0] =
			ah->av.sgid_addr._sockaddr_in6.sin6_addr.__in6_u
				.__u6_addr32[3];
	}

	if (ah->av.network_type == GRVF_NETWORK_TYPE_IPV6)
		wr_dmp[0].wr.flags |= SQ_WR_UD_IPV6;

	memcpy(&wr_dmp[4].ud_hw.hw_addr_src, ah->av.smac, ETH_ALEN);

	if (remote_qpn != QPN_MCAST) {
		memcpy(&wr_dmp[4].ud_hw.hw_addr_dst, ah->av.dmac, ETH_ALEN);
	} else {
		if (ah->av.network_type == GRVF_NETWORK_TYPE_IPV4) {
			wr_dmp[4].ud_hw.hw_addr_dst = IPV4_MCAST_MAC;
			wr_dmp[4].ud_hw.hw_addr_dst |=
				((uint64_t)0xFFFFFFF00 &
				 wr_dmp[2].wr_rdma_ud_mr.ah.gid[0])
				<< 16;
		} else {
			wr_dmp[4].ud_hw.hw_addr_dst = IPV6_MCAST_MAC;
			wr_dmp[4].ud_hw.hw_addr_dst |=
				(uint64_t)wr_dmp[2].wr_rdma_ud_mr.ah.gid[0]
				<< 16;
		}
	}
}

static void wr_set_ud_addr(struct ibv_qp_ex *ibvqp, struct ibv_ah *ibvah,
			   uint32_t remote_qpn, uint32_t remote_qkey)
{
	struct grvf_qp *qp = container_of(ibvqp, struct grvf_qp, vqp.qp_ex);

	if (qp->sq.err)
		return;

	set_ud_addr(qp, ibvah, remote_qpn, remote_qkey);
}

static inline void advance_psn(uint32_t *psn, size_t tot_length,
			       enum ibv_mtu path_mtu)
{
	size_t len_r;
	uint32_t psn_tmp = *psn;

	if (tot_length) {
		len_r = tot_length >> (path_mtu + 7);
		psn_tmp += len_r;

		if (len_r << (path_mtu + 7) != tot_length)
			psn_tmp++;
	} else {
		psn_tmp++;
	}

	*psn = psn_tmp;
}

static int set_inline_data_list(struct grvf_qp *qp, uint32_t *psn,
				size_t num_buf, const void *buf, bool ex)
{
	size_t i;
	size_t tot_length = 0;
	size_t length;
	void *addr;
	uint32_t psn_last = *psn;
	union sq_entry_t *wr_dmp = qp->sq.wr_dmp;
	uint8_t *inline_buf = (uint8_t *)&wr_dmp[3];
	const struct ibv_sge *sg_list = (struct ibv_sge *)buf;
	const struct ibv_data_buf *buf_list = (struct ibv_data_buf *)buf;

	for (i = 0; i < num_buf; i++) {
		if (ex) {
			length = buf_list[i].length;
			addr = buf_list[i].addr;
		} else {
			length = sg_list[i].length;
			addr = (void *)sg_list[i].addr;
		}

		tot_length += length;
		if (tot_length > qp->sq.wq.max_inline)
			return ENOSPC;

		memcpy((void *)inline_buf, addr, length);
		inline_buf += length;
	}

	wr_dmp[0].wr.num_sge = tot_length >> 4;

	if (tot_length & 0xF)
		wr_dmp[0].wr.num_sge += 1;

	advance_psn(&psn_last, tot_length, qp->path_mtu);

	if (qp->qp_type != IBV_QPT_UD) {
		wr_dmp[0].wr.psn_last = (psn_last + 0xFFFFFF) & 0xFFFFFF;
		wr_dmp[1].impl.rc.psn_first = *psn;
		wr_dmp[2].wr_rdma_ud_mr.rdma.length = tot_length;

		*psn = psn_last;
	}

	return 0;
}

static void wr_set_inline_data(struct ibv_qp_ex *ibvqp, void *addr,
			       size_t length)
{
	struct grvf_qp *qp = container_of(ibvqp, struct grvf_qp, vqp.qp_ex);
	struct ibv_data_buf buf = { addr, length };
	uint32_t psn = qp->sq.psn_first;

	if (qp->sq.err)
		return;

	qp->sq.err = set_inline_data_list(qp, &psn, 1, &buf, true);
	qp->sq.psn_tmp = psn;
}

static void wr_set_inline_data_list(struct ibv_qp_ex *ibvqp, size_t num_buf,
				    const struct ibv_data_buf *buf_list)
{
	struct grvf_qp *qp = container_of(ibvqp, struct grvf_qp, vqp.qp_ex);
	uint32_t psn = qp->sq.psn_first;

	if (qp->sq.err)
		return;

	qp->sq.err = set_inline_data_list(qp, &psn, num_buf, buf_list, true);
	qp->sq.psn_tmp = psn;
}

static int set_sge_list(struct grvf_qp *qp, uint32_t *psn, size_t num_sge,
			const struct ibv_sge *sg_list)
{
	union sq_entry_t *wr_dmp = qp->sq.wr_dmp;
	int offset = (qp->qp_type == IBV_QPT_UD) ? 5 : 3;
	int i;
	size_t tot_length = 0;
	uint32_t psn_last = *psn;

	if (num_sge > qp->sq.wq.max_sge)
		return ENOSPC;

	wr_dmp[0].wr.num_sge = num_sge;

	for (i = 0; i < num_sge; i++) {
		wr_dmp[offset + i].sge.lkey = sg_list[i].lkey;
		wr_dmp[offset + i].sge.size = sg_list[i].length;
		wr_dmp[offset + i].sge.vaddr = sg_list[i].addr;
		tot_length += sg_list[i].length;
	}

	advance_psn(&psn_last, tot_length, qp->path_mtu);

	if (qp->qp_type != IBV_QPT_UD) {
		wr_dmp[0].wr.psn_last = (psn_last + 0xFFFFFF) & 0xFFFFFF;
		wr_dmp[1].impl.rc.psn_first = *psn;
		wr_dmp[2].wr_rdma_ud_mr.rdma.length = tot_length;
		*psn = psn_last;
	}

	return 0;
}

static void wr_set_sge(struct ibv_qp_ex *ibvqp, uint32_t lkey, uint64_t addr,
		       uint32_t length)
{
	struct grvf_qp *qp = container_of(ibvqp, struct grvf_qp, vqp.qp_ex);
	struct ibv_sge sg = { addr, length, lkey };
	uint32_t psn = qp->sq.psn_first;

	if (qp->sq.err)
		return;

	// XXX: Pheraps, here need to add inline mode too
	qp->sq.err = set_sge_list(qp, &psn, 1, &sg);
	qp->sq.psn_tmp = psn;
}

static void wr_set_sge_list(struct ibv_qp_ex *ibvqp, size_t num_sge,
			    const struct ibv_sge *sg_list)
{
	struct grvf_qp *qp = container_of(ibvqp, struct grvf_qp, vqp.qp_ex);
	uint32_t psn = qp->sq.psn_first;

	if (qp->sq.err)
		return;

	// XXX: Pheraps, here need to add inline mode too
	qp->sq.err = set_sge_list(qp, &psn, num_sge, sg_list);
	qp->sq.psn_tmp = psn;
}

static void wr_start(struct ibv_qp_ex *ibvqp)
{
	struct grvf_qp *qp = container_of(ibvqp, struct grvf_qp, vqp.qp_ex);

	pthread_spin_lock(&qp->sq.wq.lock);

	qp->sq.err = 0;
	qp->sq.wr_count = 0;
	qp->sq.psn_first = qp->sq.psn;
	qp->sq.psn_tmp = qp->sq.psn;
	qp->sq.prod_tmp = qp->sq.prod;
}

static int wr_complete(struct ibv_qp_ex *ibvqp)
{
	struct grvf_qp *qp = container_of(ibvqp, struct grvf_qp, vqp.qp_ex);

	advance_sq(qp);

	if (qp->sq.err) {
		pthread_spin_unlock(&qp->sq.wq.lock);
		return qp->sq.err;
	}

	qp->sq.prod = qp->sq.prod_tmp;
	qp->sq.psn = qp->sq.psn_tmp;

	grvf_post_send_db(&qp->sq, qp->sq.wr_count);
	pthread_spin_unlock(&qp->sq.wq.lock);

	return 0;
}

static void wr_abort(struct ibv_qp_ex *ibvqp)
{
	struct grvf_qp *qp = container_of(ibvqp, struct grvf_qp, vqp.qp_ex);

	pthread_spin_unlock(&qp->sq.wq.lock);
}

void set_qp_send_ops(struct grvf_qp *qp, uint64_t flags)
{
	if (flags & IBV_QP_EX_WITH_RDMA_READ)
		qp->vqp.qp_ex.wr_rdma_read = wr_rdma_read;

	if (flags & IBV_QP_EX_WITH_RDMA_WRITE)
		qp->vqp.qp_ex.wr_rdma_write = wr_rdma_write;

	if (flags & IBV_QP_EX_WITH_RDMA_WRITE_WITH_IMM)
		qp->vqp.qp_ex.wr_rdma_write_imm = wr_rdma_write_imm;

	if (flags & IBV_QP_EX_WITH_SEND)
		qp->vqp.qp_ex.wr_send = wr_send;

	if (flags & IBV_QP_EX_WITH_SEND_WITH_IMM)
		qp->vqp.qp_ex.wr_send_imm = wr_send_imm;

	if (flags & IBV_QP_EX_WITH_SEND_WITH_INV)
		qp->vqp.qp_ex.wr_send_inv = wr_send_inv;

	qp->vqp.qp_ex.wr_set_ud_addr = wr_set_ud_addr;
	qp->vqp.qp_ex.wr_set_inline_data = wr_set_inline_data;
	qp->vqp.qp_ex.wr_set_inline_data_list = wr_set_inline_data_list;
	qp->vqp.qp_ex.wr_set_sge = wr_set_sge;
	qp->vqp.qp_ex.wr_set_sge_list = wr_set_sge_list;

	qp->vqp.qp_ex.wr_start = wr_start;
	qp->vqp.qp_ex.wr_complete = wr_complete;
	qp->vqp.qp_ex.wr_abort = wr_abort;
}

static int post_send_wr_to_dev(struct grvf_qp *qp, struct ibv_send_wr *ibwr)
{
	int ret;
	size_t wr_bytes;
	union sq_entry_t *wr_dmp = qp->sq.wr_dmp;
	uint8_t *wr_dev = qp->sq.wq.dev + qp->sq.prod * qp->sq.wr_size;

	switch (ibwr->opcode) {
	case IBV_WR_RDMA_WRITE:
		rdma_write_opt(qp, ibwr->wr_id, ibwr->send_flags, qp->sq.psn,
			       ibwr->wr.rdma.rkey, ibwr->wr.rdma.remote_addr);
		break;
	case IBV_WR_RDMA_WRITE_WITH_IMM:
		rdma_write_imm_opt(qp, ibwr->wr_id, ibwr->send_flags,
				   qp->sq.psn, ibwr->wr.rdma.rkey,
				   ibwr->wr.rdma.remote_addr, ibwr->imm_data);
		break;
	case IBV_WR_SEND:
		send_opt(qp, ibwr->wr_id, ibwr->send_flags, qp->sq.psn);
		break;
	case IBV_WR_SEND_WITH_IMM:
		send_imm_opt(qp, ibwr->wr_id, ibwr->send_flags, qp->sq.psn,
			     ibwr->imm_data);
		break;
	case IBV_WR_RDMA_READ:
		rdma_read_opt(qp, ibwr->wr_id, ibwr->send_flags, qp->sq.psn,
			      ibwr->wr.rdma.rkey, ibwr->wr.rdma.remote_addr);
		break;
	case IBV_WR_SEND_WITH_INV:
		send_inv_opt(qp, ibwr->wr_id, ibwr->send_flags, qp->sq.psn,
			     ibwr->imm_data);
		break;
	default:
		verbs_err(verbs_get_ctx(qp->vqp.qp.context),
			  "Unsupported opcode\n");
		ret = EINVAL;
		goto err;
		break;
	}

	if (qp->qp_type == IBV_QPT_UD) {
		set_ud_addr(qp, ibwr->wr.ud.ah, ibwr->wr.ud.remote_qpn,
			    ibwr->wr.ud.remote_qkey);
		ret = set_sge_list(qp, &qp->sq.psn, ibwr->num_sge,
				   ibwr->sg_list);
	} else {
		if (ibwr->send_flags & IBV_SEND_INLINE)
			ret = set_inline_data_list(qp, &qp->sq.psn,
						   ibwr->num_sge,
						   (void *)ibwr->sg_list,
						   false);
		else
			ret = set_sge_list(qp, &qp->sq.psn, ibwr->num_sge,
					   ibwr->sg_list);
	}

	if (ret)
		goto err;

	wr_bytes =
		(((qp->qp_type == IBV_QPT_UD) ? 5 : 3) + wr_dmp[0].wr.num_sge)
		<< 4;

	memcpy(wr_dev, wr_dmp, wr_bytes);
	qp->sq.prod = (qp->sq.prod + 1) & qp->sq.wr_count_mask;

	return 0;
err:
	return ret;
}

static inline int validate_send_wr(struct grvf_qp *qp, struct ibv_send_wr *ibwr,
				   unsigned int length)
{
	struct grvf_sq *sq = &qp->sq;

	// TODO: Need to add the QP types that our device supports.

	if (qp->cur_qp_state < IBV_QPS_RTS) {
		return EINVAL;
	}

	if (ibwr->num_sge > sq->wq.max_sge) {
		return EINVAL;
	}

	if ((ibwr->send_flags & IBV_SEND_INLINE) &&
	    (length > sq->wq.max_inline)) {
		return EINVAL;
	}

	if (ibwr->opcode == IBV_WR_RDMA_READ &&
	    (ibwr->send_flags & IBV_SEND_INLINE)) {
		return EINVAL;
	}

	if (length > 0x80000000) {
		return EINVAL;
	}

	return 0;
}

int grvf_post_one_send(struct grvf_qp *qp, struct ibv_send_wr *ibwr)
{
	size_t i;
	int err;
	unsigned int length = 0;

	for (i = 0; i < ibwr->num_sge; ++i)
		length += ibwr->sg_list[i].length;

	err = validate_send_wr(qp, ibwr, length);
	if (err) {
		verbs_err(verbs_get_ctx(qp->vqp.qp.context),
			  "validate send failed\n");

		goto err;
	}

	err = post_send_wr_to_dev(qp, ibwr);
	if (err) {
		verbs_err(verbs_get_ctx(qp->vqp.qp.context),
			  "post_send_wr_to_dev failed\n");
		goto err;
	}

	return 0;
err:
	return err;
}

void grvf_post_send_db(struct grvf_sq *sq, uint32_t wr_count)
{
	struct cmd64_t cmd;

	cmd.hdr.cmd_id = cmd_tx_qp_doorbell_id;
	cmd.hdr.qp_id_hi = 0;
	cmd.hdr.qp_id_lo = sq->num;
	cmd.bdy.cmd_tx_qp_doorbell.value = wr_count; // for testing
	mmio_write64(sq->wq.db_dev, cmd.cmd_raw);
}
