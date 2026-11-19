/*
 * wdn.h
 *
 * This file contains all the prototypes for the wdn.c file
 *
 * Author: luozhi
 *
 * Copyright (c) 2021 Shandong ZTop Microelectronics Co., Ltd
 *
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#ifndef __WDN_H__
#define __WDN_H__
/*
File name: Wireless Device Node
*/
#define SESSION_TRACKER_REG_ID_NUM 1
#define WDN_NUM_MAX         5

#define BA_REORDER_QUEUE_NUM (64)


typedef zt_bool(*zt_st_match_rule)(nic_info_st *nic_info, zt_u8 *local_naddr,
                                   zt_u8 *local_port, zt_u8 *remote_naddr,
                                   zt_u8 *remote_port);


struct wdn_ht_priv
{
    zt_u8 ht_option;
    zt_u8 ampdu_enable;
    zt_u8 tx_amsdu_enable;
    zt_u8 bss_coexist;

    zt_u32 tx_amsdu_maxlen;
    zt_u32 rx_ampdu_maxlen;

    zt_u8 rx_ampdu_min_spacing;

    zt_u8 ch_offset;
    zt_u8 sgi_20m;
    zt_u8 sgi_40m;

    zt_u8 agg_enable_bitmap;
    zt_u8 candidate_tid_bitmap;

    zt_u8 ldpc_cap;
    zt_u8 stbc_cap;
    zt_u8 smps_cap;
    zt_80211_mgmt_ht_cap_t ht_cap;

};

struct zt_ht_cap
{
    zt_u8 both_20m_40m;
    zt_u8 dssck_40m;
    zt_u8 sm_ps_mode;
    zt_u8 rx_stbc;
    struct wdn_ht_priv mcu_ht;
};


typedef struct
{
    zt_u8 s_proto;
    zt_st_match_rule rule;
} zt_st_register_st;

typedef struct
{
    zt_st_register_st reg[SESSION_TRACKER_REG_ID_NUM];
    zt_que_t tracker_q;
} zt_st_ctl_st;

struct wdninfo_stats
{

    zt_u64 rx_mgnt_pkts;
    zt_u64 rx_beacon_pkts;
    zt_u64 rx_probereq_pkts;
    zt_u64 rx_probersp_pkts;
    zt_u64 rx_probersp_bm_pkts;
    zt_u64 rx_probersp_uo_pkts;
    zt_u64 rx_ctrl_pkts;
    zt_u64 rx_data_pkts;
    zt_u64 rx_data_qos_pkts[ZT_TID_NUM];
    zt_u64 last_rx_mgnt_pkts;
    zt_u64 last_rx_beacon_pkts;
    zt_u64 last_rx_probereq_pkts;
    zt_u64 last_rx_probersp_pkts;
    zt_u64 last_rx_probersp_bm_pkts;
    zt_u64 last_rx_probersp_uo_pkts;
    zt_u64 last_rx_ctrl_pkts;
    zt_u64 last_rx_data_pkts;
    zt_u64 last_rx_data_qos_pkts[ZT_TID_NUM];


    zt_u64 tx_bytes;
    zt_u64 tx_pkts;
    zt_u64 tx_drops;
    zt_u64 cur_tx_bytes;
    zt_u64 last_tx_bytes;
    zt_u32 cur_tx_tp;

    zt_u64 rx_bytes;
    zt_u64 rx_pkts;
    zt_u64 rx_drops;
    zt_u64 cur_rx_bytes;
    zt_u64 last_rx_bytes;
    zt_u32 cur_rx_tp;
};

struct wdn_xmit_priv
{
    zt_lock_spin lock;
    zt_s32 option;
    zt_s32 apsd_setting;

    // struct tx_servq be_q;
    // struct tx_servq bk_q;
    // struct tx_servq vi_q;
    // struct tx_servq vo_q;
    zt_list_t legacy_dz;
    zt_list_t apsd;

    zt_u16 txseq_tid[TID_NUM + 1];

};

typedef void (*upload_to_kernel)(nic_info_st *nic_info, void *data);
typedef void (*free_skb_cb)(nic_info_st *nic_info, void *skb);
/* for Rx reordering buffer control */
typedef struct recv_ba_ctrl_
{
    void *nic_node;
    zt_u8  tid;
    zt_u8  enable;
    zt_u16 indicate_seq;/* =wstart_b, init_value = 0xffff */
    zt_u16 wend_b;
    zt_u8  wsize_b;
    zt_u8  ampdu_size;
    zt_que_t pending_reorder_queue;
    zt_que_t free_order_queue;
    zt_os_api_timer_t reordering_ctrl_timer;
    zt_u8 wait_timeout;
    zt_bool timer_start;
    zt_irq  val_irq;
    zt_u16  last_seq_num;
    //void *kernel_id;//kernel thread id
    //zt_s8 ba_reorder_name[32];
    zt_s32 ba_reorder_state;
    zt_u64  drop_pkts;
    zt_u32 timeout_cnt;
    upload_to_kernel upload_func;
    free_skb_cb free_skb;
    zt_os_api_lock_t pending_get_de_queue_lock; //for muti-cpu core
} recv_ba_ctrl_st;

typedef enum
{
    E_WDN_AP_STATE_IDLE,
    E_WDN_AP_STATE_READY,
    E_WDN_AP_STATE_AUTH,
    E_WDN_AP_STATE_ASSOC,
    E_WDN_AP_STATE_8021X_BLOCK,
    E_WDN_AP_STATE_8021X_UNBLOCK,
    E_WDN_AP_STATE_DAUTH,
    E_WDN_AP_STATE_DASSOC,
} wdn_state_e;

typedef struct wdn_net_info_
{
    zt_u8  mac[ZT_80211_MAC_ADDR_LEN];
    zt_u8 wdn_id;
    zt_s8 unicast_cam_id;
    zt_s8 group_cam_id;
    zt_u16 reason_code;

#ifdef CFG_ENABLE_AP_MODE
    wdn_state_e state;
    zt_pt_t ap_thrd_pt, sub_thrd_pt;
    zt_que_t ap_msg;
    zt_timer_t ap_timer;
    zt_u32 wpa_unicast_cipher;
    zt_u32 rsn_pairwise_cipher;
    zt_u8 wpa_ie[32];
    zt_u16 wpa_ie_len;
    zt_u32 rx_idle_timeout;
    zt_bool psm;
    zt_que_t psm_data_que;
    zt_os_api_lock_t psm_lock;
    zt_add_ba_parm_st  barsp_parm;
    zt_add_ba_parm_st  bareq_parm;
#endif
    zt_u32 rx_pkt_stat;
    zt_u32 rx_pkt_stat_last;
    sys_work_mode_e mode;

    zt_u8  bssid[ZT_80211_MAC_ADDR_LEN];
    zt_u8  ssid[ZT_80211_MAX_SSID_LEN+1];
    zt_u8  ssid_len;
    zt_u64 tsf;

    zt_u16  cap_info;
    zt_u8   ess_net;
    zt_u8   ibss_net;
    zt_u8   privacy;
    zt_u8   short_preamble;
    zt_u8   short_slot;
    zt_u8   radio_measure;

    zt_u16  bcn_interval;
    zt_u16  listen_interval;

    zt_u8   datarate[8];
    zt_u8   datarate_len;

    zt_u8   ext_datarate[4];
    zt_u8   ext_datarate_len;

    zt_bool   ht_enable;
    zt_80211_mgmt_ht_cap_t ht_cap;
    struct  zt_ht_cap htpriv;
    zt_u8   HT_protection;
    zt_wlan_ht_op_info_st *ht_info;

    zt_u8   channel;
    CHANNEL_WIDTH   bw_mode;
    zt_u8   channle_offset;

    zt_bool         wmm_enable;
    zt_wmm_para_st wmm_info;
    zt_u8          acm_mask;

    zt_bool   wep_enable;
    zt_bool   wpa_enable;
    zt_bool   rsn_enable;

    zt_bool   erp_enable;
    zt_u8   erp_flag;

    //    zt_80211_auth_algo_e auth_algo;
    zt_u32 auth_algo;
    zt_80211_auth_seq_e auth_seq;
    zt_u16  qos_option;
    zt_u32  iv;
    zt_u32  key_index;
    zt_u8   chlg_txt[128];
    zt_bool  ieee8021x_blocked;
    zt_u16  dot118021XPrivacy;
    zt_u16  cam_id;
    union Keytype dot11tkiptxmickey;
    union Keytype dot11tkiprxmickey;
    union Keytype dot118021x_UncstKey;
    union pn48 dot11txpn;
    union pn48 dot11rxpn;

    zt_u32  network_type;

    zt_u8   raid;
    zt_u8   tx_rate;
    que_t   defrag_q;
    zt_u8   defrag_flag;
    zt_u8   defrag_cnt;
    zt_u8   cts2self;
    zt_u8   rtsen;
    zt_u8   ldpc;
    zt_u8   stbc;
    zt_u32  aid;
    struct wdn_xmit_priv wdn_xmitpriv;
    struct wdninfo_stats wdn_stats;

    zt_u8            dialogToken[TID_NUM];
    zt_u8            ba_enable_flag[TID_NUM];
    zt_u8            ba_started_flag[TID_NUM];
    zt_u16           ba_starting_seqctrl[TID_NUM];
    //recv_ba_ctrl_st  *ba_ctl;
    recv_ba_ctrl_st  ba_ctl[TID_NUM + 1];

    zt_u16 seq_ctrl_recorder[16];
    zt_u8 is_p2p_device;
    zt_u8 p2p_status_code;

    zt_u8 dev_addr[ZT_80211_MAC_ADDR_LEN];
    zt_u8 dev_cap;
    zt_u16 config_methods;
    zt_u8 primary_dev_type[8];
    zt_u8 num_of_secdev_type;
    zt_u8 secdev_types_list[32];
    zt_u16 dev_name_len;
    zt_u8 dev_name[32];

    zt_u8 op_wfd_mode;

} wdn_net_info_st;

typedef struct
{
    zt_list_t list;
    wdn_net_info_st info;
} wdn_node_st;

typedef struct
{
    zt_list_t head; // member is wdn_node_st
    zt_list_t free;
    zt_u8 cnt; /* head node count */
} wdn_list;

extern zt_u8 WPA_OUI[4];
extern zt_u8 WMM_OUI[4];
extern zt_u8 WPS_OUI[4];
extern zt_u8 P2P_OUI[4];
extern zt_u8 WFD_OUI[4];

zt_u8 zt_wdn_get_raid_by_network_type(wdn_net_info_st *pwdn_info);
wdn_net_info_st *zt_wdn_find_info(nic_info_st *nic_info, zt_u8 *mac);
wdn_net_info_st *zt_wdn_find_info_by_id(nic_info_st *nic_info, zt_u8 wdn_id);
wdn_net_info_st *zt_wdn_add(nic_info_st *nic_info, zt_u8 *mac);
zt_s32 zt_wdn_remove(nic_info_st *nic_info, zt_u8 *mac);
zt_s32 zt_wdn_init(nic_info_st *nic_info);
zt_s32 zt_wdn_term(nic_info_st *nic_info);
#ifdef CFG_ENABLE_AP_MODE
void zt_wdn_info_ap_update(nic_info_st *nic_info, wdn_net_info_st *pwdn_info);
#endif
zt_s32 zt_wdn_info_sta_update(nic_info_st *nic_info, wdn_net_info_st *wdn_info);
zt_u8 zt_wdn_is_alive(wdn_net_info_st *wdn_net_info, zt_u8 update_tag);
void get_bratecfg_by_support_dates(zt_u8 *pdataRate, zt_u8 dataRate_len,
                                   zt_u16 *pBrateCfg);
zt_u8 zt_wdn_get_cnt(nic_info_st *pnic_info);

#endif

