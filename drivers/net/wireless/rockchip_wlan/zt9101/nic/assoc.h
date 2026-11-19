/*
 * assoc.h
 *
 * This file contains all the prototypes for the assoc.c file
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
#ifndef __ASSOC_H__
#define __ASSOC_H__

/* macro */

/* type define */
enum
{
    ZT_ASSOC_TAG_RSP     = ZT_MSG_TAG_SET(0, 0, 0),
    ZT_ASSOC_TAG_ABORT   = ZT_MSG_TAG_SET(0, 1, 0),
    ZT_ASSOC_TAG_START   = ZT_MSG_TAG_SET(0, 2, 0),
    ZT_ASSOC_TAG_DONE,
};

typedef zt_u8 assoc_rsp_t[ZT_80211_MGMT_ASSOC_SIZE_MAX];
typedef struct
{
    zt_msg_que_t msg_que;
    zt_u8 retry_cnt;
    zt_timer_t timer;
    zt_bool brun;
} assoc_info_t;

/* function declaration */
#ifdef CFG_ENABLE_AP_MODE
zt_s32 zt_assoc_ap_work(nic_info_st *pnic_info, wdn_net_info_st *pwdn_info,
                        zt_80211_mgmt_t *pmgmt, zt_u16 mgmt_len);
zt_pt_ret_t zt_assoc_ap_thrd(nic_info_st *pnic_info,
                             wdn_net_info_st *pwdn_info);
void zt_ap_add_sta_ratid(nic_info_st *pnic_info, wdn_net_info_st *pwdn_info);
void zt_assoc_ap_event_up(nic_info_st *nic_info, wdn_net_info_st *pwdn_info,
                          zt_ap_msg_t *pmsg);
#endif
zt_s32 zt_disassoc_frame_parse(nic_info_st *pnic_info,
                               wdn_net_info_st *pwdn_info,
                               zt_80211_mgmt_t *pmgmt, zt_u16 mgmt_len);
zt_s32 zt_assoc_frame_parse(nic_info_st *pnic_info, wdn_net_info_st *pwdn_info,
                            zt_80211_mgmt_t *pmgmt, zt_u16 mgmt_len);
zt_pt_ret_t zt_assoc_sta_thrd(zt_pt_t *pt, nic_info_st *pnic_info,
                              zt_s32 *pres);
zt_s32 zt_assoc_start(nic_info_st *pnic_info);
zt_s32 zt_assoc_stop(nic_info_st *pnic_info);
zt_s32 zt_assoc_init(nic_info_st *pnic_info);
zt_s32 zt_assoc_term(nic_info_st *pnic_info);

#endif

