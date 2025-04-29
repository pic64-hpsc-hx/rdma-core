// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2023-2024, Grovf, Inc. All rights reserved.
 */

#ifndef GRVF_H
#define GRVF_H

#include <infiniband/driver.h>
#include <infiniband/verbs.h>
#include "grvf-abi.h"

struct grvf_device {
	struct verbs_device verbs_dev;
	int driver_abi_ver;
};

struct grvf_context {
	struct verbs_context ibv_ctx;
};

struct grvf_ah {
	struct ibv_ah ibv_ah;
	struct grvf_av av;
};

struct grvf_cq {
	struct verbs_cq vcq;
	struct grvf_mminfo mmap_info;

	pthread_spinlock_t lock;

	/* new API support */
	struct ib_uverbs_wc *wc;
	size_t wc_size;
	uint32_t cur_index;

	uint8_t *dev;
	uint32_t cons;
	uint32_t size_mask;
};

struct grvf_wq {
	uint8_t *dev;
	uint8_t *db_dev;

	pthread_spinlock_t lock;
	unsigned int max_sge;
	unsigned int max_inline;
};

struct grvf_rq {
	struct grvf_wq wq;

	struct grvf_mminfo mmap_info;
	struct grvf_mminfo db_mmap_info;

	uint32_t num;

	uint32_t wr_count_mask;
	uint32_t wr_size;
	uint32_t prod;
};

struct grvf_sq {
	struct grvf_wq wq;

	struct grvf_mminfo mmap_info;
	struct grvf_mminfo db_mmap_info;

	uint32_t num;

	uint32_t prod;
	uint32_t wr_hdr_size;
	uint32_t wr_size;
	uint32_t wr_size_elements;
	uint32_t wr_count_mask;
	uint32_t psn;
};

struct grvf_qp {
	struct verbs_qp vqp;
	struct grvf_rq rq;
	struct grvf_sq sq;

	/* new API support */
	uint32_t cur_index;
	int err;

	enum ibv_qp_state cur_qp_state;
	enum ibv_qp_type qp_type;
	uint8_t *inline_buf;
	enum ibv_mtu path_mtu;
};

struct grvf_srq {
	struct verbs_srq vsrq;
	struct grvf_rq rq;
};

#endif
