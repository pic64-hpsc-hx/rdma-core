// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2023-2024, Grovf, Inc. All rights reserved.
 */

#ifndef GRVF_IP_H
#define GRVF_IP_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <infiniband/verbs.h>
#include "grvf.h"

enum { GRVF_NETWORK_TYPE_IPV4 = 0, GRVF_NETWORK_TYPE_IPV6 = 1 };

typedef typeof(((struct grvf_av *)0)->sgid_addr) sockaddr_union_t;

static inline int rdma_gid2ip(sockaddr_union_t *out, union ibv_gid *gid)
{
	if (IN6_IS_ADDR_V4MAPPED((struct in6_addr *)gid)) {
		memset(&out->_sockaddr_in, 0, sizeof(out->_sockaddr_in));
		memcpy(&out->_sockaddr_in.sin_addr.s_addr, gid->raw + 12, 4);
	} else {
		memset(&out->_sockaddr_in6, 0, sizeof(out->_sockaddr_in6));
		out->_sockaddr_in6.sin6_family = AF_INET6;
		memcpy(&out->_sockaddr_in6.sin6_addr.s6_addr, gid->raw, 16);
	}
	return 0;
}

#endif
