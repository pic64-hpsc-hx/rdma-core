// SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR Linux-OpenIB)
/*
 * Copyright (c) 2023-2024, Grovf, Inc. All rights reserved.
 */

#ifndef GRVF_USER_IOCTL_CMDS_H
#define GRVF_USER_IOCTL_CMDS_H

#include <linux/types.h>
#include <rdma/ib_user_ioctl_cmds.h>

enum grvf_ib_modify_qp_custom_attrs {
	GRVF_IB_ATTR_MODIFY_QP_CUSTOM_HANDLE = (1U << UVERBS_ID_NS_SHIFT),
	GRVF_IB_ATTR_MODIFY_QP_ATTRS,
	GRVF_IB_ATTR_MODIFY_QP_MASK,
};

enum grvf_ib_query_cnp_attrs {
	GRVF_IB_ATTR_QUERY_CPN_HANDLE = (1U << UVERBS_ID_NS_SHIFT),
	GRVF_IB_ATTR_QUERY_CPN_INFO_ATTRS
};

enum grvf_ib_qp_methods {
	GRVF_IB_METHOD_MODIFY_QP_CUSTOM = (1U << UVERBS_ID_NS_SHIFT),
	GRVF_IB_METHOD_QUERY_CNP
};

enum grvf_ib_objects {
	GRVF_IB_OBJECT_DBG_MON = (1U << UVERBS_ID_NS_SHIFT),
};

enum grvf_ib_query_tx_dbg_mon_attrs {
	GRVF_IB_ATTR_QUERY_TX_DBG_MON_TYPE = (1U << UVERBS_ID_NS_SHIFT),
	GRVF_IB_ATTR_QUERY_TX_DBG_MON_COUNTER,
};

enum grvf_ib_query_rx_dbg_mon_attrs {
	GRVF_IB_ATTR_QUERY_RX_DBG_MON_TYPE = (1U << UVERBS_ID_NS_SHIFT),
	GRVF_IB_ATTR_QUERY_RX_DBG_MON_COUNTER,
};

enum grvf_ib_query_global_dbg_mon_attrs {
	GRVF_IB_ATTR_QUERY_GLOBAL_DBG_MON_TYPE = (1U << UVERBS_ID_NS_SHIFT),
	GRVF_IB_ATTR_QUERY_GLOBAL_DBG_MON_COUNTER,
};

enum grvf_ib_dbg_mon_methods {
	GRVF_IB_METHOD_QUERY_TX_DBG_MON = (1U << UVERBS_ID_NS_SHIFT),
	GRVF_IB_METHOD_QUERY_RX_DBG_MON,
	GRVF_IB_METHOD_QUERY_GLOBAL_DBG_MON
};

#endif