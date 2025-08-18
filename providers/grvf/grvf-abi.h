// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2023-2024, Grovf, Inc. All rights reserved.
 */

#ifndef GRVF_ABI_H
#define GRVF_ABI_H

#include <infiniband/kern-abi.h>
#include <kernel-abi/grvf-abi.h>
#include <rdma/grvf-abi.h>

#define GRVF_ABI_VERSION 1

DECLARE_DRV_CMD(grvf_create_ah, IB_USER_VERBS_CMD_CREATE_AH, empty,
		grvf_ib_create_ah_resp);

DECLARE_DRV_CMD(grvf_alloc_pd, IB_USER_VERBS_CMD_ALLOC_PD, empty,
		grvf_ib_alloc_pd_resp);

DECLARE_DRV_CMD(grvf_create_cq, IB_USER_VERBS_CMD_CREATE_CQ, empty,
		grvf_ib_create_cq_resp);

DECLARE_DRV_CMD(grvf_create_cq_ex, IB_USER_VERBS_EX_CMD_CREATE_CQ, empty,
		grvf_ib_create_cq_resp);

DECLARE_DRV_CMD(grvf_create_qp, IB_USER_VERBS_CMD_CREATE_QP, empty,
		grvf_ib_create_qp_resp);

DECLARE_DRV_CMD(grvf_create_qp_ex, IB_USER_VERBS_EX_CMD_CREATE_QP, empty,
		grvf_ib_create_qp_resp);

DECLARE_DRV_CMD(grvf_create_srq, IB_USER_VERBS_CMD_CREATE_SRQ, empty,
		grvf_ib_create_srq_resp);

DECLARE_DRV_CMD(grvf_modify_srq, IB_USER_VERBS_CMD_MODIFY_SRQ,
		grvf_ib_modify_srq, empty);

#endif
