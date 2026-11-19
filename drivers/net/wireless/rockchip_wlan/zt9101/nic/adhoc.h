/*
 * adhoc.h
 *
 * used for AdHoc mode
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
#ifndef __ADHOC_H_
#define __ADHOC_H_
#ifdef CFG_ENABLE_ADHOC_MODE

#define ADHOC_KEEPALIVE_TIMEOUT           (10 * 1000)

typedef struct
{
    zt_os_api_sema_t      sema;
    zt_timer_t            timer;
    zt_bool               adhoc_master;
    zt_u8                 asoc_sta_count;
    zt_mlme_framework_e   framework;
} zt_adhoc_info_t;

typedef zt_u8 beacon_frame_t[ZT_80211_MGMT_BEACON_SIZE_MAX];


zt_bool zt_get_adhoc_master(nic_info_st *pnic_info);
zt_bool zt_set_adhoc_master(nic_info_st *pnic_info, zt_bool status);
zt_s32 zt_adhoc_work(nic_info_st *pnic_info, zt_80211_mgmt_t *pmgmt,
                     zt_u16 mgmt_len);
zt_pt_ret_t adhoc_thrd(nic_info_st *pnic_info, wdn_net_info_st *pwdn_info);

zt_pt_ret_t zt_adhoc_prc_bcn(nic_info_st *pnic_info, zt_msg_t *pmsg,
                             wdn_net_info_st *pwdn_info);
void zt_adhoc_wdn_info_update(nic_info_st *pnic_info,
                              wdn_net_info_st *pwdn_info);
void zt_adhoc_flush_wdn(nic_info_st *pnic_info);
void zt_adhoc_flush_all_resource(nic_info_st *pnic_info,
                                 sys_work_mode_e network_type);
zt_s32 zt_adhoc_do_probrsp(nic_info_st *pnic_info,
                           zt_80211_mgmt_t *pframe, zt_u16 frame_len);
zt_s32 zt_adhoc_new_boradcast_wdn(nic_info_st *pnic_info);
zt_s32 zt_adhoc_ibss_join(nic_info_st *pnic_info, zt_u8 framework,
                          zt_s32 reason);
zt_s32 zt_adhoc_send_beacon(nic_info_st *pnic_info);
zt_s32 zt_adhoc_keepalive_thrd(nic_info_st *pnic_info);
zt_s32 zt_adhoc_leave_ibss_msg_send(nic_info_st *pnic_info);
zt_s32 zt_adhoc_term(nic_info_st *pnic_info);
zt_s32 zt_adhoc_init(nic_info_st *pnic_info);

#endif
#endif
