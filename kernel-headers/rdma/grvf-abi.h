// SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR Linux-OpenIB)
/*
 * Copyright (c) 2023-2024, Grovf, Inc. All rights reserved.
 */

#ifndef GRVF_ABI_USER_H
#define GRVF_ABI_USER_H

#include <linux/types.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/if_ether.h>

/* Increment this value if any changes that break userspace ABI
 * compatibility are made.
 */
#define GRVF_IB_UVERBS_ABI_VERSION 1

struct grvf_ib_create_ah_resp {
	__u8 smac[ETH_ALEN];
	__u8 reserved[2];
};

struct grvf_ib_alloc_pd_resp {
	__u32 index;
};

union grvf_gid {
	__u8 raw[16];
	struct {
		__be64 subnet_prefix;
		__be64 interface_id;
	} global;
};

struct grvf_global_route {
	union grvf_gid dgid;
	__u32 flow_label;
	__u8 sgid_index;
	__u8 hop_limit;
	__u8 traffic_class;
};

struct grvf_av {
	__u8 port_num;
	__u8 network_type;
	__u8 smac[ETH_ALEN];
	__u8 dmac[ETH_ALEN];
	struct grvf_global_route grh;
	union {
		struct sockaddr_in _sockaddr_in;
		struct sockaddr_in6 _sockaddr_in6;
	} sgid_addr, dgid_addr;
};

struct grvf_mminfo {
	__aligned_u64 offset;
	__u32 size;
	__u32 pad;
};

struct grvf_ib_create_cq_resp {
	struct grvf_mminfo mi;
};

struct grvf_ib_create_qp_resp {
	struct {
		struct grvf_mminfo mi;
		struct grvf_mminfo db_mi;
		__u32 num;
	} rq;
	struct {
		struct grvf_mminfo mi;
		struct grvf_mminfo db_mi;
		__u32 num;
	} sq;
};

struct grvf_ib_create_srq_resp {
	struct grvf_mminfo mi;
	struct grvf_mminfo db_mi;
	__u32 num;
};

struct grvf_ib_modify_srq {
	__aligned_u64 mmap_info_addr;
};

#endif
