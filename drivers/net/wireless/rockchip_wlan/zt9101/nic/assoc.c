/*
 * assoc.c
 *
 * impliment of IEEE80211 management frame association stage processing
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

/* macro */
#define ASSOC_DBG(fmt, ...)         LOG_D("[%s:%d]"fmt, __func__, __LINE__, ##__VA_ARGS__)
#define ASSOC_ARRAY(data, len)      zt_log_array(data, len)
#define ASSOC_INFO(fmt, ...)        LOG_I("[%s:%d]"fmt, __func__, __LINE__, ##__VA_ARGS__)
#define ASSOC_WARN(fmt, ...)        LOG_W("[%s:%d]"fmt, __func__, __LINE__, ##__VA_ARGS__)
#define ASSOC_ERROR(fmt, ...)       LOG_E("[%s:%d]"fmt, __func__, __LINE__, ##__VA_ARGS__)

#define ASSOC_REQ_RESEND_TIMES      3
#define ASSOC_RSP_TIMEOUT           300

/* function declaration */

#ifdef CFG_ENABLE_AP_MODE
static void disassoc_wlan_hdr(nic_info_st *pnic_info,
                              struct xmit_buf *pxmit_buf)
{
    zt_u8 *pframe;
    struct wl_ieee80211_hdr *pwlanhdr;

    pframe = pxmit_buf->pbuf + TXDESC_OFFSET;
    pwlanhdr = (struct wl_ieee80211_hdr *)pframe;

    pwlanhdr->frame_ctl = 0;
    SetFrameType(pframe, WIFI_MGT_TYPE);
    SetFrameSubType(pframe, WIFI_DISASSOC);  /* set subtype */
}

static zt_s32 disassoc_xmit_frame(nic_info_st *pnic_info,
                                  zt_u8 *pmac, zt_u16 reason_code)
{
    zt_u8 *pframe;
    struct wl_ieee80211_hdr *pwlanhdr;
    struct xmit_buf *pxmit_buf;
    zt_u16 pkt_len;
    tx_info_st      *ptx_info;
    wdn_net_info_st *pwdn_info;

    ptx_info = (tx_info_st *)pnic_info->tx_info;

    pwdn_info = zt_wdn_find_info(pnic_info, pmac);
    if (pwdn_info == NULL)
    {
        ASSOC_ERROR("wdn is NULL");
        return -1;
    }

    /* alloc xmit_buf */
    pxmit_buf = zt_xmit_extbuf_new(ptx_info);
    if (pxmit_buf == NULL)
    {
        ASSOC_ERROR("pxmit_buf is NULL");
        return -1;
    }
    zt_memset(pxmit_buf->pbuf, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

    /* type of management is 1010 */
    disassoc_wlan_hdr(pnic_info, pxmit_buf);

    /* set txd at tx module */
    pframe = pxmit_buf->pbuf + TXDESC_OFFSET; /* pframe point to wlan_hdr */
    pwlanhdr = (struct wl_ieee80211_hdr *)pframe;

    pkt_len = sizeof(struct wl_ieee80211_hdr_3addr);
    pframe += pkt_len; /* point to iv or frame body */

    zt_memcpy(pwlanhdr->addr1, pwdn_info->mac, ZT_80211_MAC_ADDR_LEN);
    zt_memcpy(pwlanhdr->addr2, nic_to_local_addr(pnic_info), ZT_80211_MAC_ADDR_LEN);
    zt_memcpy(pwlanhdr->addr3, zt_wlan_get_cur_bssid(pnic_info),
              ZT_80211_MAC_ADDR_LEN);
    /*1.add reason_code*/
    pframe = set_fixed_ie(pframe, 2, (zt_u8 *)&reason_code, &pkt_len);

    pxmit_buf->pkt_len = pkt_len;
    zt_nic_mgmt_frame_xmit(pnic_info, pwdn_info, pxmit_buf, pxmit_buf->pkt_len);

    return 0;
}

static zt_s32 disassoc_work_ap(nic_info_st *pnic_info,
                               wdn_net_info_st *pwdn_info,
                               zt_80211_mgmt_t *pmgmt, zt_u16 mgmt_len)
{
    if (pwdn_info == NULL)
    {
        ASSOC_DBG("wdn_info null");
        return -1;
    }

    if (pwdn_info->mode != ZT_MASTER_MODE)
    {
        return -2;
    }

    if (mgmt_len > ZT_80211_MGMT_DISASSOC_SIZE_MAX)
    {
        ASSOC_ERROR("deauth frame length too long");
        return -3;
    }

    if (zt_80211_hdr_type_get(pmgmt) != ZT_80211_FRM_DISASSOC)
    {
        ASSOC_WARN("disassoc frame type error");
        return -4;
    }

    ASSOC_DBG("disassoc received");

    if (zt_ap_msg_load(pnic_info, &pwdn_info->ap_msg,
                       ZT_AP_MSG_TAG_DISASSOC_FRAME, pmgmt, mgmt_len))
    {
        ASSOC_WARN("disassoc msg enque fail");
        return -5;
    }

    return 0;
}

static
zt_s32 assoc_ap_xmit_frame(nic_info_st *pnic_info, wdn_net_info_st *pwdn_info,
                           zt_80211_frame_e type, zt_80211_statuscode_e status)
{
    tx_info_st *ptx_info = pnic_info->tx_info;
    zt_wlan_mgmt_info_t *pwlan_info = pnic_info->wlan_mgmt_info;
    zt_wlan_network_t *pcur_network = &pwlan_info->cur_network;
    struct xmit_buf *pxmit_buf;
    zt_80211_mgmt_t *passoc_frame;
    zt_u16 tmp_16;
    zt_u8 *pvar;
    zt_u16 var_len = 0;
    zt_80211_mgmt_ie_t *pie;

    /* alloc xmit_buf */
    pxmit_buf = zt_xmit_extbuf_new(ptx_info);
    if (pxmit_buf == NULL)
    {
        ASSOC_ERROR("xmit_buf alloc fail");
        return -1;
    }
    zt_memset(pxmit_buf->pbuf, 0,
              TXDESC_OFFSET + ZT_OFFSETOF(zt_80211_mgmt_t, assoc_resp));

    /* set frame type */
    passoc_frame = (void *)&pxmit_buf->pbuf[TXDESC_OFFSET];
    if (type == ZT_80211_FRM_ASSOC_REQ)
    {
        type = ZT_80211_FRM_ASSOC_RESP;
    }
    else if (type == ZT_80211_FRM_REASSOC_REQ)
    {
        type = ZT_80211_FRM_REASSOC_RESP;
    }
    else
    {
        ASSOC_WARN("invalid frame type");
    }
    zt_80211_hdr_type_set(passoc_frame, type);

    /* set mac address */
    zt_memcpy(passoc_frame->da, pwdn_info->mac, sizeof(passoc_frame->da));
    zt_memcpy(passoc_frame->sa, pcur_network->mac_addr, sizeof(passoc_frame->da));
    zt_memcpy(passoc_frame->bssid, pcur_network->bssid,
              sizeof(passoc_frame->bssid));

    /*
     * set fiexd fields
     */

    /* set capability */
    passoc_frame->assoc_resp.capab_info = zt_cpu_to_le16(pcur_network->cap_info);

    /* set status code */
    passoc_frame->assoc_resp.status_code = zt_cpu_to_le16(status);

    /* set AID */
    tmp_16 = pwdn_info->aid | ZT_BIT(14) | ZT_BIT(15);
    passoc_frame->assoc_resp.aid = zt_cpu_to_le16(tmp_16);

    /*
     * set variable fields
     */
    pvar = passoc_frame->assoc_resp.variable;

    /* set support rate */
    pie = (void *)&pvar[var_len];
    pie->element_id = ZT_80211_MGMT_EID_SUPP_RATES;
    pie->len = ZT_80211_BASIC_RATE_NUM;
    zt_memcpy(pie->data, pwdn_info->datarate, pie->len);
    var_len += ZT_OFFSETOF(zt_80211_mgmt_ie_t, data) + pie->len;

    /* set extend support rate */
    if (pcur_network->rate_len > ZT_80211_BASIC_RATE_NUM)
    {
        pie = (void *)&pvar[var_len];
        pie->element_id = ZT_80211_MGMT_EID_EXT_SUPP_RATES;
        pie->len = pcur_network->rate_len - ZT_80211_BASIC_RATE_NUM;
        zt_memcpy(pie->data, pwdn_info->ext_datarate, pie->len);
        var_len += ZT_OFFSETOF(zt_80211_mgmt_ie_t, data) + pie->len;
    }

    /* set ht capability */
    if (pwdn_info->ht_enable)
    {
        pie = (void *)&pvar[var_len];
        pie->element_id = ZT_80211_MGMT_EID_HT_CAPABILITY;
        pie->len = sizeof(zt_80211_mgmt_ht_cap_t);

        zt_memcpy(pie->data, &pcur_network->pht_cap, pie->len);
        var_len += ZT_OFFSETOF(zt_80211_mgmt_ie_t, data) + pie->len;


        pie = (void *)&pvar[var_len];
        pie->element_id = ZT_80211_MGMT_EID_HT_OPERATION;
        pie->len = sizeof(zt_80211_mgmt_ht_operation_t);
        zt_memcpy(pie->data, &pcur_network->pht_oper, pie->len);
        var_len += ZT_OFFSETOF(zt_80211_mgmt_ie_t, data) + pie->len;
    }

    if (pwdn_info->wpa_enable || pwdn_info->rsn_enable)
    {
        ASSOC_DBG("wpa_enable");
        zt_memcpy(&pvar[var_len], &pwdn_info->wpa_ie, pwdn_info->wpa_ie_len);
        var_len += pwdn_info->wpa_ie_len;
    }
    /* set wmm */
    //    if (pwdn_info->wmm_enable)
    //    {
    //        ASSOC_DBG("wmm_enable");
    //        pvar[var_len++] = ZT_80211_MGMT_EID_VENDOR_SPECIFIC;
    //        pvar[var_len++] = sizeof(zt_80211_wmm_param_ie_t);
    //        zt_memcpy(&pvar[var_len], &pwdn_info->wmm_info,
    //                  sizeof(zt_80211_wmm_param_ie_t)-2);
    //        var_len += sizeof(zt_80211_wmm_param_ie_t);
    //    }

    /*p2p*/
    if (zt_p2p_is_valid(pnic_info))
    {
        mlme_info_t *pmlme_info = (mlme_info_t *)pnic_info->mlme_info;

        if (pmlme_info->wps_assoc_resp_ie && pmlme_info->wps_assoc_resp_ie_len > 0)
        {
            pie = (void *)&pvar[var_len];
            zt_memcpy(pie, &pmlme_info->wps_assoc_resp_ie[0],
                      pmlme_info->wps_assoc_resp_ie_len);
            var_len += pmlme_info->wps_assoc_resp_ie_len;
            ASSOC_DBG("assoc rsp set wps ie\n");
        }

        zt_p2p_fill_assoc_rsp(pnic_info, &pvar[var_len], &var_len, ZT_P2P_IE_ASSOC_REQ);
    }

    /* dump frame data */
    pxmit_buf->pkt_len = ZT_OFFSETOF(zt_80211_mgmt_t,
                                     assoc_resp.variable) + var_len;
    zt_nic_mgmt_frame_xmit(pnic_info, pwdn_info, pxmit_buf, pxmit_buf->pkt_len);

    return 0;
}

void zt_ap_add_sta_ratid(nic_info_st *pnic_info, wdn_net_info_st *pwdn_info)
{
    ASSOC_DBG();

    if (pwdn_info->aid < 32)
    {
        pwdn_info->raid = zt_wdn_get_raid_by_network_type(pwdn_info);
        zt_mcu_rate_table_update(pnic_info, pwdn_info);
    }
    else
    {
        ASSOC_ERROR("aid exceed the max number");
    }
}

static
void status_error(nic_info_st *pnic_info, wdn_net_info_st *pwdn_info,
                  zt_ap_msg_t *pmsg, zt_80211_frame_e frame_type,
                  zt_80211_statuscode_e status_code)
{
    /* free message */
    if (pmsg)
    {
        zt_ap_msg_free(pnic_info, &pwdn_info->ap_msg, pmsg);
    }
    /* send respond with error code */
    assoc_ap_xmit_frame(pnic_info, pwdn_info, frame_type, status_code);
}

void zt_assoc_ap_event_up(nic_info_st *nic_info, wdn_net_info_st *pwdn_info,
                          zt_ap_msg_t *pmsg)
{
    ASSOC_DBG();

    if (pwdn_info == NULL)
    {
        ASSOC_ERROR("wdn_info null");
        return;
    }

    if (pwdn_info->aid > 2007)
    {
        ASSOC_ERROR("aid(%d) error", pwdn_info->aid);
        return;
    }
    else
    {
        ASSOC_DBG("aid: %d", pwdn_info->aid);
    }

    zt_os_api_ap_ind_assoc(nic_info, pwdn_info, pmsg, ZT_MLME_FRAMEWORK_NETLINK);
}

zt_pt_ret_t zt_assoc_ap_thrd(nic_info_st *pnic_info, wdn_net_info_st *pwdn_info)
{
    zt_pt_t *pt = &pwdn_info->sub_thrd_pt;
    zt_wlan_mgmt_info_t *pwlan_info = pnic_info->wlan_mgmt_info;
    zt_wlan_network_t *pcur_network = &pwlan_info->cur_network;
    sec_info_st *psec_info = pnic_info->sec_info;
    zt_ap_msg_t *pmsg;
    zt_80211_mgmt_t *pmgmt;
    zt_u16 var_len, ofs;
    zt_u8 *pvar;
    zt_80211_mgmt_ie_t *pie;
    zt_u32 wpa_multicast_cipher;
    zt_u32 wpa_unicast_cipher;
    zt_u32 rsn_group_cipher;
    zt_u32 rsn_pairwise_cipher;
    zt_u8 i, j;
    zt_s32 ret;
    zt_80211_frame_e frame_type;
    zt_80211_statuscode_e status_code = ZT_80211_STATUS_SUCCESS;
    zt_u8 wpa_ie_len;

    PT_BEGIN(pt);

    /* after auth success, wait receive a assoc request */
    pmsg = zt_ap_msg_get(&pwdn_info->ap_msg);
    if (pmsg == NULL)
    {
        ASSOC_ERROR("no assocation frame received");
        /* abort thread */
        PT_EXIT(pt);
    }
    if (pmsg->tag != ZT_AP_MSG_TAG_ASSOC_REQ_FRAME)
    {
        zt_ap_msg_free(pnic_info, &pwdn_info->ap_msg, pmsg);
        ASSOC_DBG("no assocation frame received");
        disassoc_xmit_frame(pnic_info, pwdn_info->mac,
                            ZT_80211_REASON_DISASSOC_DUE_TO_INACTIVITY);
        /* abort thread */
        PT_EXIT(pt);
    }

    ASSOC_DBG("assoc begin->"ZT_MAC_FMT, ZT_MAC_ARG(pwdn_info->mac));
    pwdn_info->state = E_WDN_AP_STATE_ASSOC;

    /* parse assocation frame */
    ASSOC_DBG("assoc arrived");

    /* retrive frame type */
    frame_type = zt_80211_hdr_type_get(&pmsg->mgmt);

    /* retrive element fiexd fields */
    pmgmt = &pmsg->mgmt;
    pwdn_info->cap_info = zt_le16_to_cpu(pmgmt->assoc_req.capab_info);
    pwdn_info->listen_interval = zt_le16_to_cpu(pmgmt->assoc_req.listen_interval);

    /* initilize */
    pwdn_info->rsn_pairwise_cipher = 0;
    pwdn_info->wmm_enable = zt_false;
    pwdn_info->wpa_enable = zt_false;
    pwdn_info->rsn_enable = zt_false;
    pwdn_info->datarate_len = 0;
    pwdn_info->ext_datarate_len = 0;
    pwdn_info->ssid_len = 0;
    zt_memset(pwdn_info->wpa_ie, 0x0, sizeof(pwdn_info->wpa_ie));

    /* element variable fields */
    if (frame_type == ZT_80211_FRM_ASSOC_REQ)
    {
        pvar = pmsg->mgmt.assoc_req.variable;
        var_len = pmsg->len - ZT_OFFSETOF(zt_80211_mgmt_t, assoc_req.variable);
    }
    else
    {
        pvar = pmsg->mgmt.reassoc_req.variable;
        var_len = pmsg->len - ZT_OFFSETOF(zt_80211_mgmt_t, reassoc_req.variable);
    }

    for (ofs = 0; ofs < var_len;
            ofs += ZT_OFFSETOF(zt_80211_mgmt_ie_t, data) + pie->len)
    {
        pie = (void *)&pvar[ofs];
        if (pie->element_id == ZT_80211_MGMT_EID_SSID)
        {
            if (pie->len == pcur_network->ssid.length &&
                    !zt_memcmp(pie->data, pcur_network->ssid.data, pie->len))
            {
                pwdn_info->ssid_len = pcur_network->ssid.length;
                zt_memcpy(pwdn_info->ssid, pcur_network->ssid.data,
                          pcur_network->ssid.length);
                pwdn_info->ssid[pwdn_info->ssid_len] = '\0';
            }
            else
            {
                ASSOC_WARN("ssid error(len=%d): ", pie->len);
                status_error(pnic_info, pwdn_info,
                             pmsg, frame_type,
                             ZT_80211_STATUS_INVALID_IE);
                /* abort thread */
                PT_EXIT(pt);
            }
        }

        else if (pie->element_id == ZT_80211_MGMT_EID_SUPP_RATES)
        {
            if (pie->len > ZT_ARRAY_SIZE(pwdn_info->datarate))
            {
                ASSOC_ERROR("rates(%d) list size over limite", pie->len);
                status_error(pnic_info, pwdn_info,
                             pmsg, frame_type,
                             ZT_80211_STATUS_INVALID_IE);
                /* abort thread */
                PT_EXIT(pt);
            }
            /* check and retrieve rate */
            zt_memset(pwdn_info->datarate, 0x0, sizeof(pwdn_info->datarate));
            for (i = 0; i < pie->len; i++)
            {
                for (j = 0; j < ZT_ARRAY_SIZE(pcur_network->rate); j++)
                {
                    if (pcur_network->rate[j] == 0x0)
                    {
                        break;
                    }
                    if ((pie->data[i] & (~IEEE80211_BASIC_RATE_MASK)) ==
                            (pcur_network->rate[j] & (~IEEE80211_BASIC_RATE_MASK)))
                    {
                        if (pwdn_info->datarate_len <
                                ZT_ARRAY_SIZE(pwdn_info->datarate))
                        {
                            pwdn_info->datarate[pwdn_info->datarate_len++] =
                                pie->data[i];
                            break;
                        }
                        else
                        {
                            ASSOC_ERROR("beyond support rate upper limit");
                            status_error(pnic_info, pwdn_info,
                                         pmsg, frame_type,
                                         ZT_80211_STATUS_ASSOC_DENIED_RATES);
                            /* abort thread */
                            PT_EXIT(pt);
                        }
                    }
                }
            }
            if (pwdn_info->datarate_len == 0)
            {
                ASSOC_ERROR("invalid support extend rates");
                status_error(pnic_info, pwdn_info,
                             pmsg, frame_type,
                             ZT_80211_STATUS_ASSOC_DENIED_RATES);
                /* abort thread */
                PT_EXIT(pt);
            }
            ASSOC_DBG("data rate(Mbps): %d, %d, %d, %d, %d, %d, %d, %d",
                      (pwdn_info->datarate[0] & 0x7F) / 2,
                      (pwdn_info->datarate[1] & 0x7F) / 2,
                      (pwdn_info->datarate[2] & 0x7F) / 2,
                      (pwdn_info->datarate[3] & 0x7F) / 2,
                      (pwdn_info->datarate[4] & 0x7F) / 2,
                      (pwdn_info->datarate[5] & 0x7F) / 2,
                      (pwdn_info->datarate[6] & 0x7F) / 2,
                      (pwdn_info->datarate[7] & 0x7F) / 2);

            /* get network type */
            if ((only_cckrates(pwdn_info->datarate, pwdn_info->datarate_len)) == 1)
            {
                pwdn_info->network_type |= WIRELESS_11B;
            }
            else if ((have_cckrates(pwdn_info->datarate, pwdn_info->datarate_len)) == 1)
            {
                pwdn_info->network_type |= WIRELESS_11BG;
            }
            else
            {
                pwdn_info->network_type |= WIRELESS_11G;
            }
        }

        else if (pie->element_id == ZT_80211_MGMT_EID_EXT_SUPP_RATES)
        {
            if (pie->len > ZT_ARRAY_SIZE(pwdn_info->ext_datarate))
            {
                ASSOC_ERROR("support extend rates(%d) list size over limite", pie->len);
                status_error(pnic_info, pwdn_info,
                             pmsg, frame_type,
                             ZT_80211_STATUS_ASSOC_DENIED_RATES);
                /* abort thread */
                PT_EXIT(pt);
            }
            /* check and retrieve rate */
            zt_memset(pwdn_info->ext_datarate, 0x0,
                      ZT_ARRAY_SIZE(pwdn_info->ext_datarate));
            for (i = 0; i < pie->len; i++)
            {
                for (j = 0; j < ZT_ARRAY_SIZE(pcur_network->rate); j++)
                {
                    if (pcur_network->rate[j] == 0x0)
                    {
                        break;
                    }
                    if ((pie->data[i] & (~IEEE80211_BASIC_RATE_MASK)) ==
                            (pcur_network->rate[j] & (~IEEE80211_BASIC_RATE_MASK)))
                    {
                        if (pwdn_info->ext_datarate_len <
                                ZT_ARRAY_SIZE(pwdn_info->ext_datarate))
                        {
                            pwdn_info->ext_datarate[pwdn_info->ext_datarate_len++] =
                                pie->data[i];
                            break;
                        }
                        else
                        {
                            ASSOC_ERROR("beyond support rate upper limit");
                            status_error(pnic_info, pwdn_info,
                                         pmsg, frame_type,
                                         ZT_80211_STATUS_ASSOC_DENIED_RATES);
                            /* abort thread */
                            PT_EXIT(pt);
                        }
                    }
                }
            }
            /* no find support ext rate */
            if (pwdn_info->ext_datarate_len == 0)
            {
                ASSOC_ERROR("invalid support extend rates");
                status_error(pnic_info, pwdn_info,
                             pmsg, frame_type,
                             ZT_80211_STATUS_ASSOC_DENIED_RATES);
                /* abort thread */
                PT_EXIT(pt);
            }
            ASSOC_DBG("extend data rate(Mbps): %d, %d, %d, %d",
                      (pwdn_info->ext_datarate[0] & 0x7F) / 2,
                      (pwdn_info->ext_datarate[1] & 0x7F) / 2,
                      (pwdn_info->ext_datarate[2] & 0x7F) / 2,
                      (pwdn_info->ext_datarate[3] & 0x7F) / 2);

            /* get network type */
            if ((only_cckrates(pwdn_info->ext_datarate, pwdn_info->ext_datarate_len)) == 1)
            {
                pwdn_info->network_type |= WIRELESS_11B;
            }
            else if ((have_cckrates(pwdn_info->ext_datarate,
                                    pwdn_info->ext_datarate_len)) == 1)
            {
                pwdn_info->network_type |= WIRELESS_11BG;
            }
            else
            {
                pwdn_info->network_type |= WIRELESS_11G;
            }
        }

        else if (pie->element_id == ZT_80211_MGMT_EID_RSN)
        {
            wpa_ie_len = ZT_OFFSETOF(zt_80211_mgmt_ie_t, data) + pie->len;
            if (!zt_80211_mgmt_rsn_parse(pie, wpa_ie_len,
                                         &rsn_group_cipher, &rsn_pairwise_cipher))
            {
                /* checkout group cipher */
                if (rsn_group_cipher != psec_info->rsn_group_cipher)
                {
                    ASSOC_WARN("RSN group cipher error");
                    status_error(pnic_info, pwdn_info,
                                 pmsg, frame_type,
                                 ZT_80211_STATUS_INVALID_GROUP_CIPHER);
                    /* abort thread */
                    PT_EXIT(pt);
                }
                /* checkout pairwise cipher */
                pwdn_info->rsn_pairwise_cipher =
                    rsn_pairwise_cipher & psec_info->rsn_pairwise_cipher;
                if (!pwdn_info->rsn_pairwise_cipher)
                {
                    ASSOC_WARN("RSN pairwise cipher error");
                    status_error(pnic_info, pwdn_info,
                                 pmsg, frame_type,
                                 ZT_80211_STATUS_INVALID_PAIRWISE_CIPHER);
                    /* abort thread */
                    PT_EXIT(pt);
                }
                /* checkout rsn ie */
                wpa_ie_len =
                    ZT_MIN(ZT_ARRAY_SIZE(pwdn_info->wpa_ie), wpa_ie_len);
                zt_memcpy(pwdn_info->wpa_ie, pie, wpa_ie_len);
                pwdn_info->wpa_ie_len = wpa_ie_len;
                pwdn_info->rsn_enable = zt_true;
            }
            else
            {
                ASSOC_WARN("RSN element error");
                status_error(pnic_info, pwdn_info,
                             pmsg, frame_type,
                             ZT_80211_STATUS_INVALID_IE);
                /* abort thread */
                PT_EXIT(pt);
            }
        }

        else if (pie->element_id == ZT_80211_MGMT_EID_VENDOR_SPECIFIC)
        {
            /* wpa parse */
            wpa_ie_len = ZT_OFFSETOF(zt_80211_mgmt_ie_t, data) + pie->len;
            ret = zt_80211_mgmt_wpa_parse(pie, wpa_ie_len,
                                          &wpa_multicast_cipher, &wpa_unicast_cipher);
            if (ret < 0 && ret >= -3)
            {
                ASSOC_WARN("vendor ie corrupt");
                status_error(pnic_info, pwdn_info,
                             pmsg, frame_type,
                             ZT_80211_STATUS_INVALID_IE);
                /* abort thread */
                PT_EXIT(pt);
            }
            if (!ret)
            {
                ASSOC_DBG("WPA element");
                /* checkout group cipher */
                if (wpa_multicast_cipher != psec_info->wpa_multicast_cipher)
                {
                    ASSOC_WARN("wpa multicast cipher error");
                    status_error(pnic_info, pwdn_info,
                                 pmsg, frame_type,
                                 ZT_80211_STATUS_INVALID_GROUP_CIPHER);
                    /* abort thread */
                    PT_EXIT(pt);
                }
                /* checkout pairwise cipher */
                pwdn_info->wpa_unicast_cipher =
                    wpa_unicast_cipher & psec_info->wpa_unicast_cipher;
                if (!pwdn_info->wpa_unicast_cipher)
                {
                    ASSOC_WARN("wpa pairwise cipher error");
                    status_error(pnic_info, pwdn_info,
                                 pmsg, frame_type,
                                 ZT_80211_STATUS_INVALID_PAIRWISE_CIPHER);
                    /* abort thread */
                    PT_EXIT(pt);
                }
                /* checkout wpa ie */
                wpa_ie_len =
                    ZT_MIN(sizeof(pwdn_info->wpa_ie), wpa_ie_len);
                zt_memcpy(pwdn_info->wpa_ie, pie, wpa_ie_len);
                pwdn_info->wpa_ie_len = wpa_ie_len;
                pwdn_info->wpa_enable = zt_true;
            }

            /* wmm parse */
            ret = zt_80211_mgmt_wmm_parse(pie,
                                          ZT_OFFSETOF(zt_80211_mgmt_ie_t, data) + pie->len);
            if (ret < 0 && ret >= -2)
            {
                ASSOC_WARN("vendor ie corrupt");
                status_error(pnic_info, pwdn_info,
                             pmsg, frame_type,
                             ZT_80211_STATUS_INVALID_IE);
                /* abort thread */
                PT_EXIT(pt);
            }
            if (!ret)
            {
                ASSOC_DBG("WMM element");
                pwdn_info->wmm_enable = zt_true;
                zt_memcpy(&pwdn_info->wmm_info, pie, ZT_OFFSETOF(zt_80211_mgmt_ie_t,
                          data) + pie->len);
            }

            if (zt_p2p_is_valid(pnic_info))
            {
                ret = zt_p2p_parse_p2pie(pnic_info, pie, ZT_OFFSETOF(zt_80211_mgmt_ie_t,
                                         data) + pie->len, ZT_P2P_IE_ASSOC_REQ);
                if (0 == ret)
                {
                    ASSOC_DBG("p2p element");
                    pwdn_info->is_p2p_device = 1;
                    zt_p2p_proc_assoc_req(pnic_info, (zt_u8 *)pie, ZT_OFFSETOF(zt_80211_mgmt_ie_t,
                                          data) + pie->len, pwdn_info, 1);
                }
            }
        }

        else if (pie->element_id == ZT_80211_MGMT_EID_HT_CAPABILITY)
        {
            ASSOC_DBG("ht capability");
            if (pcur_network->ht_enable == zt_false)
            {
                ASSOC_WARN("no support ht capability");
                status_error(pnic_info, pwdn_info,
                             pmsg, frame_type,
                             ZT_80211_STATUS_UNSPECIFIED_FAILURE);
                /* abort thread */
                PT_EXIT(pt);
            }
            zt_ie_ht_capability_update(pnic_info, pwdn_info, pie->data, pie->len);
            zt_memcpy(&pwdn_info->ht_cap, pie->data, pie->len);
        }

        else if (pie->element_id == ZT_80211_MGMT_EID_HT_OPERATION)
        {
            ASSOC_DBG("ht operation");
            if (pcur_network->ht_enable == zt_false)
            {
                ASSOC_WARN("no support ht operation");
                status_error(pnic_info, pwdn_info,
                             pmsg, frame_type,
                             ZT_80211_STATUS_UNSPECIFIED_FAILURE);
                /* abort thread */
                PT_EXIT(pt);
            }
            zt_ie_ht_operation_info_update(pnic_info, pwdn_info, pie->data, pie->len);
            zt_memcpy(&pwdn_info->ht_info, pie->data, pie->len);
        }
    }

    /* free message */
    zt_ap_msg_free(pnic_info, &pwdn_info->ap_msg, pmsg);

    /* send assoc success frame */
    assoc_ap_xmit_frame(pnic_info, pwdn_info, frame_type, status_code);
    /* notify connection establish */
    zt_mcu_media_connect_set(pnic_info, pwdn_info, zt_true);
    zt_ap_add_sta_ratid(pnic_info, pwdn_info);
    zt_action_frame_del_ba_request(pnic_info, pwdn_info->mac);

    zt_assoc_ap_event_up(pnic_info, pwdn_info, pmsg);
    ASSOC_DBG("assoc end->"ZT_MAC_FMT, ZT_MAC_ARG(pwdn_info->mac));

    /* thread end */
    PT_END(pt);
}

zt_s32 zt_assoc_ap_work(nic_info_st *pnic_info, wdn_net_info_st *pwdn_info,
                        zt_80211_mgmt_t *pmgmt, zt_u16 mgmt_len)
{
    zt_80211_frame_e frame_type;

    if (pwdn_info == NULL)
    {
        ASSOC_ERROR("wdn_info null");
        return -1;
    }

    if (mgmt_len > ZT_80211_MGMT_ASSOC_SIZE_MAX)
    {
        ASSOC_ERROR("association request frame length too long");
        return -2;
    }

    frame_type = zt_80211_hdr_type_get(pmgmt);
    if (frame_type != ZT_80211_FRM_ASSOC_REQ &&
            frame_type != ZT_80211_FRM_REASSOC_REQ)
    {
        return -3;
    }

    //    ASSOC_DBG("assoc received");

    if (pwdn_info->mode != ZT_MASTER_MODE)
    {
        ASSOC_WARN("the wdn no used for master mode");
        return -4;
    }

    if (zt_ap_msg_load(pnic_info, &pwdn_info->ap_msg,
                       ZT_AP_MSG_TAG_ASSOC_REQ_FRAME, pmgmt, mgmt_len))
    {
        ASSOC_WARN("assoc msg enque fail");
        return -5;
    }

    return 0;
}
#endif

static zt_s32 associate_xmit_frame(nic_info_st *nic_info)
{
    zt_u8 *pframe;
    struct wl_ieee80211_hdr *pwlanhdr = NULL;
    struct xmit_buf *pxmit_buf;
    zt_wlan_mgmt_info_t *wlan_mgmt_info = nic_info->wlan_mgmt_info;
    zt_wlan_network_t *pcur_network = &wlan_mgmt_info->cur_network;
    zt_u32 pkt_len;
    zt_80211_mgmt_ie_t *pie;
    wdn_net_info_st *wdn_info;
    tx_info_st *tx_info = (tx_info_st *)nic_info->tx_info;
    hw_info_st *hw_info = nic_info->hw_info;
    sec_info_st *sec_info = nic_info->sec_info;
    zt_u8 WMM_IE[] = { 0x00, 0x50, 0xf2, 0x02, 0x00, 0x01, 0x00 };
    zt_s32 ret = 0;
    //ASSOC_DBG();

    /* alloc xmit_buf */
    pxmit_buf = zt_xmit_extbuf_new(tx_info);
    if (pxmit_buf == NULL)
    {
        ASSOC_ERROR("zt_xmit_extbuf_new error");
        ret = -1;
        goto exit;
    }
    zt_memset(pxmit_buf->pbuf, 0,
              WLANHDR_OFFSET + TXDESC_OFFSET); //MAX_XMIT_EXTBUF_SZ
    pframe = pxmit_buf->pbuf + TXDESC_OFFSET;
    pwlanhdr = (struct wl_ieee80211_hdr *)pframe;
    pwlanhdr->frame_ctl = 0;

    wdn_info = zt_wdn_find_info(nic_info, zt_wlan_get_cur_bssid(nic_info));
    if (wdn_info == NULL)
    {
        ASSOC_ERROR("wdn null");
        ret = -2;
        zt_xmit_extbuf_delete(tx_info, pxmit_buf);
        goto exit;
    }

    /* sta mode */
    SetFrameType(pframe, WIFI_MGT_TYPE);
    SetFrameSubType(pframe, WIFI_ASSOCREQ);     /* set subtype */

    /* copy addr1/2/3 */
    zt_memcpy(pwlanhdr->addr1, wdn_info->mac, ZT_80211_MAC_ADDR_LEN);
    zt_memcpy(pwlanhdr->addr2, nic_to_local_addr(nic_info), ZT_80211_MAC_ADDR_LEN);
    zt_memcpy(pwlanhdr->addr3, wdn_info->bssid, ZT_80211_MAC_ADDR_LEN);

    if (0)
    {
        ASSOC_DBG("wlanhdr:addr1="ZT_MAC_FMT, ZT_MAC_ARG(pwlanhdr->addr1));
        ASSOC_DBG("wlanhdr:addr2="ZT_MAC_FMT, ZT_MAC_ARG(pwlanhdr->addr2));
        ASSOC_DBG("wlanhdr:addr3="ZT_MAC_FMT, ZT_MAC_ARG(pwlanhdr->addr3));
    }

    pkt_len = sizeof(struct wl_ieee80211_hdr_3addr);
    pframe += pkt_len; /* point to iv or frame body */

    /* capability */
    zt_memcpy(pframe, (zt_u8 *)&wdn_info->cap_info, 2);
    pkt_len += 2;
    pframe += 2;

    /* listen interval */
    zt_memcpy(pframe, (zt_u8 *)&wdn_info->listen_interval, 2);
    pkt_len += 2;
    pframe += 2;

    /* ssid */
    pframe = set_ie(pframe, ZT_80211_MGMT_EID_SSID, wdn_info->ssid_len,
                    wdn_info->ssid, &pkt_len);

    /* support rates */
    pframe = set_ie(pframe, ZT_80211_MGMT_EID_SUPP_RATES, wdn_info->datarate_len,
                    wdn_info->datarate, &pkt_len);

    /* extend support rates */
    if (wdn_info->ext_datarate_len > 0)
    {
        pframe = set_ie(pframe, ZT_80211_MGMT_EID_EXT_SUPP_RATES,
                        wdn_info->ext_datarate_len, wdn_info->ext_datarate, &pkt_len);
    }

    /* power cabability */

    /* Supported Channels */

    /* WPA */
    if (sec_info->wpa_enable)
    {
        pie = (zt_80211_mgmt_ie_t *)sec_info->supplicant_ie;
        pframe = set_ie(pframe, ZT_80211_MGMT_EID_VENDOR_SPECIFIC, pie->len, pie->data,
                        &pkt_len);
    }

    /* RSN */
    else if (sec_info->rsn_enable)
    {
        pie = (zt_80211_mgmt_ie_t *)sec_info->supplicant_ie;
        if (pie->len != 20) {
            pie->len = 20;
            pie->data[18] = 0x00;
            pie->data[19] = 0x00;
        }
        pframe = set_ie(pframe, ZT_80211_MGMT_EID_RSN, pie->len, pie->data, &pkt_len);
    }

    /* QoS Capability */
    if (wdn_info->wmm_enable)
    {
        pframe = set_ie(pframe, ZT_80211_MGMT_EID_VENDOR_SPECIFIC, 7, (zt_u8 *)WMM_IE,
                        &pkt_len);
    }

    /* RM Enabled Capabilities */

    /* Mobility Domain */

    /* Supported Operating Classes */

    /* HT Capabilities */
    if (wdn_info->ht_enable && hw_info->dot80211n_support)
    {
        pframe = set_ie(pframe, ZT_80211_MGMT_EID_HT_CAPABILITY,
                        sizeof(wdn_info->ht_cap), (zt_u8 *)&wdn_info->ht_cap, &pkt_len);
    }

    /* 20/40 BSS Coexistence */

    /* Extended Capabilities */

    /* QoS Traffic Capability */

    /* TIM Broadcast Request */

    /* Interworking */

    /* Vendor Specific*/

    /*p2p*/
    if (zt_p2p_is_valid(nic_info))
    {
        pframe = zt_p2p_fill_assoc_req(nic_info, pframe, &pkt_len, ZT_P2P_IE_ASSOC_REQ);
    }

    pxmit_buf->pkt_len = (zt_u16)pkt_len;
    zt_nic_mgmt_frame_xmit(nic_info, wdn_info, pxmit_buf, pxmit_buf->pkt_len);
    //      pcur_network->sta_state |= WIFI_ASOC_STATE;

exit:
    if (!ret)
    {
        zt_80211_mgmt_t *pmgmt = (void *)pwlanhdr;
        pcur_network->assoc_req.ie_len =
            pkt_len - ZT_OFFSETOF(zt_80211_mgmt_t, assoc_req.listen_interval);
        zt_memcpy(pcur_network->assoc_req.ie,
                  &pmgmt->assoc_req.listen_interval, pcur_network->assoc_req.ie_len);
    }

    return ret;
}

zt_s32 zt_assoc_frame_parse(nic_info_st *pnic_info, wdn_net_info_st *pwdn_info,
                            zt_80211_mgmt_t *pmgmt, zt_u16 mgmt_len)
{
    if (pnic_info == NULL || pmgmt == NULL || mgmt_len == 0)
    {
        return -1;
    }

    //ASSOC_DBG();

    {
        mlme_state_e state;
        zt_mlme_get_state(pnic_info, &state);
        ASSOC_DBG("state:%d", state);
        if (state != MLME_STATE_ASSOC)
        {
            ASSOC_WARN("mlme state:%d", state);
            return -2;
        }
    }

    if (pwdn_info == NULL)
    {
        ASSOC_WARN("pwdn_info None pointer");
        return -3;
    }

    if (!zt_80211_is_same_addr(pmgmt->da, nic_to_local_addr(pnic_info)) ||
            !zt_80211_is_same_addr(pwdn_info->bssid, zt_wlan_get_cur_bssid(pnic_info)))
    {
        ASSOC_WARN("mac addr is not equl");
        return -4;
    }
    if (mgmt_len > sizeof(assoc_rsp_t))
    {
        ASSOC_WARN("mgmt_len:%d", mgmt_len);
        return -5;
    }

    /* send message */
    {
        assoc_info_t *passoc_info = pnic_info->assoc_info;
        zt_msg_que_t *pmsg_que = &passoc_info->msg_que;
        zt_msg_t *pmsg;
        zt_s32 rst;

        rst = zt_msg_new(pmsg_que, ZT_ASSOC_TAG_RSP, &pmsg);
        if (rst)
        {
            ASSOC_WARN("msg new fail error fail: %d", rst);
            return -6;
        }
        pmsg->len = mgmt_len;
        zt_memcpy(pmsg->value, pmgmt, mgmt_len);
        rst = zt_msg_push(pmsg_que, pmsg);
        if (rst)
        {
            zt_msg_del(pmsg_que, pmsg);
            ASSOC_WARN("msg push fail error fail: %d", rst);
            return -7;
        }
    }

    return 0;
}

zt_s32 zt_disassoc_frame_parse(nic_info_st *pnic_info,
                               wdn_net_info_st *pwdn_info,
                               zt_80211_mgmt_t *pmgmt, zt_u16 mgmt_len)
{
    zt_s32 rst;

    switch (zt_local_cfg_get_work_mode(pnic_info))
    {
        case ZT_INFRA_MODE :
            if (pwdn_info)
            {
                ASSOC_DBG("ZT_80211_FRM_DISASSOC[%d] frame get, reason:%d",
                          pnic_info->ndev_id, pmgmt->disassoc.reason_code);
                rst = zt_mlme_deauth(pnic_info,
                                     zt_false,
                                     (zt_80211_reasoncode_e)pmgmt->disassoc.reason_code);
                if (rst)
                {
                    ASSOC_WARN("zt_mlme_deauth fail, reason code: %d", rst);
                    return -1;
                }
            }
            break;

#ifdef CFG_ENABLE_AP_MODE
        case ZT_MASTER_MODE :
            if (pwdn_info)
            {
                rst = disassoc_work_ap(pnic_info, pwdn_info, pmgmt, mgmt_len);
                if (rst)
                {
                    ASSOC_WARN("disassoc_work_ap fail, reason code: %d", rst);
                    return -2;
                }
            }
            break;
#endif

        default :
            return -3;
    }

    return 0;
}

zt_pt_ret_t zt_assoc_sta_thrd(zt_pt_t *pt, nic_info_st *pnic_info, zt_s32 *prsn)
{
    assoc_info_t *passoc_info;
    zt_msg_que_t *pmsg_que;
    zt_msg_t *pmsg = NULL;
    zt_s32 reason = ZT_ASSOC_TAG_DONE;
    zt_s32 rst;

    if (pt == NULL || pnic_info == NULL || prsn == NULL)
    {
        PT_EXIT(pt);
    }
    passoc_info = pnic_info->assoc_info;
    pmsg_que = &passoc_info->msg_que;

    PT_BEGIN(pt);

    PT_WAIT_WHILE(pt, nic_mlme_hw_access_trylock(pnic_info));
    /* get assoc start message. */
    do
    {
        if (zt_msg_pop(pmsg_que, &pmsg))
        {
            /* no message */
            ASSOC_WARN("no request message");
            nic_mlme_hw_access_unlock(pnic_info);
            *prsn = -1;
            PT_EXIT(pt);
        }
        if (pmsg->tag != ZT_ASSOC_TAG_START)
        {
            /* undesired message */
            ASSOC_DBG("unsuited message, tag: %d", pmsg->tag);
            zt_msg_del(pmsg_que, pmsg);
            PT_YIELD(pt);
            continue;
        }
        zt_msg_del(pmsg_que, pmsg);
        break;
    } while (zt_true);
    passoc_info->brun = zt_true;

    for (passoc_info->retry_cnt = 0;
            passoc_info->retry_cnt < ASSOC_REQ_RESEND_TIMES;
            passoc_info->retry_cnt++)
    {
        ASSOC_DBG("send assoc request");
        rst = associate_xmit_frame(pnic_info);
        if (rst)
        {
            ASSOC_WARN("assoc xmit fail, error code: %d", rst);
            reason = -2;
            break;
        }

        /* wait until receive assoc respone */
        zt_timer_set(&passoc_info->timer, ASSOC_RSP_TIMEOUT);
wait_msg :
        PT_WAIT_UNTIL(pt, !zt_msg_pop(pmsg_que, &pmsg) ||
                      zt_timer_expired(&passoc_info->timer));
        if (pmsg == NULL)
        {
            /* timeout, resend again */
            continue;
        }

        if (pmsg->tag == ZT_ASSOC_TAG_ABORT)
        {
            zt_msg_del(pmsg_que, pmsg);
            reason = ZT_ASSOC_TAG_ABORT;
            ASSOC_DBG("assoc abort");
            break;
        }
        else if (pmsg->tag == ZT_ASSOC_TAG_RSP)
        {
            zt_80211_mgmt_t *pmgmt = (zt_80211_mgmt_t *)pmsg->value;
            zt_u16 mgmt_len = (zt_u16)pmsg->len;
            if (!pmgmt->assoc_resp.status_code)
            {
                zt_wlan_mgmt_info_t *pwlan_info = pnic_info->wlan_mgmt_info;
                zt_wlan_network_t *pcur_network = &pwlan_info->cur_network;
                /* retrive AID and IE */
                pcur_network->aid = pmgmt->assoc_resp.aid;
                pcur_network->assoc_resp.ie_len =
                    mgmt_len - ZT_OFFSETOF(zt_80211_mgmt_t, assoc_resp.variable);
                zt_memcpy(pcur_network->assoc_resp.ie,
                          &pmgmt->assoc_resp.variable,
                          pcur_network->assoc_resp.ie_len);
                /* assoc success */
                ASSOC_INFO("assoc success");
                zt_msg_del(pmsg_que, pmsg);
                reason = ZT_ASSOC_TAG_DONE;
                break;
            }
            else
            {
                ASSOC_WARN("assoc status_code:0x%x", pmgmt->assoc_resp.status_code);
            }
            zt_msg_del(pmsg_que, pmsg);
        }
        else
        {
            ASSOC_WARN("unsutied message tag(%d)", pmsg->tag);
            zt_msg_del(pmsg_que, pmsg);
            goto wait_msg;
        }
    }
    if (passoc_info->retry_cnt == ASSOC_REQ_RESEND_TIMES)
    {
        ASSOC_DBG("no respone receive");
        reason = -3;
    }

    passoc_info->brun = zt_false;
    nic_mlme_hw_access_unlock(pnic_info);

    *prsn = reason;
    if (reason)
    {
        PT_EXIT(pt);
    }
    PT_END(pt);
}

zt_s32 zt_assoc_start(nic_info_st *pnic_info)
{
    assoc_info_t *passoc_info;

    if (pnic_info == NULL || ZT_CANNOT_RUN(pnic_info))
    {
        return -1;
    }

    if (!pnic_info->is_up)
    {
        return -2;
    }
    passoc_info = pnic_info->assoc_info;

    ASSOC_DBG();

    /* new message information */
    {
        zt_msg_que_t *pmsg_que = &passoc_info->msg_que;
        zt_msg_t *pmsg;
        zt_s32 rst;

        rst = zt_msg_new(pmsg_que, ZT_ASSOC_TAG_START, &pmsg);
        if (rst)
        {
            ASSOC_WARN("msg new fail error fail: %d", rst);
            return -3;
        }
        rst = zt_msg_push(pmsg_que, pmsg);
        if (rst)
        {
            ASSOC_WARN("msg push fail error fail: %d", rst);
            return -4;
        }
    }

    return 0;
}

zt_s32 zt_assoc_stop(nic_info_st *pnic_info)
{
    assoc_info_t *passoc_info;

    if (pnic_info == NULL || ZT_CANNOT_RUN(pnic_info))
    {
        return -1;
    }

    ASSOC_DBG();

    if (!pnic_info->is_up)
    {
        return -2;
    }

    passoc_info = pnic_info->assoc_info;
    if (!passoc_info->brun)
    {
        return -3;
    }

    {
        zt_msg_que_t *pmsg_que = &passoc_info->msg_que;
        zt_msg_t *pmsg;
        zt_s32 rst;

        rst = zt_msg_new(pmsg_que, ZT_ASSOC_TAG_ABORT, &pmsg);
        if (rst)
        {
            ASSOC_WARN("msg new fail error fail: %d", rst);
            return -4;
        }
        rst = zt_msg_push(pmsg_que, pmsg);
        if (rst)
        {
            zt_msg_del(pmsg_que, pmsg);
            ASSOC_WARN("msg push fail error fail: %d", rst);
            return -5;
        }
    }

    return 0;
}

zt_inline static zt_s32 assoc_msg_init(zt_msg_que_t *pmsg_que)
{
    zt_msg_init(pmsg_que);
    return (zt_msg_alloc(pmsg_que, ZT_ASSOC_TAG_RSP, sizeof(assoc_rsp_t), 2) ||
            zt_msg_alloc(pmsg_que, ZT_ASSOC_TAG_ABORT, 0, 1) ||
            zt_msg_alloc(pmsg_que, ZT_ASSOC_TAG_START, 0, 1)) ? -1 : 0;
}

zt_inline static void assoc_msg_deinit(zt_msg_que_t *pmsg_que)
{
    zt_msg_deinit(pmsg_que);
}

zt_s32 zt_assoc_init(nic_info_st *pnic_info)
{
    assoc_info_t *passoc_info;

    if (pnic_info == NULL)
    {
        return -1;
    }

    ASSOC_DBG();

    passoc_info = zt_kzalloc(sizeof(assoc_info_t));
    if (passoc_info == NULL)
    {
        ASSOC_ERROR("malloc assoc_info failed");
        return -2;
    }
    pnic_info->assoc_info = passoc_info;
    passoc_info->brun = zt_false;
    if (assoc_msg_init(&passoc_info->msg_que))
    {
        ASSOC_ERROR("assoc msg init failed");
        return -3;
    }

    return 0;
}

zt_s32 zt_assoc_term(nic_info_st *pnic_info)
{
    assoc_info_t *passoc_info;

    if (pnic_info == NULL)
    {
        return 0;
    }
    passoc_info = pnic_info->assoc_info;

    ASSOC_DBG();

    if (passoc_info)
    {
        assoc_msg_deinit(&passoc_info->msg_que);
        zt_kfree(passoc_info);
        pnic_info->assoc_info = NULL;
    }

    return 0;
}

