/*
 * wlan_mgmt.c
 *
 * used for process the IEEE80211 management frame receive from path of rx chain
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

/* 802.11 bgn managment frame compose and parse */

#include "common.h"

/* macro */
#define WLAN_MGMT_DBG(fmt, ...)     LOG_D("[%s:%d]"fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define WLAN_MGMT_INFO(fmt, ...)    LOG_I("[%s:%d]"fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define WLAN_MGMT_WARN(fmt, ...)    LOG_W("[%s:%d]"fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define WLAN_MGMT_ERROR(fmt, ...)   LOG_E("[%s:%d]"fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__)

#define LOCAL_INFO                  ((local_info_st *)pnic_info->local_info)
#define WLAN_MGMT_SCAN_QUE_DEEP     LOCAL_INFO->scan_que_deep
#define WLAN_MGMT_SCAN_NODE_TTL     LOCAL_INFO->scan_que_node_ttl

/* type define */
typedef struct
{
    phy_status_st phy_sta;
    zt_bool is_phy_sta_valid;
    zt_80211_mgmt_t mgmt_frm;
} rx_frm_msg_t;

typedef struct
{
    zt_u8 ch_num;
    zt_u8 ch_map[MAX_CHANNEL_NUM];
} scan_que_refresh_msg_t;

typedef struct
{
    zt_u8 cmd;
} chip_reset_msg_t;


/* function declaration */
zt_s32 zt_wlan_mgmt_scan_que_read_try(zt_wlan_mgmt_scan_que_t *pscan_que)
{
    if (pscan_que == NULL)
    {
        return -1;
    }

    zt_os_api_lock_lock(&pscan_que->lock);
    if (pscan_que->read_cnt == 0xFF)
    {
        zt_os_api_lock_unlock(&pscan_que->lock);
        return -2;
    }
    if (!pscan_que->read_cnt)
    {
        if (zt_os_api_sema_try(&pscan_que->sema))
        {
            zt_os_api_lock_unlock(&pscan_que->lock);
            return -3;
        }
    }
    pscan_que->read_cnt++;
    zt_os_api_lock_unlock(&pscan_que->lock);

    return 0;
}

zt_s32 zt_wlan_mgmt_scan_que_read_post(zt_wlan_mgmt_scan_que_t *pscan_que)
{
    if (pscan_que == NULL)
    {
        return -1;
    }

    zt_os_api_lock_lock(&pscan_que->lock);
    if (!pscan_que->read_cnt)
    {
        zt_os_api_lock_unlock(&pscan_que->lock);
        WLAN_MGMT_WARN("no read pend");
        return -2;
    }
    pscan_que->read_cnt--;
    if (!pscan_que->read_cnt)
    {
        zt_os_api_sema_post(&pscan_que->sema);
    }
    zt_os_api_lock_unlock(&pscan_que->lock);

    return 0;
}

zt_inline static
zt_s32 wlan_mgmt_scan_que_write_try(zt_wlan_mgmt_scan_que_t *pscan_que)
{
    return zt_os_api_sema_try(&pscan_que->sema);
}

zt_inline static
zt_s32 wlan_mgmt_scan_que_write_post(zt_wlan_mgmt_scan_que_t *pscan_que)
{
    zt_os_api_sema_post(&pscan_que->sema);

    return 0;
}

static zt_s32 is_bss_onlink(nic_info_st *pnic_info, zt_80211_bssid_t bssid)
{
    zt_wlan_mgmt_info_t *pwlan_mgmt_info = pnic_info->wlan_mgmt_info;
    zt_bool bconnected;

    zt_mlme_get_connect(pnic_info, &bconnected);
    return (bconnected &&
            zt_80211_is_same_addr(pwlan_mgmt_info->cur_network.bssid, bssid));
}

static zt_inline zt_s32
wlan_mgmt_scan_que_node_new(nic_info_st *pnic_info,
                            zt_wlan_mgmt_scan_que_node_t **pnew_node)
{
    zt_wlan_mgmt_info_t *pwlan_mgmt_info = pnic_info->wlan_mgmt_info;
    zt_wlan_mgmt_scan_que_t *pscan_que = &pwlan_mgmt_info->scan_que;
    zt_que_list_t *pnode;

    pnode = zt_deque_head(&pscan_que->free);
    if (!pnode)
    {
        WLAN_MGMT_ERROR("pscan_que->free is empty");
        return -1;
    }

    /* clearup node information */
    {
        zt_wlan_mgmt_scan_que_node_t *scan_node =
            zt_list_entry(pnode, zt_wlan_mgmt_scan_que_node_t, list);
        zt_memset(scan_node, 0x0, sizeof(*scan_node));
        *pnew_node = scan_node;
    }

    return 0;
}

#define NODE_INFO_DBG(...)  //WLAN_MGMT_DBG(__VA_ARGS__)
static zt_inline
zt_s32 wlan_mgmt_scan_node_info(nic_info_st *pnic_info,
                                rx_frm_msg_t *pfrm_msg, zt_u16 frm_msg_len,
                                zt_wlan_mgmt_scan_que_node_t *pscan_que_node)
{
    zt_80211_mgmt_t *pmgmt = &pfrm_msg->mgmt_frm;
    zt_u16 mgmt_len = frm_msg_len - ZT_OFFSETOF(rx_frm_msg_t, mgmt_frm);
    zt_u8 *pele_start, *pele_end;
    zt_80211_mgmt_ie_t *pie;
    zt_80211_mgmt_dsss_parameter_t *pdsss_para;
    zt_80211_mgmt_ht_operation_t *pht_oper;
    zt_u8 support_rate_cnt = 0;
    zt_80211_mgmt_ht_cap_t *pht_cap;
    zt_u8 i;
    zt_bool cck_spot = zt_false, ofdm_spot = zt_false;
    zt_u8 wpa_oui[4] = {0x0, 0x50, 0xf2, 0x01};
    zt_u8 wps_oui[4] = {0x0, 0x50, 0xf2, 0x04};
    zt_80211_frame_e frame_type;

    NODE_INFO_DBG("----------------------------");

    /* get frame type*/
    frame_type = zt_80211_hdr_type_get(pmgmt);
    pscan_que_node->frame_type = frame_type;

    /* get the phy status */
    if (pfrm_msg->is_phy_sta_valid)
    {
        pscan_que_node->signal_strength = pfrm_msg->phy_sta.signal_strength;
        pscan_que_node->signal_strength_scale =
            signal_scale_mapping(pfrm_msg->phy_sta.signal_strength);
        pscan_que_node->signal_qual = pfrm_msg->phy_sta.signal_qual;
    }
    NODE_INFO_DBG("RSSI=%d, signal stregth=%d, signal quality=%d",
                  translate_percentage_to_dbm(pscan_que_node->signal_strength),
                  pscan_que_node->signal_strength_scale,
                  pscan_que_node->signal_qual);

    /* get bssid */
    zt_memcpy(pscan_que_node->bssid, pmgmt->bssid, sizeof(zt_80211_bssid_t));
    NODE_INFO_DBG("BSSID=%02X:%02X:%02X:%02X:%02X:%02X",
                  pscan_que_node->bssid[0], pscan_que_node->bssid[1],
                  pscan_que_node->bssid[2], pscan_que_node->bssid[3],
                  pscan_que_node->bssid[4], pscan_que_node->bssid[5]);

    /* get operation mode */
    if (!ZT_80211_CAPAB_IS_MESH_STA_BSS(pmgmt->beacon.capab))
    {
        pscan_que_node->opr_mode =
            (pmgmt->beacon.capab & ZT_80211_MGMT_CAPAB_ESS) ? ZT_WLAN_OPR_MODE_MASTER :
            ZT_WLAN_OPR_MODE_ADHOC;
    }
    else
    {
        if (zt_p2p_is_valid(pnic_info))
        {
            p2p_info_st *p2p_info = pnic_info->p2p;
            if (!zt_memcmp(&pmgmt->beacon.variable[2],
                           p2p_info->p2p_wildcard_ssid, P2P_WILDCARD_SSID_LEN))
            {
                NODE_INFO_DBG(" This is a p2p device role = %d", p2p_info->role);
            }
        }
        else
        {
            WLAN_MGMT_WARN("operation mode is mesh");
            return -1;
        }
    }

    /* get privacy */
    pscan_que_node->cap_privacy =
        (zt_bool)!!(zt_le16_to_cpu(pmgmt->beacon.capab) & ZT_80211_MGMT_CAPAB_PRIVACY);

    /* ies formation */
    pscan_que_node->ie_len = mgmt_len - ZT_OFFSETOF(zt_80211_mgmt_t, beacon);
    if (pscan_que_node->ie_len >= sizeof(pscan_que_node->ies))
    {
        WLAN_MGMT_ERROR("ie data length too long");
        return -2;
    }
    zt_memcpy(&pscan_que_node->ies[0], &pmgmt->beacon, pscan_que_node->ie_len);
    pscan_que_node->ies[pscan_que_node->ie_len] = 0x0;

    /* mark ssid no initilize */
    pscan_que_node->ssid_type = ZT_80211_HIDDEN_SSID_UNKNOWN;

    pele_start = &pmgmt->beacon.variable[0];
    pele_end = &pele_start[mgmt_len - ZT_OFFSETOF(zt_80211_mgmt_t,
                                    beacon.variable)];
    do
    {
        pie = (zt_80211_mgmt_ie_t *)pele_start;
        switch (pie->element_id)
        {
            case ZT_80211_MGMT_EID_SSID :
                if (pie->len >= sizeof(pscan_que_node->ssid.data))
                {
                    WLAN_MGMT_WARN("invalid SSID length(%d)", pie->len);
                    return -3;
                }
                if (frame_type == ZT_80211_FRM_PROBE_RESP)
                {
                    if (!pie->len)
                    {
                        WLAN_MGMT_DBG("probe resp send from a mesh STA, bssid="ZT_MAC_FMT,
                                      ZT_MAC_ARG(pscan_que_node->bssid));
                        return -4;
                    }

                    if (pscan_que_node->ssid_type == ZT_80211_HIDDEN_SSID_UNKNOWN)
                    {
                        /* mark ssid type no hidden */
                        pscan_que_node->ssid_type = ZT_80211_HIDDEN_SSID_NOT_IN_USE;
                    }
                    /* update ssid information */
                    pscan_que_node->ssid.length = pie->len;
                    zt_memcpy(pscan_que_node->ssid.data, pie->data, pie->len);
                    pscan_que_node->ssid.data[pie->len] = '\0';
                    NODE_INFO_DBG("SSID: %s", pscan_que_node->ssid.data);
                }
                else
                {
                    if (!pie->len)
                    {
                        pscan_que_node->ssid_type = ZT_80211_HIDDEN_SSID_ZERO_LEN;
                    }
                    else if (pie->data[0] == '\0')
                    {
                        pscan_que_node->ssid_type = ZT_80211_HIDDEN_SSID_ZERO_CONTENTS;
                        pscan_que_node->ssid.length = pie->len;
                    }
                    else
                    {
                        pscan_que_node->ssid_type = ZT_80211_HIDDEN_SSID_NOT_IN_USE;
                        pscan_que_node->ssid.length = pie->len;
                        zt_memcpy(pscan_que_node->ssid.data, pie->data, pie->len);
                        pscan_que_node->ssid.data[pie->len] = '\0';
                        NODE_INFO_DBG("SSID: %s", pscan_que_node->ssid.data);
                    }
                }
                break;

            case ZT_80211_MGMT_EID_SUPP_RATES :
            case ZT_80211_MGMT_EID_EXT_SUPP_RATES :
                /* basic rate */
                if (support_rate_cnt == 0)
                {
                    zt_memset(pscan_que_node->spot_rate, 0,
                              sizeof(pscan_que_node->spot_rate));
                }
                if (pie->len == 0 || pie->len + support_rate_cnt >
                        sizeof(pscan_que_node->spot_rate))
                {
                    WLAN_MGMT_WARN("support rate number over limit");
                    return -5;
                }
                else
                {
                    zt_memcpy(&pscan_que_node->spot_rate[support_rate_cnt],
                              pie->data, pie->len);
                    support_rate_cnt += pie->len;
                }
                break;

            case ZT_80211_MGMT_EID_DS_PARAMS :
                pdsss_para = (zt_80211_mgmt_dsss_parameter_t *)pie->data;
                pscan_que_node->channel = pdsss_para->current_channel;
                break;

            case ZT_80211_MGMT_EID_HT_OPERATION :
                /* for 5G AP */
                pht_oper = (zt_80211_mgmt_ht_operation_t *)pie->data;
                pscan_que_node->channel = pht_oper->primary_chan;
                break;

            case ZT_80211_MGMT_EID_HT_CAPABILITY :
                pscan_que_node->ht_cap_en = zt_true;
                pht_cap = (zt_80211_mgmt_ht_cap_t *)pie->data;
                zt_memcpy(&pscan_que_node->mcs, pht_cap->mcs_info.rx_mask,
                          sizeof(pscan_que_node->mcs));
                pscan_que_node->bw_40mhz =
                    (zt_bool)(!!(pht_cap->cap_info &
                                 ZT_80211_MGMT_HT_CAP_SUP_WIDTH_20_40));
                pscan_que_node->short_gi =
                    (zt_bool)(!!(pht_cap->cap_info &
                                 (ZT_80211_MGMT_HT_CAP_SGI_20 |
                                  ZT_80211_MGMT_HT_CAP_SGI_40)));
                break;

            case ZT_80211_MGMT_EID_VENDOR_SPECIFIC :
                if (!zt_memcmp(pie->data, wpa_oui, sizeof(wpa_oui)))
                {
                    if (pie->len <= sizeof(pscan_que_node->wpa_ie))
                    {
                        zt_u32 len = sizeof(zt_80211_mgmt_ie_t) + pie->len;

                        zt_memcpy(&pscan_que_node->wpa_ie, pie, len);
                        zt_80211_mgmt_wpa_parse(pscan_que_node->wpa_ie, len,
                                                &pscan_que_node->wpa_multicast_cipher,
                                                &pscan_que_node->wpa_unicast_cipher);
                    }
                    else
                    {
                        WLAN_MGMT_WARN("wpa length=%d over limite", pie->len);
                        return -6;
                    }
                }
                else if (!zt_memcmp(pie->data, wps_oui, sizeof(wps_oui)))
                {
                    if (pie->len <= sizeof(pscan_que_node->wps_ie))
                    {
                        zt_memcpy(&pscan_que_node->wps_ie, pie,
                                  ZT_OFFSETOF(zt_80211_mgmt_ie_t, data) + pie->len);
                    }
                    else
                    {
                        NODE_INFO_DBG("wps length=%d over limite", pie->len);
                    }
                }
                break;

            case ZT_80211_MGMT_EID_RSN :
                if (pie->len <= sizeof(pscan_que_node->rsn_ie))
                {
                    zt_u32 len = sizeof(zt_80211_mgmt_ie_t) + pie->len;

                    zt_memcpy(&pscan_que_node->rsn_ie, pie, len);
                    zt_80211_mgmt_rsn_parse(pscan_que_node->rsn_ie, len,
                                            &pscan_que_node->rsn_group_cipher,
                                            &pscan_que_node->rsn_pairwise_cipher);
                }
                else
                {
                    WLAN_MGMT_WARN("rsn length=%d over limite", pie->len);
                    return -7;
                }
                break;
        }
        /* get next element point */
        pele_start = &pele_start[ZT_OFFSETOF(zt_80211_mgmt_ie_t, data) + pie->len];
    } while (pele_start < pele_end);

    /* parse validity check */
    if (pscan_que_node->ssid_type == ZT_80211_HIDDEN_SSID_UNKNOWN)
    {
        WLAN_MGMT_WARN("no ssid element"" bssid:"ZT_MAC_FMT,
                       ZT_MAC_ARG(pscan_que_node->bssid));
        return -8;
    }
    if (!support_rate_cnt)
    {
        WLAN_MGMT_WARN("no datarate element");
        return -9;
    }
    if (!pscan_que_node->channel)
    {
        WLAN_MGMT_WARN("no DS element");
        return -10;
    }
    if (!pscan_que_node->cap_privacy &&
            (pscan_que_node->wpa_multicast_cipher | pscan_que_node->wpa_unicast_cipher |
             pscan_que_node->rsn_group_cipher | pscan_que_node->rsn_pairwise_cipher))
    {
        WLAN_MGMT_WARN("cap_privacy(%d) field no match wpa or rsn element setting"ZT_MAC_FMT,
                       pscan_que_node->cap_privacy,
                       ZT_MAC_ARG(pscan_que_node->bssid));
        return -12;
    }

    /* get name information */
    for (i = 0; i < support_rate_cnt; i++)
    {
        switch (pscan_que_node->spot_rate[i] & 0x7F)
        {
            /* 0.5Mbps unit */
            case 2 : /* 2*0.5=1Mbps*/
            case 4 :
            case 11 :
            case 22 :
                cck_spot = zt_true;
                break;
            default :
                ofdm_spot = zt_true;
                break;
        }
    }
    if (cck_spot && ofdm_spot)
    {
        pscan_que_node->name =
            pscan_que_node->ht_cap_en ? ZT_WLAN_BSS_NAME_IEEE80211_BGN :
            ZT_WLAN_BSS_NAME_IEEE80211_BG;
    }
    else if (cck_spot)
    {
        pscan_que_node->name =
            pscan_que_node->ht_cap_en ? ZT_WLAN_BSS_NAME_IEEE80211_BN :
            ZT_WLAN_BSS_NAME_IEEE80211_B;
    }
    else
    {
        pscan_que_node->name =
            pscan_que_node->ht_cap_en ? ZT_WLAN_BSS_NAME_IEEE80211_GN :
            ZT_WLAN_BSS_NAME_IEEE80211_G;
    }

    return 0;
}
#undef NODE_INFO_DBG

static zt_inline zt_s32
wlan_mgmt_scan_que_node_push(nic_info_st *pnic_info,
                             rx_frm_msg_t *pfrm_msg,
                             zt_wlan_mgmt_scan_que_node_t *pnew_node)
{
    zt_wlan_mgmt_info_t *pwlan_mgmt_info = pnic_info->wlan_mgmt_info;
    zt_wlan_mgmt_scan_que_t *pscan_que = &pwlan_mgmt_info->scan_que;
    zt_wlan_mgmt_scan_que_node_t *pnode, *pswap_node = NULL, *pinst_pos = NULL;
    zt_list_t *pos;

    zt_list_for_each(pos, zt_que_list_head(&pscan_que->ready))
    {
        pnode = zt_list_entry(pos, zt_wlan_mgmt_scan_que_node_t, list);
        if (!pswap_node &&
                zt_80211_is_same_addr(pnew_node->bssid, pnode->bssid))
        {
            pswap_node = pnode;
        }
        if (!pinst_pos &&
                pnew_node->signal_strength > pnode->signal_strength)
        {
            pinst_pos = pnode;
        }
        if (pswap_node && pinst_pos)
        {
            break;
        }
    }

    /* if frame data no contain rssi information, can only accept it when
    scan ready queue no full, and no any replace node in scan ready queue. */
    if (!pfrm_msg->is_phy_sta_valid)
    {
        if (zt_que_is_empty(&pscan_que->free))
        {
            WLAN_MGMT_DBG("scan queue full, node without rssi can't do replase action");
            return -2;
        }
        else if (pswap_node)
        {
            WLAN_MGMT_DBG("no match any one, node without rssi can't do replase action");
            return -3;
        }
    }

    /* todo: if no replace node matched in scan ready queue, and ready queue is
    full, may select node from scan ready queue tail, whith rssi is worst one,
    but this node should't the current assected bss */
    if (!pswap_node && zt_que_is_empty(&pscan_que->free))
    {
        pswap_node = zt_list_entry(zt_que_tail(&pscan_que->ready),
                                   zt_wlan_mgmt_scan_que_node_t, list);
        if (is_bss_onlink(pnic_info, pswap_node->bssid))
        {
            WLAN_MGMT_DBG("can't remove bss, the node in ready queue is current assoc bss");
            return -4;
        }
    }

    /* todo: try to insert new node to scan ready queue, and remove one reaplse
    node */
    if (wlan_mgmt_scan_que_write_try(pscan_que))
    {
        return -5;
    }

    /* todo: a probe respone information stay in ready queue less than one
    second, can be update by a beacon frame */
    if (pswap_node &&
            pnew_node->frame_type == ZT_80211_FRM_BEACON &&
            pswap_node->frame_type == ZT_80211_FRM_PROBE_RESP &&
            !zt_timer_expired(&pswap_node->ttl))
    {
        /* update swap node use new node information */
        pswap_node->signal_qual = pnew_node->signal_qual;
        pswap_node->signal_strength = pnew_node->signal_strength;
        pswap_node->signal_strength_scale = pnew_node->signal_strength_scale;
        ((struct beacon_ie *)pswap_node->ies)->timestamp =
            ((struct beacon_ie *)pnew_node->ies)->timestamp;

        /* drop new node to free queue*/
        zt_enque_tail(&pnew_node->list, &pscan_que->free);

        /* todo: because swap node rssi has modify, the order of swap node in
        ready queue maybe adjust again. */

        /* if swap node order has no change. */
        if (pinst_pos == pswap_node)
        {
            goto exit;
        }

        /* befor adjust swap node, remove it from ready queue */
        zt_deque(&pswap_node->list, &pscan_que->ready);
        pnew_node = pswap_node;
        pswap_node = NULL;
    }
    else
    {
        /* reset ttl value */
        zt_timer_set(&pnew_node->ttl, WLAN_MGMT_SCAN_NODE_TTL * 1000); /* Second */
    }

    /* insert node to ready queue */
    if (pinst_pos)
    {
        zt_enque_prev(&pnew_node->list, &pinst_pos->list, &pscan_que->ready);
    }
    else
    {
        zt_enque_tail(&pnew_node->list, &pscan_que->ready);
    }
    /* remove node from ready queue if need */
    if (pswap_node)
    {
        zt_deque(&pswap_node->list, &pscan_que->ready);
        zt_enque_tail(&pswap_node->list, &pscan_que->free);
    }

exit:
    wlan_mgmt_scan_que_write_post(pscan_que);

    return 0;
}

zt_inline static zt_s32
wlan_mgmt_scan_que_node_flush(nic_info_st *pnic_info)
{
    zt_wlan_mgmt_info_t *pwlan_mgmt_info = pnic_info->wlan_mgmt_info;
    zt_wlan_mgmt_scan_que_t *pscan_que = &pwlan_mgmt_info->scan_que;

    if (zt_que_is_empty(&pscan_que->ready))
    {
        WLAN_MGMT_INFO("scan queue is empty");
        return 0;
    }

    if (wlan_mgmt_scan_que_write_try(pscan_que))
    {
        return -1;
    }

    {
        zt_list_t *pos, *n;
        zt_list_for_each_safe(pos, n, zt_que_list_head(&pscan_que->ready))
        {
            zt_wlan_mgmt_scan_que_node_t *pscan_que_node =
                zt_list_entry(pos, zt_wlan_mgmt_scan_que_node_t, list);
            /* flush all node except the bss whitch is current connected */
            if (!is_bss_onlink(pnic_info, pscan_que_node->bssid))
            {
                zt_deque(pos, &pscan_que->ready);
                zt_enque_tail(pos, &pscan_que->free);
            }
        }
    }

    wlan_mgmt_scan_que_write_post(pscan_que);

    return 0;
}

static zt_s32
wlan_mgmt_scan_que_refresh(nic_info_st *pnic_info, scan_que_refresh_msg_t *preq)
{
    zt_wlan_mgmt_info_t *pwlan_mgmt_info = pnic_info->wlan_mgmt_info;
    zt_wlan_mgmt_scan_que_t *pscan_que = &pwlan_mgmt_info->scan_que;
    zt_list_t *pos, *n;

    //    WLAN_MGMT_INFO(" TYPE |               SSID              | rssi | TTL  " "\r\n");
    //    WLAN_MGMT_INFO("------------------------------------------------------" "\r\n");

    zt_list_for_each_safe(pos, n, zt_que_list_head(&pscan_que->ready))
    {
        zt_wlan_mgmt_scan_que_node_t *pnode =
            zt_list_entry(pos, zt_wlan_mgmt_scan_que_node_t, list);
        zt_u8 i;

        //        WLAN_MGMT_INFO("  %-4s|" " %-32s|" " %-5d|" " %d" "\r\n",
        //                       pnode->frame_type == ZT_80211_FRM_BEACON ? "BCN" :
        //                       pnode->frame_type == ZT_80211_FRM_PROBE_RESP ? "PB" : "???",
        //                       pnode->ssid.data,
        //                       pnode->signal_strength,
        //                       zt_timer_remaining(&pnode->ttl));

        for (i = 0; i < preq->ch_num; i++)
        {
            if (pnode->channel == preq->ch_map[i])
            {
                break;
            }
        }
        if (i == preq->ch_num)
        {
            continue;
        }

        if (zt_timer_expired(&pnode->ttl))
        {
            if (!wlan_mgmt_scan_que_write_try(pscan_que))
            {
                zt_deque(&pnode->list, &pscan_que->ready);
                zt_enque_tail(&pnode->list, &pscan_que->free);
                wlan_mgmt_scan_que_write_post(pscan_que);
            }
        }
    }

    return 0;
}

static zt_bool is_bss_info_changed(nic_info_st *pnic_info,
                                   zt_wlan_mgmt_scan_que_node_t *pbss_info)
{
    zt_bool bconnect;
    zt_wlan_mgmt_info_t *pwlan_info = pnic_info->wlan_mgmt_info;
    zt_wlan_network_t *pcur_network = &pwlan_info->cur_network;
    zt_bool ret = zt_false;

    zt_mlme_get_connect(pnic_info, &bconnect);
    if (!bconnect)
    {
        return zt_false;
    }

    if (!zt_80211_is_same_addr(pbss_info->bssid, pcur_network->bssid))
    {
        return zt_false;
    }

    /* ssid */
    if (pbss_info->ssid_type == ZT_80211_HIDDEN_SSID_NOT_IN_USE ||
            pbss_info->frame_type == ZT_80211_FRM_PROBE_RESP)
    {
        if (pbss_info->ssid.length &&
                !zt_wlan_is_same_ssid(&pcur_network->ssid, &pbss_info->ssid))
        {
            WLAN_MGMT_ERROR("ssid change to \"%s\"", pbss_info->ssid.data);
            ret = zt_true;
        }
    }

    /* channel */
    if (pcur_network->channel != pbss_info->channel)
    {
        WLAN_MGMT_ERROR("channel change to \"%d\"", pbss_info->channel);
        ret = zt_true;
    }

    /* privacy*/
    if (pcur_network->cap_privacy != pbss_info->cap_privacy)
    {
        WLAN_MGMT_ERROR("cap_privacy change to \"%d\"", pbss_info->cap_privacy);
        ret = zt_true;
    }
    if (pcur_network->wpa_multicast_cipher != pbss_info->wpa_multicast_cipher)
    {
        WLAN_MGMT_ERROR("wpa_multicast_cipher change to \"0x%08X\"",
                        pbss_info->wpa_multicast_cipher);
        ret = zt_true;
    }
    if (pcur_network->wpa_unicast_cipher != pbss_info->wpa_unicast_cipher)
    {
        WLAN_MGMT_ERROR("wpa_unicast_cipher change to \"0x%08X\"",
                        pbss_info->wpa_unicast_cipher);
        ret = zt_true;
    }
    if (pcur_network->rsn_group_cipher != pbss_info->rsn_group_cipher)
    {
        WLAN_MGMT_ERROR("rsn_group_cipher change to \"0x%08X\"",
                        pbss_info->rsn_group_cipher);
        ret = zt_true;
    }
    if (pcur_network->rsn_pairwise_cipher != pbss_info->rsn_pairwise_cipher)
    {
        WLAN_MGMT_ERROR("rsn_pairwise_cipher change to \"0x%08X\"",
                        pbss_info->rsn_pairwise_cipher);
        ret = zt_true;
    }

    if (ret == zt_false)
    {
        pcur_network->bss_change_cnt = 0;
    }
    /* todo: must occur 3 times in a row. */
    else if (pcur_network->bss_change_cnt++ < 3)
    {
        WLAN_MGMT_ERROR("ap change occur time %d",
                        pcur_network->bss_change_cnt);
        ret = zt_false;
    }

    return ret;
}

static zt_s32 do_chip_reset(nic_info_st *pnic_info, zt_u8 cmd)
{
    if (ZT_CANNOT_RUN(pnic_info))
    {
        return -1;
    }

    pnic_info->is_driver_critical = zt_true;
    zt_mlme_suspend(pnic_info);
#ifdef CFG_ENABLE_AP_MODE
    zt_ap_suspend(pnic_info);
#endif
    zt_p2p_suspend(pnic_info);
    if (pnic_info->buddy_nic)
    {
        nic_info_st *pbuddy_info = pnic_info->buddy_nic;
        pbuddy_info->is_driver_critical = zt_true;
        zt_mlme_suspend(pbuddy_info);
#ifdef CFG_ENABLE_AP_MODE
        zt_ap_suspend(pbuddy_info);
#endif
        zt_p2p_suspend(pbuddy_info);
    }

    if (0)//(cmd == 0)
    {
        zt_mcu_reset_bb(pnic_info);
    }
    else
    {
        zt_s32 hif_chip_reset(void *);
        hif_chip_reset(pnic_info->hif_node);
    }

    pnic_info->is_driver_critical = zt_false;
    zt_mlme_resume(pnic_info);
#ifdef CFG_ENABLE_AP_MODE
    zt_ap_resume(pnic_info);
#endif
    zt_p2p_resume(pnic_info);
    if (pnic_info->buddy_nic)
    {
        nic_info_st *pbuddy_info = pnic_info->buddy_nic;
        pbuddy_info->is_driver_critical = zt_false;
        zt_mlme_resume(pbuddy_info);
#ifdef CFG_ENABLE_AP_MODE
        zt_ap_resume(pbuddy_info);
#endif
        zt_p2p_resume(pbuddy_info);
    }

    return 0;
}

static zt_s32 rx_frame_handle(nic_info_st *pnic_info)
{
    zt_wlan_mgmt_info_t *pwlan_mgmt_info = pnic_info->wlan_mgmt_info;
    pwr_info_st *pwr_info = pnic_info->pwr_info;
    zt_wlan_mgmt_scan_que_t *pscan_que = &pwlan_mgmt_info->scan_que;
    zt_msg_que_t *pmsg_que = &pwlan_mgmt_info->msg_que;
    zt_msg_t *pmsg;
    zt_bool uninstalling = zt_false;
    zt_s32 rst;

    zt_os_api_thread_enter_hook(pwlan_mgmt_info->tid);

    while (zt_os_api_thread_wait_stop(pwlan_mgmt_info->tid) == zt_false)
    {
        zt_msleep(1);

        /* wait new message */
        while (!zt_msg_pop(pmsg_que, &pmsg))
        {
            if (pwr_info->bInSuspend || uninstalling)
            {
                break;
            }

            switch (pmsg->tag)
            {
                case ZT_WLAN_MGMT_TAG_BEACON_FRAME :
                case ZT_WLAN_MGMT_TAG_PROBERSP_FRAME :
                {
                    rx_frm_msg_t *pfrm_msg = (void *)pmsg->value;
                    zt_u16 frm_msg_len = pmsg->len;
                    zt_wlan_mgmt_scan_que_node_t *pnew_node;

                    rst = wlan_mgmt_scan_que_node_new(pnic_info, &pnew_node);
                    if (rst)
                    {
                        WLAN_MGMT_DBG("new node fail, error code: %d", rst);
                        break;
                    }

                    rst = wlan_mgmt_scan_node_info(pnic_info,
                                                   pfrm_msg, frm_msg_len,
                                                   pnew_node);
                    if (rst)
                    {
                        WLAN_MGMT_DBG("make info fail, error code: %d", rst);
                        /* pull node back to free queue. */
                        zt_enque_head(&pnew_node->list, &pscan_que->free);
                        break;
                    }

                    rst = wlan_mgmt_scan_que_node_push(pnic_info,
                                                       pfrm_msg,
                                                       pnew_node);
                    if (rst)
                    {
                        WLAN_MGMT_DBG("node input scan queue fail, error code: %d", rst);
                        /* pull node back to free queue. */
                        zt_enque_head(&pnew_node->list, &pscan_que->free);
                    }

                    if (is_bss_info_changed(pnic_info, pnew_node))
                    {
                        WLAN_MGMT_ERROR("AP attribute has changed, break current connection");
                        zt_mlme_deauth(pnic_info, zt_true, ZT_80211_REASON_UNSPECIFIED);
                    }
                    break;
                }

                case ZT_WLAN_MGMT_TAG_SCAN_QUE_FLUSH :
                {
                    WLAN_MGMT_INFO("scan queue flush");
                    rst = wlan_mgmt_scan_que_node_flush(pnic_info);
                    if (rst)
                    {
                        WLAN_MGMT_WARN("scan queue flush fail, error code %d", rst);
                    }
                    break;
                }

                case ZT_WLAN_MGMT_TAG_SCAN_QUE_REFRESH :
                {
                    WLAN_MGMT_INFO("scan queue refresh");
                    rst = wlan_mgmt_scan_que_refresh(pnic_info, (void *)pmsg->value);
                    if (rst)
                    {
                        WLAN_MGMT_WARN("scan queue refresh fail, error code %d", rst);
                    }
                    break;
                }

                case ZT_WLAN_MGMT_TAG_CHIPRESET :
                {
                    chip_reset_msg_t *chip_reset = (void *)pmsg->value;
                    WLAN_MGMT_INFO("chip reset");

                    do_chip_reset(pnic_info, chip_reset->cmd);
                    break;
                }

                case ZT_WLAN_MGMT_TAG_PROBEREQ_P2P :
                {
                    rx_frm_msg_t *pfrm_msg = (void *)pmsg->value;
                    zt_u16 frm_msg_len = pmsg->len;
                    zt_u16 mgmt_len = frm_msg_len - ZT_OFFSETOF(rx_frm_msg_t, mgmt_frm);
                    zt_p2p_recv_probereq(pnic_info,  &pfrm_msg->mgmt_frm, mgmt_len);
                    break;
                }

                case ZT_WLAN_MGMT_TAG_ACTION :
                {
                    rx_frm_msg_t *pfrm_msg = (void *)pmsg->value;
                    zt_u16 frm_msg_len = pmsg->len;
                    zt_u16 mgmt_len = frm_msg_len - ZT_OFFSETOF(rx_frm_msg_t, mgmt_frm);
                    //WLAN_MGMT_INFO("action frame");

                    zt_action_frame_process(pnic_info, &pfrm_msg->mgmt_frm, mgmt_len);
                    break;
                }

                case ZT_WLAN_MGMT_TAG_UNINSTALL :
                {
                    WLAN_MGMT_INFO("prepare to uninstall");
                    while (wlan_mgmt_scan_que_write_try(pscan_que))
                    {
                        zt_msleep(1);
                    }
                    uninstalling = zt_true;
                    break;
                }

                default :
                    WLAN_MGMT_ERROR("unknown message tag %d", pmsg->tag);
                    break;
            }

            zt_msg_del(pmsg_que, pmsg);
        }
    }

    zt_os_api_thread_exit(pwlan_mgmt_info->tid);

    return 0;
}

static phy_status_st *get_phy_status(prx_pkt_t ppkt)
{
    /* todo: some received manage frame may no contain phy_status filed,
    judge by pkt_info.phy_status, if true mains contain phy status, otherwise
    phy_status is invalid */
    return ppkt->pkt_info.phy_status ? &ppkt->phy_status : NULL;
}

static zt_s32 frm_msg_send(zt_wlan_mgmt_info_t *pwlan_mgmt_info,
                           zt_msg_tag_t tag,
                           phy_status_st *phy_sta,
                           zt_80211_mgmt_t *pmgmt, zt_u32 mgmt_len)
{
    zt_msg_que_t *pmsg_que = &pwlan_mgmt_info->msg_que;
    zt_msg_t *pmsg;
    zt_s32 rst;

    /* new message entity */
    rst = zt_msg_new(pmsg_que, tag, &pmsg);
    if (rst)
    {
        WLAN_MGMT_DBG("msg new fail error code: %d", rst);
        return -1;
    }

    /* fill message value */
    {
        rx_frm_msg_t *prx_frm_msg = (void *)pmsg->value;
        zt_u16 value_size = ZT_OFFSETOF(rx_frm_msg_t, mgmt_frm) + mgmt_len;
        if (value_size > pmsg->alloc_value_size)
        {
            WLAN_MGMT_ERROR("frame size(%d) over limite(%d)",
                            mgmt_len, pmsg->alloc_value_size);
            return -2;
        }
        pmsg->len = value_size;
        if (phy_sta)
        {
            prx_frm_msg->phy_sta            = *phy_sta;
            prx_frm_msg->is_phy_sta_valid   = zt_true;
        }
        else
        {
            prx_frm_msg->phy_sta.rssi               = -128;
            prx_frm_msg->phy_sta.signal_strength    = 0;
            prx_frm_msg->phy_sta.signal_qual        = 0;
            prx_frm_msg->is_phy_sta_valid           = zt_false;
        }

        void *tmp_mgmt_fram = (zt_s8 *)prx_frm_msg + ZT_OFFSETOF(rx_frm_msg_t, mgmt_frm);
        zt_memcpy(tmp_mgmt_fram, pmgmt, mgmt_len);
    }

    /* new message entity */
    rst = zt_msg_push(pmsg_que, pmsg);
    if (rst)
    {
        WLAN_MGMT_WARN("zt_msg_push fail error code: %d", rst);
        return -3;
    }

    return 0;
}

zt_s32 zt_wlan_mgmt_rx_frame(void *ptr)
{
    prx_pkt_t ppkt = ptr;
    nic_info_st *pnic_info;
    zt_wlan_mgmt_info_t *pwlan_mgmt_info;
    zt_80211_mgmt_t *pmgmt;
    zt_u32 mgmt_len;
    wdn_net_info_st *pwdn_info;
    zt_80211_frame_e frm_type;
    zt_s32 rst;

    if (ppkt == NULL || ppkt->p_nic_info == NULL || ppkt->pdata == NULL)
    {
        WLAN_MGMT_ERROR("null pointer");
        return -1;
    }
    pnic_info = ppkt->p_nic_info;

    if (ZT_CANNOT_RUN(pnic_info))
    {
        return -2;
    }

    pwlan_mgmt_info = pnic_info->wlan_mgmt_info;
    if (pwlan_mgmt_info == NULL)
    {
        WLAN_MGMT_ERROR("wlan_mgmt_info null pointer");
        return -3;
    }

    mgmt_len = ppkt->len;
    if (mgmt_len == 0)
    {
        WLAN_MGMT_ERROR("frame length zero");
        return -5;
    }
    pmgmt = (void *)ppkt->pdata;

    pwdn_info = ppkt->wdn_info;
    if (pwdn_info)
    {
        pwdn_info->wdn_stats.rx_mgnt_pkts++;
    }

    frm_type = zt_80211_hdr_type_get(pmgmt);
    switch (frm_type)
    {
        case ZT_80211_FRM_BEACON :
            if (mgmt_len > ZT_80211_MGMT_BEACON_SIZE_MAX)
            {
                WLAN_MGMT_ERROR("beacon frame length(%d) over limited", mgmt_len);
                return -6;
            }

            /* scan process filter */
            rst = zt_scan_filter(pnic_info, pmgmt, mgmt_len);
            if (rst)
            {
                WLAN_MGMT_DBG("scan filter fail, error code: %d", rst);
                return -7;
            }

            /* send frame message */
            rst = frm_msg_send(pwlan_mgmt_info, ZT_WLAN_MGMT_TAG_BEACON_FRAME,
                               get_phy_status(ppkt), pmgmt, mgmt_len);
            if (rst)
            {
                WLAN_MGMT_DBG("scan frame message send fail, error code: %d", rst);
                return -8;
            }

            /* update becon timestamp*/
            if (zt_80211_is_same_addr(pmgmt->bssid, zt_wlan_get_cur_bssid(pnic_info)))
            {
                zt_wlan_network_t *pcur_network = &pwlan_mgmt_info->cur_network;
                //              WLAN_MGMT_DBG("update beacon timestamp");
                pcur_network->timestamp = pmgmt->beacon.timestamp;
                pcur_network->bcn_interval = pmgmt->beacon.intv;
                //              WLAN_MGMT_DBG("beacon.timestamp:%lld",pcur_network->timestamp);
            }

#ifdef CFG_ENABLE_ADHOC_MODE
            if (zt_local_cfg_get_work_mode(pnic_info) == ZT_ADHOC_MODE)
            {
                zt_adhoc_work(pnic_info, (void *)pmgmt, mgmt_len);
            }
#endif
            break;

        case ZT_80211_FRM_PROBE_REQ :
            if (zt_p2p_is_valid(pnic_info))
            {
                if (mgmt_len > ZT_WLAN_MGMT_TAG_PROBEREQ_P2P_SIZE_MAX)
                {
                    WLAN_MGMT_ERROR("probe req frame length(%d) over limited", mgmt_len);
                    return -6;
                }
                /* send frame message */
                rst = frm_msg_send(pwlan_mgmt_info, ZT_WLAN_MGMT_TAG_PROBEREQ_P2P,
                                   get_phy_status(ppkt), pmgmt, mgmt_len);
                if (rst)
                {
                    WLAN_MGMT_DBG("probe req frame message send fail, error code: %d", rst);
                    return -7;
                }
            }
#if defined(CFG_ENABLE_ADHOC_MODE)
            if (zt_local_cfg_get_work_mode(pnic_info) == ZT_ADHOC_MODE)
            {
                zt_adhoc_do_probrsp(pnic_info, pmgmt, mgmt_len);
                break;
            }
#endif

#ifdef CFG_ENABLE_AP_MODE
            if (zt_local_cfg_get_work_mode(pnic_info) == ZT_MASTER_MODE)
            {
                zt_ap_probe_parse(pnic_info, pmgmt, mgmt_len);
            }
#endif
            break;

        case ZT_80211_FRM_PROBE_RESP :
            if (mgmt_len > ZT_80211_MGMT_PROBERSP_SIZE_MAX)
            {
                WLAN_MGMT_ERROR("probersp frame length(%d) over limited", mgmt_len);
                return -9;
            }

            /* scan process filter */
            rst = zt_scan_filter(pnic_info, pmgmt, mgmt_len);
            if (rst)
            {
                WLAN_MGMT_DBG("scan filter fail, error code: %d", rst);
                return -10;
            }

            /* send frame message */
            rst = frm_msg_send(pwlan_mgmt_info, ZT_WLAN_MGMT_TAG_PROBERSP_FRAME,
                               get_phy_status(ppkt), pmgmt, mgmt_len);
            if (rst)
            {
                WLAN_MGMT_DBG("scan frame message send fail, error code: %d", rst);
                return -11;
            }
            break;

        case ZT_80211_FRM_AUTH :
            zt_auth_frame_parse(pnic_info, pwdn_info, pmgmt, mgmt_len);
            break;

        case ZT_80211_FRM_DEAUTH :
            zt_deauth_frame_parse(pnic_info, pwdn_info, pmgmt, mgmt_len);
            break;

#ifdef CFG_ENABLE_AP_MODE
        case ZT_80211_FRM_ASSOC_REQ :
        case ZT_80211_FRM_REASSOC_REQ :
            zt_assoc_ap_work(pnic_info, pwdn_info, (void *)pmgmt, mgmt_len);
            break;
#endif

        case ZT_80211_FRM_ASSOC_RESP :
        case ZT_80211_FRM_REASSOC_RESP :
            zt_assoc_frame_parse(pnic_info, pwdn_info, pmgmt, mgmt_len);
            break;

        case ZT_80211_FRM_DISASSOC :
            zt_disassoc_frame_parse(pnic_info, pwdn_info, pmgmt, mgmt_len);
            break;

        case ZT_80211_FRM_ACTION :
            if (mgmt_len > ZT_80211_MGMT_BEACON_SIZE_MAX)
            {
                WLAN_MGMT_ERROR("action frame length(%d) over limited", mgmt_len);
                return -6;
            }
            /* send frame message */
            rst = frm_msg_send(pwlan_mgmt_info, ZT_WLAN_MGMT_TAG_ACTION,
                               get_phy_status(ppkt), pmgmt, mgmt_len);
            if (rst)
            {
                WLAN_MGMT_DBG("action frame message send fail, error code: %d", rst);
                return -7;
            }
            break;

        default :
            WLAN_MGMT_WARN("untreated frame type: %d", frm_type);
            break;
    }

    return 0;
}

static zt_s32 wlan_mgmt_scan_que_init(nic_info_st *pnic_info)
{
    zt_wlan_mgmt_info_t *pwlan_mgmt_info = pnic_info->wlan_mgmt_info;
    zt_wlan_mgmt_scan_que_t *pscan_que = &pwlan_mgmt_info->scan_que;
    zt_s32 i;

    WLAN_MGMT_DBG();

    zt_que_init(&pscan_que->free, ZT_LOCK_TYPE_NONE);
    zt_que_init(&pscan_que->ready, ZT_LOCK_TYPE_NONE);
    for (i = 0; i < WLAN_MGMT_SCAN_QUE_DEEP; i++)
    {
        zt_wlan_mgmt_scan_que_node_t *pnode
            = zt_kzalloc(sizeof(zt_wlan_mgmt_scan_que_node_t));
        if (pnode == NULL)
        {
            WLAN_MGMT_ERROR("zt_kzalloc failed");
            return -1;
        }
        zt_enque_head(&pnode->list, &pscan_que->free);
    }
    zt_os_api_lock_init(&pscan_que->lock, ZT_LOCK_TYPE_IRQ);
    pscan_que->read_cnt = 0;
    zt_os_api_sema_init(&pscan_que->sema, 1);

    return 0;
}

static zt_s32 wlan_mgmt_scan_que_deinit(nic_info_st *pnic_info)
{
    zt_wlan_mgmt_info_t *pwlan_mgmt_info = pnic_info->wlan_mgmt_info;
    zt_wlan_mgmt_scan_que_t *pscan_que = &pwlan_mgmt_info->scan_que;

    WLAN_MGMT_DBG();

    while (zt_true)
    {
        zt_que_list_t *pque_list = zt_deque_head(&pscan_que->free);
        if (pque_list == NULL)
        {
            break;
        }
        zt_kfree(zt_list_entry(pque_list, zt_wlan_mgmt_scan_que_node_t, list));
    }
    zt_que_deinit(&pscan_que->free);

    while (zt_true)
    {
        zt_que_list_t *pque_list = zt_deque_head(&pscan_que->ready);
        if (pque_list == NULL)
        {
            break;
        }
        zt_kfree(zt_list_entry(pque_list, zt_wlan_mgmt_scan_que_node_t, list));
    }
    zt_que_deinit(&pscan_que->ready);

    zt_os_api_lock_term(&pscan_que->lock);
    zt_os_api_sema_free(&pscan_que->sema);

    return 0;
}

static zt_s32 wlan_mgmt_msg_init(zt_wlan_mgmt_info_t *pwlan_mgmt_info)
{
    zt_msg_que_t *pmsg_que = &pwlan_mgmt_info->msg_que;

    zt_msg_init(pmsg_que);
    return (zt_msg_alloc(pmsg_que, ZT_WLAN_MGMT_TAG_BEACON_FRAME,
                         ZT_OFFSETOF(rx_frm_msg_t, mgmt_frm) +
                         ZT_80211_MGMT_BEACON_SIZE_MAX, 8) ||
            zt_msg_alloc(pmsg_que, ZT_WLAN_MGMT_TAG_PROBERSP_FRAME,
                         ZT_OFFSETOF(rx_frm_msg_t, mgmt_frm) +
                         ZT_80211_MGMT_PROBERSP_SIZE_MAX, 8) ||
            zt_msg_alloc(pmsg_que, ZT_WLAN_MGMT_TAG_PROBEREQ_P2P,
                         ZT_OFFSETOF(rx_frm_msg_t, mgmt_frm) +
                         ZT_80211_MGMT_BEACON_SIZE_MAX, 8) ||
            zt_msg_alloc(pmsg_que, ZT_WLAN_MGMT_TAG_ACTION,
                         ZT_OFFSETOF(rx_frm_msg_t, mgmt_frm) +
                         ZT_80211_MGMT_BEACON_SIZE_MAX, 8) ||
            zt_msg_alloc(pmsg_que, ZT_WLAN_MGMT_TAG_SCAN_QUE_FLUSH, 0, 1) ||
            zt_msg_alloc(pmsg_que, ZT_WLAN_MGMT_TAG_UNINSTALL, 0, 1) ||
            zt_msg_alloc(pmsg_que, ZT_WLAN_MGMT_TAG_SCAN_QUE_REFRESH,
                         sizeof(scan_que_refresh_msg_t), 2) ||
            zt_msg_alloc(pmsg_que, ZT_WLAN_MGMT_TAG_CHIPRESET,
                         sizeof(chip_reset_msg_t), 2)) ? -1 : 0;
}

zt_inline static void wlan_mgmt_msg_deinit(zt_wlan_mgmt_info_t *pwlan_mgmt_info)
{
    zt_msg_deinit(&pwlan_mgmt_info->msg_que);
}

zt_s32 zt_wlan_mgmt_send_msg(nic_info_st *pnic_info, zt_msg_tag_t tag)
{
    zt_wlan_mgmt_info_t *pwlan_mgmt_info = pnic_info->wlan_mgmt_info;
    zt_msg_que_t *pmsg_que = &pwlan_mgmt_info->msg_que;
    zt_msg_t *pmsg;
    zt_s32 rst;

    /* new message entity */
    rst = zt_msg_new(pmsg_que, tag, &pmsg);
    if (rst)
    {
        WLAN_MGMT_WARN("msg new fail error code: %d", rst);
        return -1;
    }

    /* new message entity */
    rst = zt_msg_push(pmsg_que, pmsg);
    if (rst)
    {
        WLAN_MGMT_WARN("zt_msg_push fail error code: %d", rst);
        return -2;
    }

    return 0;
}

zt_s32 zt_wlan_mgmt_scan_que_refresh(nic_info_st *pnic_info,
                                     zt_u8 *pch, zt_u8 ch_num)
{
    zt_wlan_mgmt_info_t *pwlan_mgmt_info = pnic_info->wlan_mgmt_info;
    zt_msg_que_t *pmsg_que = &pwlan_mgmt_info->msg_que;
    zt_msg_t *pmsg;
    zt_s32 rst;

    if (pnic_info == NULL || pch == NULL)
    {
        WLAN_MGMT_ERROR("null point");
        return -1;
    }

    if (ch_num == 0 || ch_num > ZT_FIELD_SIZEOF(scan_que_refresh_msg_t, ch_map))
    {
        WLAN_MGMT_WARN("invalid channel number %d", ch_num);
        return -2;
    }

    /* new message entity */
    rst = zt_msg_new(pmsg_que, ZT_WLAN_MGMT_TAG_SCAN_QUE_REFRESH, &pmsg);
    if (rst)
    {
        WLAN_MGMT_WARN("refresh msg new fail error code: %d", rst);
        return -3;
    }

    /* load value */
    if (sizeof(scan_que_refresh_msg_t) > pmsg->alloc_value_size)
    {
        WLAN_MGMT_ERROR("msg->value length(%d) error", pmsg->alloc_value_size);
        return -4;
    }
    {
        scan_que_refresh_msg_t *ptr = (void *)pmsg->value;
        ptr->ch_num = ch_num;
        zt_memcpy(ptr->ch_map, pch, ch_num);
    }

    /* new message entity */
    rst = zt_msg_push(pmsg_que, pmsg);
    if (rst)
    {
        WLAN_MGMT_WARN("zt_msg_push fail error code: %d", rst);
        return -5;
    }

    return 0;
}

zt_s32 zt_wlan_mgmt_chip_reset(nic_info_st *pnic_info, zt_u8 cmd)
{
    zt_wlan_mgmt_info_t *pwlan_mgmt_info = pnic_info->wlan_mgmt_info;
    zt_msg_que_t *pmsg_que = &pwlan_mgmt_info->msg_que;
    zt_msg_t *pmsg;
    zt_s32 rst;

    if (pnic_info == NULL)
    {
        WLAN_MGMT_ERROR("null point");
        return -1;
    }

    /* new message entity */
    rst = zt_msg_new(pmsg_que, ZT_WLAN_MGMT_TAG_CHIPRESET, &pmsg);
    if (rst)
    {
        WLAN_MGMT_WARN("reset msg new fail error code: %d", rst);
        return -2;
    }

    /* load value */
    if (sizeof(chip_reset_msg_t) > pmsg->alloc_value_size)
    {
        WLAN_MGMT_ERROR("msg->value length(%d) error", pmsg->alloc_value_size);
        return -3;
    }
    {
        chip_reset_msg_t *ptr = (void *)pmsg->value;
        ptr->cmd = cmd;
    }

    /* new message entity */
    rst = zt_msg_push(pmsg_que, pmsg);
    if (rst)
    {
        WLAN_MGMT_WARN("zt_msg_push fail error code: %d", rst);
        return -4;
    }

    return 0;
}

zt_s32 zt_wlan_mgmt_init(nic_info_st *pnic_info)
{
    zt_wlan_mgmt_info_t *pwlan_mgmt_info;
    zt_s32 rst;

    WLAN_MGMT_INFO("ndev_id:%d", pnic_info->ndev_id);

    pwlan_mgmt_info = zt_kzalloc(sizeof(zt_wlan_mgmt_info_t));
    if (pwlan_mgmt_info == NULL)
    {
        WLAN_MGMT_ERROR("zt_kzalloc failed");
        return -1;
    }
    pnic_info->wlan_mgmt_info = pwlan_mgmt_info;

    rst = wlan_mgmt_scan_que_init(pnic_info);
    if (rst)
    {
        WLAN_MGMT_ERROR("scan queue initilize fail, error code: %d", rst);
        return -2;
    }

    rst = wlan_mgmt_msg_init(pwlan_mgmt_info);
    if (rst)
    {
        WLAN_MGMT_ERROR("message queue initilize fail, error code: %d", rst);
        return -3;
    }

    /* create thread for rx frame handle */
    zt_sprintf(pwlan_mgmt_info->name, "wlan_mgmt_%d%d",
               pnic_info->hif_node_id, pnic_info->ndev_id);
    pwlan_mgmt_info->tid =
        zt_os_api_thread_create(&pwlan_mgmt_info->tid, pwlan_mgmt_info->name,
                                (void *)rx_frame_handle, pnic_info);
    if (pwlan_mgmt_info->tid == NULL)
    {
        WLAN_MGMT_ERROR("create thread failed");
        return -4;
    }
    zt_os_api_thread_wakeup(pwlan_mgmt_info->tid);

    return 0;
}

zt_s32 zt_wlan_mgmt_term(nic_info_st *pnic_info)
{
    zt_wlan_mgmt_info_t *pwlan_mgmt_info = pnic_info->wlan_mgmt_info;

    if (pwlan_mgmt_info == NULL)
    {
        WLAN_MGMT_ERROR("null point");
        return 0;
    }

    WLAN_MGMT_INFO();

    {
        zt_msg_que_t *pmsg_que = &pwlan_mgmt_info->msg_que;
        zt_msg_t *pnew_msg;
        zt_s32 rst;

        rst = zt_msg_new(pmsg_que, ZT_WLAN_MGMT_TAG_UNINSTALL, &pnew_msg);
        if (rst)
        {
            WLAN_MGMT_ERROR("new message fail, error code: %d", rst);
        }
        zt_msg_push(pmsg_que, pnew_msg);
    }

    /* destory thread */
    if (pwlan_mgmt_info->tid)
    {
        zt_os_api_thread_destory(pwlan_mgmt_info->tid);
        pwlan_mgmt_info->tid = 0;
    }

    /* free wlan info */
    wlan_mgmt_scan_que_deinit(pnic_info);
    wlan_mgmt_msg_deinit(pwlan_mgmt_info);
    zt_kfree(pwlan_mgmt_info);
    pnic_info->wlan_mgmt_info = NULL;

    return 0;
}

void zt_wlan_set_cur_ssid(nic_info_st *pnic_info, zt_wlan_ssid_t *pssid)
{
    zt_wlan_mgmt_info_t *pwlan_mgmt_info = pnic_info->wlan_mgmt_info;

    zt_memcpy(&pwlan_mgmt_info->cur_network.ssid, pssid, sizeof(zt_wlan_ssid_t));
    pwlan_mgmt_info->cur_network.ssid.data[pwlan_mgmt_info->cur_network.ssid.length]
        = '\0';
}

zt_wlan_ssid_t *zt_wlan_get_cur_ssid(nic_info_st *pnic_info)
{
    zt_wlan_mgmt_info_t *pwlan_mgmt_info = pnic_info->wlan_mgmt_info;

    return &pwlan_mgmt_info->cur_network.ssid;
}

void zt_wlan_set_cur_bssid(nic_info_st *pnic_info, zt_u8 *bssid)
{
    zt_wlan_mgmt_info_t *pwlan_mgmt_info = pnic_info->wlan_mgmt_info;

    zt_memcpy(pwlan_mgmt_info->cur_network.bssid, bssid, ZT_80211_MAC_ADDR_LEN);
}


zt_inline zt_u8 *zt_wlan_get_cur_bssid(nic_info_st *pnic_info)
{
    zt_wlan_mgmt_info_t *pwlan_mgmt_info = pnic_info->wlan_mgmt_info;

    return (zt_u8 *)pwlan_mgmt_info->cur_network.bssid;
}


void zt_wlan_set_cur_channel(nic_info_st *pnic_info, zt_u8 channel)
{
    zt_wlan_mgmt_info_t *pwlan_mgmt_info = pnic_info->wlan_mgmt_info;

    pwlan_mgmt_info->cur_network.channel = channel;
}


zt_u8 zt_wlan_get_cur_channel(nic_info_st *pnic_info)
{
    zt_wlan_mgmt_info_t *pwlan_mgmt_info = pnic_info->wlan_mgmt_info;

    return pwlan_mgmt_info->cur_network.channel;
}

void zt_wlan_set_cur_bw(nic_info_st *pnic_info, CHANNEL_WIDTH bw)
{
    zt_wlan_mgmt_info_t *pwlan_mgmt_info = pnic_info->wlan_mgmt_info;

    pwlan_mgmt_info->cur_network.bw = bw;
}

CHANNEL_WIDTH zt_wlan_get_cur_bw(nic_info_st *pnic_info)
{
    zt_wlan_mgmt_info_t *pwlan_mgmt_info = pnic_info->wlan_mgmt_info;

    return pwlan_mgmt_info->cur_network.bw;
}

static zt_u16 mcs_rate_func(zt_u8 bw_40MHz, zt_u8 short_GI, zt_u8 *MCS_rate)
{
    zt_u16 max_rate = 0;

    if (MCS_rate[0] & ZT_BIT(7))
    {
        max_rate = (bw_40MHz) ? ((short_GI) ? 1500 : 1350) : ((short_GI) ? 722 : 650);
    }
    else if (MCS_rate[0] & ZT_BIT(6))
    {
        max_rate = (bw_40MHz) ? ((short_GI) ? 1350 : 1215) : ((short_GI) ? 650 : 585);
    }
    else if (MCS_rate[0] & ZT_BIT(5))
    {
        max_rate = (bw_40MHz) ? ((short_GI) ? 1200 : 1080) : ((short_GI) ? 578 : 520);
    }
    else if (MCS_rate[0] & ZT_BIT(4))
    {
        max_rate = (bw_40MHz) ? ((short_GI) ? 900 : 810) : ((short_GI) ? 433 : 390);
    }
    else if (MCS_rate[0] & ZT_BIT(3))
    {
        max_rate = (bw_40MHz) ? ((short_GI) ? 600 : 540) : ((short_GI) ? 289 : 260);
    }
    else if (MCS_rate[0] & ZT_BIT(2))
    {
        max_rate = (bw_40MHz) ? ((short_GI) ? 450 : 405) : ((short_GI) ? 217 : 195);
    }
    else if (MCS_rate[0] & ZT_BIT(1))
    {
        max_rate = (bw_40MHz) ? ((short_GI) ? 300 : 270) : ((short_GI) ? 144 : 130);
    }
    else if (MCS_rate[0] & ZT_BIT(0))
    {
        max_rate = (bw_40MHz) ? ((short_GI) ? 150 : 135) : ((short_GI) ? 72 : 65);
    }

    return max_rate;
}

zt_s32 zt_wlan_get_max_rate(nic_info_st *pnic_info, zt_u8 *mac,
                            zt_u16 *max_rate)
{
    zt_u16 rate = 0;
    zt_u8 short_GI = 0;
    zt_s32 i = 0;
    wdn_net_info_st *pwdn_info = NULL;

    pwdn_info = zt_wdn_find_info(pnic_info, mac);
    if (NULL == pwdn_info)
    {
        return -1;
    }

    short_GI = zt_ra_sGI_get(pwdn_info, 1);
    if ((pwdn_info->network_type) & (WIRELESS_11_24N))
    {
        *max_rate = mcs_rate_func(((pwdn_info->bw_mode == CHANNEL_WIDTH_40) ? 1 : 0),
                                  short_GI, pwdn_info->datarate);
    }
    else
    {
        for (i = 0; i < pwdn_info->datarate_len; i++)
        {
            rate = pwdn_info->datarate[i] & 0x7F;
            if (rate > *max_rate)
            {
                *max_rate = rate;
            }
        }

        for (i = 0; i < pwdn_info->ext_datarate_len; i++)
        {
            rate = pwdn_info->ext_datarate[i] & 0x7F;
            if (rate > *max_rate)
            {
                *max_rate = rate;
            }
        }

        *max_rate = *max_rate * 10 / 2;
    }

    return 0;
}


zt_s32 zt_wlan_get_signal_and_qual(nic_info_st *pnic_info, zt_u8 *qual,
                                   zt_u8 *level)
{
    zt_wlan_mgmt_info_t *pwlan_mgmt_info = (zt_wlan_mgmt_info_t *)
                                           pnic_info->wlan_mgmt_info;
    zt_wlan_mgmt_scan_que_node_t *pscan_que_node = NULL;
    zt_wlan_mgmt_scan_que_for_rst_e scan_que_for_rst =
        ZT_WLAN_MGMT_SCAN_QUE_FOR_RST_FAIL;

    zt_wlan_mgmt_scan_que_for_begin(pnic_info, pscan_que_node)
    {
        if (!zt_memcmp(pscan_que_node->bssid, pwlan_mgmt_info->cur_network.bssid,
                       ZT_80211_MAC_ADDR_LEN))
        {
            *qual = pscan_que_node->signal_qual;
            *level = pscan_que_node->signal_strength;
            break;
        }
    }
    zt_wlan_mgmt_scan_que_for_end(scan_que_for_rst);

    return scan_que_for_rst;
}

