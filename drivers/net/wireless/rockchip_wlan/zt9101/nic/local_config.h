/*
 * local_config.h
 *
 * used for local information
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
#ifndef __LOCAL_CFG_H__
#define __LOCAL_CFG_H__


typedef enum
{
    ZT_AUTO_MODE =      0,       /* Let the driver decides */
#ifdef CFG_ENABLE_ADHOC_MODE
    ZT_ADHOC_MODE =     1,      /* Single cell network */
#endif
    ZT_INFRA_MODE =     2,      /* Multi cell network, roaming, ... */
#ifdef CFG_ENABLE_AP_MODE
    ZT_MASTER_MODE =    3,     /* Synchronisation master or Access Point */
#endif
    ZT_REPEAT_MODE =    4,     /* Wireless Repeater (forwarder) */
    ZT_SECOND_MODES =   5,    /* Secondary master/repeater (backup) */
#ifdef CFG_ENABLE_MONITOR_MODE
    ZT_MONITOR_MODE =   6,    /* Passive monitor (listen only) */
#endif
    ZT_MESH_MODE =      7,       /* Mesh (IEEE 802.11s) network */

    ZT_MODE_MAX =       8,
} sys_work_mode_e;


typedef struct
{
    sys_work_mode_e     work_mode;
    zt_u8               channel;
    CHANNEL_WIDTH       bw;
#ifdef CFG_ENABLE_ADHOC_MODE
    zt_bool             adhoc_master;
#endif
#ifdef CFG_ENABLE_AP_MODE
    zt_u8               ssid[32];
#endif
    zt_u8               channel_plan;
    zt_u8               ba_enable_tx;
    zt_u8               ba_enable_rx;
    zt_u8               scan_ch_to; /* scan timeout on channel in ms */
    zt_u8               scan_prb_times; /* scan probe request times on
                                        each channel */
    zt_u8               scan_que_deep; /* wlan_mgmt scan queue deep */
    zt_u8               scan_que_node_ttl; /* wlan_mgmt scan queue node
                                            TTL(time to life) */
    zt_u8               ars_policy; /* ars policy select */
    zt_u8               max_ampdu_len_ulimit; /* A-mpdu max length upper limit */
    zt_u8               wlan_guard; /* wlan status guard funcation */
    zt_u8               rf_power; /* RF output power offset */
    zt_u8               vco_cur; /* VCO current adustment */
} local_info_st;

#define NIC_INFO_2_WORK_MODE(nic) ((local_info_st *)nic->local_info)->work_mode

zt_s32 zt_local_cfg_init(nic_info_st *nic_info);
zt_s32 zt_local_cfg_term(nic_info_st *nic_info);
zt_s32 zt_local_cfg_set_default(nic_info_st *nic_info);
zt_s32 zt_local_cfg_get_default(nic_info_st *nic_info);
sys_work_mode_e zt_local_cfg_get_work_mode(nic_info_st *pnic_info);
void zt_local_cfg_set_work_mode(nic_info_st *pnic_info, sys_work_mode_e mode);

#endif
