// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2023-2024, Grovf, Inc. All rights reserved.
 */

#ifndef GRVFDV_H
#define GRVFDV_H

#include <infiniband/grvf_api.h>

#ifdef __cplusplus
extern "C" {
#endif

int grvfdv_modify_qp_custom(struct ibv_qp *ibvqp,
			    struct grvfdv_qp_custom_attr *attr, int attr_mask);

int grvfdv_query_tx_dbg_mon(struct ibv_context *context,
			    enum grvfdv_dbg_mon_tx_events_type event_type,
			    uint32_t *counter);

int grvfdv_query_rx_dbg_mon(struct ibv_context *context,
			    enum grvfdv_dbg_mon_rx_events_type event_type,
			    uint32_t *counter);

int grvfdv_query_global_dbg_mon(
	struct ibv_context *context,
	enum grvfdv_dbg_mon_global_events_type event_type, uint32_t *counter);

int grvfdv_query_cnp(struct ibv_qp *ibvqp,
		     struct grvfdv_congestion_cnp_attrs *attr);

#ifdef __cplusplus
}
#endif

#endif
