/*
 * p2p_proto_mgt.c
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

#include "common.h"

#define P2P_PROTO_DBG(fmt, ...)      LOG_D("P2P_PROTO[%s:%d][%d] "fmt, __func__,__LINE__, pnic_info->ndev_id,##__VA_ARGS__)
#define P2P_PROTO_ARRAY(data, len)   zt_log_array(data, len)
#define P2P_PROTO_INFO(fmt, ...)     LOG_I("P2P_PROTO[%s:%d][%d] "fmt, __func__,__LINE__, pnic_info->ndev_id,##__VA_ARGS__)
#define P2P_PROTO_WARN(fmt, ...)     LOG_E("P2P_PROTO[%s:%d][%d] "fmt, __func__,__LINE__, pnic_info->ndev_id,##__VA_ARGS__)
#define P2P_NEGO_TIME (15000) //ms

zt_s32 zt_p2p_nego_timer_set(nic_info_st *pnic_info, zt_u32 timeout)
{
    p2p_info_st *p2p_info       = NULL;
    if (NULL == pnic_info)
    {
        return -1;
    }
    p2p_info = pnic_info->p2p;
    if (P2P_CONN_NEGO_TIME == timeout)
    {
        if (p2p_info->go_negoing)
        {
            zt_os_api_timer_set(&p2p_info->nego_timer, P2P_CONN_NEGO_TIME);
        }
    }
    else if (P2P_SCAN_NEGO_TIME == timeout)
    {
        if (p2p_info->go_negoing)
        {
            zt_os_api_timer_set(&p2p_info->nego_timer, P2P_SCAN_NEGO_TIME);
        }
    }
    else if (P2P_EAPOL_NEGO_TIME == timeout)
    {
        zt_os_api_timer_set(&p2p_info->nego_timer, P2P_EAPOL_NEGO_TIME);
    }
    return 0;
}
zt_s32 zt_p2p_cannel_remain_on_channel(nic_info_st *pnic_info, zt_u8 tag_cmd)
{
    p2p_info_st *p2p_info       = NULL;
    CHANNEL_WIDTH cw            = CHANNEL_WIDTH_20;
    HAL_PRIME_CH_OFFSET offset  = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
    zt_u8 hw_ch                 = 0;
    zt_u8 buddy_ch              = 0;

    if (NULL == pnic_info)
    {
        return -1;
    }

    p2p_info = pnic_info->p2p;
    if (zt_false == p2p_info->is_ro_ch)
    {
        return 0;
    }
    p2p_info->is_ro_ch = zt_false;
    if (tag_cmd)
    {
        buddy_ch = zt_p2p_get_buddy_channel(pnic_info);
        zt_hw_info_get_channel_bw_ext(pnic_info, &hw_ch, &cw, &offset);

        P2P_PROTO_INFO("(%d,%d,%d) hw_ch[%d]  listen_ch:%u, remain_ch:%d, buddy_ch:%d,peer_listen_ch:%d, link_ch:%d,nego:0x%x\n"
                       , p2p_info->role, p2p_info->p2p_state, p2p_info->pre_p2p_state, hw_ch
                       , p2p_info->listen_channel, p2p_info->remain_ch, buddy_ch
                       , p2p_info->peer_listen_channel, p2p_info->link_channel, p2p_info->go_negoing);

        if (buddy_ch)
        {
            if (0 == p2p_info->go_negoing ||
                 p2p_info->go_negoing & ZT_BIT(P2P_GO_NEGO_CONF) ||
                 p2p_info->go_negoing & ZT_BIT(P2P_INVIT_RESP))
            {
                if (hw_ch != buddy_ch)
                {
                    zt_hw_info_set_channel_bw(pnic_info,  buddy_ch, cw, offset);
                }
            }
        }

    }

    if (NULL != p2p_info->scb.remain_on_channel_expired)
    {
        p2p_info->scb.remain_on_channel_expired(pnic_info, NULL, 0);
    }

    zt_p2p_set_state(p2p_info, p2p_info->pre_p2p_state);

    p2p_info->last_ro_ch_time = 0;
    return 0;
}

zt_s32 zt_p2p_remain_on_channel(nic_info_st *pnic_info)
{
    p2p_info_st *p2p_info                   = NULL;
    CHANNEL_WIDTH cw = CHANNEL_WIDTH_20;
    HAL_PRIME_CH_OFFSET offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
    zt_u8 hw_ch = 0;
    zt_u8 buddy_ch = 0;
    if (NULL == pnic_info)
    {
        return -1;
    }

    p2p_info                   = pnic_info->p2p;
    p2p_info->is_ro_ch = zt_true;

    buddy_ch = zt_p2p_get_buddy_channel(pnic_info);

    zt_hw_info_get_channel_bw_ext(pnic_info, &hw_ch, &cw, &offset);
    P2P_PROTO_INFO("(%d,%d,%d) hw_ch[%d]  listen_ch:%u, remain_ch:%d, buddy_ch:%d,peer_listen_ch:%d, link_ch:%d,nego:0x%x\n"
                   , p2p_info->role, p2p_info->p2p_state, p2p_info->pre_p2p_state, hw_ch
                   , p2p_info->listen_channel, p2p_info->remain_ch, buddy_ch
                   , p2p_info->peer_listen_channel, p2p_info->link_channel, p2p_info->go_negoing);

    // if (hw_ch != p2p_info->remain_ch)
    {
        //        zt_wlan_set_cur_channel(pnic_info, p2p_info->remain_ch);
        zt_hw_info_set_channel_bw(pnic_info, p2p_info->remain_ch, cw, offset);
    }

    zt_p2p_set_state(p2p_info, P2P_STATE_LISTEN);
    zt_p2p_set_pre_state(p2p_info, P2P_STATE_LISTEN);

    while (p2p_info->ro_ch_duration > 0 && p2p_info->ro_ch_duration < 400)
    {
        p2p_info->ro_ch_duration = p2p_info->ro_ch_duration * 3;
    }
    zt_os_api_timer_set(&p2p_info->remain_ch_timer, p2p_info->ro_ch_duration);
    p2p_info->last_ro_ch_time = zt_os_api_timestamp();
    return 0;
}


