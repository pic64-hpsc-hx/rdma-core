// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2023-2024, Grovf, Inc. All rights reserved.
 */

#ifndef GRVF_POST_H
#define GRVF_POST_H

#include <stdint.h>
#include "grvf.h"

int grvf_post_one_send(struct grvf_qp *qp, struct ibv_send_wr *wr);
int grvf_post_one_recv(struct grvf_rq *rq, struct ibv_recv_wr *wr);
void grvf_post_send_db(struct grvf_sq *sq, uint32_t wr_count);
void grvf_post_recv_db(struct grvf_rq *rq, uint32_t wr_count);

#endif
