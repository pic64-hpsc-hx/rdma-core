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
#include "grvfdv.h"
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

static inline bool is_wr_local_operation(enum ibv_wr_opcode opcode)
{
	return opcode == IBV_WR_BIND_MW || opcode == IBV_WR_LOCAL_INV;
}

static inline int validate_send_wr(struct grvf_qp *qp, struct ibv_send_wr *ibwr,
				   unsigned int length)
{
	struct grvf_sq *sq = &qp->sq;
	enum ibv_wr_opcode opcode = ibwr->opcode;

	if (ibwr->num_sge > sq->wq.max_sge)
		return EINVAL;

	if ((opcode == IBV_WR_ATOMIC_CMP_AND_SWP) ||
	    (opcode == IBV_WR_ATOMIC_FETCH_AND_ADD))
		if (length < 8 || ibwr->wr.atomic.remote_addr & 0x7)
			return EINVAL;

	if ((ibwr->send_flags & IBV_SEND_INLINE) &&
	    (length > sq->wq.max_inline))
		return EINVAL;

	if (ibwr->opcode == IBV_WR_BIND_MW) {
		if (length)
			return EINVAL;
		if (ibwr->num_sge)
			return EINVAL;
		if (ibwr->imm_data)
			return EINVAL;
		if (qp->vqp.qp.qp_type != IBV_QPT_RC &&
		    qp->vqp.qp.qp_type != IBV_QPT_UC)
			return EINVAL;
	}
	return 0;
}

static inline int send_wr_to_dev(struct grvf_qp *qp, struct ibv_send_wr *ibwr)
{
	struct grvf_ah *ah;
	uint8_t *wr_dev;
	uint32_t len = 0;
	uint32_t len_r;
	uint8_t *inline_buf;
	uint32_t psn_first;
	union sq_entry_t wr_sge;
	union sq_entry_t wr_hdr[5];
	size_t wr_hdr_len, i;

	if (qp->cur_qp_state < IBV_QPS_RTS)
		return EINVAL; // can post send request in current qp state

	if (qp->qp_type == IBV_QPT_UD && (ibwr->send_flags & IBV_SEND_INLINE))
		return EINVAL;

	if (ibwr->opcode == IBV_WR_RDMA_READ &&
	    (ibwr->send_flags & IBV_SEND_INLINE))
		return EINVAL;

	if (ibwr->num_sge > qp->sq.wq.max_sge)
		return EINVAL; // num_sge is too large

	wr_dev = qp->sq.wq.dev + qp->sq.prod * qp->sq.wr_size;

	wr_hdr[0].wr.wr_id = ibwr->wr_id;
	wr_hdr[0].wr.opcoode = ibwr->opcode;
	wr_hdr[0].wr.flags = ibwr->send_flags;
	wr_hdr[0].wr.num_sge = ibwr->num_sge;

	if (qp->qp_type == IBV_QPT_UD) {
		wr_hdr[1].impl.ud.remote_qkey = ibwr->wr.ud.remote_qkey;
		wr_hdr[1].impl.ud.remote_qpn = ibwr->wr.ud.remote_qpn;
	}

	wr_hdr[1].impl.imm_data = ibwr->imm_data;
	// wr_hdr[1].impl.tx_sideband_tag = wqe->tx_sideband; FIXME

	switch (ibwr->opcode) {
	case IBV_WR_RDMA_WRITE:
	case IBV_WR_RDMA_WRITE_WITH_IMM:
	case IBV_WR_RDMA_READ:
		wr_hdr[2].wr_rdma_ud_mr.rdma.length = 0xFFFFFFFF;
		wr_hdr[2].wr_rdma_ud_mr.rdma.rkey = ibwr->wr.rdma.rkey;
		wr_hdr[2].wr_rdma_ud_mr.rdma.vaddr = ibwr->wr.rdma.remote_addr;
		break;

	case IBV_WR_SEND:
	case IBV_WR_SEND_WITH_IMM:
	case IBV_WR_SEND_WITH_INV:
		if (qp->qp_type == IBV_QPT_UD) {
			ah = container_of(ibwr->wr.ud.ah, struct grvf_ah,
					  ibv_ah);

			if (ah->av.network_type == GRVF_NETWORK_TYPE_IPV4) {
				wr_hdr[2].wr_rdma_ud_mr.ah.gid[3] = 0;
				wr_hdr[2].wr_rdma_ud_mr.ah.gid[2] = 0;
				wr_hdr[2].wr_rdma_ud_mr.ah.gid[1] = 0xffff0000;
				wr_hdr[2].wr_rdma_ud_mr.ah.gid[0] =
					ah->av.dgid_addr._sockaddr_in.sin_addr
						.s_addr;

				wr_hdr[3].wr_rdma_ud_mr.ah.gid[3] = 0;
				wr_hdr[3].wr_rdma_ud_mr.ah.gid[2] = 0;
				wr_hdr[3].wr_rdma_ud_mr.ah.gid[1] = 0xffff0000;
				wr_hdr[3].wr_rdma_ud_mr.ah.gid[0] =
					ah->av.sgid_addr._sockaddr_in.sin_addr
						.s_addr;
			} else {
				wr_hdr[2].wr_rdma_ud_mr.ah.gid[3] =
					ah->av.dgid_addr._sockaddr_in6.sin6_addr
						.__in6_u.__u6_addr32[0];
				wr_hdr[2].wr_rdma_ud_mr.ah.gid[2] =
					ah->av.dgid_addr._sockaddr_in6.sin6_addr
						.__in6_u.__u6_addr32[1];
				wr_hdr[2].wr_rdma_ud_mr.ah.gid[1] =
					ah->av.dgid_addr._sockaddr_in6.sin6_addr
						.__in6_u.__u6_addr32[2];
				wr_hdr[2].wr_rdma_ud_mr.ah.gid[0] =
					ah->av.dgid_addr._sockaddr_in6.sin6_addr
						.__in6_u.__u6_addr32[3];

				wr_hdr[3].wr_rdma_ud_mr.ah.gid[3] =
					ah->av.sgid_addr._sockaddr_in6.sin6_addr
						.__in6_u.__u6_addr32[0];
				wr_hdr[3].wr_rdma_ud_mr.ah.gid[2] =
					ah->av.sgid_addr._sockaddr_in6.sin6_addr
						.__in6_u.__u6_addr32[1];
				wr_hdr[3].wr_rdma_ud_mr.ah.gid[1] =
					ah->av.sgid_addr._sockaddr_in6.sin6_addr
						.__in6_u.__u6_addr32[2];
				wr_hdr[3].wr_rdma_ud_mr.ah.gid[0] =
					ah->av.sgid_addr._sockaddr_in6.sin6_addr
						.__in6_u.__u6_addr32[3];
			}

			if (ah->av.network_type == GRVF_NETWORK_TYPE_IPV6) {
				wr_hdr[0].wr.flags |= SQ_WR_UD_IPV6;
			}

			memcpy(&wr_hdr[4].ud_hw.hw_addr_src, ah->av.smac,
			       ETH_ALEN);

			if (ibwr->wr.ud.remote_qpn != QPN_MCAST) {
				memcpy(&wr_hdr[4].ud_hw.hw_addr_dst,
				       ah->av.dmac, ETH_ALEN);
			} else {
				//mac addr for mqp_typecast ud
				if (ah->av.network_type ==
				    GRVF_NETWORK_TYPE_IPV4) {
					//printf("IPV4_MCAST_MAC\n");

					wr_hdr[4].ud_hw.hw_addr_dst =
						IPV4_MCAST_MAC;
					wr_hdr[4].ud_hw.hw_addr_dst |=
						((uint64_t)0xFFFFFFF00 &
						 wr_hdr[2]
							 .wr_rdma_ud_mr.ah
							 .gid[0])
						<< 16;

				} else {
					wr_hdr[4].ud_hw.hw_addr_dst =
						IPV6_MCAST_MAC;
					wr_hdr[4].ud_hw.hw_addr_dst |=
						(uint64_t)wr_hdr[2]
							.wr_rdma_ud_mr.ah.gid[0]
						<< 16;
				}
			}
		}
		break;
	default:
		return EINVAL;
	}

	wr_hdr_len = qp->sq.wr_hdr_size;
	len = 0;

	if ((ibwr->send_flags & IBV_SEND_INLINE) &&
	    ibwr->opcode != IBV_WR_LOCAL_INV &&
	    ibwr->opcode != IBV_WR_RDMA_READ) {
		// inline mode
		inline_buf = qp->inline_buf;

		for (i = 0; i < ibwr->num_sge; ++i) {
			len += ibwr->sg_list[i].length;

			if (len > qp->sq.wq.max_inline)
				return EINVAL;

			if (ibwr->sg_list[i].length > 0) {
				memcpy(inline_buf,
				       (uint8_t *)(long)ibwr->sg_list[i].addr,
				       ibwr->sg_list[i].length);
			}

			inline_buf += ibwr->sg_list[i].length;
		}

		wr_hdr[0].wr.num_sge = 0;
		for (i = 0; i < len; i += 8) {
			memcpy(wr_dev + (wr_hdr_len << 4) + i,
			       (uint64_t *)(qp->inline_buf + i),
			       sizeof(uint64_t));
		}

		wr_hdr[0].wr.num_sge = len >> 4;
		if (len & 0xF)
			wr_hdr[0].wr.num_sge += 1;
	} else {
		for (i = 0; i < ibwr->num_sge; i++) {
			wr_sge.sge.lkey = ibwr->sg_list[i].lkey;
			wr_sge.sge.size = ibwr->sg_list[i].length;

			len += ibwr->sg_list[i].length;

			wr_sge.sge.vaddr = ibwr->sg_list[i].addr;

			memcpy(wr_dev + ((wr_hdr_len + i) << 4),
			       &wr_sge.raw.qw0, sizeof(wr_sge.raw.qw0));
			memcpy(wr_dev + ((wr_hdr_len + i) << 4) + 8,
			       &wr_sge.raw.qw1, sizeof(wr_sge.raw.qw1));
		}
	}

	psn_first = qp->sq.psn;

	if (qp->qp_type == IBV_QPT_UD) {
		qp->sq.psn++;
	} else if (!is_wr_local_operation(ibwr->num_sge)) {
		if (len != 0) {
			len_r = len >> (qp->path_mtu + 7);
			qp->sq.psn += len_r;

			if (len_r << (qp->path_mtu + 7) != len) {
				qp->sq.psn++;
			}
		} else {
			qp->sq.psn++;
		}

		qp->sq.psn = qp->sq.psn & 0xFFFFFF;
	}

	wr_hdr[0].wr.psn_last = (qp->sq.psn + 0xFFFFFF) & 0xFFFFFF;

	if (qp->qp_type != IBV_QPT_UD && !is_wr_local_operation(ibwr->opcode)) {
		wr_hdr[1].impl.rc.psn_first = psn_first;
		wr_hdr[2].wr_rdma_ud_mr.rdma.length = len;
	}

	if (len > 0x80000000)
		return EINVAL; // can't send frame, too big

	for (i = 0; i < wr_hdr_len; ++i) {
		memcpy(wr_dev + (i << 4), &wr_hdr[i].raw.qw0,
		       sizeof(wr_hdr[i].raw.qw0));
		memcpy(wr_dev + (i << 4) + 8, &wr_hdr[i].raw.qw1,
		       sizeof(wr_hdr[i].raw.qw1));
	}

	qp->sq.prod = (qp->sq.prod + 1) & qp->sq.wr_count_mask;

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

		return err;
	}
	err = send_wr_to_dev(qp, ibwr);
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
