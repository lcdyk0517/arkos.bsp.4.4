/*
 * action.h
 *
 * used for xmit action frame
 *
 * Author: songqiang
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
#ifndef _ACTION_H
#define _ACTION_H

#define ZT_GET_LE16(a) ((zt_u16) (((a)[1] << 8) | (a)[0]))

enum zt_action_frame_category
{
    ZT_WLAN_CATEGORY_SPECTRUM_MGMT = 0,
    ZT_WLAN_CATEGORY_QOS = 1,
    ZT_WLAN_CATEGORY_DLS = 2,
    ZT_WLAN_CATEGORY_BACK = 3,
    ZT_WLAN_CATEGORY_PUBLIC = 4,
    ZT_WLAN_CATEGORY_RADIO_MEASUREMENT = 5,
    ZT_WLAN_CATEGORY_FT = 6,
    ZT_WLAN_CATEGORY_HT = 7,
    ZT_WLAN_CATEGORY_SA_QUERY = 8,
    ZT_WLAN_CATEGORY_UNPROTECTED_WNM = 11,
    ZT_WLAN_CATEGORY_TDLS = 12,
    ZT_WLAN_CATEGORY_SELF_PROTECTED = 15,
    ZT_WLAN_CATEGORY_WMM = 17,
    ZT_WLAN_CATEGORY_P2P = 0x7f,
};

enum zt_action_block_ack_actioncode
{
    ZT_WLAN_ACTION_ADDBA_REQ = 0,
    ZT_WLAN_ACTION_ADDBA_RESP = 1,
    ZT_WLAN_ACTION_DELBA = 2,
};

enum zt_public_action
{
    ZT_WLAN_ACTION_PUBLIC_BSSCOEXIST = 0,
    ZT_WLAN_ACTION_PUBLIC_DSE_ENABLE = 1,
    ZT_WLAN_ACTION_PUBLIC_DSE_DEENABLE = 2,
    ZT_WLAN_ACTION_PUBLIC_DSE_REG_LOCATION = 3,
    ZT_WLAN_ACTION_PUBLIC_EXT_CHL_SWITCH = 4,
    ZT_WLAN_ACTION_PUBLIC_DSE_MSR_REQ = 5,
    ZT_WLAN_ACTION_PUBLIC_DSE_MSR_RPRT = 6,
    ZT_WLAN_ACTION_PUBLIC_MP = 7,
    ZT_WLAN_ACTION_PUBLIC_DSE_PWR_CONSTRAINT = 8,
    ZT_WLAN_ACTION_PUBLIC_VENDOR = 9,
    ZT_WLAN_ACTION_PUBLIC_GAS_INITIAL_REQ = 10,
    ZT_WLAN_ACTION_PUBLIC_GAS_INITIAL_RSP = 11,
    ZT_WLAN_ACTION_PUBLIC_GAS_COMEBACK_REQ = 12,
    ZT_WLAN_ACTION_PUBLIC_GAS_COMEBACK_RSP = 13,
    ZT_WLAN_ACTION_PUBLIC_TDLS_DISCOVERY_RSP = 14,
    ZT_WLAN_ACTION_PUBLIC_LOCATION_TRACK = 15,
    ZT_WLAN_ACTION_PUBLIC_MAX
};

#ifdef CFG_ENABLE_AP_MODE
zt_s32 zt_action_frame_ba_to_issue_ap(nic_info_st *nic_info, wdn_net_info_st *pwdn_info, zt_u8 action);
#endif

zt_s32 zt_action_frame_process(nic_info_st *nic_info,
                               zt_80211_mgmt_t *pmgmt, zt_u16 mgmt_len);
zt_s32 zt_action_frame_ba_to_issue(nic_info_st *nic_info, zt_u8 action);
zt_s32 zt_action_frame_add_ba_request(nic_info_st *nic_info,
                                      struct xmit_frame *pxmitframe);
zt_s32 zt_action_frame_del_ba_request(nic_info_st *nic_info, zt_u8 *addr);
zt_s32 zt_action_frame_parse(zt_u8 *frame, zt_u32 frame_len, zt_u8 *category,
                             zt_u8 *action);

#endif
