// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2023-2024, Grovf, Inc. All rights reserved.
 */

#ifndef GRVF_API_H
#define GRVF_API_H

#include <infiniband/grvf_user_ioctl_verbs.h>

#define grvfdv_qp_custom_attr_mask grvf_ib_uapi_qp_custom_attr_mask
#define grvfdv_qp_custom_attr grvf_ib_uapi_qp_custom_attr
#define grvfdv_dbg_mon_type grvf_ib_uapi_dbg_mon_type
#define grvfdv_dbg_mon_tx_events_type grvf_ib_uapi_dbg_mon_tx_events_type
#define grvfdv_dbg_mon_rx_events_type grvf_ib_uapi_dbg_mon_rx_events_type
#define grvfdv_dbg_mon_global_events_type                                      \
	grvf_ib_uapi_dbg_mon_global_events_type
#define grvfdv_congestion_cnp_attrs grvf_ib_uapi_congestion_cnp_attrs

#endif