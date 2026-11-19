/*
 * auth.h
 *
 * This file contains all the prototypes for the auth.c file
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
#ifndef __AUTH_H__
#define __AUTH_H__

/* macro */
#define _CAPABILITY_            2
#define _BEACON_ITERVAL_        2
#define _RSON_CODE_             2
#define _TIMESTAMP_             8


#define WIFI_FW_NULL_STATE      FW_STATE_NOLINK
#define WIFI_FW_STATION_STATE   FW_STATE_STATION
#define WIFI_FW_AP_STATE        FW_STATE_AP
#define WIFI_FW_ADHOC_STATE     FW_STATE_ADHOC

/* type define */
enum
{
    ZT_AUTH_TAG_RSP     = ZT_MSG_TAG_SET(0, 0, 0),
    ZT_AUTH_TAG_ABORT   = ZT_MSG_TAG_SET(0, 1, 0),
    ZT_AUTH_TAG_START   = ZT_MSG_TAG_SET(0, 2, 0),
    ZT_AUTH_TAG_DONE,
};

typedef zt_u8 auth_rsp_t[ZT_80211_MGMT_AUTH_SIZE_MAX];
typedef struct
{
    wdn_net_info_st *pwdn_info;
    zt_u8 retry_cnt;
    zt_timer_t timer;
    zt_bool brun;
    zt_msg_que_t msg_que;
} auth_info_t;

/* function declaration */
#ifdef CFG_ENABLE_AP_MODE
zt_pt_ret_t zt_auth_ap_thrd(nic_info_st *pnic_info, wdn_net_info_st *pwdn_info);
#endif
zt_pt_ret_t zt_auth_sta_thrd(zt_pt_t *pt, nic_info_st *pnic_info, zt_s32 *pres);
zt_s32 zt_auth_sta_start(nic_info_st *pnic_info);
zt_s32 zt_auth_sta_stop(nic_info_st *pnic_info);
zt_s32 zt_auth_frame_parse(nic_info_st *pnic_info, wdn_net_info_st *pwdn_info,
                           zt_80211_mgmt_t *pmgmt, zt_u16 mgmt_len);
zt_s32 zt_auth_init(nic_info_st *pnic_info);
zt_s32 zt_auth_term(nic_info_st *pnic_info);
zt_s32 zt_deauth_xmit_frame(nic_info_st *pnic_info, zt_u8 *pmac,
                            zt_u16 reason_code);
zt_s32 zt_deauth_frame_parse(nic_info_st *pnic_info, wdn_net_info_st *pwdn_info,
                             zt_80211_mgmt_t *pmgmt, zt_u16 mgmt_len);

#endif

