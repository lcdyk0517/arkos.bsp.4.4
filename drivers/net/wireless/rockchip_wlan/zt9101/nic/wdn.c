/*
 * wdn.c
 *
 * impliment WDN(wireless device node) management
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

#define WDN_INFO_DUMP

#define WDN_DBG(fmt, ...)       LOG_D("[%s:%d]"fmt, __func__, __LINE__, ##__VA_ARGS__)
#define WDN_ARRAY(data, len)    zt_log_array(data, len)
#define WDN_INFO(fmt, ...)      LOG_I("[%s:%d]"fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define WDN_WARN(fmt, ...)      LOG_W("[%s:%d]"fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define WDN_ERROR(fmt, ...)     LOG_E("[%s:%d]"fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__)

zt_u8 WPA_OUI[4] = { 0x00, 0x50, 0xf2, 0x01 };
zt_u8 WMM_OUI[4] = { 0x00, 0x50, 0xf2, 0x02 };
zt_u8 WPS_OUI[4] = { 0x00, 0x50, 0xf2, 0x04 };
zt_u8 P2P_OUI[4] = { 0x50, 0x6F, 0x9A, 0x09 };
zt_u8 WFD_OUI[4] = { 0x50, 0x6F, 0x9A, 0x0A };


static zt_s32 new_wdn_id(nic_info_st *pnic_info, zt_u8 *pwdn_id)
{
    zt_u8 i;
    zt_s32 bit_mask;

    for (i = 0; i < 32; i++)
    {
        bit_mask = ZT_BIT(i);
        if (!(*pnic_info->wdn_id_bitmap & bit_mask))
        {
            *pnic_info->wdn_id_bitmap |= bit_mask;
            *pwdn_id = i;
            return 0;
        }
    }

    return -1;
}

static zt_s32 free_wdn_id(nic_info_st *pnic_info, zt_u16 id)
{
    if (id >= 32)
    {
        return -1;
    }

    *pnic_info->wdn_id_bitmap &= ~ ZT_BIT(id);
    return 0;
}

void get_bratecfg_by_support_dates(zt_u8 *pdataRate, zt_u8 dataRate_len,
                                   zt_u16 *pBrateCfg)
{
    zt_u8 i, is_brate, brate;

    for (i = 0; i < dataRate_len; i++)
    {
        is_brate = pdataRate[i] & IEEE80211_BASIC_RATE_MASK;
        brate = pdataRate[i] & 0x7f;

        if (is_brate)
        {
            switch (brate)
            {
                case ZT_80211_CCK_RATE_1MB:
                    *pBrateCfg |= ZT_80211_CCK_RATE_1MB_MASK;
                    break;
                case ZT_80211_CCK_RATE_2MB:
                    *pBrateCfg |= ZT_80211_CCK_RATE_2MB_MASK;
                    break;
                case ZT_80211_CCK_RATE_5MB:
                    *pBrateCfg |= ZT_80211_CCK_RATE_5MB_MASK;
                    break;
                case ZT_80211_CCK_RATE_11MB:
                    *pBrateCfg |= ZT_80211_CCK_RATE_11MB_MASK;
                    break;
                case ZT_80211_OFDM_RATE_6MB:
                    *pBrateCfg |= ZT_80211_OFDM_RATE_6MB_MASK;
                    break;
                case ZT_80211_OFDM_RATE_9MB:
                    *pBrateCfg |= ZT_80211_OFDM_RATE_9MB_MASK;
                    break;
                case ZT_80211_OFDM_RATE_12MB:
                    *pBrateCfg |= ZT_80211_OFDM_RATE_12MB_MASK;
                    break;
                case ZT_80211_OFDM_RATE_18MB:
                    *pBrateCfg |= ZT_80211_OFDM_RATE_18MB;
                    break;
                case ZT_80211_OFDM_RATE_24MB:
                    *pBrateCfg |= ZT_80211_OFDM_RATE_24MB;
                    break;
                case ZT_80211_OFDM_RATE_36MB:
                    *pBrateCfg |= ZT_80211_OFDM_RATE_36MB;
                    break;
                case ZT_80211_OFDM_RATE_48MB:
                    *pBrateCfg |= ZT_80211_OFDM_RATE_48MB;
                    break;
                case ZT_80211_OFDM_RATE_54MB:
                    *pBrateCfg |= ZT_80211_OFDM_RATE_54MB;
                    break;
            }
        }
    }
}

zt_u8 zt_wdn_get_raid_by_network_type(wdn_net_info_st *pwdn_info)
{
    zt_u8 raid = RATEID_IDX_B;

    switch (pwdn_info->network_type)
    {
        case WIRELESS_11B:
            raid = RATEID_IDX_B;
            pwdn_info->tx_rate = MGN_1M;
            break;

        case WIRELESS_11G:
            raid = RATEID_IDX_G;
            pwdn_info->tx_rate = MGN_1M;
            break;

        case WIRELESS_11BG:
            raid = RATEID_IDX_BG;
            pwdn_info->tx_rate = MGN_1M;
            break;

        case WIRELESS_11_24N:
        case WIRELESS_11G_24N:
        case WIRELESS_11B_24N:
            raid = RATEID_IDX_GN;
            pwdn_info->tx_rate = MGN_MCS0;
            break;

        case WIRELESS_11BG_24N:
            if (pwdn_info->bw_mode == CHANNEL_WIDTH_20)
            {
                raid = RATEID_IDX_BGN_20M;
            }
            else
            {
                raid = RATEID_IDX_BGN_40M;
            }
            pwdn_info->tx_rate = MGN_1M;
            break;

        default:
            WDN_WARN("error network type(0x%x)\n", pwdn_info->network_type);
            break;

    }

    return raid;
}

wdn_net_info_st *zt_wdn_find_info(nic_info_st *pnic_info, zt_u8 *pmac)
{
    zt_list_t *pos, *pos_next;
    wdn_node_st *pwdn_node;
    wdn_list *pwdn = pnic_info->wdn;

    if (NULL == pmac)
    {
        return NULL;
    }

    zt_list_for_each_safe(pos, pos_next, &pwdn->head)
    {
        pwdn_node = zt_list_entry(pos, wdn_node_st, list);
        if (pwdn_node && !zt_memcmp(pwdn_node->info.mac, pmac, ZT_80211_MAC_ADDR_LEN))
        {
            return &pwdn_node->info;
        }
    }

    return NULL;
}

wdn_net_info_st *zt_wdn_find_info_by_id(nic_info_st *nic_info, zt_u8 wdn_id)
{
    zt_list_t *pos = NULL;
    zt_list_t *next = NULL;
    wdn_node_st *tmp_node = NULL;
    wdn_list *wdn = (wdn_list *)nic_info->wdn;
    wdn_net_info_st *tmp_node_info = NULL;

    zt_list_for_each_safe(pos, next, &wdn->head)
    {
        tmp_node = zt_list_entry(pos, wdn_node_st, list);
        if (tmp_node && (tmp_node->info.wdn_id == wdn_id))
        {
            tmp_node_info = &tmp_node->info;
            break;

        }
        tmp_node = NULL;
    }

    return tmp_node_info;
}

wdn_net_info_st *zt_wdn_add(nic_info_st *pnic_info, zt_u8 *pmac)
{
    zt_u8 tid;
    zt_u16 i;
    wdn_list *pwdn = pnic_info->wdn;
    wdn_node_st *pwdn_node;
    wdn_net_info_st *pwdn_info;
    recv_ba_ctrl_st *ba_ctl;
    rx_reorder_queue_st *order_node;
    zt_u8 wdn_info_id;

    /* return the wdn if already existed */
    pwdn_info = zt_wdn_find_info(pnic_info, pmac);
    if (pwdn_info != NULL)
    {
        return pwdn_info;
    }

    if (zt_list_is_empty(&pwdn->free))
    {
        WDN_WARN("no more wdn resource");
        return NULL;
    }

    if (new_wdn_id(pnic_info, &wdn_info_id))
    {
        WDN_WARN("alloc wdn id fail");
        return NULL;
    }

    WDN_INFO("[%d] wdn_id:%d", pnic_info->ndev_id, wdn_info_id);
    /* node remove from free list */
    pwdn_node = zt_list_entry(pwdn->free.pnext, wdn_node_st, list);
    zt_list_delete(&pwdn_node->list);

    /* update wdn_info */
    pwdn_info = &pwdn_node->info;
    zt_memset(pwdn_info, 0, sizeof(wdn_net_info_st));
    zt_memcpy(pwdn_info->mac, pmac, ZT_80211_MAC_ADDR_LEN);
    pwdn_info->unicast_cam_id = -1;
    pwdn_info->group_cam_id = -1;
    pwdn_info->wdn_id = wdn_info_id;

    for (tid  = 0; tid  < TID_NUM; tid++)
    {
        ba_ctl                  = &pwdn_info->ba_ctl[tid];
        ba_ctl->enable          = zt_false;
        ba_ctl->indicate_seq    = 0xffff;
        ba_ctl->wend_b          = 0xffff;
        ba_ctl->wsize_b         = 64;/* max_ampdu_sz; */ /* ex. 32(kbytes) -> wsize_b = 32 */
        ba_ctl->ampdu_size      = 0xff;
        ba_ctl->nic_node        = pnic_info;
        ba_ctl->timer_start     = zt_false;
        ba_ctl->drop_pkts       = 0;
        ba_ctl->upload_func     = NULL;
        zt_que_init(&ba_ctl->pending_reorder_queue, ZT_LOCK_TYPE_NONE);
        zt_que_init(&ba_ctl->free_order_queue, ZT_LOCK_TYPE_NONE);
        zt_os_api_lock_init(&ba_ctl->pending_get_de_queue_lock, ZT_LOCK_TYPE_BH);

        //zt_os_api_timer_reg(&ba_ctl->reordering_ctrl_timer, (void *)rx_reorder_timeout_handle, ba_ctl);
        zt_os_api_timer_reg(&ba_ctl->reordering_ctrl_timer, rx_reorder_timeout_handle,
                            &ba_ctl->reordering_ctrl_timer);

        for (i = 0; i < BA_REORDER_QUEUE_NUM; i++)
        {
            order_node = zt_kzalloc(sizeof(rx_reorder_queue_st));
            if (NULL == order_node)
            {
                LOG_E("[%s] zt_kzalloc failed", __func__);
                break;
            }
            order_node->pskb = NULL;
            zt_enque_tail(&order_node->list, &ba_ctl->free_order_queue);

        }

    }

    /* link the node to head list */
    zt_list_insert_tail(&pwdn_node->list, &pwdn->head);

    /* update wdn count */
    pwdn->cnt++;

    return pwdn_info;
}

zt_s32 zt_wdn_remove(nic_info_st *pnic_info, zt_u8 *pmac)
{
    zt_u8 tid;
    wdn_list *pwdn = pnic_info->wdn;
    wdn_node_st *pwdn_node;
    wdn_net_info_st *pwdn_info;
    recv_ba_ctrl_st *ba_ctl;
    rx_reorder_queue_st *order_node;

    pwdn_info = zt_wdn_find_info(pnic_info, pmac);
    if (!pwdn_info) {
        WDN_WARN("wdn no find");
        return -1;
    }

    WDN_INFO("[%d] wdn_id:%d", pnic_info->ndev_id, pwdn_info->wdn_id);

    /* free key */
    switch (zt_local_cfg_get_work_mode(pnic_info))
    {
        case ZT_INFRA_MODE:
             zt_sec_free_key(pnic_info, pwdn_info->unicast_cam_id, pwdn_info->group_cam_id);
            break;
#ifdef CFG_ENABLE_AP_MODE
        case ZT_MASTER_MODE:
            if (zt_80211_is_bcast_addr(pmac)) {
                zt_sec_free_key(pnic_info, pwdn_info->unicast_cam_id, pwdn_info->group_cam_id);
            } else {
                LOG_E("[%s]: remove a key but group key", __func__);
                zt_sec_free_key(pnic_info, pwdn_info->unicast_cam_id, -1);
            }
            break;
#endif
        default:
            zt_sec_free_key(pnic_info, pwdn_info->unicast_cam_id, pwdn_info->group_cam_id);
            break;
    }

    for (tid  = 0; tid  < TID_NUM; tid++) {
        ba_ctl = &pwdn_info->ba_ctl[tid];
        if (NULL == ba_ctl) {
            continue;
        }
        zt_os_api_lock_lock(&ba_ctl->pending_get_de_queue_lock);

        LOG_I("[wdn_id=%d]:start free pending_reorder_queue", pwdn_info->wdn_id);
        while (1) {
            order_node = rx_pending_reorder_dequeue(ba_ctl);
            if (NULL == order_node)
            {
                break;
            }
            if (NULL != order_node->pskb)
            {
                ba_ctl->free_skb(pnic_info, order_node->pskb);
                order_node->pskb = NULL;
            }
            zt_kfree(order_node);
        }


        LOG_I("[%s, %d]", __func__, __LINE__);

        while (1) {
            order_node = rx_free_reorder_dequeue(ba_ctl);
            if (NULL == order_node)
                break;

            zt_kfree(order_node);
        }

        ba_ctl->enable          = zt_false;
        ba_ctl->indicate_seq    = 0xffff;
        ba_ctl->wend_b          = 0xffff;
        ba_ctl->wsize_b         = 64;/* max_ampdu_sz; */ /* ex. 32(kbytes) -> wsize_b = 32 */
        ba_ctl->ampdu_size      = 0xff;
        ba_ctl->nic_node        = pnic_info;
        ba_ctl->timer_start     = zt_false;
        ba_ctl->drop_pkts       = 0;
        zt_os_api_timer_unreg(&ba_ctl->reordering_ctrl_timer);
        zt_os_api_lock_unlock(&ba_ctl->pending_get_de_queue_lock);
    }

    /* node remove from head list */
    pwdn_node = zt_list_entry(pwdn_info, wdn_node_st, info);
    zt_list_delete(&pwdn_node->list);
    /* link the node to free list */
    zt_list_insert_tail(&pwdn_node->list, &pwdn->free);

    /* update wdn */
    free_wdn_id(pnic_info, pwdn_info->wdn_id);
#ifdef CFG_ENABLE_AP_MODE
    pwdn_info->state = E_WDN_AP_STATE_IDLE;
#endif
    pwdn->cnt--;

    return 0;
}

zt_s32 zt_wdn_init(nic_info_st *pnic_info)
{
    wdn_list *pwdn;
    zt_u8 i;
    wdn_node_st *pwdn_node;

    pwdn = zt_kzalloc(sizeof(wdn_list));
    if (NULL == pwdn)
    {
        WDN_WARN("malloc pwd failed");
        return -1;
    }

    zt_list_init(&pwdn->head);
    zt_list_init(&pwdn->free);
    pwdn->cnt = 0;
    pnic_info->wdn = pwdn;

    /* add list node */
    for (i = 0; i < WDN_NUM_MAX; i++)
    {
        pwdn_node = zt_kzalloc(sizeof(wdn_node_st));
        if (NULL == pwdn_node)
        {
            WDN_WARN("zt_kzalloc pwdn_node failed, check!!!");
            return -2;
        }

        zt_list_insert_tail(&pwdn_node->list, &pwdn->free);
    }

    return 0;
}

zt_s32 zt_wdn_term(nic_info_st *pnic_info)
{
    wdn_list *pwdn = pnic_info->wdn;
    zt_list_t *pos, *pos_next;
    wdn_node_st *pwdn_node;

    LOG_I("[%s] start", __func__);
    if (pwdn == NULL)
    {
        return -1;
    }

    zt_list_for_each_safe(pos, pos_next, &pwdn->head)
    {
        pwdn_node = zt_list_entry(pos, wdn_node_st, list);
        zt_list_delete(&pwdn_node->list);
        zt_kfree(pwdn_node);
    }

    zt_list_for_each_safe(pos, pos_next, &pwdn->free)
    {
        pwdn_node = zt_list_entry(pos, wdn_node_st, list);
        zt_list_delete(&pwdn_node->list);
        zt_kfree(pwdn_node);
    }

    zt_kfree(pwdn);

    return 0;
}

#ifdef CFG_ENABLE_AP_MODE
void zt_wdn_info_ap_update(nic_info_st *nic_info, wdn_net_info_st *pwdn_info)
{
    /* ap message queue initilize */
    zt_que_init(&pwdn_info->ap_msg, ZT_LOCK_TYPE_BH);
    /* ap trhead initilize */
    PT_INIT(&pwdn_info->ap_thrd_pt);
    /* generate wdn aid */
    pwdn_info->aid = pwdn_info->wdn_id + 1;
    /* retreive bssid into wdn_info */
    zt_memcpy(pwdn_info->bssid, nic_to_local_addr(nic_info),
              ZT_ARRAY_SIZE(pwdn_info->bssid));
    /* reset connection rx packets statistics */
    pwdn_info->rx_pkt_stat = 0;
    /* set mode */
    pwdn_info->mode = ZT_MASTER_MODE;
    pwdn_info->ieee8021x_blocked = zt_true;
    pwdn_info->state = E_WDN_AP_STATE_READY;

    pwdn_info->psm = zt_false;
    zt_que_init(&pwdn_info->psm_data_que, ZT_LOCK_TYPE_NONE);
    zt_os_api_lock_init(&pwdn_info->psm_lock, ZT_LOCK_TYPE_BH);
}
#endif

zt_s32 zt_wdn_info_sta_update(nic_info_st *nic_info, wdn_net_info_st *wdn_info)
{
    zt_u8 i;
    zt_u8 *pele_start;
    zt_80211_mgmt_ie_t *pie;
    zt_wlan_mgmt_info_t *wlan_mgmt_info = nic_info->wlan_mgmt_info;
    zt_wlan_network_t *cur_network = &wlan_mgmt_info->cur_network;
    hw_info_st *hw_info = nic_info->hw_info;

    /* init defrag que */
    queue_initialize(&wdn_info->defrag_q);

    /* set mode */
    wdn_info->mode = zt_local_cfg_get_work_mode(nic_info);

    /* set ie info */
    pele_start = &cur_network->ies[0];

    /* set bssid */
    zt_memcpy(wdn_info->bssid, cur_network->bssid, ZT_80211_MAC_ADDR_LEN);

    /* set ssid */
    wdn_info->ssid_len = (zt_u8)cur_network->ssid.length;

    /* set channel */
    wdn_info->channel = cur_network->channel;
    wdn_info->bw_mode = cur_network->bw;

    /* set bcn interval */
    wdn_info->bcn_interval = cur_network->bcn_interval;
    {
        zt_u64 tmp = cur_network->timestamp;
        wdn_info->tsf = cur_network->timestamp - zt_os_api_do_div(tmp,
                        cur_network->bcn_interval * 1024) - 1024;
    }

    /* set listen interval */
    wdn_info->listen_interval = 3;

    /* set capability info */
    zt_ie_cap_info_update(nic_info, wdn_info, cur_network->cap_info);

    if (zt_80211_mgmt_ies_search(pele_start, (zt_u16)cur_network->ies_length,
                                 ZT_80211_MGMT_EID_SSID, &pie) == ZT_RETURN_OK)
    {
        zt_ie_ssid_update(nic_info, wdn_info, pie->data, pie->len);
    }
    else
    {
        return ZT_RETURN_FAIL;
    }

    if (zt_80211_mgmt_ies_search(pele_start, (zt_u16)cur_network->ies_length,
                                 ZT_80211_MGMT_EID_SUPP_RATES, &pie) == ZT_RETURN_OK)
    {
        zt_ie_supported_rates_update(nic_info, wdn_info, pie->data, pie->len);
    }
    else
    {
        return ZT_RETURN_FAIL;
    }

    if (zt_80211_mgmt_ies_search(pele_start, (zt_u16)cur_network->ies_length,
                                 ZT_80211_MGMT_EID_EXT_SUPP_RATES, &pie) == ZT_RETURN_OK)
    {
        zt_ie_extend_supported_rates_update(nic_info, wdn_info, pie->data, pie->len);
    }

    if ((zt_80211_mgmt_ies_search(pele_start, (zt_u16)cur_network->ies_length,
                                  ZT_80211_MGMT_EID_HT_OPERATION, &pie) == ZT_RETURN_OK)
            && hw_info->dot80211n_support)
    {
        WDN_DBG("HT Operation Info Parse");
        zt_ie_ht_operation_info_update(nic_info, wdn_info, pie->data, pie->len);
    }

    if ((zt_80211_mgmt_ies_search(pele_start, (zt_u16)cur_network->ies_length,
                                  ZT_80211_MGMT_EID_HT_CAPABILITY, &pie) == ZT_RETURN_OK)
            && hw_info->dot80211n_support)
    {
        WDN_DBG("HT Capability Info Parse");
        zt_ie_ht_capability_update(nic_info, wdn_info, pie->data, pie->len);
    }

    if (zt_80211_mgmt_ies_search(pele_start, (zt_u16)cur_network->ies_length,
                                 ZT_80211_MGMT_EID_ERP_INFO, &pie) == ZT_RETURN_OK)
    {
        WDN_DBG("ERP Info Parse");
        zt_ie_erp_update(nic_info, wdn_info, pie->data, pie->len);
    }

    if (zt_80211_mgmt_ies_search_with_oui(pele_start,
                                          (zt_u16)cur_network->ies_length, ZT_80211_MGMT_EID_VENDOR_SPECIFIC, WMM_OUI,
                                          &pie) == ZT_RETURN_OK)
    {
        WDN_DBG("WMM in IE [oui:%x-%x-%x-%x  len:%d]", pie->data[0], pie->data[1],
                pie->data[2], pie->data[3], pie->len);
        zt_ie_wmm_update(nic_info, wdn_info, pie->data, pie->len);
    }

    if (zt_80211_mgmt_ies_search_with_oui(pele_start,
                                          (zt_u16)cur_network->ies_length, ZT_80211_MGMT_EID_VENDOR_SPECIFIC, WPA_OUI,
                                          &pie) == ZT_RETURN_OK)
    {
        WDN_DBG("WPA in IE [oui:%x-%x-%x-%x  len:%d]", pie->data[0], pie->data[1],
                pie->data[2], pie->data[3], pie->len);
        zt_ie_wpa_update(nic_info, wdn_info, pie->data, pie->len);
    }

    if (zt_80211_mgmt_ies_search(pele_start, (zt_u16)cur_network->ies_length,
                                 ZT_80211_MGMT_EID_RSN, &pie) == ZT_RETURN_OK)
    {
        zt_ie_rsn_update(nic_info, wdn_info, pie->data, pie->len);
    }

    wdn_info->raid = zt_wdn_get_raid_by_network_type(wdn_info);

    wdn_info->ieee8021x_blocked = zt_true;

    /* add debug info */
#ifdef WDN_INFO_DUMP
#define _DUMP   WDN_INFO
    _DUMP("== WDN INFO DUMP ==");

    _DUMP("ID: %d", wdn_info->wdn_id);
    _DUMP("SSID: %s", wdn_info->ssid);
    _DUMP("short_slot: %d", wdn_info->short_slot);
    _DUMP("short_preamble: %d", wdn_info->short_preamble);
    _DUMP("bw_mode: %d", wdn_info->bw_mode);
    _DUMP("tsf: %lld", wdn_info->tsf);

    _DUMP("Supported Rates:");
    for (i = 0; i < wdn_info->datarate_len; i++)
    {
        _DUMP("rate_%d: 0x%x", i, wdn_info->datarate[i]);
    }
    _DUMP("Extend Supported Rates:");
    for (i = 0; i < wdn_info->ext_datarate_len; i++)
    {
        _DUMP("rate_%d: 0x%x", i, wdn_info->ext_datarate[i]);
    }


    if (wdn_info->network_type == WIRELESS_11B)
    {
        _DUMP("network_type: 802.11 B");
    }
    else if (wdn_info->network_type == WIRELESS_11G)
    {
        _DUMP("network_type: 802.11 G");
    }
    else if (wdn_info->network_type == WIRELESS_11BG)
    {
        _DUMP("network_type: 802.11 BG");
    }
    else if (wdn_info->network_type == WIRELESS_11G_24N)
    {
        _DUMP("network_type: 802.11 GN");
    }
    else if (wdn_info->network_type == WIRELESS_11B_24N)
    {
        _DUMP("network_type: 802.11 BN");
    }
    else if (wdn_info->network_type == WIRELESS_11BG_24N)
    {
        _DUMP("network_type: 802.11 BGN");
    }

    _DUMP("User Rate ID: %d", wdn_info->raid);

    if (wdn_info->auth_algo == 0)
    {
        _DUMP("OPEN SYSTEM");
    }
    //    else
    {
        zt_mcu_set_preamble(nic_info, PREAMBLE_LONG);
        if (wdn_info->wep_enable)
        {
            _DUMP("WEP");
        }

        if (wdn_info->wpa_enable)
        {
            _DUMP("WPA");
        }

        if (wdn_info->rsn_enable)
        {
            _DUMP("WPA2");
        }
    }

    _DUMP("ampdu_max_len: %d", wdn_info->htpriv.mcu_ht.rx_ampdu_maxlen);
    _DUMP("ampdu_min_spacing: %d", wdn_info->htpriv.mcu_ht.rx_ampdu_min_spacing);
#undef _DUMP
#endif

    return ZT_RETURN_OK;
}


#define wdn_update_last_rx_pkts(wdn_stats) \
    do { \
        wdn_stats.last_rx_mgnt_pkts = wdn_stats.rx_mgnt_pkts; \
        wdn_stats.last_rx_beacon_pkts = wdn_stats.rx_beacon_pkts; \
        wdn_stats.last_rx_probereq_pkts = wdn_stats.rx_probereq_pkts; \
        wdn_stats.last_rx_probersp_pkts = wdn_stats.rx_probersp_pkts; \
        wdn_stats.last_rx_probersp_bm_pkts = wdn_stats.rx_probersp_bm_pkts; \
        wdn_stats.last_rx_probersp_uo_pkts = wdn_stats.rx_probersp_uo_pkts; \
        wdn_stats.last_rx_ctrl_pkts = wdn_stats.rx_ctrl_pkts; \
        wdn_stats.last_rx_data_pkts = wdn_stats.rx_data_pkts; \
    } while(0)

zt_u8 zt_wdn_is_alive(wdn_net_info_st *wdn_net_info, zt_u8 update_tag)
{
    if ((wdn_net_info->wdn_stats.last_rx_data_pkts +
            wdn_net_info->wdn_stats.last_rx_ctrl_pkts) ==
            (wdn_net_info->wdn_stats.rx_data_pkts + wdn_net_info->wdn_stats.rx_ctrl_pkts))
    {
        return zt_false;
    }

    if (update_tag)
    {
        wdn_update_last_rx_pkts(wdn_net_info->wdn_stats);
    }

    return zt_true;
}



zt_u8 zt_wdn_get_cnt(nic_info_st *pnic_info)
{
    wdn_list *pwdn = NULL;

    if (NULL == pnic_info)
    {
        return 0;
    }

    pwdn = pnic_info->wdn;
    if (NULL == pwdn)
    {
        return 0;
    }

    return pwdn->cnt;
}
