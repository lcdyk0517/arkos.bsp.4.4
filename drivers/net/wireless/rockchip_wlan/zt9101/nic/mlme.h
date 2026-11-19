/*
 * mlme.h
 *
 * This file contains all the prototypes for the mlme.c file
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
#ifndef __MLME_H__
#define __MLME_H__

/* macro */

#define ZT_NAME_MAX_LEN         (32)

/* type define */
typedef enum
{
    ZT_MLME_FRAMEWORK_WEXT,
    ZT_MLME_FRAMEWORK_NETLINK,
    ZT_MLME_FRAMEWORK_NDIS,
    ZT_MLME_FRAMEWORK_NONE,
} zt_mlme_framework_e;

enum
{
    /* priority level 0 */
    ZT_MLME_TAG_SCAN_ABORT      = ZT_MSG_TAG_SET(0, 0, 0),
    ZT_MLME_TAG_SCAN_RSP,
    ZT_MLME_TAG_IBSS_LEAVE,
    ZT_MLME_TAG_IBSS_BEACON_FRAME,
    ZT_MLME_TAG_CONN_ABORT,
    ZT_MLME_TAG_DEAUTH,
    ZT_MLME_TAG_DEASSOC,

    /* priority level 1 */
    ZT_MLME_TAG_SCAN            = ZT_MSG_TAG_SET(0, 1, 0),
    ZT_MLME_TAG_CONN,
    ZT_MLME_TAG_CONN_IBSS,

    /* priority level 2 */
    ZT_MLME_TAG_ADD_BA_REQ      = ZT_MSG_TAG_SET(0, 2, 0),
    ZT_MLME_TAG_ADD_BA_RSP,
    ZT_MLME_TAG_KEEPALIVE,

#ifdef CONFIG_LPS
    ZT_MLME_TAG_LPS,
#endif
};

typedef enum
{
    MLME_STA_INIT_BIT   = ZT_BIT(31),
    MLME_STA_RUN_BIT    = ZT_BIT(30),
    MLME_STA_TERM_BIT   = ZT_BIT(29),

    MLME_STA_FREEZE = 0,
    MLME_STA_IDLE,
    MLME_STA_SCAN,
    MLME_STA_CONN_SCAN,
    MLME_STA_AUTH,
    MLME_STA_ASSOC,
    MLME_STA_DEAUTH,
    MLME_STA_DEASSOC,
    MLME_STA_ADD_BA_RESP,
    MLME_STA_ADD_BA_REQ,
    MLME_STA_IBSS_CONN_SCAN,

    MLME_STATE_INIT     = MLME_STA_INIT_BIT,
    MLME_STATE_FREEZE   = MLME_STA_INIT_BIT | MLME_STA_FREEZE,
    MLME_STATE_IDLE     = MLME_STA_INIT_BIT | MLME_STA_IDLE,

    /* bss scan */
    MLME_STATE_SCAN             = MLME_STA_RUN_BIT | MLME_STA_SCAN,
    /* bss connection */
    MLME_STATE_CONN_SCAN        = MLME_STA_RUN_BIT | MLME_STA_CONN_SCAN,
    MLME_STATE_AUTH             = MLME_STA_RUN_BIT | MLME_STA_AUTH,
    MLME_STATE_ASSOC            = MLME_STA_RUN_BIT | MLME_STA_ASSOC,
    MLME_STATE_DEAUTH           = MLME_STA_RUN_BIT | MLME_STA_DEAUTH,
    MLME_STATE_DEASSOC          = MLME_STA_RUN_BIT | MLME_STA_DEASSOC,
    MLME_STATE_ADD_BA_RESP      = MLME_STA_RUN_BIT | MLME_STA_ADD_BA_RESP,
    MLME_STATE_ADD_BA_REQ       = MLME_STA_RUN_BIT | MLME_STA_ADD_BA_REQ,
    /* ibss */
    MLME_STATE_IBSS_CONN_SCAN   = MLME_STA_RUN_BIT | MLME_STA_IBSS_CONN_SCAN,

    MLME_STATE_TERM = MLME_STA_TERM_BIT,
} mlme_state_e;

typedef struct
{
    zt_u32  num_tx_ok_in_period_with_tid[TID_NUM];
    zt_u32  num_tx_ok_in_period;
    zt_u32  num_rx_ok_in_period;
    zt_u32  num_rx_unicast_ok_in_period;
    zt_bool busy_traffic;
} zt_link_info_st;

typedef struct
{
    zt_bool local_disconn;
    zt_80211_reasoncode_e reason_code;
} zt_mlme_conn_res_t;

typedef struct
{
    nic_info_st *parent;
    void *tid;
    zt_s8 mlmeName[ZT_NAME_MAX_LEN];
    mlme_state_e state;
    zt_bool connect;
    zt_os_api_lock_t state_lock;
    zt_os_api_lock_t connect_lock;
    zt_link_info_st link_info;

    zt_add_ba_parm_st  barsp_parm;
    zt_add_ba_parm_st  bareq_parm;
    zt_u8              baCreating;

    zt_pt_t pt[10];
    zt_msg_que_t msg_que;
    zt_timer_t traffic_timer;
    zt_timer_t keep_alive_timer;
    wdn_net_info_st *pwdn_info;
    zt_u8 try_cnt;

    zt_msg_t *pconn_msg;
    zt_msg_t *pscan_msg;
    zt_msg_t *pba_rsp_msg;

    zt_mlme_conn_res_t conn_res;

    zt_bool freeze_pend;

    zt_u8 probereq_wps_ie[ZT_80211_IES_SIZE_MAX];
    zt_u8 wps_beacon_ie[ZT_80211_IES_SIZE_MAX];
    zt_u8 wps_probe_resp_ie[ZT_80211_IES_SIZE_MAX];
    zt_u8 wps_assoc_resp_ie[ZT_80211_IES_SIZE_MAX];

    zt_u32 wps_ie_len;
    zt_u32 wps_beacon_ie_len;
    zt_u32 wps_probe_resp_ie_len;
    zt_u32 wps_assoc_resp_ie_len;

    zt_u8 action_public_dialog_token;
    zt_u16 action_public_rxseq;

    zt_bool vir_scanning_intf;
} mlme_info_t;

/* function declaration */
zt_inline
static zt_bool zt_mlme_check_mode(nic_info_st *pnic_info, sys_work_mode_e mode)
{
    local_info_st *plocal_info = pnic_info->local_info;

    return (zt_bool)(plocal_info->work_mode == mode);
}

zt_s32 zt_mlme_get_state(nic_info_st *pnic_info, mlme_state_e *state);
zt_s32 zt_mlme_get_connect(nic_info_st *pnic_info, zt_bool *bconnect);
zt_s32 zt_mlme_get_traffic_busy(nic_info_st *pnic_info, zt_bool *bbusy);
zt_s32 zt_mlme_abort(nic_info_st *pnic_info);
zt_s32 zt_mlme_scan_start(nic_info_st *pnic_info, scan_type_e type,
                          zt_wlan_ssid_t ssids[], zt_u8 ssid_num,
                          zt_u8 chs[], zt_u8 ch_num,
                          zt_mlme_framework_e frm_work);

#ifdef CFG_ENABLE_ADHOC_MODE

zt_s32 zt_mlme_scan_ibss_start(nic_info_st *pnic_info,
                               zt_wlan_ssid_t *pssid,
                               zt_u8 *pch,
                               zt_mlme_framework_e frm_work);
#endif

zt_s32 zt_mlme_scan_abort(nic_info_st *pnic_info);
zt_s32 zt_mlme_conn_scan_rsp(nic_info_st *pnic_info,
                             zt_80211_mgmt_t *pmgmt, zt_u16 mgmt_len);
zt_s32 zt_mlme_conn_start(nic_info_st *pnic_info, zt_80211_bssid_t bssid,
                          zt_wlan_ssid_t *pssid, zt_mlme_framework_e frm_work,
                          zt_bool indicate_en);
zt_s32 zt_mlme_conn_abort(nic_info_st *pnic_info,
                          zt_bool local_gen,
                          zt_80211_reasoncode_e reason);
zt_s32 zt_mlme_deauth(nic_info_st *pnic_info,
                      zt_bool local_gen,
                      zt_80211_reasoncode_e reason);
zt_s32 zt_mlme_deassoc(nic_info_st *pnic_info,
                       zt_bool local_gen,
                       zt_80211_reasoncode_e reason);
zt_s32 zt_mlme_add_ba_req(nic_info_st *pnic_info);
zt_s32 zt_mlme_add_ba_rsp(nic_info_st *pnic_info,
                          zt_add_ba_parm_st *barsp_parm);
zt_s32 zt_mlme_init(nic_info_st *pnic_info);
zt_s32 zt_mlme_term(nic_info_st *pnic_info);
zt_s32 zt_mlme_suspend(nic_info_st *pnic_info);
zt_s32 zt_mlme_resume(nic_info_st *pnic_info);

#endif

