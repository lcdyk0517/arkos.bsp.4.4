/*
 * ap.h
 *
 * This file contains all the prototypes for the ap.c file
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
#ifndef __AP_H__
#define __AP_H__

#define BEACON_IE_OFFSET      12
#define _PUBLIC_ACTION_IE_OFFSET_   8
#define _FIXED_IE_LENGTH_ BEACON_IE_OFFSET
#ifdef CFG_ENABLE_AP_MODE

typedef enum
{
    ZT_AP_STATE_BIT_INIT   = ZT_BIT(31),
    ZT_AP_STATE_BIT_RUN    = ZT_BIT(30),
    ZT_AP_STATE_BIT_TERM   = ZT_BIT(29),

    ZT_AP_STA_STANBY = 0,
    ZT_AP_STA_ESTABLISHED,
    ZT_AP_STA_FREEZE,

    ZT_AP_STATE_INIT    = ZT_AP_STATE_BIT_INIT,
    ZT_AP_STATE_FREEZE  = ZT_AP_STATE_BIT_INIT | ZT_AP_STA_FREEZE,

    ZT_AP_STATE_STANBY          = ZT_AP_STATE_BIT_RUN | ZT_AP_STA_STANBY,
    ZT_AP_STATE_ESTABLISHED     = ZT_AP_STATE_BIT_RUN | ZT_AP_STA_ESTABLISHED,

    ZT_AP_STATE_TERM = ZT_AP_STATE_BIT_TERM,
} zt_ap_status;

typedef enum
{
    ZT_AP_MSG_TAG_AUTH_FRAME,
    ZT_AP_MSG_TAG_DEAUTH_FRAME,
    ZT_AP_MSG_TAG_ASSOC_REQ_FRAME,
    ZT_AP_MSG_TAG_DISASSOC_FRAME,
    ZT_AP_MSG_TAG_BA_REQ_FRAME,
    ZT_AP_MSG_TAG_BA_RSP_FRAME,

    ZT_AP_MSG_TAG_MAX,
} zt_ap_msg_tag_e;

typedef struct
{
    zt_que_list_t list;
    zt_ap_msg_tag_e tag;
    zt_u16 len;
    union
    {
        zt_u8 data[0];
        zt_80211_mgmt_t mgmt;
    };
} zt_ap_msg_t;

zt_ap_status zt_ap_status_get(nic_info_st *pnic_info);
zt_s32 zt_ap_msg_load(nic_info_st *pnic_info, zt_que_t *pque_tar,
                      zt_ap_msg_tag_e tag, void *pdata, zt_u16 len);
zt_ap_msg_t *zt_ap_msg_get(zt_que_t *pque);
zt_s32 zt_ap_msg_free(nic_info_st *pnic_info, zt_que_t *pque,
                      zt_ap_msg_t *pmsg);

zt_s32 zt_ap_probe_parse(nic_info_st *pnic_info, zt_80211_mgmt_t *pframe,
                         zt_u16 frame_len);
zt_s32 zt_ap_set_beacon(nic_info_st *pnic_info, zt_u8 *pbuf, zt_u32 len,
                        zt_u8 framework);
void zt_ap_resend_bcn(nic_info_st *pnic_info, zt_u8 channel);
zt_s32 zt_ap_resume_bcn(nic_info_st *pnic_info);
zt_u32 zt_ap_update_beacon(nic_info_st *pnic_info,
                           zt_u8 ie_id, zt_u8 *oui, zt_u8 tx);

zt_s32 zt_ap_add_ba_req(nic_info_st *pnic_info, void *pwdn_info);
zt_s32 zt_ap_new_sta(nic_info_st *pnic_info, zt_u8 *mac, void **rwdn_info);
zt_s32 zt_ap_deauth_all_sta(nic_info_st *pnic_info, zt_u16 reason);
zt_s32 zt_ap_work_start(nic_info_st *pnic_info);
zt_s32 zt_ap_work_stop(nic_info_st *pnic_info);
zt_s32 zt_ap_suspend(nic_info_st *pnic_info);
zt_s32 zt_ap_resume(nic_info_st *pnic_info);
zt_s32 zt_ap_init(nic_info_st *pnic_info);
zt_s32 zt_ap_term(nic_info_st *pnic_info);

zt_s32 zt_ap_get_num(nic_info_st *pnic_info);
zt_bool zt_bmp_is_set(zt_u8 bmp, zt_u8 id);
void zt_bmp_set(zt_u8 *bmp, zt_u8 id);
void zt_bmp_clear(zt_u8 *bmp, zt_u8 id);

#endif

#endif/* __AP_H__ */

