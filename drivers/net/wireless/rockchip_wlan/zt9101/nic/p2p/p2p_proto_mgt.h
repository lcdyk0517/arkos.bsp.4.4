/*
 * p2p_proto_mgt.h
 *
 * used for p2p
 *
 * Author: kanglin
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
#ifndef __P2P_PROTO_MGT_H__
#define __P2P_PROTO_MGT_H__

#define P2P_CONN_NEGO_TIME (20000) //ms
#define P2P_SCAN_NEGO_TIME (10000)
#define P2P_EAPOL_NEGO_TIME (5000)

typedef struct
{
    zt_u8 action;
    zt_u8 tx_ch;
    zt_u32 len;
    zt_u8 buf[512];
} p2p_nego_param_st;

zt_s32 zt_p2p_remain_on_channel(nic_info_st *pnic_info);
zt_s32 zt_p2p_nego_timer_set(nic_info_st *pnic_info, zt_u32 timeout);
zt_s32 zt_p2p_cannel_remain_on_channel(nic_info_st *pnic_info, zt_u8 tag_cmd);

#endif
