/*
 * scan.h
 *
 * This file contains all the prototypes for the scan.c file
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
#ifndef __SCAN_H__
#define __SCAN_H__

/* macro */
#define ZT_SCAN_REQ_SSID_NUM            9
#define ZT_SCAN_REQ_CHANNEL_NUM         (14 + 37)

/* type define */
enum
{
    ZT_SCAN_TAG_ABORT   = ZT_MSG_TAG_SET(0, 0, 0),
    ZT_SCAN_TAG_START   = ZT_MSG_TAG_SET(0, 1, 0),
    ZT_SCAN_TAG_DONE,
};

typedef struct
{
    scan_type_e type;
    zt_80211_addr_t bssid;
    zt_wlan_ssid_t ssids[ZT_SCAN_REQ_SSID_NUM];
    zt_u8 ssid_num;
    zt_u8 ch_map[MAX_CHANNEL_NUM];
    zt_u8 ch_num;
} zt_scan_req_t;

typedef struct
{
    zt_os_api_sema_t sema;
    zt_u8 ch_idx;
    zt_u8 retry_cnt;
    zt_bool brun;
    zt_timer_t timer, pass_time;
    zt_os_api_sema_t req_lock;
    zt_scan_req_t *preq;
    zt_msg_que_t msg_que;

    struct
    {
        zt_u8 number;
        CHANNEL_WIDTH width;
        HAL_PRIME_CH_OFFSET offset;
    } chnl_bak;

    zt_bool ap_resume_done;
} zt_scan_info_t;

/* function declaration */

zt_pt_ret_t zt_scan_thrd(zt_pt_t *pt, nic_info_st *pnic_info, zt_s32 *pres);
zt_s32 zt_scan_probe_send(nic_info_st *pnic_info);
zt_s32 zt_scan_filter(nic_info_st *pnic_info,
                      zt_80211_mgmt_t *pmgmt, zt_u16 mgmt_len);
zt_s32 zt_scan_start(nic_info_st *pnic_info, scan_type_e type,
                     zt_80211_bssid_t bssid,
                     zt_wlan_ssid_t ssids[], zt_u8 ssid_num,
                     zt_u8 chs[], zt_u8 ch_num);
zt_s32 zt_scan_stop(nic_info_st *pnic_info);
zt_s32 zt_scan_wait_done(nic_info_st *pnic_info, zt_bool babort, zt_u16 to_ms);
zt_bool zt_is_scanning(nic_info_st *pnic_info);
zt_s32 zt_scan_init(nic_info_st *pnic_info);
zt_s32 zt_scan_term(nic_info_st *pnic_info);

#endif

