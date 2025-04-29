#pragma once

#include "grovf-rdma-types.h"

enum rdma_commands_t {

	cmd_begin = 0,

	//commands for RX
	cmd_rx_qp_create_id,
	cmd_rx_qp_set_qp_transport_type_id,
	cmd_rx_qp_set_cq_num_id,
	cmd_rx_qp_set_access_flag_id,
	cmd_rx_qp_set_pkey_REMOVED_id,
	cmd_rx_qp_modify_id,
	cmd_rx_qp_set_mtu_shift_id,
	cmd_rx_qp_set_qp_num_dest_id,
	cmd_rx_qp_set_psn_id,
	cmd_rx_qp_set_max_dest_rd_atomic_id,
	cmd_rx_qp_set_min_rnr_timer_id,
	cmd_rx_qp_set_local_gid_0_id,
	cmd_rx_qp_set_local_gid_1_id,
	cmd_rx_qp_set_local_gid_2_id,
	cmd_rx_qp_set_local_gid_3_id,
	cmd_rx_qp_set_ip_mode_id,
	cmd_rx_qp_set_remote_hw_addr_lo_id,
	cmd_rx_qp_set_remote_hw_addr_hi_id,
	cmd_rx_qp_set_remote_gid_0_id,
	cmd_rx_qp_set_remote_gid_1_id,
	cmd_rx_qp_set_remote_gid_2_id,
	cmd_rx_qp_set_remote_gid_3_id,
	cmd_rx_qp_set_rq_num_id,
	cmd_rx_qp_set_priveleged_access_id,
	cmd_rx_qp_set_pd_id,
	cmd_rx_qp_reset_id,
	cmd_rx_qp_query_state_id,
	cmd_rx_qp_set_qkey_id,
	cmd_rx_qp_set_wr_sideband_id,
	cmd_rx_qp_set_local_hw_addr_lo_id,
	cmd_rx_qp_set_local_hw_addr_hi_id,
	cmd_rx_qp_set_src_udp_port_id,
	cmd_rx_qp_set_hw_ip_udp_validation_id,
	cmd_rx_qp_query_psn_id,
	cmd_rx_qp_set_vm_id,
	cmd_rx_qp_query_cnp_current_counters_id,
	cmd_rx_qp_set_ecn_local_support_type_id,
	cmd_rx_qp_set_cnp_imp_type_id,
	cmd_rx_qp_ack_cnp_id,

	cmd_rx_qp_cmd_end = 40,

	cmd_rx_rq_create_id,
	cmd_rx_rq_set_max_wr_id,
	cmd_rx_rq_set_max_sqe_id,
	cmd_rx_rq_set_phaddr_lo_id,
	cmd_rx_rq_set_phaddr_hi_id,
	cmd_rx_rq_doorbell_id,
	//cmd_rx_rq_set_pkey_id,
	cmd_rx_rq_set_srq_id,
	cmd_rx_rq_set_qp_num_id,
	cmd_rx_rq_set_srq_limit_id,
	cmd_rx_rq_set_vm_id,

	cmd_rx_rq_cmd_end = 55,

	cmd_mr_set_flags_id,
	cmd_mr_set_vaddr_lo_id,
	cmd_mr_set_vaddr_hi_id,
	cmd_mr_set_pagetable_phaddr_lo_id,
	cmd_mr_set_pagetable_phaddr_hi_id,
	cmd_mr_set_page_offset_id,
	cmd_mr_set_length_id,
	cmd_mr_set_pd_id,

	cmd_cq_cmd_begin = 70,

	cmd_cq_reset_id,
	cmd_cq_add_flags_id,
	cmd_cq_clear_flags_id,
	cmd_cq_set_size_id,
	cmd_cq_set_phaddr_lo_id,
	cmd_cq_set_phaddr_hi_id,
	cmd_cq_set_iommu_tag_id,
	cmd_cq_set_vm_id,

	cmd_tx_cmd_begin = 80,

	cmd_tx_qp_create_id,
	cmd_tx_qp_set_qp_transport_type_id,
	cmd_tx_qp_set_cq_num_id,
	//cmd_tx_qp_set_access_flag_id,
	cmd_tx_qp_set_pkey_REMOVED_id,
	cmd_tx_qp_modify_id,
	cmd_tx_qp_set_mtu_shift_id,
	cmd_tx_qp_set_qp_num_dest_id,
	cmd_tx_qp_set_psn_id,
	cmd_tx_qp_set_local_gid_0_id,
	cmd_tx_qp_set_local_gid_1_id,
	cmd_tx_qp_set_local_gid_2_id,
	cmd_tx_qp_set_local_gid_3_id,
	cmd_tx_qp_set_ip_mode_id,
	cmd_tx_qp_set_remote_hw_addr_lo_id,
	cmd_tx_qp_set_remote_hw_addr_hi_id,
	cmd_tx_qp_set_remote_gid_0_id,
	cmd_tx_qp_set_remote_gid_1_id,
	cmd_tx_qp_set_remote_gid_2_id,
	cmd_tx_qp_set_remote_gid_3_id,
	cmd_tx_qp_set_priveleged_access_id,
	cmd_tx_qp_set_pd_id,
	cmd_tx_qp_reset_id,
	cmd_tx_qp_set_max_wr_id,
	cmd_tx_qp_set_max_sge_id,
	cmd_tx_qp_query_state_id,
	cmd_tx_qp_set_sq_phaddr_lo_id,
	cmd_tx_qp_set_sq_phaddr_hi_id,
	cmd_tx_qp_doorbell_id,
	cmd_tx_qp_set_wr_size_id,
	cmd_tx_qp_set_wr_hdr_size_id,
	cmd_tx_qp_set_max_rd_atomic_id,
	cmd_tx_qp_set_timeout_id,
	cmd_tx_qp_set_rnr_max_id,
	cmd_tx_qp_set_retry_max_id,
	cmd_tx_qp_set_sig_all_id,
	cmd_tx_qp_set_local_hw_addr_lo_id,
	cmd_tx_qp_set_local_hw_addr_hi_id,
	cmd_tx_qp_set_src_udp_port_id,
	cmd_tx_qp_set_hw_ip_udp_validation_id,
	cmd_tx_qp_set_sqd_enable_async_notify_id,
	cmd_tx_qp_query_psn_id,
	cmd_tx_qp_set_vm_id,
	cmd_tx_qp_set_RESERVED_tag,

	cmd_tx_qp_set_ecn_support_type_id,
	cmd_tx_qp_set_cnp_time_period_id,
	cmd_tx_qp_set_cnp_imp_type_id,
	cmd_tx_qp_query_cnp_current_counters_REMOVED_id,
	cmd_tx_qp_ack_ecn_type_change_id,
	cmd_tx_qp_set_cnp_generation_ratio_id,
	cmd_tx_qp_set_explicit_ack_req_upper_bound,

	cmd_tx_cmd_end_rx_dbgmon_begin = 150,

	cmd_rx_dbgmon_reset_id,
	cmd_rx_dbgmon_query_counter_id,
	cmd_rx_dbgmon_ack_event_id,

	cmd_rx_dbgmon_end_tx_dbgmon_begin = 160,

	cmd_tx_dbgmon_reset_id,
	cmd_tx_dbgmon_query_counter_id,
	cmd_tx_dbgmon_ack_event_id,
	cmd_tx_dbgmon_end = 170,

	cmd_qp1_tbl_begin = 180,

	cmd_qp1_tbl_add_hw_addr_hi_id,
	cmd_qp1_tbl_add_hw_addr_lo_id,
	cmd_qp1_tbl_add_hw_addr_set_enabled_id,
	cmd_qp1_tbl_add_ipv4_addr_id,
	cmd_qp1_tbl_add_ipv4_set_enabled_id,
	cmd_qp1_tbl_add_ipv6_addr_id,
	cmd_qp1_tbl_add_ipv6_set_enabled_id,
	cmd_qp1_tbl_set_qp_num_id,

	cmd_qp1_tbl_end = 190,

	cmd_rx_cmd_mcast_begin = 240,
	cmd_rx_qp_mcast_add_qp_id,
	cmd_rx_qp_mcast_remove_qp_id,
	cmd_rx_qp_mcast_clear_all_vm_id,
	cmd_rx_cmd_mcast_end = 250

};
//WARN: little-endian ordering

struct cmd64_id_t {
	u8 cmd_id;
	union {
		u8 qp_id_hi;
		u8 rq_id_hi;
		u8 mr_id_hi;
		u8 cq_id_hi;
		u8 mg_id_hi;
		u8 dm_id_hi;
		u8 qp1_addr;
	};

	union {
		u16 qp_id_lo;
		u16 rq_id_lo;
		u16 mr_id_lo;
		u16 cq_id_lo;
		u16 mg_id_lo;
		u16 dm_id_lo;
		u16 qp1_vm;
	};
};

typedef u64 cmd64_raw_t;

struct cmd64_t {
	union {
		cmd64_raw_t cmd_raw;

		struct {
			struct cmd64_id_t hdr;

			///////////////////////////////////////////////////////////////
			//
			// RX_DEV commands
			//

			union {
				///////////////////////////////////////////////////////////////
				//
				// RX_DEV commands
				//

				struct {
					u32 cmd_tag;
				} cmd_rx_qp_create;

				struct {
					u32 transport_type;
				} cmd_rx_qp_set_qp_transport_type;

				struct {
					u32 cq_num;
				} cmd_rx_qp_set_cq_num;

				struct {
					u32 qp_access_flag;
				} cmd_rx_qp_access_flag;

				//				struct {
				//					u32 pkey;
				//				} cmd_rx_qp_pkey;

				struct {
					u16 cmd_tag;
					u16 new_state;
				} cmd_rx_qp_modify;

				struct {
					u32 mtu_shift;
				} cmd_rx_qp_set_mtu_shift;

				struct {
					u32 qp_num_dest;
				} cmd_rx_qp_set_qp_num_dest;

				struct {
					u32 psn;
				} cmd_rx_qp_set_psn;

				struct {
					u32 max_rd_atomic;
				} cmd_rx_qp_set_max_dest_rd_atomic;

				struct {
					u32 rnr_timer;
				} cmd_rx_qp_set_min_rnr_timer;

				struct {
					u32 gid_0;
				} cmd_rx_qp_set_local_gid_0;

				struct {
					u32 gid_1;
				} cmd_rx_qp_set_local_gid_1;

				struct {
					u32 gid_2;
				} cmd_rx_qp_set_local_gid_2;

				struct {
					u32 gid_3;
				} cmd_rx_qp_set_local_gid_3;

				struct {
					u32 ip_mode;
				} cmd_rx_qp_set_ip_mode;

				struct {
					u32 hw_addr_lo;
				} cmd_rx_qp_set_remote_hw_addr_lo;

				struct {
					u32 hw_addr_hi;
				} cmd_rx_qp_set_remote_hw_addr_hi;

				struct {
					u32 hw_addr_lo;
				} cmd_rx_qp_set_local_hw_addr_lo;

				struct {
					u32 hw_addr_hi;
				} cmd_rx_qp_set_local_hw_addr_hi;

				struct {
					u32 udp_port;
				} cmd_rx_qp_set_src_udp_port;

				struct {
					u32 val_hw_ip_port;
				} cmd_rx_qp_set_hw_ip_udp_validation;

				struct {
					u32 gid_0;
				} cmd_rx_qp_set_remote_gid_0;

				struct {
					u32 gid_1;
				} cmd_rx_qp_set_remote_gid_1;

				struct {
					u32 gid_2;
				} cmd_rx_qp_set_remote_gid_2;

				struct {
					u32 gid_3;
				} cmd_rx_qp_set_remote_gid_3;

				struct {
					u32 rq_num;
				} cmd_rx_qp_set_rq_num;

				struct {
					u32 priv_access;
				} cmd_rx_qp_set_priveleged_access;

				struct {
					u32 reserved;
				} cmd_rx_qp_reset;

				struct {
					u32 pd;
				} cmd_rx_qp_set_pd;

				struct {
					u32 cmd_tag;
				} cmd_rx_qp_query_state;

				struct {
					u32 srq_limit;
				} cmd_rx_rq_set_srq_limit;

				struct {
					u32 qkey;
				} cmd_rx_qp_set_qkey;

				struct {
					u32 sideband;
				} cmd_rx_qp_set_wr_sideband;

				struct {
					u32 activate_mcast;
				} cmd_rx_qp_set_ud_mcast;

				struct {
					u32 cmd_tag;
				} cmd_rx_qp_destroy;

				struct {
					u32 local_ecn_support_type;
				} cmd_rx_qp_set_ecn_local_support_type;

				struct {
					u32 impl_type;
				} cmd_rx_qp_set_cnp_imp_type;

				//
				// RX QP commands
				//
				///////////////////////////////////////////////////////////////

				///////////////////////////////////////////////////////////////
				//
				// RX RQ commands
				//

				struct {
					u32 max_wr;
				} cmd_rx_rq_set_max_wr;

				struct {
					u32 max_sge;
				} cmd_rx_rq_set_max_sqe;

				struct {
					u32 dummy;
				} cmd_rx_rq_create;

				struct {
					u32 rq_phaddr_lo;
				} cmd_rx_rq_set_phaddr_lo;

				struct {
					u32 rq_phaddr_hi;
				} cmd_rx_rq_set_phaddr_hi;

				//				struct {
				//					u32 pkey;
				//				} cmd_rx_rq_set_pkey;

				struct {
					u32 value;
				} cmd_rx_rq_doorbell;

				struct {
					u32 srq;
				} cmd_rx_rq_set_srq;

				struct {
					u32 qp_num;
				} cmd_rx_rq_set_qp_num;

				struct {
					u32 vm_id;
				} cmd_rx_qp_set_vm;

				struct {
					u32 vm_iommu_tag;
				} cmd_rx_qp_set_vm_iommu_tag;

				struct {
					u32 vm_id;
				} cmd_rx_rq_set_vm;

				//
				// RX RQ commands
				//
				///////////////////////////////////////////////////////////////

				///////////////////////////////////////////////////////////////
				//
				// MR LUT commands
				//

				struct {
					u32 flags;
				} cmd_mr_set_flags;

				struct {
					u32 vaddr_lo;
				} cmd_mr_set_vaddr_lo;

				struct {
					u32 vaddr_hi;
				} cmd_mr_set_vaddr_hi;

				struct {
					u32 phaddr_lo;
				} cmd_mr_set_pagetable_phaddr_lo;

				struct {
					u32 phaddr_hi;
				} cmd_mr_set_pagetable_phaddr_hi;

				struct {
					u32 offst;
				} cmd_mr_set_page_offset;

				struct {
					u32 len;
				} cmd_mr_set_length;

				struct {
					u32 pd;
				} cmd_mr_set_pd;

				//
				// MR LUT commands
				//
				///////////////////////////////////////////////////////////////

				///////////////////////////////////////////////////////////////
				//
				// CQ LUT commands
				//

				struct {
					u16 cmd_tag;
					u16 event_needed;
				} cmd_cq_reset;

				struct {
					u32 flags;
				} cmd_cq_add_flags;

				struct {
					u32 flags;
				} cmd_cq_clear_flags;

				struct {
					u32 cq_size_elements;
				} cmd_cq_set_size;

				struct {
					u32 phaddr_lo;
				} cmd_cq_set_phaddr_lo;

				struct {
					u32 phaddr_hi;
				} cmd_cq_set_phaddr_hi;

				struct {
					u32 private_data_lo;
				} cmd_cq_set_private_data_lo;

				struct {
					u32 private_data_hi;
				} cmd_cq_set_private_data_hi;

				struct {
					u32 vm_iommu_tag;
				} cmd_cq_set_vm_iommu_tag;

				struct {
					u32 vm_id;
				} cmd_cq_set_vm_id;

				//
				// CQ LUT commands
				//
				///////////////////////////////////////////////////////////////

				///////////////////////////////////////////////////////////////
				//
				// TX-DEV Commands
				//

				struct cmd_tx_qp_create_id {
					u32 cmd_tag;
				} cmd_tx_qp_create;

				struct cmd_tx_qp_set_qp_transport_type_id {
					u32 transport_type;
				} cmd_tx_qp_set_qp_transport_type;

				struct cmd_tx_qp_set_cq_num_id {
					u32 cq_num;
				} cmd_tx_qp_set_cq_num;

				//				struct cmd_tx_qp_set_access_flag_id {
				//					u32 access_flags;
				//				}cmd_tx_qp_set_access_flag;

				struct {
					u32 pkey;
				} cmd_tx_qp_set_pkey;

				struct {
					u16 cmd_tag;
					u16 new_state;
				} cmd_tx_qp_modify;

				struct {
					u32 mtu_shift;
				} cmd_tx_qp_set_mtu_shift;

				struct {
					u32 qp_dest;
				} cmd_tx_qp_set_qp_num_dest;

				struct {
					u32 psn;
				} cmd_tx_qp_set_psn;

				struct {
					u32 gid0;
				} cmd_tx_qp_set_local_gid_0;

				struct {
					u32 gid1;
				} cmd_tx_qp_set_local_gid_1;

				struct {
					u32 gid2;
				} cmd_tx_qp_set_local_gid_2;

				struct {
					u32 gid3;
				} cmd_tx_qp_set_local_gid_3;

				struct {
					u32 ip_mode;
				} cmd_tx_qp_set_ip_mode;

				struct {
					u32 hw_addr_lo;
				} cmd_tx_qp_set_remote_hw_addr_lo;

				struct {
					u32 hw_addr_hi;
				} cmd_tx_qp_set_remote_hw_addr_hi;

				struct {
					u32 hw_addr_lo;
				} cmd_tx_qp_set_local_hw_addr_lo;

				struct {
					u32 hw_addr_hi;
				} cmd_tx_qp_set_local_hw_addr_hi;

				struct {
					u32 gid0;
				} cmd_tx_qp_set_remote_gid_0;

				struct {
					u32 gid1;
				} cmd_tx_qp_set_remote_gid_1;

				struct {
					u32 gid2;
				} cmd_tx_qp_set_remote_gid_2;

				struct {
					u32 gid3;
				} cmd_tx_qp_set_remote_gid_3;

				struct {
					u32 udp_port;
				} cmd_tx_qp_set_local_udp_port;

				struct {
					u32 val_hw_ip_port;
				} cmd_tx_qp_set_val_hw_ip_port;

				struct {
					u32 priv_access;
				} cmd_tx_qp_set_priveleged_access;

				struct {
					u32 pd;
				} cmd_tx_qp_set_pd;

				struct {
					u32 dummy;
				} cmd_tx_qp_reset;

				struct {
					u32 tag;
				} cmd_tx_qp_query_state;

				struct {
					u32 max_wr;
				} cmd_tx_qp_set_max_wr;

				struct {
					u32 max_sge;
				} cmd_tx_qp_set_max_sge;

				struct {
					u32 phaddr_lo;
				} cmd_tx_qp_set_sq_phaddr_low;

				struct {
					u32 phaddr_hi;
				} cmd_tx_qp_set_sq_phaddr_hi;

				struct {
					u32 value;
				} cmd_tx_qp_doorbell;

				struct {
					u32 wr_size;
				} cmd_tx_qp_set_wr_size;

				struct {
					u32 wr_hdr_size;
				} cmd_tx_qp_set_wr_hdr_size;

				struct {
					u32 max_rd_atomic;
				} cmd_tx_qp_set_max_rd_atomic;

				struct {
					u32 timeout;
				} cmd_tx_qp_set_timeout;

				struct {
					u32 rnr_max;
				} cmd_tx_qp_set_rnr_max;

				struct {
					u32 retry_max;
				} cmd_tx_qp_set_retry_max;

				struct {
					u32 sig_all;
				} cmd_tx_qp_set_sig_all;

				struct {
					u32 explicit_ack_req_upper_bound;
				} cmd_tx_qp_set_explicit_ack_req_upper_bound;

				struct {
					u32 enable;
				} cmd_tx_qp_sqd_enable_async_notify;

				struct {
					u32 vm_id;
				} cmd_tx_qp_set_vm;

				struct {
					u32 vm_iommu_tag;
				} cmd_tx_qp_set_vm_iommu_tag;

				struct {
					u32 cmd_tag;
				} cmd_tx_qp_destroy;

				struct {
					u32 local_ecn_support_type;
				} cmd_tx_qp_set_ecn_local_support_type;

				struct {
					u32 time_period_us;
				} cmd_tx_qp_set_cnp_time_period;

				struct {
					u32 cnp_ratio;
				} cmd_tx_qp_set_cnp_generation_ratio;

				struct {
					u32 impl_type;
				} cmd_tx_qp_set_cnp_imp_type;

				//
				// TX-DEV Commands
				//
				////////////////////////////////////////////////////////////////

				////////////////////////////////////////////////////////////////
				//
				// MCAST commands
				//

				struct {
					u16 host_tag;
					u16 qp_id;
				} cmd_rx_qp_mcast_add_qp;

				struct {
					u16 mcast_num;
					u16 qp_pos;
				} cmd_rx_qp_mcast_remove_qp;

				struct {
					u32 vm_id;
				} cmd_rx_qp_mcast_clear_all_vm;

				//
				// MCAST commands
				//
				/////////////////////////////////////////////////////////////////

				/////////////////////////////////////////////////////////////////
				//
				// DBGMON commands
				//

				struct {
					u32 dummy;
				} cmd_rx_dbgmon_reset;

				struct {
					u16 host_tag;
					u16 drop_reson;
				} cmd_rx_dbgmon_query_counter;

				struct {
					u32 drop_reason_mask;
				} cmd_rx_dbgmon_ack_event;

				struct {
					u32 dummy;
				} cmd_tx_dbgmon_reset;

				struct {
					u16 host_tag;
					u16 drop_reson;
				} cmd_tx_dbgmon_query_counter;

				struct {
					u32 drop_reason_mask;
				} cmd_tx_dbgmon_ack_event;

				//
				// DBGMON commands
				//
				/////////////////////////////////////////////////////////////////

				/////////////////////////////////////////////////////////////////
				//
				// QP1 commands
				//

				struct {
					u32 hw_addr_hi;
				} cmd_qp1_tbl_add_hw_addr_hi;

				struct {
					u32 hw_addr_lo;
				} cmd_qp1_tbl_add_hw_addr_lo;

				struct {
					u32 enabled;
				} cmd_qp1_tbl_add_hw_addr_set_enabled;

				struct {
					u32 val;
				} cmd_qp1_tbl_add_ipv4_addr;

				struct {
					u32 enabled;
				} cmd_qp1_tbl_add_ipv4_set_enabled;

				struct {
					u32 val;
				} cmd_qp1_tbl_add_ipv6_addr;

				struct {
					u32 enabled;
				} cmd_qp1_tbl_add_ipv6_set_enabled;

				struct {
					u32 qp_num;
				} cmd_qp1_tbl_set_qp_num;

				//
				// QP1 commands
				//
				/////////////////////////////////////////////////////////////////

			} bdy;
		};
	};
};
