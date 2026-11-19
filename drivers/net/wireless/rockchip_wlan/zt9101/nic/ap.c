/*
 * ap.c
 *
 * impliment IEEE80211 management frame logic of ap role
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

#include "common.h"

#define AP_DBG(fmt, ...)        LOG_D("[%s:%d]"fmt, __func__, __LINE__, ##__VA_ARGS__)
#define AP_WARN(fmt, ...)       LOG_W("[%s:%d]"fmt, __func__, __LINE__, ##__VA_ARGS__)
#define AP_ARRAY(data, len)     zt_log_array(data, len)

#define ZT_EPERM  (1)
#define ZT_EINVAL (22)
#define ZT_EINTR  (4)

#define TIM_BITMAP_LEN  1
#define MAX_AID         7

#ifdef CFG_ENABLE_AP_MODE
zt_inline static
void ap_status_set(nic_info_st *pnic_info, zt_ap_status ap_state)
{
    zt_wlan_mgmt_info_t *pwlan_info = pnic_info->wlan_mgmt_info;
    zt_wlan_network_t *pcur_network = &pwlan_info->cur_network;

    pcur_network->ap_state = ap_state;
}

zt_ap_status zt_ap_status_get(nic_info_st *pnic_info)
{
    zt_wlan_mgmt_info_t *pwlan_info = pnic_info->wlan_mgmt_info;
    zt_wlan_network_t *pcur_network = &pwlan_info->cur_network;

    return pcur_network->ap_state;
}

zt_s32 zt_ap_msg_load(nic_info_st *pnic_info, zt_que_t *pque_tar,
                      zt_ap_msg_tag_e tag, void *pdata, zt_u16 len)
{
    zt_wlan_mgmt_info_t *pwlan_info = pnic_info->wlan_mgmt_info;
    zt_wlan_network_t *pcur_network = &pwlan_info->cur_network;
    zt_que_t *pque;
    zt_que_list_t *pnode;
    zt_ap_msg_t *pmsg;
    wdn_net_info_st *pwdn_info =
        ZT_CONTAINER_OF(pque_tar, wdn_net_info_st, ap_msg);

    zt_os_api_lock_lock(&pcur_network->wdn_del_lock);
    if (pwdn_info->state == E_WDN_AP_STATE_IDLE)
    {
        zt_os_api_lock_unlock(&pcur_network->wdn_del_lock);
        AP_WARN("wdn state is idle");
        return -1;
    }

    /* pop message from queue free */
    pque = &pwlan_info->cur_network.ap_msg_free[tag];
    if ((pnode = zt_deque_head(pque)) == NULL)
    {
        zt_os_api_lock_unlock(&pcur_network->wdn_del_lock);
        AP_WARN("queue empty");
        return -2;
    }

    /* fill message */
    pmsg = ZT_CONTAINER_OF(pnode, zt_ap_msg_t, list);
    pmsg->tag = tag;
    pmsg->len = len;
    if (pdata && len)
    {
        if (tag == ZT_AP_MSG_TAG_BA_RSP_FRAME)
        {
            void *tmp_data = (zt_s8*)pmsg + offsetof(zt_ap_msg_t, data);
            zt_memcpy(tmp_data, pdata, len);
        }
        else
        {
            void *tmp_mgmt = (zt_s8*)pmsg + offsetof(zt_ap_msg_t, mgmt);
            zt_memcpy(tmp_mgmt, pdata, len);
        }
    }

    /* message push into queue load */
    zt_enque_tail(pnode, pque_tar);
    zt_os_api_lock_unlock(&pcur_network->wdn_del_lock);

    return 0;
}

zt_ap_msg_t *zt_ap_msg_get(zt_que_t *pque)
{
    zt_que_list_t *pnode_list;

    if (zt_que_is_empty(pque))
    {
        return NULL;
    }

    pnode_list = zt_que_head(pque);
    return ZT_CONTAINER_OF(pnode_list, zt_ap_msg_t, list);
}


zt_s32 zt_ap_msg_free(nic_info_st *pnic_info, zt_que_t *pque, zt_ap_msg_t *pmsg)
{
    zt_wlan_mgmt_info_t *pwlan_info = pnic_info->wlan_mgmt_info;

    /* pop message from load queue */
    if (zt_deque(&pmsg->list, pque) == NULL)
    {
        AP_WARN("queue empty");
        return -1;
    }

    /* push the message back into free queue */
    zt_enque_head(&pmsg->list, &pwlan_info->cur_network.ap_msg_free[pmsg->tag]);

    return 0;
}

static
void ap_msg_que_clearup(nic_info_st *pnic_info, wdn_net_info_st *pwdn_info)
{
    zt_que_t *pque = &pwdn_info->ap_msg;
    zt_ap_msg_t *pmsg;

    while (zt_true)
    {
        pmsg = zt_ap_msg_get(pque);
        if (pmsg)
        {
            zt_ap_msg_free(pnic_info, pque, pmsg);
        }
        else
        {
            break;
        }
    }
}

static zt_s32 ap_msg_que_init(nic_info_st *pnic_info)
{
    zt_wlan_mgmt_info_t *pwlan_info = pnic_info->wlan_mgmt_info;
    zt_u8 i;
    zt_que_t *pque;
    zt_ap_msg_t *pnode;

    AP_DBG();

    /* auth queue */
#define ZT_AP_MSG_AUTH_QUE_DEEP     (WDN_NUM_MAX * 3)
#define ZT_AP_MSG_AUTH_SIZE_MAX     (ZT_OFFSETOF(zt_ap_msg_t, mgmt) + \
                                     ZT_80211_MGMT_AUTH_SIZE_MAX)
    pque = &pwlan_info->cur_network.ap_msg_free[ZT_AP_MSG_TAG_AUTH_FRAME];
    zt_que_init(pque, ZT_LOCK_TYPE_BH);
    for (i = 0; i < ZT_AP_MSG_AUTH_QUE_DEEP; i++)
    {
        pnode = zt_kzalloc(ZT_AP_MSG_AUTH_SIZE_MAX);
        if (pnode == NULL)
        {
            AP_WARN("malloc failed");
            return -1;
        }
        zt_enque_head(&pnode->list, pque);
    }

    /* deauth queue */
#define ZT_AP_MSG_DEAUTH_QUE_DEEP   WDN_NUM_MAX
#define ZT_AP_MSG_DEAUTH_SIZE_MAX   (ZT_OFFSETOF(zt_ap_msg_t, mgmt) + \
                                     ZT_80211_MGMT_DEAUTH_SIZE_MAX)
    pque = &pwlan_info->cur_network.ap_msg_free[ZT_AP_MSG_TAG_DEAUTH_FRAME];
    zt_que_init(pque, ZT_LOCK_TYPE_BH);
    for (i = 0; i < ZT_AP_MSG_DEAUTH_QUE_DEEP; i++)
    {
        pnode = zt_kzalloc(ZT_AP_MSG_DEAUTH_SIZE_MAX);
        if (pnode == NULL)
        {
            AP_WARN("malloc failed");
            return -1;
        }
        zt_enque_head(&pnode->list, pque);
    }

    /* asoc queue */
#define ZT_AP_MSG_ASSOC_REQ_QUE_DEEP    WDN_NUM_MAX
#define ZT_AP_MSG_ASSOC_REQ_SIZE_MAX    (ZT_OFFSETOF(zt_ap_msg_t, mgmt) + \
        ZT_80211_MGMT_ASSOC_SIZE_MAX)
    pque = &pwlan_info->cur_network.ap_msg_free[ZT_AP_MSG_TAG_ASSOC_REQ_FRAME];
    zt_que_init(pque, ZT_LOCK_TYPE_BH);
    for (i = 0; i < ZT_AP_MSG_ASSOC_REQ_QUE_DEEP; i++)
    {
        pnode = zt_kzalloc(ZT_AP_MSG_ASSOC_REQ_SIZE_MAX);
        if (pnode == NULL)
        {
            AP_WARN("malloc failed");
            return -2;
        }
        zt_enque_head(&pnode->list, pque);
    }

    /* disassoc queue */
#define ZT_AP_MSG_DISASSOC_QUE_DEEP   WDN_NUM_MAX
#define ZT_AP_MSG_DISASSOC_SIZE_MAX   (ZT_OFFSETOF(zt_ap_msg_t, mgmt) + \
                                       ZT_80211_MGMT_DISASSOC_SIZE_MAX)
    pque = &pwlan_info->cur_network.ap_msg_free[ZT_AP_MSG_TAG_DISASSOC_FRAME];
    zt_que_init(pque, ZT_LOCK_TYPE_BH);
    for (i = 0; i < ZT_AP_MSG_DISASSOC_QUE_DEEP; i++)
    {
        pnode = zt_kzalloc(ZT_AP_MSG_DISASSOC_SIZE_MAX);
        if (pnode == NULL)
        {
            AP_WARN("malloc failed");
            return -1;
        }
        zt_enque_head(&pnode->list, pque);
    }

#define ZT_AP_MSG_BA_REQ_QUE_DEEP   TID_NUM
#define ZT_AP_MSG_BA_REQ_SIZE_MAX   ZT_OFFSETOF(zt_ap_msg_t, mgmt)
        pque = &pwlan_info->cur_network.ap_msg_free[ZT_AP_MSG_TAG_BA_REQ_FRAME];
        zt_que_init(pque, ZT_LOCK_TYPE_SPIN);
        for (i = 0; i < ZT_AP_MSG_BA_REQ_QUE_DEEP; i++)
        {
            pnode = zt_kzalloc(ZT_AP_MSG_BA_REQ_SIZE_MAX);
            if (pnode == NULL)
            {
                AP_WARN("malloc failed");
                return -1;
            }
            zt_enque_head(&pnode->list, pque);
        }

#define ZT_AP_MSG_BA_RSP_QUE_DEEP   TID_NUM
#define ZT_AP_MSG_BA_RSP_SIZE_MAX   (ZT_OFFSETOF(zt_ap_msg_t, data) + \
                                        ZT_80211_MGMT_BA_RSP_SIZE_MAX)
        pque = &pwlan_info->cur_network.ap_msg_free[ZT_AP_MSG_TAG_BA_RSP_FRAME];
        zt_que_init(pque, ZT_LOCK_TYPE_SPIN);
        for (i = 0; i < ZT_AP_MSG_BA_RSP_QUE_DEEP; i++)
        {
            pnode = zt_kzalloc(ZT_AP_MSG_BA_RSP_SIZE_MAX);
            if (pnode == NULL)
            {
                AP_WARN("malloc failed");
                return -1;
            }
            zt_enque_head(&pnode->list, pque);
        }

    return 0;
}

static zt_s32 ap_msg_deinit(nic_info_st *pnic_info)
{
    zt_wlan_mgmt_info_t *pwlan_info = pnic_info->wlan_mgmt_info;
    zt_u8 i;
    zt_que_t *pque;
    zt_ap_msg_t *pap_msg;
    zt_que_list_t *pnode;
    zt_list_t *pos, *pos_next;
    wdn_list *pwdn = (wdn_list *)pnic_info->wdn;
    wdn_net_info_st *pwdn_info;
    wdn_node_st *pwdn_node;

    /* free message queue in cur_network */
    AP_DBG("free ap_msg_free");
    for (i = 0; i < ZT_AP_MSG_TAG_MAX; i++)
    {
        pque = &pwlan_info->cur_network.ap_msg_free[i];
        while ((pnode = zt_deque_head(pque)))
        {
            pap_msg = ZT_CONTAINER_OF(pnode, zt_ap_msg_t, list);
            zt_kfree(pap_msg);
        }
    }

    /* free message queue in wdn_info */
    AP_DBG("free wdn_ap_msg");
    zt_list_for_each_safe(pos, pos_next, &pwdn->head)
    {
        pwdn_node = zt_list_entry(pos, wdn_node_st, list);
        pwdn_info = &pwdn_node->info;
        if (pwdn_info->mode == ZT_MASTER_MODE)
        {
            /* free message queue */
            pque = &pwdn_info->ap_msg;
            while ((pnode = zt_deque_head(pque)))
            {
                pap_msg = ZT_CONTAINER_OF(pnode, zt_ap_msg_t, list);
                zt_kfree(pap_msg);
            }
            if (!MacAddr_isBcst(pwdn_info->mac))
            {
                zt_deauth_xmit_frame(pnic_info, pwdn_info->mac,
                                     ZT_80211_REASON_QSTA_TIMEOUT);
            }
            /* free the wdn */
            zt_wdn_remove(pnic_info, pwdn_info->mac);
        }
    }
    if (zt_false == pnic_info->is_surprise_removed)
    {
        zt_mcu_set_media_status(pnic_info, WIFI_FW_STATION_STATE);
    }

    return 0;
}

// static zt_pt_ret_t
// ap_core_conn_maintain_traffic(zt_pt_t *pt,  nic_info_st *pnic_info)
// {
//     mlme_info_t *pmlme_info  = (mlme_info_t *)pnic_info->mlme_info;
//     tx_info_st *ptx_info = pnic_info->tx_info;
//     zt_u16 BusyThreshold;

//     if (ptx_info == NULL)
//     {
//         AP_WARN("tx_info NULL");
//     }

//     PT_BEGIN(pt);

//     zt_timer_set(&pmlme_info->traffic_timer, 1000);

//     for (;;)
//     {
// #if 0
//         MLME_DBG("num_tx_ok_in_period=%d  num_rx_ok_in_period=%d",
//                  pmlme_info->link_info.num_tx_ok_in_period,
//                  pmlme_info->link_info.num_rx_ok_in_period);
// #endif
//         PT_WAIT_UNTIL(pt, zt_timer_expired(&pmlme_info->traffic_timer));

//         {
//             zt_u16 BusyThresholdHigh    = 100;
//             zt_u16 BusyThresholdLow     = 75;
//             BusyThreshold = pmlme_info->link_info.busy_traffic ?
//                             BusyThresholdLow : BusyThresholdHigh;
//             if (pmlme_info->link_info.num_rx_ok_in_period > BusyThreshold ||
//                     pmlme_info->link_info.num_tx_ok_in_period > BusyThreshold)
//             {
//                 pmlme_info->link_info.busy_traffic = zt_true;
//             }
//             else
//             {
//                 pmlme_info->link_info.busy_traffic = zt_false;
//             }
//         }

//         if (!pnic_info->nic_num)
//         {
//             zt_s32 i;
//             for (i = 0; i < TID_NUM; i++)
//             {
//                 pmlme_info->link_info.num_tx_ok_in_period_with_tid[i] = 0;
//             }
//             pmlme_info->link_info.num_rx_ok_in_period = 0;
//             pmlme_info->link_info.num_tx_ok_in_period = 0;
//             pmlme_info->link_info.num_rx_unicast_ok_in_period = 0;
//         }

//         zt_timer_restart(&pmlme_info->traffic_timer);
//     }

//     PT_END(pt);
// }

static zt_s32 ap_maintain_ba_rsp_issue(nic_info_st *pnic_info, wdn_net_info_st *pwdn_info, void *pdata)
{
    hw_info_st *phw_info = pnic_info->hw_info;
    zt_add_ba_parm_st *pbarsp_parm = (zt_add_ba_parm_st *)pdata;

    if (phw_info->ba_enable_tx)
    {
        zt_memcpy(&pwdn_info->barsp_parm, pbarsp_parm,
                  sizeof(pwdn_info->barsp_parm));
        if (zt_action_frame_ba_to_issue_ap(pnic_info, pwdn_info,
                                        ZT_WLAN_ACTION_ADDBA_RESP) < 0)
        {
            pwdn_info->ba_ctl[pbarsp_parm->tid].enable = zt_false;
            AP_WARN("*** zt_action_frame_ba_to_issue(ZT_WLAN_ACTION_ADDBA_RESP) failed***");
            return -1;
        }
        else
        {
            pwdn_info->ba_ctl[pbarsp_parm->tid].enable = zt_true;
            pwdn_info->ba_ctl[pbarsp_parm->tid].wait_timeout =
                pbarsp_parm->timeout;
        }
    }

    return 0;
}

static
zt_pt_ret_t maintain_thrd(nic_info_st *pnic_info, wdn_net_info_st *pwdn_info)
{
    //hw_info_st *phw_info = pnic_info->hw_info;
    sec_info_st *psec_info = pnic_info->sec_info;
    zt_pt_t *pt = &pwdn_info->sub_thrd_pt;
    //zt_pt_t *pt_traffic = &pt[1];
    zt_ap_msg_t *pmsg;

    PT_BEGIN(pt);

    AP_DBG("established->"ZT_MAC_FMT, ZT_MAC_ARG(pwdn_info->mac));

    /* state set to established */
    pwdn_info->state =
        psec_info->dot11AuthAlgrthm == dot11AuthAlgrthm_8021X ?
        E_WDN_AP_STATE_8021X_BLOCK : E_WDN_AP_STATE_8021X_UNBLOCK;
    ap_status_set(pnic_info, ZT_AP_STATE_ESTABLISHED);

    /* initilize rx packet statistic */
    pwdn_info->rx_pkt_stat_last = pwdn_info->rx_pkt_stat;
    pwdn_info->rx_idle_timeout = 0;

//    AP_DBG("ba_enable_rx = %d, ba_enable_tx = %d", phw_info->ba_enable_rx, phw_info->ba_enable_tx);
    // if (phw_info->ba_enable_rx)
    // {
    //     rx_info_t *rx_info = pnic_info->rx_info;
    //     pwdn_info->ba_ctl = rx_info->ba_ctl;
    //     AP_DBG("pwdn_info->ba_ctl == %s", pwdn_info->ba_ctl == NULL ? "NULL" : "NOT NULL");
    // }
    zt_rx_ba_all_reinit(pnic_info);
    zt_timer_set(&pwdn_info->ap_timer, 1000);

    for (;;)
    {
        ap_rx_watch(pnic_info, zt_true);
//        ap_core_conn_maintain_traffic(pt_traffic, pnic_info);
        PT_WAIT_UNTIL(pt, (pmsg = zt_ap_msg_get(&pwdn_info->ap_msg)) ||
                      zt_timer_expired(&pwdn_info->ap_timer));
        /* one second timeon */
        if (pmsg == NULL)
        {
            zt_timer_reset(&pwdn_info->ap_timer);
            /* todo: compare packet statistics with last value, if equal mains
            current connection has idle during the last one second */
            if (pwdn_info->rx_pkt_stat != pwdn_info->rx_pkt_stat_last)
            {
                pwdn_info->rx_pkt_stat_last = pwdn_info->rx_pkt_stat;
                pwdn_info->rx_idle_timeout = 0;
                continue;
            }
            pwdn_info->rx_idle_timeout++;

            /* if idle timeout detected, checkout whether the conntion is well. */
#define RX_IDLE_TIMEOUT_SECONDS     (5) /* 5 seconds */
#define RX_IDLE_BREAK_SECONDS       (10) /* 10 seconds */
            if (pwdn_info->rx_idle_timeout >= RX_IDLE_TIMEOUT_SECONDS)
            {
                /* if station is sleeping, launch beacon with TIM to wakeup it. */
                if (pwdn_info->psm)
                {
                    int ret;
                    zt_wlan_mgmt_info_t *pwlan_info = pnic_info->wlan_mgmt_info;
                    zt_wlan_network_t *pcur_network = &pwlan_info->cur_network;

                    if (!zt_bmp_is_set(pcur_network->tim_bitmap, pwdn_info->aid))
                    {
                        zt_bmp_set(&pcur_network->tim_bitmap, pwdn_info->aid);
                        ret = zt_ap_update_beacon(pnic_info, ZT_80211_MGMT_EID_TIM,
                                                  NULL, zt_true);
                        if (ret)
                        {
                            zt_bmp_clear(&pcur_network->tim_bitmap, pwdn_info->aid);
                        }
                    }
                }
                /* try send null packet to station used for testing connection. */
                if (!zt_nic_null_xmit(pnic_info, pwdn_info, zt_false, 100))
                {
                    pwdn_info->rx_idle_timeout = 0;
                    continue;
                }
            }
            if (pwdn_info->rx_idle_timeout >= RX_IDLE_BREAK_SECONDS)
            {
                AP_WARN("STA idle timeout->"ZT_MAC_FMT, ZT_MAC_ARG(pwdn_info->mac));
                /* send deauth frame to STA */
                zt_deauth_xmit_frame(pnic_info, pwdn_info->mac,
                                     ZT_80211_REASON_QSTA_TIMEOUT);
                break;
            }
        }

        else if (pmsg->tag == ZT_AP_MSG_TAG_AUTH_FRAME)
        {
            AP_DBG("reauth->"ZT_MAC_FMT, ZT_MAC_ARG(pwdn_info->mac));
            break;
        }

        else if (pmsg->tag == ZT_AP_MSG_TAG_DEAUTH_FRAME)
        {
            AP_DBG("deauth->"ZT_MAC_FMT, ZT_MAC_ARG(pwdn_info->mac));
            zt_action_frame_del_ba_request(pnic_info, pwdn_info->mac);
            pwdn_info->state = E_WDN_AP_STATE_DAUTH;
            if (pmsg->len == 0)
            {
                /* send deauth frame to STA as respond */
                zt_deauth_xmit_frame(pnic_info, pwdn_info->mac, pwdn_info->reason_code);
            }
            else
            {
                pwdn_info->reason_code = pmsg->mgmt.deauth.reason_code;
            }
            break;
        }

        else if (pmsg->tag == ZT_AP_MSG_TAG_DISASSOC_FRAME)
        {
            AP_DBG("disassoc->"ZT_MAC_FMT, ZT_MAC_ARG(pwdn_info->mac));
            /* status 2 timeout */
            zt_timer_set(&pwdn_info->ap_timer, 5 * 1000);
            PT_YIELD_UNTIL(pt, (pmsg = zt_ap_msg_get(&pwdn_info->ap_msg)) ||
                           zt_timer_expired(&pwdn_info->ap_timer));
            if (pmsg && pmsg->tag == ZT_AP_MSG_TAG_ASSOC_REQ_FRAME)
            {
                /* back to process assoc */
                pwdn_info->state = E_WDN_AP_STATE_ASSOC;
                /* notify connection break */
                zt_os_api_ap_ind_disassoc(pnic_info, pwdn_info,
                                          ZT_MLME_FRAMEWORK_NETLINK);
                zt_mcu_msg_sta_info_set(pnic_info, pwdn_info, zt_false);
                zt_mcu_media_connect_set(pnic_info, pwdn_info, zt_false);
            }
            break;
        }

        else if (pmsg->tag == ZT_AP_MSG_TAG_BA_REQ_FRAME)
        {
            AP_DBG("ba_req->"ZT_MAC_FMT, ZT_MAC_ARG(pwdn_info->mac));
            zt_ap_msg_free(pnic_info, &pwdn_info->ap_msg, pmsg);

            /* send action ba request frame to STA */
            zt_action_frame_ba_to_issue_ap(pnic_info, pwdn_info,
                                        ZT_WLAN_ACTION_ADDBA_REQ);
        }

        else if (pmsg->tag == ZT_AP_MSG_TAG_BA_RSP_FRAME)
        {
            AP_DBG("ba_rsp->"ZT_MAC_FMT, ZT_MAC_ARG(pwdn_info->mac));

            /* send action ba response frame to STA */
            ap_maintain_ba_rsp_issue(pnic_info, pwdn_info, ((void *)pmsg->data));
            zt_ap_msg_free(pnic_info, &pwdn_info->ap_msg, pmsg);
        }

        else
        {
            AP_DBG("unknown tag-> %d", pmsg->tag);
            zt_ap_msg_free(pnic_info, &pwdn_info->ap_msg, pmsg);
        }
    }

    PT_END(pt);
}

static void clearup(nic_info_st *pnic_info, wdn_net_info_st *pwdn_info)
{
    zt_wlan_mgmt_info_t *pwlan_info = pnic_info->wlan_mgmt_info;
    zt_wlan_network_t *pcur_network = &pwlan_info->cur_network;

    /* up event to hostapd */
    if (pwdn_info->state > E_WDN_AP_STATE_ASSOC)
    {
        zt_os_api_ap_ind_disassoc(pnic_info, pwdn_info, ZT_MLME_FRAMEWORK_NETLINK);
        zt_mcu_msg_sta_info_set(pnic_info, pwdn_info, zt_false);
        zt_mcu_media_connect_set(pnic_info, pwdn_info, zt_false);
    }

    /* clearup wdn psm data queue */
    {
        zt_list_t *pos, *pos_next;

        zt_os_api_lock_lock(&pwdn_info->psm_lock);
        /* clearup sleep message queue */
        zt_list_for_each_safe(pos, pos_next,
                              zt_que_list_head(&pwdn_info->psm_data_que))
        {
            struct xmit_frame *pxmitframe =
                ZT_CONTAINER_OF(pos, struct xmit_frame, list);
            tx_info_st *ptx_info = pnic_info->tx_info;
            zt_deque(pos, &pwdn_info->psm_data_que);

            zt_free_skb(pxmitframe->pkt);
            pxmitframe->pkt = NULL;
            zt_xmit_frame_enqueue(ptx_info, pxmitframe);
        }
        pwdn_info->psm = zt_false;
        zt_os_api_lock_unlock(&pwdn_info->psm_lock);

        /* clear beacon TIM filed */
        zt_bmp_clear(&pcur_network->tim_bitmap, pwdn_info->aid);
        zt_ap_update_beacon(pnic_info, ZT_80211_MGMT_EID_TIM, NULL, zt_true);
    }

    /* free key */
    zt_sec_free_key(pnic_info, pwdn_info->unicast_cam_id, -1 /*pwdn_info->group_cam_id */);

    zt_os_api_lock_lock(&pcur_network->wdn_del_lock);
    /* clearup message queue in the wdn */
    ap_msg_que_clearup(pnic_info, pwdn_info);
    /* free the wdn */
    {
        wdn_list *pwdn = pnic_info->wdn;
        wdn_node_st *pwdn_node;

        AP_DBG("[%d] wdn_id:%d", pnic_info->ndev_id, pwdn_info->wdn_id);

        /* node remove from head list */
        pwdn_node = zt_list_entry(pwdn_info, wdn_node_st, info);
        zt_list_delete(&pwdn_node->list);
        /* link the node to free list */
        zt_list_insert_tail(&pwdn_node->list, &pwdn->free);

        /* update wdn */
        *pnic_info->wdn_id_bitmap &= ~ ZT_BIT(pwdn_info->wdn_id);
        pwdn_info->state = E_WDN_AP_STATE_IDLE;
        pwdn->cnt--;
    }
    pcur_network->sta_cnt--;
    zt_os_api_lock_unlock(&pcur_network->wdn_del_lock);

    /* if no any one connection has established(except wdn_id=1 for boardcast),
    ap status set back to runing */
    if (!pcur_network->sta_cnt)
    {
        /* update ap status */
        if (pcur_network->freeze_pend)
        {
            ap_status_set(pnic_info, ZT_AP_STATE_FREEZE);
        }
        else
        {
            ap_status_set(pnic_info, ZT_AP_STATE_STANBY);
        }
    }
}

static zt_pt_ret_t ap_thrd(nic_info_st *pnic_info, wdn_net_info_st *pwdn_info)
{
    zt_pt_t *pt = &pwdn_info->ap_thrd_pt;
    zt_pt_ret_t thrd_ret;

    PT_BEGIN(pt);

    for (;;)
    {
        /* auth */
        PT_SPAWN(pt, &pwdn_info->sub_thrd_pt,
                 thrd_ret = zt_auth_ap_thrd(pnic_info, pwdn_info));
        if (thrd_ret == PT_EXITED)
        {
            break;
        }

        /* assoc */
assoc_entry:
        PT_SPAWN(pt, &pwdn_info->sub_thrd_pt,
                 thrd_ret = zt_assoc_ap_thrd(pnic_info, pwdn_info));
        if (thrd_ret == PT_EXITED)
        {
            break;
        }

        /* maintain */
        PT_SPAWN(pt, &pwdn_info->sub_thrd_pt,
                 thrd_ret = maintain_thrd(pnic_info, pwdn_info));
        if (pwdn_info->state == E_WDN_AP_STATE_ASSOC)
        {
            goto assoc_entry;
        }
        else
        {
            break;
        }
    }

    clearup(pnic_info, pwdn_info);

    PT_END(pt);
}

static zt_inline zt_s32 new_boradcast_wdn(nic_info_st *pnic_info)
{
    wdn_net_info_st *pwdn_info;
    zt_u8 bc_addr[ZT_80211_MAC_ADDR_LEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

    LOG_D("new_boradcast_wdn!!!!!!!!");
    pwdn_info = zt_wdn_add(pnic_info, bc_addr);
    if (pwdn_info == NULL)
    {
        AP_WARN("alloc bmc wdn error");
        return -1;
    }
    zt_que_init(&pwdn_info->ap_msg, ZT_LOCK_TYPE_BH);
    pwdn_info->aid = 0;
    pwdn_info->qos_option = 0;
    pwdn_info->state = E_WDN_AP_STATE_8021X_UNBLOCK;
    pwdn_info->ieee8021x_blocked = zt_true;
    pwdn_info->network_type = WIRELESS_11B;
    pwdn_info->mode = ZT_MASTER_MODE;

    /* notify connection establish */
    zt_ap_add_sta_ratid(pnic_info, pwdn_info);

    pwdn_info->psm = zt_false;
    zt_que_init(&pwdn_info->psm_data_que, ZT_LOCK_TYPE_NONE);
    zt_os_api_lock_init(&pwdn_info->psm_lock, ZT_LOCK_TYPE_BH);
    return 0;
}

static void ap_poll(nic_info_st *pnic_info)
{
    zt_wlan_mgmt_info_t *pwlan_info = pnic_info->wlan_mgmt_info;
    zt_wlan_network_t *pcur_network = &pwlan_info->cur_network;
    pwr_info_st *pwr_info = pnic_info->pwr_info;

    AP_DBG();

    zt_os_api_thread_enter_hook(pcur_network->ap_tid);

    for (;;)
    {
        /* if uninstall process is detected, stop ap process immediately */
        if (ZT_CANNOT_RUN(pnic_info))
        {
            break;
        }

        if (zt_local_cfg_get_work_mode(pnic_info) == ZT_MASTER_MODE &&
            !pnic_info->is_driver_critical && pnic_info->is_up)
            ap_rx_watch(pnic_info, zt_false);

        if (zt_local_cfg_get_work_mode(pnic_info) != ZT_MASTER_MODE ||
                pwr_info->bInSuspend ||
                pcur_network->ap_state == ZT_AP_STATE_FREEZE ||
                !pcur_network->sta_cnt)
        {
            zt_msleep(1);
            continue;
        }

        /* poll ap thread use each wdn_info */
        do
        {
            zt_list_t *pos, *pos_next;
            wdn_list *pwdn = pnic_info->wdn;

            zt_list_for_each_safe(pos, pos_next, &pwdn->head)
            {
                wdn_node_st *pwdn_node = zt_list_entry(pos, wdn_node_st, list);
                wdn_net_info_st *pwdn_info = &pwdn_node->info;

                /* skip broadcast wdn handle */
                if (pwdn_info->mode != ZT_MASTER_MODE ||
                        zt_80211_is_bcast_addr(pwdn_info->mac))
                {
                    continue;
                }

                /* ap thread handle */
                ap_thrd(pnic_info, pwdn_info);
            }
            zt_msleep(1);
        } while (pcur_network->sta_cnt);
    }

    AP_DBG("wait for thread destory...");
    while (!zt_os_api_thread_wait_stop(pcur_network->ap_tid))
    {
        zt_msleep(1);
    }

    ap_status_set(pnic_info, ZT_AP_STATE_TERM);
    zt_os_api_thread_exit(pcur_network->ap_tid);
}

zt_s32 zt_ap_probe_parse(nic_info_st *pnic_info,
                         zt_80211_mgmt_t *pframe, zt_u16 frame_len)
{
    struct xmit_buf *pxmit_buf;
    tx_info_st *ptx_info;
    zt_wlan_mgmt_info_t *pwlan_info = pnic_info->wlan_mgmt_info;
    zt_wlan_network_t *pcur_network = &pwlan_info->cur_network;
    zt_80211_mgmt_t *pmgmt;

    if (!pnic_info->is_up)
    {
        return -1;
    }

    if (!(zt_ap_status_get(pnic_info) & ZT_AP_STATE_BIT_RUN))
    {
        return -2;
    }

    if (zt_80211_hdr_type_get(pframe) != ZT_80211_FRM_PROBE_REQ)
    {
        return -3;
    }

    if (!(zt_80211_is_same_addr(pframe->da, pcur_network->mac_addr) ||
            zt_80211_is_bcast_addr(pframe->da)))
    {
       // AP_WARN("probe request target address invalid");
        return -4;
    }

    {
        zt_80211_mgmt_ie_t *pies = (void *)pframe->probe_req.variable, *pie;
        zt_u16 ies_len = ZT_OFFSETOF(zt_80211_mgmt_t, probe_req.variable);
        zt_wlan_ssid_t *local_ssid =
            pcur_network->hidden_ssid_mode != ZT_80211_HIDDEN_SSID_NOT_IN_USE ?
            &pcur_network->hidden_ssid : &pcur_network->ssid;

        if (zt_80211_mgmt_ies_search(pies, ies_len, ZT_80211_MGMT_EID_SSID, &pie))
        {
            AP_WARN("missing ie of ssid filed");
            return -5;
        }

        if (pie->len && (pie->len != local_ssid->length ||
                         zt_memcmp(pie->data, local_ssid->data, pie->len)))
        {
            AP_DBG("it is not a wildcard ssid, but do not probe request to me");
            return 0;
        }
    }

//    AP_DBG("receive probe request");

    /* alloc xmit_buf */
    ptx_info = (tx_info_st *)pnic_info->tx_info;
    pxmit_buf = zt_xmit_extbuf_new(ptx_info);
    if (pxmit_buf == NULL)
    {
        AP_WARN("pxmit_extbuf is NULL");
        return -6;
    }

    /* clear frame head(txd + 80211head) */
    zt_memset(pxmit_buf->pbuf, 0,
              TXDESC_OFFSET + ZT_OFFSETOF(zt_80211_mgmt_t, beacon));

    /* set frame type */
    pmgmt = (void *)&pxmit_buf->pbuf[TXDESC_OFFSET];
    zt_80211_hdr_type_set(pmgmt, ZT_80211_FRM_PROBE_RESP);

    /* set address */
    zt_memcpy(pmgmt->da, pframe->sa, ZT_ARRAY_SIZE(pmgmt->da));
    zt_memcpy(pmgmt->sa, pcur_network->mac_addr, ZT_ARRAY_SIZE(pmgmt->sa));
    zt_memcpy(pmgmt->bssid, pcur_network->bssid, ZT_ARRAY_SIZE(pmgmt->bssid));

    /* set ie fiexd field */
    pmgmt->beacon.intv = zt_cpu_to_le16(pcur_network->bcn_interval);
    pmgmt->beacon.capab = zt_cpu_to_le16(pcur_network->cap_info);

    switch (pcur_network->hidden_ssid_mode)
    {
        case ZT_80211_HIDDEN_SSID_NOT_IN_USE :
            /* set ie variable fields */
            zt_memcpy(pmgmt->beacon.variable,
                      pcur_network->ies, pcur_network->ies_length);
            /* send packet */
            pxmit_buf->pkt_len = ZT_OFFSETOF(zt_80211_mgmt_t, probe_resp.variable) +
                                 pcur_network->ies_length;
            break;

        case ZT_80211_HIDDEN_SSID_ZERO_LEN :
            if (!zt_memcmp(pframe->probe_req.variable + 2,
                           pcur_network->hidden_ssid.data,
                           pcur_network->hidden_ssid.length))
            {
                zt_wlan_set_cur_ssid(pnic_info, &pcur_network->hidden_ssid);
                pmgmt->beacon.variable[0] = ZT_80211_MGMT_EID_SSID;
                pmgmt->beacon.variable[1] = pcur_network->hidden_ssid.length;
                zt_memcpy(pmgmt->beacon.variable + 2, pcur_network->hidden_ssid.data,
                          pcur_network->hidden_ssid.length);

                zt_memcpy(pmgmt->beacon.variable + 2 + pcur_network->hidden_ssid.length,
                          pcur_network->ies + 2 + pcur_network->ies[1],
                          pcur_network->ies_length -  2 - pcur_network->ies[1]);
                /* send packet */
                pxmit_buf->pkt_len = ZT_OFFSETOF(zt_80211_mgmt_t, probe_resp.variable) +
                                     pcur_network->ies_length + pcur_network->hidden_ssid.length;
            }
            else
            {
                zt_xmit_extbuf_delete(ptx_info, pxmit_buf);
                return 0;
            }
            break;
        case ZT_80211_HIDDEN_SSID_ZERO_CONTENTS :
            if (!zt_memcmp(pframe->probe_req.variable + 2,
                           pcur_network->hidden_ssid.data,
                           pcur_network->hidden_ssid.length))
            {
                zt_wlan_set_cur_ssid(pnic_info, &pcur_network->hidden_ssid);
                zt_memcpy(pmgmt->beacon.variable,
                          pcur_network->ies, pcur_network->ies_length);
                zt_memcpy(pmgmt->beacon.variable + 2, pcur_network->hidden_ssid.data,
                          pcur_network->hidden_ssid.length);
                /* send packet */
                pxmit_buf->pkt_len = ZT_OFFSETOF(zt_80211_mgmt_t, probe_resp.variable) +
                                     pcur_network->ies_length;
            }
            else
            {
                zt_xmit_extbuf_delete(ptx_info, pxmit_buf);
                return 0;
            }
            break;
        default :
            zt_xmit_extbuf_delete(ptx_info, pxmit_buf);
            return -7;
    }

    /* send packet */
    zt_nic_mgmt_frame_xmit(pnic_info, NULL, pxmit_buf, pxmit_buf->pkt_len);

    return 0;
}

static zt_s32 ap_launch_beacon(nic_info_st *pnic_info)
{
    struct xmit_buf *pxmit_buf;
    tx_info_st *ptx_info;
    zt_wlan_mgmt_info_t *pwlan_info = pnic_info->wlan_mgmt_info;
    zt_wlan_network_t *pcur_network = &pwlan_info->cur_network;
    zt_80211_mgmt_t *pmgmt;

    ptx_info = (tx_info_st *)pnic_info->tx_info;

    /* alloc xmit_buf */
    ptx_info->is_bcn_pkt = zt_true;
    pxmit_buf = zt_xmit_extbuf_new(ptx_info);
    if (pxmit_buf == NULL)
    {
        AP_WARN("pxmit_buf is NULL pcur_network->tim_bitmap=%02x",
                pcur_network->tim_bitmap);
        return -1;
    }

    /* clear frame head(txd + 80211head) */
    zt_memset(pxmit_buf->pbuf, 0,
              TXDESC_OFFSET + ZT_OFFSETOF(zt_80211_mgmt_t, beacon));

    /* set frame type */
    pmgmt = (void *)&pxmit_buf->pbuf[TXDESC_OFFSET];
    zt_80211_hdr_type_set(pmgmt, ZT_80211_FRM_BEACON);

    /* set address */
    zt_memset(pmgmt->da, 0xff, ZT_80211_MAC_ADDR_LEN);
    zt_memcpy(pmgmt->sa, pcur_network->mac_addr, ZT_80211_MAC_ADDR_LEN);
    zt_memcpy(pmgmt->bssid, pcur_network->bssid, ZT_80211_MAC_ADDR_LEN);

    /* set ie fiexd field */
    pmgmt->beacon.intv = zt_cpu_to_le16(pcur_network->bcn_interval);
    pmgmt->beacon.capab = zt_cpu_to_le16(pcur_network->cap_info);

    /* set ie variable fields */
    zt_memcpy(pmgmt->beacon.variable,
              pcur_network->ies, pcur_network->ies_length);

    /* send packet */
    pxmit_buf->pkt_len =
        ZT_OFFSETOF(zt_80211_mgmt_t, beacon.variable) + pcur_network->ies_length;
    zt_nic_beacon_xmit(pnic_info, pxmit_buf, pxmit_buf->pkt_len);

    return 0;
}

static void ap_update_reg(nic_info_st *pnic_info)
{
    zt_wlan_mgmt_info_t *pwlan_info = pnic_info->wlan_mgmt_info;
    zt_wlan_network_t *pcur_network = &pwlan_info->cur_network;
    sec_info_st *psec_info = pnic_info->sec_info;
    zt_u32 tmp32u;
    zt_u16 br_cfg = 0;

    AP_DBG();

    /* set ht paramete */
    if (pcur_network->ht_enable == zt_true)
    {
        zt_mcu_set_max_ampdu_len(pnic_info,
                                 pcur_network->pht_cap.ampdu_params_info &
                                 ZT_80211_MGMT_HT_AMPDU_PARM_FACTOR);
    }

    /* clear CAM */
    zt_mcu_set_dk_cfg(pnic_info, psec_info->dot11AuthAlgrthm, zt_false);
    zt_mcu_set_on_rcr_am(pnic_info, zt_false);

    /* set AP mode */
    zt_mcu_set_ap_mode(pnic_info);

    /* set bssid */
    zt_mcu_set_bssid(pnic_info, (void *)pcur_network->bssid);

    /* set AC_PARAM */
    zt_mcu_set_ac_vo(pnic_info);
    zt_mcu_set_ac_vi(pnic_info);
    zt_mcu_set_ac_be(pnic_info);
    zt_mcu_set_ac_bk(pnic_info);

    /* Set Security */
#define TX_USE_DEF_KEY              ZT_BIT(0)
#define RX_USE_DEF_KEY              ZT_BIT(1)
#define TX_ENC_ENABLE               ZT_BIT(2)
#define RX_DEC_ENABLE               ZT_BIT(3)
#define SEACH_KEY_BY_A2             ZT_BIT(4)
#define NO_SEACH_MULTICAST          ZT_BIT(5)
#define TX_BROADCAST_USE_DEF_KEY    ZT_BIT(6)
#define RX_BROADCAST_USE_DEF_KEY    ZT_BIT(7)
    switch (psec_info->dot11AuthAlgrthm)
    {
        case dot11AuthAlgrthm_Shared :
            tmp32u = TX_USE_DEF_KEY | RX_USE_DEF_KEY |
                     TX_ENC_ENABLE | RX_DEC_ENABLE |
                     TX_BROADCAST_USE_DEF_KEY | RX_BROADCAST_USE_DEF_KEY;
            break;
        case dot11AuthAlgrthm_8021X :
            tmp32u = TX_ENC_ENABLE | RX_DEC_ENABLE |
                     TX_BROADCAST_USE_DEF_KEY | RX_BROADCAST_USE_DEF_KEY;
            break;
        case dot11AuthAlgrthm_Open :
        default :
            tmp32u = 0x0;
            break;
    }
    zt_mcu_set_sec_cfg(pnic_info, tmp32u);

    /* set beacon interval */
    zt_mcu_set_bcn_intv(pnic_info, pcur_network->bcn_interval);

    /* set SISF */
    zt_mcu_set_sifs(pnic_info); /* 0x0808 -> for CCK, 0x0a0a -> for OFDM */

    /* set wireless mode */

    /* set basic rate */
    get_bratecfg_by_support_dates(pcur_network->rate, pcur_network->rate_len,
                                  &br_cfg);
    zt_mcu_set_basic_rate(pnic_info, br_cfg);

    /* set preamble */
    zt_mcu_set_preamble(pnic_info,
                        (pcur_network->cap_info & ZT_80211_MGMT_CAPAB_SHORT_PREAMBLE) ?
                        zt_true : zt_false);

    /* set slot time */
    zt_mcu_set_slot_time(pnic_info, pcur_network->short_slot);

    /* set hif channel basebond */
    *pnic_info->hw_bw = pcur_network->bw;
    *pnic_info->hw_offset = pcur_network->channle_offset;
    *pnic_info->hw_ch = pcur_network->channel;

    /* set channel basebond */
    zt_hw_info_set_channel_bw(pnic_info,
                               pcur_network->channel,
                               pcur_network->bw,
                               pcur_network->channle_offset);
}

void reset_beacon_channel(nic_info_st *pnic_info, zt_u8 channel)
{
    zt_wlan_mgmt_info_t *pwlan_info = pnic_info->wlan_mgmt_info;
    zt_wlan_network_t *pcur_network = &pwlan_info->cur_network;
    zt_80211_mgmt_ie_t *pie;
    zt_u16 ofs;

    LOG_I("%s nic_num:%d  channel:%d", __func__, pnic_info->nic_num, channel);

    if (pnic_info == NULL)
    {
        LOG_E("pnic NULL");
        return;
    }
    if (pcur_network->ies[0] == '\0')
    {
        LOG_E("ie NULL");
        return;
    }

    for (ofs = 0; ofs < pcur_network->ies_length;
            ofs += ZT_OFFSETOF(zt_80211_mgmt_ie_t, data) + pie->len)
    {
        pie = (void *)&pcur_network->ies[ofs];

        if (pie->element_id == ZT_80211_MGMT_EID_DS_PARAMS)
        {
            pie->data[0] = channel;
            LOG_D("%s  channel:%d", __func__, channel);
            break;
        }
    }
}

zt_s32 zt_ap_set_beacon(nic_info_st *pnic_info, zt_u8 *pbuf, zt_u32 len,
                        zt_u8 framework)
{
    zt_wlan_mgmt_info_t *pwlan_info = pnic_info->wlan_mgmt_info;
    sec_info_st *psec_info = pnic_info->sec_info;
    zt_wlan_network_t *pcur_network = &pwlan_info->cur_network;
    hw_info_st *phw_info = (hw_info_st *)pnic_info->hw_info;
    zt_u32 ies_len;
    struct beacon_ie *pies;
    zt_u16 var_len, ofs;
    zt_u8 *pvar;
    zt_80211_mgmt_ie_t *pie;
    zt_80211_wmm_param_ie_t *pwmm_ie;
    zt_80211_mgmt_ht_cap_t *pht_cap = NULL;
    zt_80211_mgmt_ht_operation_t *pht_oper;
    zt_u16 i, j;
    zt_bool bConnect;
    wdn_net_info_st *pwdn = NULL;

    AP_DBG();

    ies_len = len;
    if (ies_len <= ZT_OFFSETOF(struct beacon_ie, variable))
    {
        AP_WARN("ie data corrupt");
        return -1;
    }

    /* initilize value */
    pcur_network->ssid.length = 0;
    pcur_network->channel = 0;
    pcur_network->rate_len = 0;
    pcur_network->ht_enable = zt_false;
    zt_memset(&pcur_network->pwmm, 0, sizeof(zt_80211_wmm_param_ie_t));
    zt_memset(&pcur_network->pht_cap, 0, sizeof(zt_80211_mgmt_ht_cap_t));
    zt_memset(&pcur_network->pht_oper, 0, sizeof(zt_80211_mgmt_ht_operation_t));

    /* update mac/bssid */
    zt_memcpy(pcur_network->mac_addr, nic_to_local_addr(pnic_info),
              ZT_80211_MAC_ADDR_LEN);
    zt_memcpy(pcur_network->bssid, nic_to_local_addr(pnic_info),
              ZT_80211_MAC_ADDR_LEN);
    AP_DBG("bssid: "ZT_MAC_FMT, ZT_MAC_ARG(pcur_network->bssid));

    /* get ie fixed field */
    pies = (void *)pbuf;
    pcur_network->bcn_interval   = zt_le16_to_cpu(pies->intv);
    pcur_network->cap_info       = zt_le16_to_cpu(pies->capab);
    AP_DBG("beacon interval: (%d), capability information: (0x%04X)",
           pcur_network->bcn_interval, pcur_network->cap_info);

    /* save ies variable field */
    pvar = pies->variable;
    var_len = ies_len - ZT_OFFSETOF(struct beacon_ie, variable);
    zt_memcpy(pcur_network->ies, pvar, var_len);
    pcur_network->ies_length = var_len;
    //    AP_ARRAY(pcur_network->ies, ies_len);
    pcur_network->tim_bitmap = 0x0;
    zt_ap_update_beacon(pnic_info, ZT_80211_MGMT_EID_TIM, NULL, zt_false);
    zt_ap_update_beacon(pnic_info, ZT_80211_MGMT_EID_VENDOR_SPECIFIC, WPS_OUI, zt_false);

    /* checkout ie variable field */
    for (ofs = 0; ofs < var_len;
            ofs += ZT_OFFSETOF(zt_80211_mgmt_ie_t, data) + pie->len)
    {
        pie = (void *)&pvar[ofs];

        switch (pie->element_id)
        {
            case ZT_80211_MGMT_EID_SSID :
                if (pie->len >= sizeof(pcur_network->ssid.data))
                {
                    AP_WARN("ssid length(%d) over limite", pie->len);
                    break;
                }
                pcur_network->ssid.length = pie->len;
                zt_memcpy(pcur_network->ssid.data, pie->data,
                          pcur_network->ssid.length);
                pcur_network->ssid.data[pcur_network->ssid.length] = '\0';
                AP_DBG("ssid: %s", pcur_network->ssid.data);
                if (zt_p2p_is_valid(pnic_info))
                {
                    p2p_info_st *p2p_info = pnic_info->p2p;
                    zt_memcpy(p2p_info->p2p_group_ssid, pcur_network->ssid.data,
                              pcur_network->ssid.length);
                    p2p_info->p2p_group_ssid_len = pcur_network->ssid.length;
                }
                break;

            case ZT_80211_MGMT_EID_DS_PARAMS :
                if (pnic_info->buddy_nic)
                {
                    zt_mlme_get_connect(pnic_info->buddy_nic, &bConnect);
                    if (bConnect == zt_true &&
                            zt_local_cfg_get_work_mode(pnic_info->buddy_nic) == ZT_INFRA_MODE)
                    {
                        pwdn = zt_wdn_find_info_by_id(pnic_info->buddy_nic, 0);
                        if (pwdn)
                        {
                            AP_DBG("sta channel is : %d", pwdn->channel);
                            pcur_network->channel = pwdn->channel;
                            reset_beacon_channel(pnic_info, pwdn->channel);
                        }
                    }
                    else
                    {
                        pcur_network->channel = pie->data[0];
                    }
                }
                else
                {
                    pcur_network->channel = pie->data[0];
                }

                AP_DBG("channel: %d", pcur_network->channel);
                break;

            case ZT_80211_MGMT_EID_EXT_SUPP_RATES :
            case ZT_80211_MGMT_EID_SUPP_RATES :
                if (pie->len >= sizeof(pcur_network->rate) - pcur_network->rate_len)
                {
                    AP_WARN("support rates size over limite");
                    break;
                }
                /* check and retrieve rate */
                if (pcur_network->rate_len == 0)
                    zt_memset(pcur_network->rate, 0x0,
                              ZT_ARRAY_SIZE(pcur_network->rate));
                for (i = 0; i < pie->len; i++)
                {
                    for (j = 0; j < ZT_ARRAY_SIZE(phw_info->datarate); j++)
                    {
                        if (phw_info->datarate[j] == 0x0)
                        {
                            break;
                        }
                        if ((pie->data[i] & ~IEEE80211_BASIC_RATE_MASK) ==
                                (phw_info->datarate[j] & ~IEEE80211_BASIC_RATE_MASK))
                        {
                            pcur_network->rate[pcur_network->rate_len++] =
                                pie->data[i];
                            break;
                        }
                    }
                }
                AP_DBG("data rate(Mbps): %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d",
                       (pcur_network->rate[0] & 0x7F) / 2,
                       (pcur_network->rate[1] & 0x7F) / 2,
                       (pcur_network->rate[2] & 0x7F) / 2,
                       (pcur_network->rate[3] & 0x7F) / 2,
                       (pcur_network->rate[4] & 0x7F) / 2,
                       (pcur_network->rate[5] & 0x7F) / 2,
                       (pcur_network->rate[6] & 0x7F) / 2,
                       (pcur_network->rate[7] & 0x7F) / 2,
                       (pcur_network->rate[8] & 0x7F) / 2,
                       (pcur_network->rate[9] & 0x7F) / 2,
                       (pcur_network->rate[10] & 0x7F) / 2,
                       (pcur_network->rate[11] & 0x7F) / 2,
                       (pcur_network->rate[12] & 0x7F) / 2,
                       (pcur_network->rate[13] & 0x7F) / 2,
                       (pcur_network->rate[14] & 0x7F) / 2,
                       (pcur_network->rate[15] & 0x7F) / 2);
                break;

            case ZT_80211_MGMT_EID_ERP_INFO :
                break;

            case ZT_80211_MGMT_EID_RSN :
                if (!zt_80211_mgmt_rsn_parse(pie,
                                             ZT_OFFSETOF(zt_80211_mgmt_ie_t, data) + pie->len,
                                             &psec_info->rsn_group_cipher,
                                             &psec_info->rsn_pairwise_cipher))
                {
                    /* PSK */
                    psec_info->dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;
                    psec_info->wpa_psk |= ZT_BIT(1);
                    AP_DBG("RSN element");
                    break;
                }

            case ZT_80211_MGMT_EID_VENDOR_SPECIFIC :
            {
                zt_u32 wpa_multicast_cipher;
                zt_u32 wpa_unicast_cipher;
                if (!zt_80211_mgmt_wpa_parse(pie,
                                             ZT_OFFSETOF(zt_80211_mgmt_ie_t, data) + pie->len,
                                             &wpa_multicast_cipher,
                                             &wpa_unicast_cipher))
                {
                    psec_info->wpa_multicast_cipher = wpa_multicast_cipher;
                    psec_info->wpa_unicast_cipher = wpa_unicast_cipher;
                    psec_info->dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;
                    psec_info->wpa_psk |= ZT_BIT(0);
                    AP_DBG("WPA element");
                }
                else if (!zt_80211_mgmt_wmm_parse(pie,
                                                  ZT_OFFSETOF(zt_80211_mgmt_ie_t, data) + pie->len))
                {
                    zt_memcpy(&pcur_network->pwmm, pie, sizeof(pcur_network->pwmm));
                    /* adjust WMM information */
                    pwmm_ie = (void *)pie;
                    pwmm_ie->qos_info |= ZT_80211_MGMT_WMM_IE_AP_QOSINFO_UAPSD;
                    /* disable ACM for BE BK VI VO */
                    for (i = 0; i < ZT_ARRAY_SIZE(pwmm_ie->ac); i++)
                    {
                        pwmm_ie->ac[i].aci_aifsn &= ~ ZT_BIT(4);
                    }
                    AP_DBG("WMM element");
                }
                else if (zt_p2p_is_valid(pnic_info) &&
                         !zt_p2p_parse_p2pie(pnic_info, pie, ZT_OFFSETOF(zt_80211_mgmt_ie_t,
                                             data) + pie->len, ZT_P2P_IE_BEACON))
                {
                    AP_DBG("p2p element");
                }
            }
            break;

            case ZT_80211_MGMT_EID_HT_CAPABILITY :
                ((zt_80211_mgmt_ht_cap_t *)pie->data)->cap_info |= ZT_BIT(7);
                zt_memcpy(&pcur_network->pht_cap, pie->data, pie->len);
                pht_cap = &pcur_network->pht_cap;
                pcur_network->ht_enable = zt_true;
                AP_DBG("ht capatility");
                break;

            case ZT_80211_MGMT_EID_HT_OPERATION :
                zt_memcpy(&pcur_network->pht_oper, pie->data, pie->len);
                pht_oper = &pcur_network->pht_oper;
                AP_DBG("ht operation");
                break;
        }
    }

    /* checkout and adjust ht capability field */
    if (pht_cap)
    {
        LOG_D("cbw40_support = %d, pht_cap->cap_info = 0x%x", phw_info->cbw40_support, pht_cap->cap_info);
        /* cap_info */
        if ((pht_cap->cap_info & ZT_80211_MGMT_HT_CAP_SUP_WIDTH_20_40) &&
                phw_info->cbw40_support)
        {
            pcur_network->bw = CHANNEL_WIDTH_40;
        }
        else
        {
            pcur_network->bw = CHANNEL_WIDTH_20;
            pht_cap->cap_info &= ~ZT_80211_MGMT_HT_CAP_SGI_40;
        }

        if (pnic_info->buddy_nic)
        {
            if (bConnect == zt_true &&
                    zt_local_cfg_get_work_mode(pnic_info->buddy_nic) == ZT_INFRA_MODE)
            {
                pwdn = zt_wdn_find_info_by_id(pnic_info->buddy_nic, 0);
                if (pwdn)
                {
                    AP_DBG("sta bw is : %d", pwdn->bw_mode);
                    pcur_network->bw = pwdn->bw_mode;
                }
            }
        }

        if (!phw_info->ldpc_support)
        {
            pht_cap->cap_info &= ~ZT_80211_MGMT_HT_CAP_LDPC_CODING;
        }

        if (!phw_info->tx_stbc_support)
        {
            pht_cap->cap_info &= ~ZT_80211_MGMT_HT_CAP_TX_STBC;
        }

        if (!phw_info->rx_stbc_support)
        {
            pht_cap->cap_info &= ~ZT_80211_MGMT_HT_CAP_RX_STBC;
        }
        else if ((phw_info->rx_stbc_num & ZT_80211_MGMT_HT_CAP_RX_STBC) <
                 (pht_cap->cap_info & ZT_80211_MGMT_HT_CAP_RX_STBC))
        {
            pht_cap->cap_info &= ~ZT_80211_MGMT_HT_CAP_RX_STBC;
            pht_cap->cap_info |=
                (phw_info->rx_stbc_num << ZT_80211_MGMT_HT_CAP_RX_STBC_SHIFT) &
                ZT_80211_MGMT_HT_CAP_RX_STBC;
        }

        /* ampdu_params_info */
        if ((phw_info->max_rx_ampdu_factor & ZT_80211_MGMT_HT_AMPDU_PARM_FACTOR) <
                (pht_cap->ampdu_params_info & ZT_80211_MGMT_HT_AMPDU_PARM_FACTOR))
        {
            pht_cap->ampdu_params_info &= ~ZT_80211_MGMT_HT_AMPDU_PARM_FACTOR;
            pht_cap->ampdu_params_info |=
                (phw_info->max_rx_ampdu_factor & ZT_80211_MGMT_HT_AMPDU_PARM_FACTOR);
        }

        if ((psec_info->wpa_unicast_cipher & ZT_CIPHER_SUITE_CCMP) ||
                (psec_info->rsn_pairwise_cipher & ZT_CIPHER_SUITE_CCMP))
        {
            if ((phw_info->best_ampdu_density & ZT_80211_MGMT_HT_AMPDU_PARM_DENSITY) >
                    (pht_cap->ampdu_params_info & ZT_80211_MGMT_HT_AMPDU_PARM_DENSITY))
            {
                pht_cap->ampdu_params_info &= ~ZT_80211_MGMT_HT_AMPDU_PARM_DENSITY;
                pht_cap->ampdu_params_info |=
                    (phw_info->best_ampdu_density << ZT_80211_MGMT_HT_AMPDU_PARM_DENSITY_SHIFT) &
                    ZT_80211_MGMT_HT_AMPDU_PARM_DENSITY; /* 16usec */
            }
        }
        else
        {
            pht_cap->ampdu_params_info &= ~ZT_80211_MGMT_HT_AMPDU_PARM_DENSITY;
        }

        /* support mcs set */
        for (i = 0; i < ZT_ARRAY_SIZE(pht_cap->mcs_info.rx_mask); i++)
        {
            pht_cap->mcs_info.rx_mask[i] &=
                phw_info->default_supported_mcs_set[i];
        }
    }

    /* checkout and adjust ht option field */
    if (pcur_network->ht_enable == zt_true)
    {
        if (pcur_network->channel == 0x0)
        {
            pcur_network->channel = pcur_network->pht_oper.primary_chan;
        }
        else if (pcur_network->channel != pcur_network->pht_oper.primary_chan)
            AP_WARN("primary channel(%d) inconsistent with DSSS(%d)",
                    pcur_network->pht_oper.primary_chan, pcur_network->channel);

        if (!(pcur_network->pht_oper.ht_param &
                ZT_80211_MGMT_HT_OP_PARAM_CHAN_WIDTH_ANY))
        {
            pcur_network->bw = CHANNEL_WIDTH_20;
        }

        if (pcur_network->bw == CHANNEL_WIDTH_40)
            pcur_network->channle_offset =
                pcur_network->pht_oper.ht_param &
                ZT_80211_MGMT_HT_OP_PARAM_CHA_SEC_OFFSET;
    }

    if (pnic_info->buddy_nic)
    {
        if (bConnect == zt_true &&
                zt_local_cfg_get_work_mode(pnic_info->buddy_nic) == ZT_INFRA_MODE)
        {
            pwdn = zt_wdn_find_info_by_id(pnic_info->buddy_nic, 0);
            if (pwdn)
            {
                AP_DBG("sta channel_offset is : %d", pwdn->channle_offset);
                pcur_network->channle_offset = pwdn->channle_offset;
            }
        }
    }

    /* get network type */
    if (pcur_network->channel > 14)
    {
        pcur_network->cur_wireless_mode = WIRELESS_INVALID;
    }
    else
    {
        if (only_cckrates(pcur_network->rate, pcur_network->rate_len))
        {
            pcur_network->cur_wireless_mode = WIRELESS_11B;
        }
        else if (have_cckrates(pcur_network->rate, pcur_network->rate_len))
        {
            pcur_network->cur_wireless_mode = WIRELESS_11BG;
        }
        else
        {
            pcur_network->cur_wireless_mode = WIRELESS_11G;
        }

        if (pcur_network->ht_enable == zt_true)
        {
            pcur_network->cur_wireless_mode |= WIRELESS_11_24N;
        }
    }

    /* get short slot time */
    if (pcur_network->cap_info & ZT_80211_MGMT_CAPAB_IBSS)
    {
        pcur_network->short_slot = NON_SHORT_SLOT_TIME;
    }
    else if (pcur_network->cur_wireless_mode & WIRELESS_11_24N)
    {
        pcur_network->short_slot = SHORT_SLOT_TIME;
    }
    else if (pcur_network->cur_wireless_mode & WIRELESS_11G)
    {
        if (pcur_network->cap_info & ZT_80211_MGMT_CAPAB_SHORT_SLOT_TIME)
        {
            pcur_network->short_slot = SHORT_SLOT_TIME;
        }
        else
        {
            pcur_network->short_slot = NON_SHORT_SLOT_TIME;
        }
    }
    else
    {
        pcur_network->short_slot = NON_SHORT_SLOT_TIME;
    }

    /* update beacon related regiest */
    ap_update_reg(pnic_info);

    /*set bcnq valid */
    zt_mcu_set_bcn_valid(pnic_info);

    /* select bcq to store beacon*/
    zt_mcu_set_bcn_sel(pnic_info);

    /* send beacon */
    ap_launch_beacon(pnic_info);

    /* enable data queue */
    zt_os_api_enable_all_data_queue(pnic_info->ndev);

    /* indicate connect */
    zt_os_api_ind_connect(pnic_info, framework);

    return 0;
}

void zt_ap_resend_bcn(nic_info_st *pnic_info, zt_u8 channel)
{
    reset_beacon_channel(pnic_info, channel);
    /* send beacon */
    ap_launch_beacon(pnic_info);
}

zt_s32 zt_ap_resume_bcn(nic_info_st *pnic_info)
{
    if (!pnic_info)
    {
        return -1;
    }

    if (zt_local_cfg_get_work_mode(pnic_info) == ZT_MASTER_MODE)
    {
        zt_u32 bcn_valid;

        zt_mcu_set_bcn_valid(pnic_info);
        zt_mcu_set_bcn_sel(pnic_info);

        if (ap_launch_beacon(pnic_info) ||
                zt_mcu_get_bcn_valid(pnic_info, &bcn_valid) ||
                !bcn_valid)
        {
            return -2;
        }
        zt_mcu_set_bcn_queue(pnic_info, zt_true);
    }

    return 0;
}

static void bcn_wps_ie_update_func(nic_info_st *pnic_info, zt_u8 flag)
{
    zt_u8 *pwps_ie = NULL, *pwps_ie_src, *premainder_ie,
           *pbackup_remainder_ie = NULL;
    zt_u32 wps_ielen = 0, wps_offset, remainder_ielen;
    zt_wlan_mgmt_info_t *pwlan_info = pnic_info->wlan_mgmt_info;
    zt_wlan_network_t *pcur_network = &pwlan_info->cur_network;
    mlme_info_t *pmlme_info = (mlme_info_t *)pnic_info->mlme_info;
    zt_u8 *ie = pcur_network->ies ;
    zt_u32 ielen = pcur_network->ies_length;

    AP_DBG("%s\n", __FUNCTION__);

    pwps_ie = zt_wlan_get_wps_ie(ie + _FIXED_IE_LENGTH_, ielen - _FIXED_IE_LENGTH_,
                                 NULL, &wps_ielen);
    if (pwps_ie == NULL || wps_ielen == 0)
    {
        return;
    }

    pwps_ie_src = pmlme_info->wps_beacon_ie;
    if (pwps_ie_src == NULL)
    {
        return;
    }

    wps_offset = (zt_u32)(pwps_ie - ie);

    premainder_ie = pwps_ie + wps_ielen;

    remainder_ielen = ielen - wps_offset - wps_ielen;

    if (remainder_ielen > 0)
    {
        pbackup_remainder_ie = zt_kzalloc(remainder_ielen);
        if (pbackup_remainder_ie)
        {
            zt_memcpy(pbackup_remainder_ie, premainder_ie, remainder_ielen);
        }
    }

    wps_ielen = (zt_u32) pwps_ie_src[1];
    if ((wps_offset + wps_ielen + 2 + remainder_ielen) <= MAX_IE_SZ)
    {
        if (flag)
        {
            zt_memcpy(pwps_ie, pwps_ie_src, wps_ielen + 2);
            pwps_ie += (wps_ielen + 2);
        }
        if (pbackup_remainder_ie)
        {
            zt_memcpy(pwps_ie, pbackup_remainder_ie, remainder_ielen);
        }

        pcur_network->ies_length = wps_offset + (wps_ielen + 2) + remainder_ielen;
    }

    if (pbackup_remainder_ie)
    {
        zt_kfree(pbackup_remainder_ie);
    }

#if defined( CONFIG_INTERRUPT_BASED_TXBCN )
    if ((pmlme_info->state & 0x03) == WIFI_FW_AP_STATE)
    {
        zt_u8 sr = 0;
        zt_wlan_get_wps_attr_content(1, pwps_ie_src, wps_ielen,
                                     WPS_ATTR_SELECTED_REGISTRAR, (zt_u8 *)(&sr), NULL);

        if (sr)
        {
            pnic_info->nic_state |= WIFI_UNDER_WPS;
            AP_DBG("%s, set WIFI_UNDER_WPS\n", __func__);
        }
    }
#endif
}


static void bcn_vendor_spec_ie_update_func(nic_info_st *pnic_info, zt_u8 *oui,
        zt_u8 flag)
{
    AP_DBG("%s\n", __FUNCTION__);

    if (flag)
    {
        if (zt_memcmp(WPA_OUI, oui, 4))
        {
            //bcn_wpa_ie_update_func(pwadptdata, 1);
        }
        else if (zt_memcmp(WMM_OUI, oui, 4))
        {
            //bcn_wmm_ie_update_func(pwadptdata, 1);
        }
        else if (zt_memcmp(WPS_OUI, oui, 4))
        {
            bcn_wps_ie_update_func(pnic_info, 1);
        }
        else if (zt_memcmp(P2P_OUI, oui, 4))
        {
            //bcn_p2p_ie_update_func(pwadptdata, 1);
        }
        else
        {
            AP_WARN("unknown OUI type!\n");
        }
    }

}

zt_bool zt_bmp_is_set(zt_u8 bmp, zt_u8 id)
{
    if (id > MAX_AID)
    {
        return 0;
    }

    return (zt_bool)(bmp & ZT_BIT(id));
}

void zt_bmp_set(zt_u8 *bmp, zt_u8 id)
{
    if (id <= MAX_AID)
    {
        *bmp |= ZT_BIT(id);
    }
}

void zt_bmp_clear(zt_u8 *bmp, zt_u8 id)
{
    if (id <= MAX_AID)
    {
        *bmp &= ~ ZT_BIT(id);
    }
}

zt_u8 zt_set_tim_ie(zt_u8 dtim_cnt, zt_u8 dtim_period, const zt_u8 *tim_bmp,
                    zt_u8 *tim_ie)
{
    zt_u8 *p = tim_ie;

    *p++ = ZT_80211_MGMT_EID_TIM;
    *p++ = 2 + 1 + TIM_BITMAP_LEN;
    *p++ = dtim_cnt;
    *p++ = dtim_period;
    *p++ = (zt_bmp_is_set(*tim_bmp, 0) ?  ZT_BIT(0) : 0);
    zt_memcpy(p, tim_bmp, TIM_BITMAP_LEN);

    return 2 + 2 + 1 + TIM_BITMAP_LEN;
}

static void update_BCNTIM(nic_info_st *pnic_info)
{
    zt_wlan_mgmt_info_t *pwlan_info = pnic_info->wlan_mgmt_info;
    zt_wlan_network_t *pcur_network = &pwlan_info->cur_network;
    zt_u8 *pie =  pcur_network->ies;

    zt_u8 *p, *dst_ie, *premainder_ie = NULL, *pbackup_remainder_ie = NULL;
    zt_u32 offset, tmp_len, tim_ielen, tim_ie_offset, remainder_ielen;

    p = zt_wlan_get_ie(pie, ZT_80211_MGMT_EID_TIM, &tim_ielen,
                       pcur_network->ies_length);
    if (p != NULL && tim_ielen > 0)
    {
        tim_ielen += 2;

        premainder_ie = p + tim_ielen;

        tim_ie_offset = (zt_s32)(p - pie);

        remainder_ielen = pcur_network->ies_length - tim_ie_offset - tim_ielen;

        /*append TIM IE from dst_ie offset*/
        dst_ie = p;
    }
    else
    {
        tim_ielen = 0;
        offset = 0;

        /* get ssid_ie len */
        p = zt_wlan_get_ie(pie, ZT_80211_MGMT_EID_SSID, &tmp_len,
                           pcur_network->ies_length);
        if (p != NULL)
        {
            offset += tmp_len + 2;
        }

        /*get supported rates len*/
        p = zt_wlan_get_ie(pie, ZT_80211_MGMT_EID_SUPP_RATES, &tmp_len,
                           pcur_network->ies_length);
        if (p !=  NULL)
        {
            offset += tmp_len + 2;
        }

        /*DS Parameter Set IE, len=3*/
        offset += 3;

        premainder_ie = pie + offset;

        remainder_ielen = pcur_network->ies_length - offset - tim_ielen;

        /*append TIM IE from offset*/
        dst_ie = pie + offset;

    }

    if (remainder_ielen > 0)
    {
        pbackup_remainder_ie = zt_kzalloc(remainder_ielen);
        if (pbackup_remainder_ie && premainder_ie)
        {
            zt_memcpy(pbackup_remainder_ie, premainder_ie, remainder_ielen);
        }
    }

    /* append TIM IE */
    dst_ie += zt_set_tim_ie(0, 1,  &pcur_network->tim_bitmap, dst_ie);

    /*copy remainder IE*/
    if (pbackup_remainder_ie)
    {
        zt_memcpy(dst_ie, pbackup_remainder_ie, remainder_ielen);

        zt_kfree(pbackup_remainder_ie);
    }

    offset = (zt_u32)(dst_ie - pie);
    pcur_network->ies_length = offset + remainder_ielen;
}

zt_u32 zt_ap_update_beacon(nic_info_st *pnic_info,
                           zt_u8 ie_id, zt_u8 *oui, zt_u8 tx)
{
    if (!pnic_info)
    {
        return -1;
    }

    //if (_FALSE == pmlmeext->bstart_bss)
    //  return;

    //spin_lock_bh(&pmlmepriv->bcn_update_lock);

    switch (ie_id)
    {
        case 0xFF:
            //bcn_fixed_ie_update_func(pwadptdata, 1);
            break;

        case ZT_80211_MGMT_EID_TIM:
            update_BCNTIM(pnic_info);
            break;

        case  ZT_80211_MGMT_EID_ERP_INFO:

            //      bcn_erpinfo_ie_update_func(pwadptdata, 1);

            break;

        case ZT_80211_MGMT_EID_HT_CAPABILITY:

            //      bcn_htcap_ie_update_func(pwadptdata, 1);

            break;

        case ZT_80211_MGMT_EID_RSN:

            //      bcn_rsn_ie_update_func(pwadptdata, 1);

            break;

        case ZT_80211_MGMT_EID_HT_OPERATION:

            //      bcn_htinfo_ie_update_func(pwadptdata, 1);

            break;

        case ZT_80211_MGMT_EID_EXT_CAPABILITY:

            //      bcn_ext_capab_ie_update_func(pwadptdata, 1);

            break;

        case ZT_80211_MGMT_EID_VENDOR_SPECIFIC:

            bcn_vendor_spec_ie_update_func(pnic_info, oui, 1);

            break;

        default:
            break;
    }

    //  pmlmepriv->update_bcn = _TRUE;

    //spin_unlock_bh(&pmlmepriv->bcn_update_lock);

#ifndef CONFIG_INTERRUPT_BASED_TXBCN

    if (tx)
    {
        AP_DBG(" ie_id:%u - send beacon \n", ie_id);
        return ap_launch_beacon(pnic_info);
    }
#endif

    return 0;
}

zt_s32 zt_ap_add_ba_req(nic_info_st *pnic_info, void *pwdn_info)
{
    if (pnic_info == NULL)
    {
        return -1;
    }

    if (!pnic_info->is_up)
    {
        AP_WARN("ndev down");
        return -2;
    }

    AP_DBG();

    if (zt_ap_msg_load(pnic_info, &((wdn_net_info_st *)pwdn_info)->ap_msg,
                       ZT_AP_MSG_TAG_BA_REQ_FRAME, NULL, 0))
    {
        return -3;
    }

    return 0;
}

zt_s32 zt_ap_new_sta(nic_info_st *pnic_info, zt_u8 *mac, void **rwdn_info)
{
    zt_wlan_mgmt_info_t *pwlan_info = pnic_info->wlan_mgmt_info;
    zt_wlan_network_t *pcur_network = &pwlan_info->cur_network;

    if (!zt_80211_is_valid_unicast(mac))
    {
        return -1;
    }
    AP_DBG("create new wdn: "ZT_MAC_FMT, ZT_MAC_ARG(mac));

    zt_os_api_lock_lock(&pcur_network->wdn_new_lock);
    if (pcur_network->freeze_pend)
    {
        zt_os_api_lock_unlock(&pcur_network->wdn_new_lock);
        return -2;
    }
    {
        wdn_net_info_st *pwdn_info;
        pwdn_info = zt_wdn_add(pnic_info, mac);
        if (pwdn_info == NULL)
        {
            zt_os_api_lock_unlock(&pcur_network->wdn_new_lock);
            AP_WARN("wdn alloc fail");
            return -3;
        }
        zt_wdn_info_ap_update(pnic_info, pwdn_info);
        *rwdn_info = pwdn_info;
        pcur_network->sta_cnt++;
    }
    zt_os_api_lock_unlock(&pcur_network->wdn_new_lock);

    return 0;
}

zt_s32 zt_ap_deauth_all_sta(nic_info_st *pnic_info, zt_u16 reason)
{
    zt_wlan_mgmt_info_t *pwlan_info = pnic_info->wlan_mgmt_info;
    zt_wlan_network_t *pcur_network = &pwlan_info->cur_network;
    wdn_list *pwdn = (wdn_list *)pnic_info->wdn;
    zt_list_t *pos, *pos_next;
    wdn_node_st *pwdn_node;
    wdn_net_info_st *pwdn_info;
    zt_s32 ret = 0;

    AP_DBG();

    zt_os_api_lock_lock(&pcur_network->wdn_new_lock);
    zt_list_for_each_safe(pos, pos_next, &pwdn->head)
    {
        pwdn_node = zt_list_entry(pos, wdn_node_st, list);
        pwdn_info = &pwdn_node->info;
        pwdn_info->reason_code = reason;
        if (pwdn_info->mode != ZT_MASTER_MODE ||
                !zt_80211_is_valid_unicast(pwdn_info->mac))
        {
            continue;
        }
        if (zt_ap_msg_load(pnic_info, &pwdn_info->ap_msg,
                           ZT_AP_MSG_TAG_DEAUTH_FRAME, NULL, 0))
        {
            AP_WARN("deauth msg send fail");
            ret--;
        }
    }
    zt_os_api_lock_unlock(&pcur_network->wdn_new_lock);

    return ret;
}

zt_s32 zt_ap_work_start(nic_info_st *pnic_info)
{
    zt_wlan_mgmt_info_t *pwlan_info = pnic_info->wlan_mgmt_info;
    zt_wlan_network_t *pcur_network = &pwlan_info->cur_network;

    AP_DBG();

    if (zt_local_cfg_get_work_mode(pnic_info) != ZT_MASTER_MODE)
    {
        return 0;
    }

    if (pnic_info->is_driver_critical)
    {
        return -2;
    }

    {
        sec_info_st *psec_info = pnic_info->sec_info;
        wdn_net_info_st *pwdn_info = NULL;
        zt_u8 bc_addr[ZT_80211_MAC_ADDR_LEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
        pwdn_info = zt_wdn_find_info(pnic_info, bc_addr);
        if (pwdn_info == NULL)
        {
            AP_DBG("sta has already been removed or never been added");
            /* new boradcast wdn for ap send beacon. */
            if (new_boradcast_wdn(pnic_info))
            {
                AP_WARN("create boardcast wdn_info fail");
                return -3;
            }
        }
        else
        {
            ap_update_reg(pnic_info);
            zt_os_api_enable_all_data_queue(pnic_info->ndev);
            zt_sec_ap_set_group_key(pnic_info, &pwdn_info->group_cam_id, bc_addr);

            pwdn_info->dot118021XPrivacy = psec_info->dot118021XGrpPrivacy;
            pwdn_info->ieee8021x_blocked = zt_false;
            pwdn_info->state = E_WDN_AP_STATE_8021X_UNBLOCK;
        }
    }

    zt_mcu_set_user_info(pnic_info, zt_true);

    zt_mcu_set_bcn_queue(pnic_info, zt_true);
    ap_status_set(pnic_info, ZT_AP_STATE_STANBY);
    pcur_network->freeze_pend = zt_false;

    return 0;
}

zt_s32 zt_ap_work_stop(nic_info_st *pnic_info)
{
    zt_wlan_mgmt_info_t *pwlan_info = pnic_info->wlan_mgmt_info;
    zt_wlan_network_t *pcur_network = &pwlan_info->cur_network;
    zt_u8 bc_addr[ZT_80211_MAC_ADDR_LEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

    AP_DBG();

    if (zt_local_cfg_get_work_mode(pnic_info) != ZT_MASTER_MODE)
    {
        return 0;
    }
    pcur_network->freeze_pend = zt_true;

    if (pcur_network->ap_state & ZT_AP_STATE_BIT_RUN)
    {
        /* deauth all station */
        if (zt_ap_deauth_all_sta(pnic_info, ZT_80211_REASON_DEAUTH_LEAVING))
        {
            return -2;
        }

        AP_DBG("wait until ap handle stop");
        {
            zt_timer_t to;
            zt_timer_set(&to, 2000);
            while (pcur_network->sta_cnt)
            {
                zt_msleep(10);
                if (zt_timer_expired(&to))
                {
                    AP_WARN("ap stop timeout!");
                    return -3;
                }
            }
        }
        AP_DBG("ap handle done");
    }
    /* notify framework ap stop */
    zt_os_api_disable_all_data_queue(pnic_info->ndev);
    /* remove boradcast wdn */
    if (!pnic_info->is_driver_critical)
        zt_wdn_remove(pnic_info, bc_addr);
    zt_mcu_set_bcn_queue(pnic_info, zt_false);

    return 0;
}

zt_inline zt_s32 zt_ap_suspend(nic_info_st *pnic_info)
{
    AP_DBG();

    return zt_ap_work_stop(pnic_info);
}

zt_inline zt_s32 zt_ap_resume(nic_info_st *pnic_info)
{
    AP_DBG();

    return zt_ap_work_start(pnic_info);
}

zt_s32 zt_ap_init(nic_info_st *pnic_info)
{
    zt_wlan_mgmt_info_t *pwlan_info = pnic_info->wlan_mgmt_info;
    zt_wlan_network_t *pcur_network = &pwlan_info->cur_network;
    sec_info_st *psec_info = pnic_info->sec_info;

    AP_DBG();

    psec_info->busetkipkey = zt_false;

    /* initilize ap message queue */
    if (ap_msg_que_init(pnic_info))
    {
        AP_WARN("AP message initilize failed");
        return -1;
    }

    /* create thread for AP process */
    zt_os_api_lock_init(&pcur_network->wdn_new_lock, ZT_LOCK_TYPE_BH);
    zt_os_api_lock_init(&pcur_network->wdn_del_lock, ZT_LOCK_TYPE_BH);
    pcur_network->sta_cnt = 0;
    pcur_network->freeze_pend = zt_true;
    zt_sprintf(pcur_network->ap_name, "ap_%d%d",
               pnic_info->hif_node_id, pnic_info->ndev_id);
    pcur_network->ap_tid = zt_os_api_thread_create(pcur_network->ap_tid,
                           pcur_network->ap_name,
                           (void *)ap_poll, pnic_info);
    if (!pcur_network->ap_tid)
    {
        AP_WARN("tid error");
        return -2;
    }
    zt_os_api_thread_wakeup(pcur_network->ap_tid);
    /* update AP status */
    ap_status_set(pnic_info, ZT_AP_STATE_INIT);

    return 0;
}

zt_s32 zt_ap_term(nic_info_st *pnic_info)
{
    zt_wlan_mgmt_info_t *pwlan_info = pnic_info->wlan_mgmt_info;
    zt_wlan_network_t *pcur_network;

    AP_DBG();

    if (!pwlan_info)
    {
        return -1;
    }

    /* stop ap thread first */
    zt_ap_work_stop(pnic_info);

    /* destory thread */
    pcur_network = &pwlan_info->cur_network;
    if (pcur_network->ap_tid)
    {
        zt_os_api_thread_destory(pcur_network->ap_tid);
        pcur_network->ap_tid = NULL;
    }

    ap_msg_deinit(pnic_info);
    zt_memset(pnic_info->sec_info, 0, sizeof(sec_info_st));
    zt_os_api_disable_all_data_queue(pnic_info->ndev);
    zt_os_api_lock_term(&pcur_network->wdn_new_lock);
    zt_os_api_lock_term(&pcur_network->wdn_del_lock);

    return 0;
}

zt_s32 zt_ap_get_num(nic_info_st *pnic_info)
{
    nic_info_st *buddy_nic = NULL;
    sys_work_mode_e work_mode;
    zt_s32 ap_num = 0;
    zt_bool bconnect = zt_false;

    if (NULL == pnic_info)
    {
        return 0;
    }

    work_mode = zt_local_cfg_get_work_mode(pnic_info);
    if (ZT_MASTER_MODE == work_mode)
    {
        zt_mlme_get_connect(pnic_info, &bconnect);
        if (zt_true == bconnect)
        {
            ap_num++;
        }
    }

    buddy_nic = pnic_info->buddy_nic;

    if (NULL == buddy_nic)
    {
        return ap_num;
    }

    work_mode = zt_local_cfg_get_work_mode(buddy_nic);
    if (ZT_MASTER_MODE == work_mode)
    {
        zt_mlme_get_connect(buddy_nic, &bconnect);
        if (zt_true == bconnect)
        {
            ap_num++;
        }
    }

    return ap_num;
}
#endif

