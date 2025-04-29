#pragma once

#include "grovf-rdma-types.h"

//WARN: little-endian ordering

////////////////////////////////////////////////////
//
// RQ data structures
//

struct rq_wqe_t {
	u64 wr_id;
	u32 num_sge;
	u32 length; // written by RDMA IP (not by host)
};

struct rq_sge_t {
	u64 vaddr;
	u32 lkey;
	u32 size;
};

struct rq_raw_t {
	u64 qw0;
	u64 qw1;
};

union rq_entry_t {
	struct rq_wqe_t wqe;
	struct rq_sge_t sge;
	struct rq_raw_t raw;
};

//
// RQ data structures
//
////////////////////////////////////////////////////

////////////////////////////////////////////////////
//
// SQ data structures
//

struct sq_wr_t {
	u64 wr_id;

	u8 num_sge;
	u8 opcoode;

//#define  SQ_WR_HW_CALC_LENGTH (1 << 15)
#define SQ_WR_UD_IPV6 (1 << 15)
	u16 flags;

	u32 psn_last;
};

struct sq_wr_rdma_ah_mr_t {
	union {
		struct {
			u64 vaddr;
			u32 rkey;
			u32 length;
		} rdma;

		struct {
			u32 gid[4];
		} ah;

		struct {
			u32 lkey;
			u32 flags;
			u32 length;
			u16 pd;
			u16 page_offset;
		} mr;
	};
};

struct sq_impl_specific_or_ud_t {
	u32 tx_sideband_tag;
	union {
		u32 imm_data;
		u32 inv_rkey;
	};

	//if WR is UD
	union {
		struct {
			u32 remote_qkey;
			u32 remote_qpn;
		} ud;

		struct {
			u32 psn_first;
			u32 dummy;
		} rc;
	};
};

struct sq_ah_t {
	u32 gid[4];
};

struct sq_sge_t {
	u64 vaddr;
	u32 lkey;
	u32 size;
};

struct sq_ud_hw_addr_t {
	u64 hw_addr_src;
	u64 hw_addr_dst;
};

struct sq_raw_t {
	u64 qw0;
	u64 qw1;
};

struct sq_mr_inv_t {
	u32 lkey;
	u32 dummy0;
	u64 dummy1;
};

struct sq_mr_reg2_t {
	u64 vaddr;
	u64 page_phaddr;
};

union sq_entry_t {
	struct sq_wr_t wr;
	struct sq_wr_rdma_ah_mr_t wr_rdma_ud_mr;
	struct sq_impl_specific_or_ud_t impl;
	struct sq_mr_reg2_t mr2;
	struct sq_ah_t ah;
	struct sq_sge_t sge;
	struct sq_raw_t raw;
	struct sq_ud_hw_addr_t ud_hw;
	struct sq_mr_reg2_t mr_reg2;
};

//
// SQ data structures
//
////////////////////////////////////////////////////

////////////////////////////////////////////////////
//
// COMPLETION
//

#define CQ_ENTRY_SIZE 40

#define CQ_ENTRY_STRIDE 64
#define CQ_ENTRY_STRIDE_BITS 6

struct cq_entry_t {
	union {
		struct {
			u64 qw0;
			u64 qw1;
			u64 qw2;
			u64 qw3;
			union {
				u64 qw4; //highest bit is a flag
				struct {
					u8 reserved[7];
#define CQ_FLAG_RDY (1 << 0)
					u8 flag;
				};
			};
		} raw;

		struct {
			u8 b[40];
		} raw_bytes;

		struct {
			u64 wr_id; /* ID of the completed Work Request (WR) */
			u32 verndor_err; /* Vendor error syndrome */
			u32 byte_len; /* Number of bytes transferred */
			union {
				u32 imm_data; /* Immediate data (in network byte order) */
				u32 invalidate_rkey; /* Local RKey that was invalidated */
			};
			u32 qp_num; /* Local QP number of completed WR */
			u32 src_qp; /* Source QP number (remote QP number) of completed WR (valid only for UD QPs) */
			u32 wc_flags; //ibv_wc_flags		/* Flags of the completed WR */
			u16 pkey_index; /* P_Key index (valid only for GSI QPs) */
			u8 status; //ibv_wc_status			/* Status of the operation */
			u8 opcode; //ibv_wc_opcode			/* Operation type specified in the completed WR */
			u32 reserved;
		} wc;
	};
};

//
// COMPLETION
//
////////////////////////////////////////////////////

////////////////////////////////////////////////////
//
// EVENT
//

enum event_results_t {
	event_type_cmd_result = 1,
	event_type_cq_completion_notification = 2,
	event_type_async_dev_event = 3,
	event_dbg_mon = 4,
	event_congestion = 5
};

#define EV_ENTRY_SIZE 32

#define EV_ENTRY_STRIDE 32
#define EV_ENTRY_STRIDE_BITS 5

struct ev_entry_t {
	union {
		struct {
			u64 qw0;
			u64 qw1;
			u64 qw2;
			union {
				u64 qw3;
				struct {
					u8 reserved[7];

#define EV_ENTRY_ACTIVE (1 << 0)
					u8 flag;
				};
			};
		} raw;

		struct {
			struct {
				u32 id_ev;
				u32 id_obj;
			} hdr;

			union {
				//CQ requested async event (by ibv_req_notify_cq)
				struct {
					//TBD: remove private data
					//u64 cq_private_data;
					u32 cq_num;
					u8 reserverd[12];
				} cq_cn_ev;

				//device requested async event
				struct {
					union {
						u32 qp_num;
						u32 srq_num;
						u32 obj_num;
					};
					u16 ibv_event_type;
					u8 reserverd[10];
				} dev_ev;

				struct {
					u64 qw0;
					u64 qw1;
				} raw_ev;

				struct {
					u16 host_tag;
					u16 error_code;
					u32 vendor_error_code;
					u32 param1;
					u32 param2;
				} cmd_ev;

				struct {
					u32 drop_reason;
					u32 drop_counter;
					u16 dmon_id;
					u8 dmon_type;
				} dbgmon_ev;

				struct {
					u32 cnp_packet_count;
					u32 total_frames_generated;
					u8 remote_ecn_support_type;
					u8 ecn_event_type;
				} congestion_ev;

			} bdy;
		} ev;
	};
};

//
// EVENT
//
////////////////////////////////////////////////////
