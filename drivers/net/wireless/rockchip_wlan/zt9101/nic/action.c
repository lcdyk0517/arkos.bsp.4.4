/*
 * action.c
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

#include "common.h"

/* macro */
#define ACTION_DBG(fmt, ...)        LOG_D("[%s:%d]"fmt, __func__, __LINE__, ##__VA_ARGS__)
#define ACTION_ARRAY(data, len)     zt_log_array(data, len)
#define ACTION_INFO(fmt, ...)       LOG_I("[%s:%d]"fmt, __func__, __LINE__, ##__VA_ARGS__)
#define ACTION_WARN(fmt, ...)       LOG_W("[%s:%d]"fmt, __func__, __LINE__, ##__VA_ARGS__)
#define ACTION_ERROR(fmt, ...)      LOG_E("[%s:%d]"fmt, __func__, __LINE__, ##__VA_ARGS__)

#define IEEE80211_ADDBA_PARAM_TID_MASK          0x003C
#define IEEE80211_ADDBA_PARAM_BUF_SIZE_MASK     0xFFC0
#define IEEE80211_DELBA_PARAM_TID_MASK          0xF000
#define IEEE80211_DELBA_PARAM_INITIATOR_MASK    0x0800

static
zt_u8 get_rx_ampdu_size(nic_info_st *nic_info)
{
    zt_u8 size          = 0;
    hw_info_st *hw_info = nic_info->hw_info;

    switch (hw_info->max_rx_ampdu_factor)
    {
        case MAX_AMPDU_FACTOR_64K:
            size = 64;
            break;
        case MAX_AMPDU_FACTOR_32K:
            size = 32;
            break;
        case MAX_AMPDU_FACTOR_16K:
            size = 16;
            break;
        case MAX_AMPDU_FACTOR_8K:
            size = 8;
            break;
        default:
            size = 64;
            break;
    }

    return size;
}

static
zt_s32 action_frame_add_ba_response(nic_info_st *nic_info,
                                    zt_add_ba_parm_st *barsp_parm)
{
    zt_s32 rst;

    barsp_parm->size = get_rx_ampdu_size(nic_info);
    rst = zt_mlme_add_ba_rsp(nic_info, barsp_parm);
    if (rst)
    {
        ACTION_WARN("zt_mlme_add_ba_rsp fail, error code: %d", rst);
        return -1;
    }

    return 0;
}

#ifdef CFG_ENABLE_AP_MODE
static
zt_s32 action_ba_req_work_ap(nic_info_st *pnic_info, wdn_net_info_st *pwdn_info, zt_add_ba_parm_st *barsp_parm)
{
    if (pwdn_info == NULL)
    {
        ACTION_DBG("wdn_info null");
        return -1;
    }

    ACTION_DBG("action ba request received");

    if (zt_ap_msg_load(pnic_info, &pwdn_info->ap_msg,
                       ZT_AP_MSG_TAG_BA_RSP_FRAME, barsp_parm, sizeof(zt_add_ba_parm_st)))
    {
        ACTION_WARN("action ba msg enque fail");
        return -3;
    }

    return 0;
}

#endif

static
zt_s32 action_frame_block_ack(nic_info_st *nic_info, wdn_net_info_st *pwdn_info,
                              zt_u8 *pkt, zt_u16 pkt_len)
{
    zt_80211_mgmt_t *pmgmt      = (zt_80211_mgmt_t *)pkt;
    zt_u16 status, tid          = 0;
    zt_u16 reason_code          = 0;
    zt_u8 *frame_body           = NULL;
    struct ADDBA_request *preq  = NULL;
    zt_u8 action                = 0;
    zt_u16 param                = 0;
    mlme_info_t *mlme_info      = nic_info->mlme_info;

    if (pkt_len == 0)
    {
        return -1;
    }

    frame_body = &pmgmt->action.variable[0];
    action = pmgmt->action.action_field;
    if (pwdn_info == NULL)
    {
        return -1;
    }

    switch (action)
    {
        case ZT_WLAN_ACTION_ADDBA_REQ:
        {
            zt_add_ba_parm_st parm;
            zt_add_ba_parm_st *barsp_parm = &parm;

            frame_body = &pmgmt->action.variable[0];
            preq = (struct ADDBA_request *)frame_body;
            barsp_parm->dialog = preq->dialog_token;
            param = zt_le16_to_cpu(preq->BA_para_set);
            barsp_parm->param = param;
            barsp_parm->tid = (param & 0x3c) >> 2;
            barsp_parm->policy = (param & 0x2) >> 1;
            barsp_parm->size = (zt_u8)(param & (~0xe03f)) >> 6;

            barsp_parm->timeout = zt_le16_to_cpu(preq->BA_timeout_value);
            barsp_parm->start_seq = zt_le16_to_cpu(preq->ba_starting_seqctrl) >> 4;
            barsp_parm->status = 0;

            ACTION_DBG("ZT_WLAN_ACTION_ADDBA_REQ TID:%d dialog:%d size:%d policy:%d start_req:%d timeout:%d",
                       barsp_parm->tid, barsp_parm->dialog, barsp_parm->size, barsp_parm->policy,
                       barsp_parm->start_seq, barsp_parm->timeout);

            if (zt_local_cfg_get_work_mode(nic_info) == ZT_INFRA_MODE)
            {
                action_frame_add_ba_response(nic_info, barsp_parm);
            }
#ifdef CFG_ENABLE_AP_MODE
            else if (zt_local_cfg_get_work_mode(nic_info) == ZT_MASTER_MODE)
            {
                barsp_parm->size = get_rx_ampdu_size(nic_info);
                action_ba_req_work_ap(nic_info, pwdn_info, barsp_parm);
            }
#endif

        }
        break;

        case ZT_WLAN_ACTION_ADDBA_RESP:
        {
            status = ZT_GET_LE16(&frame_body[1]);
            tid = ((frame_body[3] >> 2) & 0x7);
            if (status == 0)
            {
                pwdn_info->htpriv.mcu_ht.agg_enable_bitmap |= 1 << tid;
                pwdn_info->htpriv.mcu_ht.candidate_tid_bitmap &= ~(ZT_BIT(tid));

                if (frame_body[3] & 1)
                {
                    pwdn_info->htpriv.mcu_ht.tx_amsdu_enable = zt_true;
                }
            }
            else
            {
                pwdn_info->htpriv.mcu_ht.agg_enable_bitmap &= ~(ZT_BIT(tid));
            }

            mlme_info->baCreating = 0;

            ACTION_DBG("ZT_WLAN_ACTION_ADDBA_RESP status:%d tid:%d  (agg_enable_bitmap:%d candidate_tid_bitmap:%d)",
                       status, tid, pwdn_info->htpriv.mcu_ht.agg_enable_bitmap,
                       pwdn_info->htpriv.mcu_ht.candidate_tid_bitmap);

        }
        break;

        case ZT_WLAN_ACTION_DELBA:
        {
            if ((frame_body[0] & ZT_BIT(3)) == 0)
            {
                tid = (frame_body[0] >> 4) & 0x0F;

                pwdn_info->htpriv.mcu_ht.agg_enable_bitmap &= ~(1 << tid);
                pwdn_info->htpriv.mcu_ht.candidate_tid_bitmap &= ~(1 << tid);

                reason_code = ZT_GET_LE16(&frame_body[2]);
            }
            else if ((frame_body[0] & ZT_BIT(3)) == ZT_BIT(3))
            {
                tid = (frame_body[0] >> 4) & 0x0F;
            }

            ACTION_DBG("ZT_WLAN_ACTION_DELBA reason_code:%d tid:%d", reason_code, tid);
        }
        break;
        default:
            break;
    }

    return 0;
}

static
void action_frame_wlan_hdr(nic_info_st *pnic_info, struct xmit_buf *pxmit_buf)
{
    zt_u8 *pframe                       = NULL;
    struct wl_ieee80211_hdr *pwlanhdr   = NULL;

    if (pnic_info == NULL)
    {
        return;
    }

    //nic_unused_check(pnic_info);
    //ACTION_DBG("[action]%s",__func__);
    pframe = pxmit_buf->pbuf + TXDESC_OFFSET;
    pwlanhdr = (struct wl_ieee80211_hdr *)pframe;

    pwlanhdr->frame_ctl = 0;
    SetFrameType(pframe, WIFI_MGT_TYPE);
    SetFrameSubType(pframe, WIFI_ACTION);  /* set subtype */

}

#ifdef CFG_ENABLE_AP_MODE
zt_s32 zt_action_frame_ba_to_issue_ap(nic_info_st *nic_info, wdn_net_info_st *pwdn_info, zt_u8 action)
{
    zt_s32 rst                          = 0;
    zt_u8 *pframe                       = NULL;
    zt_u16 ba_para_set                  = 0;
    zt_u16 ba_timeout_value             = 0;
    zt_u16 ba_starting_seqctrl          = 0;
    zt_u16 start_seq                    = 0;
    struct wl_ieee80211_hdr *pwlanhdr   = NULL;
    struct xmit_buf *pxmit_buf          = NULL;
    zt_u16 pkt_len                      = 0;
    tx_info_st  *ptx_info               = NULL;
    mlme_info_t *mlme_info              = NULL;
    zt_u8 initiator                     = 0;
    zt_u8 category                      = ZT_WLAN_CATEGORY_BACK;
    zt_add_ba_parm_st *barsp_info       = NULL;
    zt_add_ba_parm_st *bareq_info       = NULL;

    ptx_info = (tx_info_st *)nic_info->tx_info;
    mlme_info = (mlme_info_t *)nic_info->mlme_info;
    barsp_info = &pwdn_info->barsp_parm;
    bareq_info = &pwdn_info->bareq_parm;

    /* alloc xmit_buf */
    pxmit_buf = zt_xmit_extbuf_new(ptx_info);
    if (pxmit_buf == NULL)
    {
        ACTION_ERROR("pxmit_buf is NULL");
        return -1;
    }
    zt_memset(pxmit_buf->pbuf, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

    action_frame_wlan_hdr(nic_info, pxmit_buf);

    pframe = pxmit_buf->pbuf + TXDESC_OFFSET;
    pwlanhdr = (struct wl_ieee80211_hdr *)pframe;

    zt_memcpy(pwlanhdr->addr1, pwdn_info->mac, ZT_80211_MAC_ADDR_LEN);
    zt_memcpy(pwlanhdr->addr2, nic_to_local_addr(nic_info), ZT_80211_MAC_ADDR_LEN);
    zt_memcpy(pwlanhdr->addr3, pwdn_info->bssid, ZT_80211_MAC_ADDR_LEN);

    pkt_len = sizeof(struct wl_ieee80211_hdr_3addr);
    pframe += pkt_len;

    pframe = set_fixed_ie(pframe, 1, &(category), &pkt_len);
    pframe = set_fixed_ie(pframe, 1, &(action), &pkt_len);

    switch (action)
    {

        case ZT_WLAN_ACTION_ADDBA_RESP:
        {
            pframe = set_fixed_ie(pframe, 1, &(barsp_info->dialog), &pkt_len);
            pframe = set_fixed_ie(pframe, 2, (zt_u8 *) & (barsp_info->status), &pkt_len);

            ba_para_set = barsp_info->param;
            ba_para_set &= ~IEEE80211_ADDBA_PARAM_TID_MASK;
            ba_para_set |= (barsp_info->tid << 2) & IEEE80211_ADDBA_PARAM_TID_MASK;
            ba_para_set &= ~IEEE80211_ADDBA_PARAM_BUF_SIZE_MASK;
            ba_para_set |= (barsp_info->size << 6) & IEEE80211_ADDBA_PARAM_BUF_SIZE_MASK;
            ba_para_set &= ~(ZT_BIT(0));
            ba_para_set = zt_cpu_to_le16(ba_para_set);

            pframe = set_fixed_ie(pframe, 2, (zt_u8 *)(&(ba_para_set)), &pkt_len);
            pframe = set_fixed_ie(pframe, 2, (zt_u8 *)(&(barsp_info->timeout)), &pkt_len);

            ACTION_INFO("tid:%d dialog:%d  ba_para_set:0x%x  timeout:%d  status:%d",
                        barsp_info->tid, barsp_info->dialog, ba_para_set,
                        barsp_info->timeout, barsp_info->status);
        }
        break;

        case ZT_WLAN_ACTION_ADDBA_REQ:
        {
            zt_u8 dialog;

            mlme_info->baCreating = 1;

            dialog = pwdn_info->dialogToken[bareq_info->tid] + 1;
            if (dialog > 7)
            {
                dialog = 1;
            }

            pwdn_info->dialogToken[bareq_info->tid] = dialog;
            pframe = set_fixed_ie(pframe, 1, &(dialog), &pkt_len);

            ba_para_set = (0x1002 | ((bareq_info->tid & 0xf) << 2));
            ba_para_set = zt_cpu_to_le16(ba_para_set);
            pframe = set_fixed_ie(pframe, 2, (zt_u8 *)(&(ba_para_set)), &pkt_len);

            ba_timeout_value = 5000;
            ba_timeout_value = zt_cpu_to_le16(ba_timeout_value);
            pframe = set_fixed_ie(pframe, 2, (zt_u8 *)(&(ba_timeout_value)), &pkt_len);

            if (pwdn_info != NULL)
            {
                start_seq = (pwdn_info->wdn_xmitpriv.txseq_tid[bareq_info->tid] & 0xfff) + 1;

                pwdn_info->ba_starting_seqctrl[bareq_info->tid] = start_seq;
                ba_starting_seqctrl = start_seq << 4;
            }

            ba_starting_seqctrl = zt_cpu_to_le16(ba_starting_seqctrl);
            pframe = set_fixed_ie(pframe, 2, (zt_u8 *)(&(ba_starting_seqctrl)),
                                  &pkt_len);
            ACTION_INFO("[action request] TID:%d  dialog:%d  ba_para_set:0x%x  start_req:%d",
                        bareq_info->tid, dialog, ba_para_set, start_seq);
        }
        break;

        case ZT_WLAN_ACTION_DELBA:
            ba_para_set = 0;
            ba_para_set |= (barsp_info->tid << 12) & IEEE80211_DELBA_PARAM_TID_MASK;
            ba_para_set |= (initiator << 11) & IEEE80211_DELBA_PARAM_INITIATOR_MASK;

            ba_para_set = zt_cpu_to_le16(ba_para_set);
            pframe = set_fixed_ie(pframe, 2, (zt_u8 *)(&(ba_para_set)), &pkt_len);
            barsp_info->status = zt_cpu_to_le16(barsp_info->status);
            pframe = set_fixed_ie(pframe, 2, (zt_u8 *)(&(barsp_info->status)),
                                  &pkt_len);

            ACTION_DBG("[action delete] reason:%d  ba_para_set:0x%x", barsp_info->status,
                       ba_para_set);
            break;
        default:
            break;

    }
    pxmit_buf->pkt_len = pkt_len;

    //rst = zt_nic_mgmt_frame_xmit_with_ack(nic_info, pwdn_info, pxmit_buf, pxmit_buf->pkt_len);
    rst = zt_nic_mgmt_frame_xmit(nic_info, pwdn_info, pxmit_buf,
                                 pxmit_buf->pkt_len);

    return rst;
}
#endif

zt_s32 proc_on_action_public_vendor_func(nic_info_st *pnic_info, zt_u8 *pkt,
        zt_u16 pkt_len)
{
    zt_s32 ret = 0;
    zt_u8 *frame_body = pkt + sizeof(struct wl_ieee80211_hdr_3addr);

    if ((pnic_info == NULL) || (pkt_len == 0))
    {
        return -1;
    }

    if (zt_memcmp(frame_body + 2, P2P_OUI, 4) == 0)
    {
        if (zt_p2p_is_valid(pnic_info))
        {
            ret = zt_p2p_recv_public_action(pnic_info, pkt, pkt_len);
        }
    }
    else
    {
        ACTION_DBG("OUI:");
        ACTION_ARRAY(frame_body + 2, 4);
    }

    return ret;
}

zt_s32 zt_action_frame_public(nic_info_st *nic_info, zt_u8 *pdata,
                              zt_u16 pkt_len)
{
    zt_80211_mgmt_t *pmgmt = (zt_80211_mgmt_t *)pdata;
    hw_info_st *hw_info = nic_info->hw_info;
    zt_u8 action;
    zt_u8 category;
    zt_s32 ret = 0;
    ACTION_DBG("[%d] "ZT_MAC_FMT, nic_info->ndev_id, ZT_MAC_ARG(hw_info->macAddr));

    category = pmgmt->action.action_category;
    action = pmgmt->action.action_field;

    if (zt_memcmp(pmgmt->da,  hw_info->macAddr, ZT_80211_MAC_ADDR_LEN))
    {
        ACTION_DBG("[%d] "ZT_MAC_FMT, nic_info->ndev_id, ZT_MAC_ARG(pmgmt->da));
        goto exit;
    }

    ACTION_DBG("[%d] category=%d,action=%d", nic_info->ndev_id, category, action);
    if (category != ZT_WLAN_CATEGORY_PUBLIC)
    {
        goto exit;
    }

    if (action == ZT_WLAN_ACTION_PUBLIC_VENDOR)
    {
        ret = proc_on_action_public_vendor_func(nic_info, pdata, pkt_len);
    }
exit:
    return ret;

}

zt_s32 zt_action_frame_parse(zt_u8 *frame, zt_u32 frame_len, zt_u8 *category,
                             zt_u8 *action)
{
    zt_u8 *frame_body = frame + sizeof(struct wl_ieee80211_hdr_3addr);
    zt_u16 fc;
    zt_u8 c;
    zt_u8 a = ZT_WLAN_ACTION_PUBLIC_MAX;

    fc = zt_le16_to_cpu(((zt_80211_hdr_3addr_t *)frame)->frame_control);

    if ((fc & (ZT_80211_FCTL_FTYPE | ZT_80211_FCTL_STYPE))
            != (ZT_80211_FTYPE_MGMT | ZT_80211_STYPE_ACTION))
    {
        return -1;
    }

    c = frame_body[0];
    switch (c)
    {
        case ZT_80211_CATEGORY_P2P:
            break;
        default:
            a = frame_body[1];
    }

    if (category)
    {
        *category = c;
    }
    if (action)
    {
        *action = a;
    }

    return 0;
}

static zt_s32 action_frame_p2p_proc(nic_info_st *pnic_info, zt_u8 *pframe,
                                    zt_u32 frame_len)
{
    if ((pnic_info == NULL) || (pframe == NULL) || (frame_len == 0))
    {
        return -1;
    }

    if (zt_p2p_is_valid(pnic_info))
    {
        zt_u8 *frame_body   = NULL;
        zt_u8 category      = 0;
        zt_u32 len          = frame_len;
        if (0 != zt_memcmp(nic_to_local_addr(pnic_info), GetAddr1Ptr(pframe),
                           ZT_80211_MAC_ADDR_LEN))
        {
            return ZT_RETURN_OK;
        }

        frame_body = (zt_u8 *)(pframe + sizeof(struct wl_ieee80211_hdr_3addr));

        category = frame_body[0];
        if (category != ZT_WLAN_CATEGORY_P2P)
        {
            return ZT_RETURN_OK;
        }

        if (zt_cpu_to_be32(*((zt_u32 *)(frame_body + 1))) != P2POUI)
        {
            return ZT_RETURN_OK;
        }

        return zt_p2p_rx_action_precess(pnic_info, pframe, len);
    }

    return ZT_RETURN_OK;

}

zt_s32 zt_action_frame_process(nic_info_st *nic_info,
                               zt_80211_mgmt_t *pmgmt, zt_u16 mgmt_len)
{
    zt_u8 category = 0;

    category = pmgmt->action.action_category;

    LOG_D("category = %d", category);
    switch (category)
    {
        case ZT_WLAN_CATEGORY_BACK:
        {
            hw_info_st *hw_info = nic_info->hw_info;
            wdn_net_info_st *pwdn_info = NULL;

            LOG_D("pmgmt->sa = "ZT_MAC_FMT, ZT_MAC_ARG(pmgmt->sa));
            /* retrive wdn_info */
            pwdn_info = zt_wdn_find_info(nic_info, pmgmt->sa);
            if (pwdn_info == NULL)
            {
                break;
            }

            /* for AP */
            if (zt_local_cfg_get_work_mode(nic_info) == ZT_MASTER_MODE)
            {
#ifdef CFG_ENABLE_AP_MODE
                if (ZT_AP_STATE_ESTABLISHED != zt_ap_status_get(nic_info))
                {
                    /* no connection for ap */
                    ACTION_ERROR("No connection has been established");
                    return -1;
                }
#else
                return -1;
#endif
            }
            else
            {
                zt_bool bconnect;
                zt_mlme_get_connect(nic_info, &bconnect);
                if (!bconnect)
                {
                    ACTION_WARN("No connection has been established");
                    return -2;
                }
            }

            if (hw_info->ba_enable_rx == zt_true)
            {
                action_frame_block_ack(nic_info, pwdn_info, (zt_u8 *)pmgmt, mgmt_len);
            }
        }
        break;
        case ZT_WLAN_CATEGORY_SPECTRUM_MGMT:
        {
            //zt_action_frame_spectrum(nic_info, pkt, pkt_len);
        }
        break;
        case ZT_WLAN_CATEGORY_PUBLIC:
        {
            zt_action_frame_public(nic_info, (zt_u8 *)pmgmt, mgmt_len);
        }
        break;
        case ZT_WLAN_CATEGORY_HT:
        {
            //zt_action_frame_ht(nic_info, pkt, pkt_len);
        }
        break;
        case ZT_WLAN_CATEGORY_SA_QUERY:
        {

        }
        break;
        case ZT_WLAN_CATEGORY_P2P:
        {
            action_frame_p2p_proc(nic_info, (zt_u8 *)pmgmt, mgmt_len);
        }
        break;
        default:
        {

        }
        break;
    }

    return 0;

}

zt_s32 zt_action_frame_ba_to_issue(nic_info_st *nic_info, zt_u8 action)
{
    zt_s32 rst                          = 0;
    zt_u8 *pframe                       = NULL;
    zt_u16 ba_para_set                  = 0;
    zt_u16 ba_timeout_value             = 0;
    zt_u16 ba_starting_seqctrl          = 0;
    zt_u16 start_seq                    = 0;
    struct wl_ieee80211_hdr *pwlanhdr   = NULL;
    struct xmit_buf *pxmit_buf          = NULL;
    zt_u16 pkt_len                      = 0;
    tx_info_st  *ptx_info               = NULL;
    wdn_net_info_st *pwdn_info          = NULL;
    mlme_info_t *mlme_info              = NULL;
    zt_u8 initiator                     = 0;
    zt_u8 category                      = ZT_WLAN_CATEGORY_BACK;
    zt_add_ba_parm_st *barsp_info       = NULL;
    zt_add_ba_parm_st *bareq_info       = NULL;

    pwdn_info = zt_wdn_find_info(nic_info, zt_wlan_get_cur_bssid(nic_info));
    if (pwdn_info == NULL)
    {
        return -1;
    }

    ptx_info = (tx_info_st *)nic_info->tx_info;
    mlme_info = (mlme_info_t *)nic_info->mlme_info;
    barsp_info = &mlme_info->barsp_parm;
    bareq_info = &mlme_info->bareq_parm;

    /* alloc xmit_buf */
    pxmit_buf = zt_xmit_extbuf_new(ptx_info);
    if (pxmit_buf == NULL)
    {
        ACTION_ERROR("pxmit_buf is NULL");
        return -1;
    }
    zt_memset(pxmit_buf->pbuf, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

    action_frame_wlan_hdr(nic_info, pxmit_buf);

    pframe = pxmit_buf->pbuf + TXDESC_OFFSET;
    pwlanhdr = (struct wl_ieee80211_hdr *)pframe;

    zt_memcpy(pwlanhdr->addr1, pwdn_info->mac, ZT_80211_MAC_ADDR_LEN);
    zt_memcpy(pwlanhdr->addr2, nic_to_local_addr(nic_info), ZT_80211_MAC_ADDR_LEN);
    zt_memcpy(pwlanhdr->addr3, pwdn_info->bssid, ZT_80211_MAC_ADDR_LEN);

    pkt_len = sizeof(struct wl_ieee80211_hdr_3addr);
    pframe += pkt_len;

    pframe = set_fixed_ie(pframe, 1, &(category), &pkt_len);
    pframe = set_fixed_ie(pframe, 1, &(action), &pkt_len);

    switch (action)
    {

        case ZT_WLAN_ACTION_ADDBA_RESP:
        {
            pframe = set_fixed_ie(pframe, 1, &(barsp_info->dialog), &pkt_len);
            pframe = set_fixed_ie(pframe, 2, (zt_u8 *) & (barsp_info->status), &pkt_len);

            ba_para_set = barsp_info->param;
            ba_para_set &= ~IEEE80211_ADDBA_PARAM_TID_MASK;
            ba_para_set |= (barsp_info->tid << 2) & IEEE80211_ADDBA_PARAM_TID_MASK;
            ba_para_set &= ~IEEE80211_ADDBA_PARAM_BUF_SIZE_MASK;
            ba_para_set |= (barsp_info->size << 6) & IEEE80211_ADDBA_PARAM_BUF_SIZE_MASK;
            ba_para_set &= ~(ZT_BIT(0));
            ba_para_set = zt_cpu_to_le16(ba_para_set);

            pframe = set_fixed_ie(pframe, 2, (zt_u8 *)(&(ba_para_set)), &pkt_len);
            pframe = set_fixed_ie(pframe, 2, (zt_u8 *)(&(barsp_info->timeout)), &pkt_len);

            ACTION_INFO("tid:%d dialog:%d  ba_para_set:0x%x  timeout:%d  status:%d",
                        barsp_info->tid, barsp_info->dialog, ba_para_set,
                        barsp_info->timeout, barsp_info->status);
        }
        break;

        case ZT_WLAN_ACTION_ADDBA_REQ:
        {
            zt_u8 dialog;

            mlme_info->baCreating = 1;

            dialog = pwdn_info->dialogToken[bareq_info->tid] + 1;
            if (dialog > 7)
            {
                dialog = 1;
            }

            pwdn_info->dialogToken[bareq_info->tid] = dialog;
            pframe = set_fixed_ie(pframe, 1, &(dialog), &pkt_len);

            ba_para_set = (0x1002 | ((bareq_info->tid & 0xf) << 2));
            ba_para_set = zt_cpu_to_le16(ba_para_set);
            pframe = set_fixed_ie(pframe, 2, (zt_u8 *)(&(ba_para_set)), &pkt_len);

            ba_timeout_value = 5000;
            ba_timeout_value = zt_cpu_to_le16(ba_timeout_value);
            pframe = set_fixed_ie(pframe, 2, (zt_u8 *)(&(ba_timeout_value)), &pkt_len);

            if (pwdn_info != NULL)
            {
                start_seq = (pwdn_info->wdn_xmitpriv.txseq_tid[bareq_info->tid] & 0xfff) + 1;

                pwdn_info->ba_starting_seqctrl[bareq_info->tid] = start_seq;
                ba_starting_seqctrl = start_seq << 4;
            }

            ba_starting_seqctrl = zt_cpu_to_le16(ba_starting_seqctrl);
            pframe = set_fixed_ie(pframe, 2, (zt_u8 *)(&(ba_starting_seqctrl)),
                                  &pkt_len);
            ACTION_INFO("[action request] TID:%d  dialog:%d  ba_para_set:0x%x  start_req:%d",
                        bareq_info->tid, dialog, ba_para_set, start_seq);
        }
        break;

        case ZT_WLAN_ACTION_DELBA:
            ba_para_set = 0;
            ba_para_set |= (barsp_info->tid << 12) & IEEE80211_DELBA_PARAM_TID_MASK;
            ba_para_set |= (initiator << 11) & IEEE80211_DELBA_PARAM_INITIATOR_MASK;

            ba_para_set = zt_cpu_to_le16(ba_para_set);
            pframe = set_fixed_ie(pframe, 2, (zt_u8 *)(&(ba_para_set)), &pkt_len);
            barsp_info->status = zt_cpu_to_le16(barsp_info->status);
            pframe = set_fixed_ie(pframe, 2, (zt_u8 *)(&(barsp_info->status)),
                                  &pkt_len);

            ACTION_DBG("[action delete] reason:%d  ba_para_set:0x%x", barsp_info->status,
                       ba_para_set);
            break;
        default:
            break;

    }
    pxmit_buf->pkt_len = pkt_len;

    //rst = zt_nic_mgmt_frame_xmit_with_ack(nic_info, pwdn_info, pxmit_buf, pxmit_buf->pkt_len);
    rst = zt_nic_mgmt_frame_xmit(nic_info, pwdn_info, pxmit_buf,
                                 pxmit_buf->pkt_len);

    return rst;
}

zt_s32 zt_action_frame_add_ba_request(nic_info_st *nic_info,
                                      struct xmit_frame *pxmitframe)
{
    zt_u8 issued                = 0;
    mlme_info_t *mlme_info      = NULL;
    wdn_net_info_st *pwdn_info  = NULL;

    if (pxmitframe->bmcast)
    {
        return -1;
    }

    mlme_info = (mlme_info_t *)nic_info->mlme_info;

    pwdn_info = pxmitframe->pwdn;
    if (pwdn_info == NULL)
    {
        return -1;
    }

    if (pwdn_info->ba_enable_flag[pxmitframe->priority] == zt_true)
    {
        return -1;
    }

    if (pwdn_info->htpriv.mcu_ht.ampdu_enable == zt_true)
    {
        issued = (pwdn_info->htpriv.mcu_ht.agg_enable_bitmap >> pxmitframe->priority) & 0x1;
        issued |= (pwdn_info->htpriv.mcu_ht.candidate_tid_bitmap >> pxmitframe->priority) & 0x1;
        if (issued == 0)
        {
            if ((pxmitframe->frame_tag == DATA_FRAMETAG) && (pxmitframe->ether_type != ZT_ETH_P_ARP) &&
                    (pxmitframe->ether_type != ZT_ETH_P_EAPOL) && (pxmitframe->dhcp_pkt != 1))
            {
                pwdn_info->htpriv.mcu_ht.candidate_tid_bitmap |= ZT_BIT(pxmitframe->priority);
                mlme_info->bareq_parm.tid = pxmitframe->priority;
                pwdn_info->ba_enable_flag[pxmitframe->priority] = zt_true;
                if (zt_local_cfg_get_work_mode(nic_info) == ZT_MASTER_MODE)
                {
                    zt_ap_add_ba_req(nic_info, pwdn_info);
                }
                else
                {
			        zt_mlme_add_ba_req(nic_info);
                }
                return 0;
            }
        }
    }

    return -1;
}

zt_s32 zt_action_frame_del_ba_request(nic_info_st *nic_info, zt_u8 *addr)
{
    wdn_net_info_st *wdn_net_info   = NULL;
    mlme_info_t *mlme_info          = (mlme_info_t *)nic_info->mlme_info;

    wdn_net_info  = zt_wdn_find_info(nic_info, addr);
    if (NULL != wdn_net_info)
    {
        if (zt_false == zt_wdn_is_alive(wdn_net_info, 1))
        {
            zt_u32 tid;
            for (tid = 0; tid < TID_NUM; tid++)
            {
                if (wdn_net_info->ba_started_flag[tid] == 1)
                {
                    mlme_info->barsp_parm.tid = tid;
                    zt_action_frame_ba_to_issue(nic_info, ZT_WLAN_ACTION_DELBA);

                    wdn_net_info->ba_started_flag[tid] = zt_false;
                    wdn_net_info->ba_enable_flag[tid]  = zt_false;
                }
            }
            wdn_net_info->htpriv.mcu_ht.agg_enable_bitmap = 0;
            wdn_net_info->htpriv.mcu_ht.candidate_tid_bitmap = 0;
        }
    }

    return 0;
}


