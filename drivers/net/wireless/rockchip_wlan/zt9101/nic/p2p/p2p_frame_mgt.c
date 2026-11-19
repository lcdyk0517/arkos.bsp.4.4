/*
 * p2p_frame_mgt.c
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

#define ZT_PUBLIC_ACTION_IE_OFFSET (8)

#define P2P_FRAME_ARRAY(data, len)   zt_log_array(data, len)
#define P2P_FRAME_DBG(fmt, ...)      LOG_D("P2P_FRAME[%s:%d][%d]"fmt, __func__,__LINE__,pnic_info->ndev_id, ##__VA_ARGS__)
#define P2P_FRAME_INFO(fmt, ...)     LOG_I("P2P_FRAME[%s:%d][%d]"fmt, __func__,__LINE__,pnic_info->ndev_id, ##__VA_ARGS__)
#define P2P_FRAME_WARN(fmt, ...)     LOG_W("P2P_FRAME[%s:%d]"fmt, __func__,__LINE__, ##__VA_ARGS__)
#define P2P_FRAME_ERR(fmt, ...)      LOG_E("P2P_FRAME[%s:%d]"fmt, __func__,__LINE__, ##__VA_ARGS__)

#define zt_memmove memmove

zt_u8 *zt_p2p_get_ie(zt_u8 *in_ie, zt_s32 in_len, zt_u8 *p2p_ie,
                     zt_u32 *p2p_ielen)
{
    zt_u32 cnt;
    zt_u8 *p2p_ie_ptr = NULL;
    zt_u8 eid;

    if (p2p_ielen)
    {
        *p2p_ielen = 0;
    }

    if (!in_ie || in_len < 0)
    {
        P2P_FRAME_ERR("in_len is not right");
        return p2p_ie_ptr;
    }

    if (in_len <= 0)
    {
        return p2p_ie_ptr;
    }

    cnt = 0;

    while (cnt + 1 + 4 < in_len)
    {
        eid = in_ie[cnt];

        if (cnt + 1 + 4 >= ZT_80211_IES_SIZE_MAX)
        {
            P2P_FRAME_ERR("cnt is not right");
            return NULL;
        }

        if (eid == ZT_80211_MGMT_EID_VENDOR_SPECIFIC &&
                zt_memcmp(&in_ie[cnt + 2], P2P_OUI, 4) == 0)
        {
            p2p_ie_ptr = in_ie + cnt;

            if (p2p_ie)
            {
                zt_memcpy(p2p_ie, &in_ie[cnt], in_ie[cnt + 1] + 2);
            }

            if (p2p_ielen)
            {
                *p2p_ielen = in_ie[cnt + 1] + 2;
            }

            break;
        }
        else
        {
            cnt += in_ie[cnt + 1] + 2;
        }

    }

    return p2p_ie_ptr;
}

static zt_u8 *p2p_get_attr(zt_u8 *p2p_ie, zt_u32 p2p_ielen,
                           zt_u8 target_attr_id, zt_u8 *buf_attr, zt_u32 *len_attr)
{
    zt_u8 *attr_ptr = NULL;
    zt_u8 *target_attr_ptr = NULL;
    if (len_attr)
    {
        *len_attr = 0;
    }

    if (!p2p_ie || p2p_ielen <= 6 ||
            (p2p_ie[0] != ZT_80211_MGMT_EID_VENDOR_SPECIFIC) ||
            (zt_memcmp(p2p_ie + 2, P2P_OUI, 4) != 0))
    {
        return attr_ptr;
    }

    attr_ptr = p2p_ie + 6;

    while ((attr_ptr - p2p_ie + 3) <= p2p_ielen)
    {
        zt_u8 attr_id = *attr_ptr;
        zt_u16 attr_data_len = ZT_GET_LE16(attr_ptr + 1);
        zt_u16 attr_len = attr_data_len + 3;

        if ((attr_ptr - p2p_ie + attr_len) > p2p_ielen)
        {
            break;
        }

        if (attr_id == target_attr_id)
        {
            target_attr_ptr = attr_ptr;

            if (buf_attr)
            {
                zt_memcpy(buf_attr, attr_ptr, attr_len);
            }

            if (len_attr)
            {
                *len_attr = attr_len;
            }

            break;
        }
        else
        {
            attr_ptr += attr_len;
        }
    }

    return target_attr_ptr;
}

zt_u8 *zt_p2p_get_attr_content(zt_u8 *p2p_ie, zt_u32 p2p_ielen,
                               zt_u8 target_attr_id, zt_u8 *buf_content, zt_u32 *len_content)
{
    zt_u8 *attr_ptr = NULL;
    zt_u32 attr_len = 0;

    if (len_content)
    {
        *len_content = 0;
    }

    attr_ptr = p2p_get_attr(p2p_ie, p2p_ielen, target_attr_id, NULL, &attr_len);

    if (attr_ptr && attr_len)
    {
        if (buf_content)
        {
            zt_memcpy(buf_content, attr_ptr + 3, attr_len - 3);
        }

        if (len_content)
        {
            *len_content = attr_len - 3;
        }

        return attr_ptr + 3;
    }

    return NULL;
}

zt_u32 p2p_set_ie(zt_u8 *pbuf, zt_u8 index, zt_u16 attr_len, zt_u8 *pdata_attr)
{
    zt_u32 a_len = 0;
    *pbuf = index;

    ZT_PUT_LE16(pbuf + 1, attr_len);

    if (pdata_attr)
    {
        zt_memcpy(pbuf + 3, pdata_attr, attr_len);
    }

    a_len = attr_len + 3;

    return a_len;
}

zt_s32 zt_p2p_fill_assoc_rsp(nic_info_st *pnic_info, zt_u8 *pframe,
                             zt_u16 *pkt_len, ZT_P2P_IE_E pie_type)
{
    p2p_info_st *p2p_info = pnic_info->p2p;

    if (ZT_P2P_IE_MAX <= pie_type)
    {
        P2P_FRAME_ERR("unknown ie_type:%d", pie_type);
        return -1;
    }
    if (p2p_info->role == P2P_ROLE_GO)
    {
        if (p2p_info->p2p_ie[ZT_P2P_IE_ASSOC_RSP] &&
                p2p_info->p2p_ie_len[ZT_P2P_IE_ASSOC_RSP] > 0)
        {
            P2P_FRAME_DBG(" %s:%d", zt_p2p_ie_to_str(ZT_P2P_IE_ASSOC_RSP),
                          p2p_info->p2p_ie_len[ZT_P2P_IE_ASSOC_RSP]);
            zt_memcpy((void *)pframe, (void *)p2p_info->p2p_ie[ZT_P2P_IE_ASSOC_RSP],
                      p2p_info->p2p_ie_len[ZT_P2P_IE_ASSOC_RSP]);
            pframe += p2p_info->p2p_ie_len[ZT_P2P_IE_ASSOC_RSP];
            *pkt_len += p2p_info->p2p_ie_len[ZT_P2P_IE_ASSOC_RSP];

        }
        else if (p2p_info->p2p_ie[pie_type] && p2p_info->p2p_ie_len[pie_type] > 0)
        {
            P2P_FRAME_DBG("%s:%d", zt_p2p_ie_to_str(pie_type),
                          p2p_info->p2p_ie_len[pie_type]);
            zt_memcpy((void *)pframe, (void *)p2p_info->p2p_ie[pie_type],
                      p2p_info->p2p_ie_len[pie_type]);
            pframe += p2p_info->p2p_ie_len[pie_type];
            *pkt_len += p2p_info->p2p_ie_len[pie_type];
        }
    }

    if (zt_p2p_wfd_is_valid(pnic_info))
    {
        zt_u32 wfdielen = 0;
        if (p2p_info->role == P2P_ROLE_GO)
        {
            wfdielen = zt_p2p_wfd_append_assoc_resp_ie(pnic_info, pframe, 1);
            pframe += wfdielen;
            *pkt_len += wfdielen;
        }
    }

    return 0;
}

zt_u8 *zt_p2p_fill_assoc_req(nic_info_st *pnic_info, zt_u8 *pframe,
                             zt_u32 *pkt_len, ZT_P2P_IE_E pie_type)
{

    p2p_info_st *p2p_info           = pnic_info->p2p;

    P2P_FRAME_DBG("tart");
    if (ZT_P2P_IE_MAX <= pie_type)
    {
        P2P_FRAME_ERR("unknown ie_type:%d", pie_type);
        return NULL;
    }

    if (p2p_info->p2p_enabled)
    {
        if (p2p_info->p2p_ie[pie_type] && p2p_info->p2p_ie_len[pie_type] > 0)
        {
            P2P_FRAME_DBG("%s:%d", zt_p2p_ie_to_str(pie_type),
                          p2p_info->p2p_ie_len[pie_type]);
            zt_memcpy((void *)pframe, (void *)p2p_info->p2p_ie[pie_type],
                      p2p_info->p2p_ie_len[pie_type]);
            pframe += p2p_info->p2p_ie_len[pie_type];
            *pkt_len += p2p_info->p2p_ie_len[pie_type];
        }
    }
    else
    {
        P2P_FRAME_DBG("not support wext");
    }

    if (zt_p2p_wfd_is_valid(pnic_info))
    {
        zt_u32 wfdielen = 0;

        wfdielen  = zt_p2p_wfd_append_assoc_req_ie(pnic_info, pframe, 1);
        pframe   += wfdielen;
        *pkt_len += wfdielen;
    }

    return pframe;
}

zt_u32 p2p_set_attr_content(zt_u8 *pbuf, zt_u8 attr_id, zt_u16 attr_len,
                            zt_u8 *pdata_attr, zt_u8 flag)
{
    zt_u32 a_len = 0;

    *pbuf = attr_id;
    ZT_PUT_LE16(pbuf + 1, attr_len);
    if (flag && pdata_attr)
    {
        zt_memcpy(pbuf + 3, pdata_attr, attr_len);
    }
    a_len = attr_len + 3;

    return a_len;
}

zt_s32 p2p_send_probereq(nic_info_st *pnic_info, zt_u8 *da, zt_s32 wait_ack,
                         zt_u8 flag)
{
    tx_info_st *ptx_info        = (tx_info_st *)pnic_info->tx_info;
    struct xmit_buf *pxmit_buf  = NULL;
    zt_80211_mgmt_t *pframe     = NULL;
    zt_u32 var_len              = 0;
    zt_u8 *pvar                 = NULL;
    p2p_info_st *p2p_info       = pnic_info->p2p;
    zt_u32 wfdielen = 0;

    /* alloc xmit_buf */
    pxmit_buf = zt_xmit_extbuf_new(ptx_info);
    if (pxmit_buf == NULL)
    {
        P2P_FRAME_ERR("pxmit_buf is NULL");
        return -1;
    }

    /* set frame head */
    zt_memset(pxmit_buf->pbuf, 0, TXDESC_OFFSET + ZT_OFFSETOF(zt_80211_mgmt_t,
              probe_req));
    pframe = (void *)&pxmit_buf->pbuf[TXDESC_OFFSET];

    /* set control field */
    zt_80211_hdr_type_set(pframe, ZT_80211_FRM_PROBE_REQ);

    /* set address field */
//    if (da && zt_80211_is_valid_bssid(da))
//    {
//        zt_memcpy((void *)pframe->da, (void *)da, sizeof(pframe->da));
//        zt_memcpy(pframe->sa, nic_to_local_addr(pnic_info), sizeof(pframe->sa));
//        zt_memcpy((void *)pframe->bssid, (void *)da, sizeof(pframe->bssid));
//    }
//    else
    {
        zt_memset(pframe->da, 0xff, sizeof(pframe->da));
        zt_memcpy(pframe->sa, nic_to_local_addr(pnic_info), sizeof(pframe->sa));
        zt_memset(pframe->bssid, 0xff, sizeof(pframe->bssid));
    }
    //    SCAN_DBG("SA="ZT_MAC_FMT, ZT_MAC_ARG(pframe->da));
    //    SCAN_DBG("DA="ZT_MAC_FMT, ZT_MAC_ARG(pframe->sa));
    //    SCAN_DBG("BSSID="ZT_MAC_FMT, ZT_MAC_ARG(pframe->bssid));

    /* set variable field */
    var_len = 0;
    pvar = &pframe->probe_req.variable[0];
    /*1.SSID*/
    {
        zt_scan_info_t *pscan_info = pnic_info->scan_info;
        if (pscan_info->preq->ssid_num && p2p_info->role == P2P_ROLE_CLIENT)
        {
            pvar = set_ie(pvar, ZT_80211_MGMT_EID_SSID,
                          pscan_info->preq->ssids[0].length,
                          pscan_info->preq->ssids[0].data,
                          &var_len);
            P2P_FRAME_DBG("%s, da-"ZT_MAC_FMT, pscan_info->preq->ssids[0].data,
                          ZT_MAC_ARG(pframe->da));
        }
        else
        {
            pvar = set_ie(pvar, ZT_80211_MGMT_EID_SSID, P2P_WILDCARD_SSID_LEN,
                          p2p_info->p2p_wildcard_ssid, &var_len);
            P2P_FRAME_DBG("%s, da-"ZT_MAC_FMT, p2p_info->p2p_wildcard_ssid,
                          ZT_MAC_ARG(pframe->da));
        }
    }

    /*2.Supported Rates and BSS Membership Selectors*/
    pvar = set_ie(pvar, PROBE_REQUEST_IE_RATE, 8, &p2p_info->p2p_support_rate[0],
                  &var_len);

    /*4. wps ie and p2p ie*/
    {

        mlme_info_t *mlme_info = pnic_info->mlme_info;
        if (mlme_info && mlme_info->probereq_wps_ie && mlme_info->wps_ie_len)
        {
            P2P_FRAME_DBG("wps_ie_len:%d", mlme_info->wps_ie_len);
            zt_memcpy(pvar, &mlme_info->probereq_wps_ie[0], mlme_info->wps_ie_len);
            var_len += mlme_info->wps_ie_len;
            pvar += mlme_info->wps_ie_len;
        }

        if (p2p_info->p2p_ie[ZT_P2P_IE_PROBE_REQ] &&
                p2p_info->p2p_ie_len[ZT_P2P_IE_PROBE_REQ])
        {
            P2P_FRAME_DBG("p2p_probe_req_ie_len:%d",
                          p2p_info->p2p_ie_len[ZT_P2P_IE_PROBE_REQ]);
            zt_memcpy(pvar, p2p_info->p2p_ie[ZT_P2P_IE_PROBE_REQ],
                      p2p_info->p2p_ie_len[ZT_P2P_IE_PROBE_REQ]);
            var_len += p2p_info->p2p_ie_len[ZT_P2P_IE_PROBE_REQ];
            pvar += p2p_info->p2p_ie_len[ZT_P2P_IE_PROBE_REQ];
        }

    }

    if (zt_p2p_wfd_is_valid(pnic_info))
    {
        wfdielen = zt_p2p_wfd_append_probe_req_ie(pnic_info, pvar, 1);
        pvar += wfdielen;
        var_len += wfdielen;
    }

    /* frame send */
    pxmit_buf->pkt_len = ZT_OFFSETOF(zt_80211_mgmt_t, probe_req.variable) + var_len;
    if (zt_nic_mgmt_frame_xmit(pnic_info, NULL, pxmit_buf, pxmit_buf->pkt_len))
    {
        P2P_FRAME_ERR("probe frame send fail");
        return -1;
    }
    return 0;
}

zt_s32 zt_p2p_send_probereq(nic_info_st *nic_info, zt_u8 *da)
{
    return p2p_send_probereq(nic_info, da, zt_false, 1);
}
/*
*p2p rx frame handle of probereq
*
*/
zt_s32 p2p_check_probereq(p2p_info_st *p2p_info, zt_u8 *probereq_ie, zt_u16 len,
                          zt_u8 flag)
{
    zt_u8 *p;
    zt_s32 ret = -1;
    zt_u8 *p2pie;
    zt_u32 p2pielen = 0;
    zt_s32 ssid_len = 0, rate_cnt = 0;

    p = zt_wlan_get_ie(probereq_ie, ZT_80211_MGMT_EID_SUPP_RATES,
                       (zt_s32 *)&rate_cnt, len - WLAN_HDR_A3_LEN);

    if (rate_cnt <= 4)
    {
        zt_s32 i, g_rate = 0;

        for (i = 0; i < rate_cnt; i++)
        {
            if (((*(p + 2 + i) & 0xff) != 0x02) && ((*(p + 2 + i) & 0xff) != 0x04) &&
                    ((*(p + 2 + i) & 0xff) != 0x0B) && ((*(p + 2 + i) & 0xff) != 0x16))
            {
                g_rate = 1;
            }
        }

        if (g_rate == 0)
        {
            return ret;
        }
    }

    p = zt_wlan_get_ie(probereq_ie, ZT_80211_MGMT_EID_SSID, (zt_s32 *)&ssid_len,
                       len - WLAN_HDR_A3_LEN);

    if (flag)
    {
        ssid_len &= 0xff;
    }
    if ((p2p_info->role == P2P_ROLE_DEVICE) || (p2p_info->role == P2P_ROLE_GO))
    {
        if ((p2pie = zt_p2p_get_ie(probereq_ie, len - WLAN_HDR_A3_LEN, NULL,
                                   &p2pielen)))
        {
            if ((p != NULL) &&
                    (zt_memcmp((void *)(p + 2), (void *)p2p_info->p2p_wildcard_ssid, 7) == 0))
            {
                ret = 0;
            }
            else if ((p != NULL) && (ssid_len == 0))
            {
                ret = 0;
            }
        }
        else
        {
        }

    }

    return ret;

}

static zt_s32 p2p_build_probe_resp_p2p_ie(nic_info_st *pnic_info)
{
    p2p_info_st *p2p_info = pnic_info->p2p;
    zt_u8 p2pie[256] = { 0x00 };
    zt_u32 p2pielen = 0;
    zt_u8 *pframe = p2p_info->p2p_ie[ZT_P2P_IE_PROBE_RSP];
    zt_u32 pframe_len = 0;

    /*  P2P OUI */
    p2pielen = 0;
    p2pie[p2pielen++] = 0x50;
    p2pie[p2pielen++] = 0x6F;
    p2pie[p2pielen++] = 0x9A;
    p2pie[p2pielen++] = 0x09;   /*  WFA P2P v1.0 */

    /*  Commented by Chuanghou 20240505 */
    /*  According to the P2P Specification, the probe response frame should contain 5 P2P attributes */
    /*  1. P2P Capability */
    /*  2. Extended Listen Timing */
    /*  3. Notice of Absence ( NOA )	  ( Only GO needs this ) */
    /*  4. Device Info */
    /*  5. Group Info   ( Only GO need this ) */

    /*  P2P Capability ATTR */
    /*  Type: */
    p2pie[p2pielen++] = P2P_ATTR_CAPABILITY;

    /*  Length: */
    ZT_PUT_LE16(p2pie + p2pielen, 0x0002);
    p2pielen += 2;

    /*  Value: */
    /*  Device Capability Bitmap, 1 byte */
    p2pie[p2pielen++] = DMP_P2P_DEVCAP_SUPPORT;

    /*  Group Capability Bitmap, 1 byte */
    if (P2P_ROLE_GO == p2p_get_role(p2p_info)) {
        p2pie[p2pielen] = (P2P_GRPCAP_GO | P2P_GRPCAP_INTRABSS);
        if (p2p_info->p2p_state == P2P_STATE_PROVISIONING_ING)
            p2pie[p2pielen] |= P2P_GRPCAP_GROUP_FORMATION;
        p2pielen++;
    } else if (p2p_get_role(p2p_info) == P2P_ROLE_DEVICE) {
        /*  Group Capability Bitmap, 1 byte */
        p2pie[p2pielen++] = 0;
    }

    /*  Extended Listen Timing ATTR */
    /*  Type: */
    p2pie[p2pielen++] = P2P_ATTR_EX_LISTEN_TIMING;

    /*  Length: */
    ZT_PUT_LE16(p2pie + p2pielen, 0x0004);
    p2pielen += 2;

    /*  Value: */
    /*  Availability Period */
    ZT_PUT_LE16(p2pie + p2pielen, 0xFFFF);
    p2pielen += 2;

    /*  Availability Interval */
    ZT_PUT_LE16(p2pie + p2pielen, 0xFFFF);
    p2pielen += 2;


    /* Notice of Absence ATTR */
    /*  Type:  */
    /*  Length: */
    /*  Value: */
    if (p2p_get_role(p2p_info) == P2P_ROLE_GO) {
        /* go_add_noa_attr(pwdinfo); */
    }

    /*  Device Info ATTR */
    /*  Type: */
    p2pie[p2pielen++] = P2P_ATTR_DEVICE_INFO;

    /*  Length: */
    /*  21->P2P Device Address (6bytes) + Config Methods (2bytes) + Primary Device Type (8bytes)	*/
    /*  + NumofSecondDevType (1byte) + WPS Device Name ID field (2bytes) + WPS Device Name Len field (2bytes) */
    ZT_PUT_LE16(p2pie + p2pielen, 21 + p2p_info->p2p_device_ssid_len);
    p2pielen += 2;

    /*  Value: */
    /*  P2P Device Address */
    zt_memcpy(p2pie + p2pielen, nic_to_local_addr(pnic_info), ZT_80211_MAC_ADDR_LEN);
    p2pielen += ZT_80211_MAC_ADDR_LEN;

    /*  Config Method */
    /*  This field should be big endian. Noted by P2P specification. */
    ZT_PUT_BE16(p2pie + p2pielen, p2p_info->supported_wps_cm);
    p2pielen += 2;

    {
        /*  Primary Device Type */
        /*  Category ID */
        ZT_PUT_BE16(p2pie + p2pielen, WPS_PDT_CID_TELEPHONE);
        p2pielen += 2;

        /*  OUI */
        ZT_PUT_BE32(p2pie + p2pielen, WPSOUI);
      p2pielen += 4;

        /*  Sub Category ID */
        ZT_PUT_BE16(p2pie + p2pielen, WPS_PDT_SCID_MEDIA_SERVER);
        p2pielen += 2;
    }

    /*  Number of Secondary Device Types */
    p2pie[p2pielen++] = 0x00;   /*  No Secondary Device Type List */

    /*  Device Name */
    /*  Type: */
    ZT_PUT_BE16(p2pie + p2pielen, WPS_ATTR_DEVICE_NAME);
    p2pielen += 2;

    /*  Length: */
    ZT_PUT_BE16(p2pie + p2pielen, p2p_info->p2p_device_ssid_len);
    p2pielen += 2;

    /*  Value: */
    zt_memcpy(p2pie + p2pielen, p2p_info->p2p_device_ssid, p2p_info->p2p_device_ssid_len);
    p2pielen += p2p_info->p2p_device_ssid_len;

    /* Group Info ATTR */
    /*  Type: */
    /*  Length: */
    /*  Value: */
    pframe = set_ie(pframe, ZT_80211_MGMT_EID_VENDOR_SPECIFIC, p2pielen, p2pie, &pframe_len);

    p2p_info->p2p_ie_len[ZT_P2P_IE_PROBE_RSP] = pframe_len;

    return 0;

}

static zt_s32 p2p_build_probe_resp_wps_ie(nic_info_st *pnic_info)
{
    mlme_info_t *pmlme_info = (mlme_info_t *)pnic_info->mlme_info;
    p2p_info_st *p2p_info = pnic_info->p2p;
    zt_u8 *pframe = pmlme_info->wps_probe_resp_ie;
    zt_u32 pframe_len = 0;
    zt_u8 wpsielen = 0;
    zt_u8 wpsie[255] = { 0x00 };
    /*  Todo: WPS IE */
    /*  Noted by Houchuang 20240505 */
    /*  According to the WPS specification, all the WPS attribute is presented by Big Endian. */

    /*  WPS OUI */
    *(zt_u32 *)(wpsie) = zt_cpu_to_be32(WPSOUI);
    wpsielen += 4;

    /*  WPS version */
    /*  Type: */
    *(zt_u16 *)(wpsie + wpsielen) = zt_cpu_to_be16(WPS_ATTR_VER1);
    wpsielen += 2;

    /*  Length: */
    *(zt_u16 *)(wpsie + wpsielen) = zt_cpu_to_be16(0x0001);
    wpsielen += 2;

    /*  Value: */
    wpsie[wpsielen++] = WPS_VERSION_1;  /*  Version 1.0 */

    /*  WiFi Simple Config State */
    /*  Type: */
    *(zt_u16 *)(wpsie + wpsielen) = zt_cpu_to_be16(WPS_ATTR_SIMPLE_CONF_STATE);
    wpsielen += 2;

    /*  Length: */
    *(zt_u16 *)(wpsie + wpsielen) = zt_cpu_to_be16(0x0001);
    wpsielen += 2;

    /*  Value: */
    wpsie[wpsielen++] = WPS_WSC_STATE_NOT_CONFIG;   /*  Not Configured. */

    /*  Response Type */
    /*  Type: */
    *(zt_u16 *)(wpsie + wpsielen) = zt_cpu_to_be16(WPS_ATTR_RESP_TYPE);
    wpsielen += 2;

    /*  Length: */
    *(zt_u16 *)(wpsie + wpsielen) = zt_cpu_to_be16(0x0001);
    wpsielen += 2;

    /*  Value: */
    wpsie[wpsielen++] = WPS_RESPONSE_TYPE_INFO_ONLY;

    /*  UUID-E */
    /*  Type: */
    *(zt_u16 *)(wpsie + wpsielen) = zt_cpu_to_be16(WPS_ATTR_UUID_E);
    wpsielen += 2;

    /*  Length: */
    *(zt_u16 *)(wpsie + wpsielen) = zt_cpu_to_be16(0x0010);
    wpsielen += 2;

    /*  Value: */
    zt_memcpy(wpsie + wpsielen, p2p_info->p2p_uuid, 0x10);
    wpsielen += 0x10;

    /*  Manufacturer */
    /*  Type: */
    *(zt_u16 *)(wpsie + wpsielen) = zt_cpu_to_be16(WPS_ATTR_MANUFACTURER);
    wpsielen += 2;

    /*  Length: */
    *(zt_u16 *)(wpsie + wpsielen) = zt_cpu_to_be16(0x0004);
    wpsielen += 2;

    /*  Value: */
    zt_memcpy(wpsie + wpsielen, "ZTOP", 4);
    wpsielen += 4;

    /*  Model Name */
    /*  Type: */
    *(zt_u16 *)(wpsie + wpsielen) = zt_cpu_to_be16(WPS_ATTR_MODEL_NAME);
    wpsielen += 2;

    /*  Length: */
    *(zt_u16 *)(wpsie + wpsielen) = zt_cpu_to_be16(0x0006);
    wpsielen += 2;

    /*  Value: */
    zt_memcpy(wpsie + wpsielen, "zt9101", 6);
    wpsielen += 6;

    /*  Model Number */
    /*  Type: */
    *(zt_u16 *)(wpsie + wpsielen) = zt_cpu_to_be16(WPS_ATTR_MODEL_NUMBER);
    wpsielen += 2;

    /*  Length: */
    *(zt_u16 *)(wpsie + wpsielen) = zt_cpu_to_be16(0x0001);
    wpsielen += 2;

    /*  Value: */
    wpsie[wpsielen++] = 0x31; 	  /*  character 1 */

    /*  Serial Number */
    /*  Type: */
    *(zt_u16 *)(wpsie + wpsielen) = zt_cpu_to_be16(WPS_ATTR_SERIAL_NUMBER);
    wpsielen += 2;

    /*  Length: */
    *(zt_u16 *)(wpsie + wpsielen) = zt_cpu_to_be16(1);
    wpsielen += 2;

    /*  Value: */
    *(zt_u8 *)(wpsie + wpsielen) = 20;
    wpsielen += 1;

    /*  Primary Device Type */
    /*  Type: */
    *(zt_u16 *)(wpsie + wpsielen) = zt_cpu_to_be16(WPS_ATTR_PRIMARY_DEV_TYPE);
    wpsielen += 2;

    /*  Length: */
    *(zt_u16 *)(wpsie + wpsielen) = zt_cpu_to_be16(0x0008);
    wpsielen += 2;

    /*  Value: */
    /*  Category ID */
    *(zt_u16 *)(wpsie + wpsielen) = zt_cpu_to_be16(WPS_PDT_CID_TELEPHONE);
    wpsielen += 2;

    /*  OUI */
    *(zt_u32 *)(wpsie + wpsielen) = zt_cpu_to_be32(WPSOUI);
    wpsielen += 4;

    /*  Sub Category ID */
    *(zt_u16 *)(wpsie + wpsielen) = zt_cpu_to_be16(WPS_PDT_SCID_MEDIA_SERVER);
    wpsielen += 2;

    /*  Device Name */
    /*  Type: */
    *(zt_u16 *)(wpsie + wpsielen) = zt_cpu_to_be16(WPS_ATTR_DEVICE_NAME);
    wpsielen += 2;

    /*  Length: */
    *(zt_u16 *)(wpsie + wpsielen) = zt_cpu_to_be16(p2p_info->p2p_device_ssid_len);
    wpsielen += 2;

    /*  Value: */
    zt_memcpy(wpsie + wpsielen, p2p_info->p2p_device_ssid, p2p_info->p2p_device_ssid_len);
    wpsielen += p2p_info->p2p_device_ssid_len;

    /*  Config Method */
    /*  Type: */
    *(zt_u16 *)(wpsie + wpsielen) = zt_cpu_to_be16(WPS_ATTR_CONF_METHOD);
    wpsielen += 2;

    /*  Length: */
    *(zt_u16 *)(wpsie + wpsielen) = zt_cpu_to_be16(0x0002);
    wpsielen += 2;

    /*  Value: */
    *(zt_u16 *)(wpsie + wpsielen) = zt_cpu_to_be16(0x4388);
    wpsielen += 2;

    /*	Vendor Extension */
    /*	Type: */
    *(zt_u16 *)(wpsie + wpsielen) = zt_cpu_to_be16(WPS_ATTR_VENDOR_EXT);
    wpsielen += 2;

    /*	Length: */
    *(zt_u16 *)(wpsie + wpsielen) = zt_cpu_to_be16(0x0006);
    wpsielen += 2;

    /*	Value: */
    *(zt_u16 *)(wpsie + wpsielen) = zt_cpu_to_be16(0x0037);
    wpsielen += 2;
    *(zt_u16 *)(wpsie + wpsielen) = zt_cpu_to_be16(0x2A00);
    wpsielen += 2;
    *(zt_u16 *)(wpsie + wpsielen) = zt_cpu_to_be16(0x0120);
    wpsielen += 2;

    pframe = set_ie(pframe, ZT_80211_MGMT_EID_VENDOR_SPECIFIC, wpsielen, wpsie, &pframe_len);

    pmlme_info->wps_probe_resp_ie_len = pframe_len;

    return 0;
}


zt_s32 p2p_send_probersp(nic_info_st *pnic_info, unsigned char *da, zt_u8 flag)
{
    tx_info_st *ptx_info        = (tx_info_st *)pnic_info->tx_info;
    struct xmit_buf *pxmit_buf  = NULL;
    zt_80211_mgmt_t *pframe     = NULL;
    zt_u32 var_len              = 0;
    zt_u8 *pvar                 = NULL;
    p2p_info_st *p2p_info       = pnic_info->p2p;
    mlme_info_t *pmlme_info     = (mlme_info_t *)pnic_info->mlme_info;
    zt_u16 beacon_interval      = 100;
    zt_u16 capInfo = 0;
    zt_u32 wfdielen = 0;

    P2P_FRAME_DBG("rsp to "ZT_MAC_FMT, ZT_MAC_ARG(da));

    /* aclloc xmit buf*/
    pxmit_buf = zt_xmit_extbuf_new(ptx_info);
    if (pxmit_buf == NULL)
    {
        P2P_FRAME_ERR("pxmit_buf aclloc failed ");
        return -1;
    }

    /*set frame head */
    zt_memset(pxmit_buf->pbuf, 0, TXDESC_OFFSET + ZT_OFFSETOF(zt_80211_mgmt_t,
              probe_resp));
    pframe = (void *)&pxmit_buf->pbuf[TXDESC_OFFSET];

    /*set control field*/
    zt_80211_hdr_type_set(pframe, ZT_80211_FRM_PROBE_RESP);

    /*set address*/
    zt_memcpy((void *)pframe->da, (void *)da, sizeof(pframe->da));
    zt_memcpy((void *)pframe->sa, nic_to_local_addr(pnic_info), sizeof(pframe->sa));
    zt_memcpy((void *)pframe->bssid, nic_to_local_addr(pnic_info),
              sizeof(pframe->bssid));

    /*set pies fiexd field */
    pframe->probe_resp.intv = beacon_interval;

    capInfo |= ZT_BIT(5);  //cap_ShortPremble
    capInfo |= ZT_BIT(10); //cap_ShortSlot
    pframe->probe_resp.capab = capInfo;

    /*set variable filed*/
    pvar = &pframe->probe_resp.variable[0];

    pvar = set_ie(pvar, ZT_80211_MGMT_EID_SSID, 7, p2p_info->p2p_wildcard_ssid,
                  &var_len);

    pvar = set_ie(pvar, ZT_80211_MGMT_EID_SUPP_RATES, 8,
                  &p2p_info->p2p_support_rate[0], &var_len);

    pvar = set_ie(pvar, ZT_80211_MGMT_EID_DS_PARAMS, 1,
                  (zt_u8 *)&p2p_info->listen_channel, &var_len);


    if (pmlme_info->wps_probe_resp_ie_len == 0)
    {
        p2p_build_probe_resp_wps_ie(pnic_info);
    }

    if (p2p_info->p2p_ie_len[ZT_P2P_IE_PROBE_RSP] == 0)
    {
        p2p_build_probe_resp_p2p_ie(pnic_info);
    }

    if (pmlme_info->wps_probe_resp_ie_len != 0 &&
            p2p_info->p2p_ie_len[ZT_P2P_IE_PROBE_RSP] != 0)
    {
        zt_memcpy(pvar, &pmlme_info->wps_probe_resp_ie[0],
                  pmlme_info->wps_probe_resp_ie_len);
        var_len += pmlme_info->wps_probe_resp_ie_len;
        pvar += pmlme_info->wps_probe_resp_ie_len;

        zt_memcpy(pvar, p2p_info->p2p_ie[ZT_P2P_IE_PROBE_RSP],
                  p2p_info->p2p_ie_len[ZT_P2P_IE_PROBE_RSP]);
        var_len += p2p_info->p2p_ie_len[ZT_P2P_IE_PROBE_RSP];
        pvar += p2p_info->p2p_ie_len[ZT_P2P_IE_PROBE_RSP];
    }

    if (zt_p2p_wfd_is_valid(pnic_info))
    {
        wfdielen = zt_p2p_wfd_append_probe_resp_ie(pnic_info, pvar, 1);
        pvar += wfdielen;
        var_len += wfdielen;
    }

    /*frame send*/
    pxmit_buf->pkt_len = ZT_OFFSETOF(zt_80211_mgmt_t,
                                     probe_resp.variable) + var_len;
    //P2P_FRAME_ARRAY(pframe,pxmit_buf->pkt_len);
    //    P2P_FRAME_DBG("p2p probersp frame send  pkt_len=%d",pxmit_buf->pkt_len);

    P2P_FRAME_DBG("p2p probersp send ");
    if (zt_nic_mgmt_frame_xmit(pnic_info, NULL, pxmit_buf, pxmit_buf->pkt_len))
    {
        P2P_FRAME_ERR("p2p probersp frame send fail");
        return -1;
    }

    return 0;

}


static zt_u32 p2p_listen_state_process(nic_info_st *pnic_info, zt_u8 *da,
                                       zt_u8 flag)
{

    p2p_info_st *p2p_info = pnic_info->p2p;

    P2P_FRAME_DBG("response=%d, listen_ch:%d,remain_ch:%d", p2p_info->is_ro_ch,
                  p2p_info->listen_channel, p2p_info->remain_ch);

    if (flag && p2p_info->is_ro_ch == zt_true )//&& 0 == p2p_info->go_negoing)
    {
        return p2p_send_probersp(pnic_info, da, 1);
    }
    return -1;
}



zt_s32 zt_p2p_recv_probereq(nic_info_st *pnic_info, zt_80211_mgmt_t *pframe,
                            zt_u16 frame_len)
{
    zt_wlan_mgmt_info_t *pwlan_info = pnic_info->wlan_mgmt_info;
    p2p_info_st *p2p_info = pnic_info->p2p;
    zt_u8 *probereq_ie = pframe->probe_req.variable;
    zt_s8 is_valid_p2p_probereq = 0;

    if (!pnic_info->is_up)
    {
        return -1;
    }

    if (!(zt_80211_is_same_addr(pframe->da, pwlan_info->cur_network.mac_addr) ||
            zt_80211_is_bcast_addr(pframe->da)))
    {
        P2P_FRAME_WARN("probe request target address invalid");
        return -3;
    }
    if ((p2p_info->p2p_state != P2P_STATE_NONE) && zt_true == p2p_info->report_mgmt)
    {
        if (zt_true == p2p_info->scb.init_flag && p2p_info->scb.rx_mgmt)
        {
            P2P_FRAME_DBG(" da addr    : "ZT_MAC_FMT, ZT_MAC_ARG(pframe->da));
            P2P_FRAME_DBG(" sa addr    : "ZT_MAC_FMT, ZT_MAC_ARG(pframe->sa));
            P2P_FRAME_DBG(" bssid addr : "ZT_MAC_FMT, ZT_MAC_ARG(pframe->bssid));
            return p2p_info->scb.rx_mgmt(pnic_info, pframe, frame_len);
        }
    }

    is_valid_p2p_probereq = p2p_check_probereq(p2p_info, probereq_ie, frame_len, 1);
    if (is_valid_p2p_probereq != 0)
    {
        return -4;
    }

    P2P_FRAME_DBG("%s,%s", zt_p2p_state_to_str(p2p_info->p2p_state),
                  zt_p2p_role_to_str(p2p_info->role));
    if ((p2p_info->p2p_state != P2P_STATE_NONE) &&
            (p2p_info->p2p_state != P2P_STATE_IDLE) &&
            (p2p_info->role      != P2P_ROLE_CLIENT) &&
            (p2p_info->p2p_state != P2P_STATE_SCAN) &&
            (p2p_info->p2p_state != P2P_STATE_FIND_PHASE_SEARCH)
       )
    {
        if (P2P_ROLE_DEVICE == p2p_info->role)
        {

            if (p2p_listen_state_process(pnic_info, pframe->sa, 1))
            {
                P2P_FRAME_DBG("listen sate process do not send rsp");
            }

        }
        else if (P2P_ROLE_GO == p2p_info->role)
        {
            return -1;
        }
    }

    return 0;
}

static zt_u8 *p2p_dump_attr_ch_list(zt_u8 *p2p_ie, zt_u32 p2p_ielen, zt_u8 *buf,
                                    zt_u32 buf_len, zt_u8 flag)
{
    zt_u32 attr_contentlen  = 0;
    zt_u8 *pattr            = NULL;
    zt_u8 ch_cnt            = 0;
    zt_u8 ch_list[40]       = {0};

    pattr = zt_p2p_get_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_CH_LIST, NULL,
                                    &attr_contentlen);
    if (pattr)
    {
        zt_s32 i, j;
        zt_u32 num_of_ch;
        zt_u8 *pattr_temp = pattr + 3;
        zt_u8 index = 0;

        attr_contentlen -= 3;
        if (flag)
        {
            zt_memset(ch_list, 0, 40);
        }
        while (attr_contentlen > 0)
        {
            num_of_ch = *(pattr_temp + 1);

            for (i = 0; i < num_of_ch; i++)
            {
                for (j = 0; j < ch_cnt; j++)
                {
                    if (ch_list[j] == *(pattr_temp + 2 + i))
                    {
                        break;
                    }
                }
                if (j >= ch_cnt)
                {
                    ch_list[ch_cnt++] = *(pattr_temp + 2 + i);
                }

            }

            pattr_temp += (2 + num_of_ch);
            attr_contentlen -= (2 + num_of_ch);
        }


        for (j = 0; j < ch_cnt; j++)
        {
            if (9 >= ch_list[j])
            {
                buf[index++] = ch_list[j] + '0';
            }
            else if (10 <= ch_list[j] && 99 >= ch_list[j])
            {
                zt_u8 ch_n1  = ch_list[j] / 10;
                zt_u8 ch_n2  = ch_list[j] % 10;
                buf[index++] = ch_n1 + '0';
                buf[index++] = ch_n2 + '0';
            }

            if (j != ch_cnt - 1)
            {
                buf[index++] = ',';
            }
        }
    }
    return buf;
}

static zt_bool p2p_compare_nego_intent(zt_u8 req, zt_u8 resp, zt_u8 flag)
{
    if (flag)
    {
        if (req >> 1 == resp >> 1)
        {
            return req & 0x01 ? zt_true : zt_false;
        }
        else if (req >> 1 > resp >> 1)
        {
            return zt_true;
        }
        else
        {
            return zt_false;
        }
    }

    return zt_false;
}

static zt_bool p2p_check_ch_list_with_buddy(nic_info_st *pnic_info,
        zt_u8 *frame_body, zt_u32 len, zt_u8 flag)
{
    zt_bool fit         = zt_false;
    zt_u8 *ies          = NULL;
    zt_u8 *p2p_ie       = NULL;
    zt_u32 ies_len      = 0;
    zt_u32 p2p_ielen    = 0;
    zt_u8 buddy_ch = zt_p2p_get_buddy_channel(pnic_info);

    ies = (zt_u8 *)(frame_body + ZT_PUBLIC_ACTION_IE_OFFSET);
    ies_len = len - ZT_PUBLIC_ACTION_IE_OFFSET;

    p2p_ie = zt_p2p_get_ie(ies, ies_len, NULL, &p2p_ielen);
    if (0 == flag)
    {
        return fit;
    }

    while (p2p_ie)
    {
        zt_u32 attr_contentlen = 0;
        zt_u8 *pattr = NULL;
        pattr = zt_p2p_get_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_CH_LIST, NULL,
                                        (zt_u32 *) & attr_contentlen);
        if (pattr)
        {
            zt_s32 i;
            zt_u32 num_of_ch;
            zt_u8 *pattr_temp = pattr + 3;

            attr_contentlen -= 3;

            while (attr_contentlen > 0)
            {
                num_of_ch = *(pattr_temp + 1);

                for (i = 0; i < num_of_ch; i++)
                {
                    if (*(pattr_temp + 2 + i) == buddy_ch)
                    {
                        P2P_FRAME_DBG(" ch_list fit buddy_ch:%u\n", buddy_ch);
                        fit = zt_true;
                        break;
                    }
                }

                pattr_temp += (2 + num_of_ch);
                attr_contentlen -= (2 + num_of_ch);
            }
        }

        p2p_ie = zt_p2p_get_ie(p2p_ie + p2p_ielen, ies_len - (p2p_ie - ies + p2p_ielen),
                               NULL, &p2p_ielen);
    }

    return fit;
}

static void p2p_adjust_channel(nic_info_st *pnic_info, zt_u8 *frame_body,
                               zt_u32 len, zt_u8 flag)
{
    zt_u8 *ies = NULL;
    zt_u8 *p2p_ie = NULL;
    zt_u32 ies_len, p2p_ielen;
    zt_u8 buddy_ch = zt_p2p_get_buddy_channel(pnic_info);

    P2P_FRAME_DBG("buddy_ch:%d", buddy_ch);

    ies = (zt_u8 *)(frame_body + ZT_PUBLIC_ACTION_IE_OFFSET);
    ies_len = len - ZT_PUBLIC_ACTION_IE_OFFSET;

    p2p_ie = zt_p2p_get_ie(ies, ies_len, NULL, &p2p_ielen);

    if (0 == flag)
    {
        return;
    }

    while (p2p_ie)
    {
        zt_u32 attr_contentlen = 0;
        zt_u8 *pattr = NULL;

        pattr = zt_p2p_get_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_CH_LIST, NULL,
                                        (zt_u32 *) & attr_contentlen);
        if (pattr)
        {
            zt_s32 i;
            zt_u32 num_of_ch;
            zt_u8 *pattr_temp = pattr + 3;
            P2P_FRAME_DBG("attr_contentlen:%d", attr_contentlen);
            P2P_FRAME_ARRAY(pattr, attr_contentlen);

            attr_contentlen -= 3;

            while (attr_contentlen > 0)
            {
                num_of_ch = *(pattr_temp + 1);

                for (i = 0; i < num_of_ch; i++)
                {
                    *(pattr_temp + 2 + i) = buddy_ch;
                }

                pattr_temp      += (2 + num_of_ch);
                attr_contentlen -= (2 + num_of_ch);
            }
        }
        else
        {
            P2P_FRAME_DBG("no P2P_ATTR_CH_LIST");
        }

        attr_contentlen = 0;
        pattr = NULL;

        pattr = zt_p2p_get_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_OPERATING_CH, NULL,
                                        (zt_u32 *) & attr_contentlen);
        if (pattr)
        {
            P2P_FRAME_DBG("attr_contentlen:%d", attr_contentlen);
            P2P_FRAME_ARRAY(pattr, attr_contentlen);
            *(pattr + 4) = buddy_ch;
        }
        else
        {
            P2P_FRAME_DBG("no P2P_ATTR_OPERATING_CH");
        }

        p2p_ie = zt_p2p_get_ie(p2p_ie + p2p_ielen, ies_len - (p2p_ie - ies + p2p_ielen),
                               NULL, &p2p_ielen);

    }

}

static void p2p_change_p2pie_ch_list(nic_info_st *pnic_info, zt_u8 *frame_body,
                                     zt_u32 len, zt_u8 ch, zt_u8 flag)
{
    zt_u8 *ies, *p2p_ie;
    zt_u32 ies_len, p2p_ielen;
    ies = (zt_u8 *)(frame_body + ZT_PUBLIC_ACTION_IE_OFFSET);
    ies_len = len - ZT_PUBLIC_ACTION_IE_OFFSET;

    p2p_ie = zt_p2p_get_ie(ies, ies_len, NULL, &p2p_ielen);

    if (0 == flag)
    {
        return;
    }

    while (p2p_ie)
    {
        zt_u32 attr_contentlen = 0;
        zt_u8 *pattr = NULL;

        pattr = zt_p2p_get_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_CH_LIST, NULL,
                                        (zt_u32 *) & attr_contentlen);
        if (pattr)
        {
            zt_s32 i;
            zt_u32 num_of_ch;
            zt_u8 *pattr_temp = pattr + 3;

            attr_contentlen -= 3;

            while (attr_contentlen > 0)
            {
                num_of_ch = *(pattr_temp + 1);

                for (i = 0; i < num_of_ch; i++)
                {
                    *(pattr_temp + 2 + i) = ch;
                }

                pattr_temp += (2 + num_of_ch);
                attr_contentlen -= (2 + num_of_ch);
            }
        }

        p2p_ie = zt_p2p_get_ie(p2p_ie + p2p_ielen, ies_len - (p2p_ie - ies + p2p_ielen),
                               NULL, &p2p_ielen);
    }

}

static zt_bool p2p_check_p2pie_op_ch_with_buddy(nic_info_st *pnic_info,
        zt_u8 *frame_body, zt_u32 len, zt_u8 flag)
{
    zt_bool fit = zt_false;
    zt_u8 *ies, *p2p_ie;
    zt_u32 ies_len, p2p_ielen;
    zt_u8 buddy_ch = zt_p2p_get_buddy_channel(pnic_info);

    ies = (zt_u8 *)(frame_body + ZT_PUBLIC_ACTION_IE_OFFSET);
    ies_len = len - ZT_PUBLIC_ACTION_IE_OFFSET;

    p2p_ie = zt_p2p_get_ie(ies, ies_len, NULL, &p2p_ielen);

    if (0 == flag)
    {
        return fit;
    }
    while (p2p_ie)
    {
        zt_u32 attr_contentlen = 0;
        zt_u8 *pattr = NULL;

        attr_contentlen = 0;
        pattr = zt_p2p_get_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_OPERATING_CH, NULL,
                                        (zt_u32 *) & attr_contentlen);
        if (pattr)
        {
            if (*(pattr + 4) == buddy_ch)
            {
                P2P_FRAME_DBG(" op_ch fit buddy_ch:%u\n", buddy_ch);
                fit = zt_true;
                break;
            }
        }

        p2p_ie = zt_p2p_get_ie(p2p_ie + p2p_ielen, ies_len - (p2p_ie - ies + p2p_ielen),
                               NULL, &p2p_ielen);
    }


    return fit;
}

zt_s32 p2p_check_nego_req(nic_info_st *pnic_info,
                          p2p_frame_check_param_st *check_param)
{
    zt_u8 *cont             = NULL;
    zt_u32 cont_len         = 0;
    zt_u8 ch_list_buf[128]  = { '\0' };
    zt_s32 op_ch            = 0;
    zt_s32 listen_ch        = 0;
    zt_u8 intent            = 0;
    zt_u16 capability       = 0;
    zt_u8 go_timeout        = 0;
    zt_u8 gc_timeout        = 0;
    zt_u8 ch;
    CHANNEL_WIDTH bw;
    HAL_PRIME_CH_OFFSET offset;
    p2p_info_st *p2p_info   = pnic_info->p2p;
    zt_widev_nego_info_t *nego_info = &p2p_info->nego_info;

    if (check_param->is_tx)
    {
        if (zt_p2p_check_buddy_linkstate(pnic_info) &&
                p2p_info->full_ch_in_p2p_handshake == 0)
        {
            p2p_adjust_channel(pnic_info, check_param->frame_body,
                               check_param->frame_body_len, 1);
        }
    }
    else
    {
        if (zt_p2p_check_buddy_linkstate(pnic_info)
                && p2p_check_ch_list_with_buddy(pnic_info,
                                                check_param->frame_body,
                                                check_param->frame_body_len,
                                                1) == zt_false
                && p2p_info->full_ch_in_p2p_handshake == 0)
        {
            P2P_FRAME_DBG(" ch_list has no intersect with buddy\n");
            p2p_change_p2pie_ch_list(pnic_info, check_param->frame_body,
                                     check_param->frame_body_len,
                                     0, 1);
        }
    }

    if ((cont = zt_p2p_get_attr_content(check_param->p2p_ie, check_param->p2p_ielen,
                                        P2P_ATTR_OPERATING_CH, NULL, &cont_len)))
    {
        op_ch = *(cont + 4);
    }
    if ((cont = zt_p2p_get_attr_content(check_param->p2p_ie, check_param->p2p_ielen,
                                        P2P_ATTR_LISTEN_CH, NULL, &cont_len)))
    {
        listen_ch = *(cont + 4);
    }
    if ((cont = zt_p2p_get_attr_content(check_param->p2p_ie, check_param->p2p_ielen,
                                        P2P_ATTR_GO_INTENT, NULL, &cont_len)))
    {
        intent = *cont;
    }
    if ((cont = zt_p2p_get_attr_content(check_param->p2p_ie, check_param->p2p_ielen,
                                        P2P_ATTR_CAPABILITY, NULL, &cont_len)))
    {
        capability = *(zt_u16 *)cont;
    }
    if ((cont = zt_p2p_get_attr_content(check_param->p2p_ie, check_param->p2p_ielen,
                                        P2P_ATTR_CONF_TIMEOUT, NULL, &cont_len)))
    {
        go_timeout = cont[0];
        gc_timeout = cont[1];
    }

    if (nego_info->token != check_param->dialogToken)
    {

        zt_widev_nego_info_init(nego_info);
    }

    zt_memcpy(nego_info->peer_mac,
              check_param->is_tx ? GetAddr1Ptr(check_param->buf) : GetAddr2Ptr(
                  check_param->buf), ZT_80211_MAC_ADDR_LEN);
    nego_info->active           = check_param->is_tx ? 1 : 0;
    nego_info->token            = check_param->dialogToken;
    nego_info->req_op_ch        = op_ch;
    nego_info->req_listen_ch    = listen_ch;
    nego_info->req_intent       = intent;
    nego_info->state            = 0;
    if (!check_param->is_tx)
    {
        p2p_info->peer_listen_channel = listen_ch;
    }

    p2p_dump_attr_ch_list(check_param->p2p_ie, check_param->p2p_ielen, ch_list_buf,
                          128, 1);
    zt_hw_info_get_channel_bw_ext(pnic_info, &ch, &bw, &offset);
    P2P_FRAME_INFO("%s(%d~%d):P2P_GO_NEGO_REQ, dialogToken=%d, intent:%u%s, listen_ch:%d, op_ch:%d, ch_list:%s, capability:0x%x, go_t:%d,gc_t:%d\n",
                   (check_param->is_tx == zt_true) ? "Tx" : "Rx", p2p_info->remain_ch, ch,
                   check_param->dialogToken,
                   (intent >> 1), intent & 0x1 ? "+" : "-", listen_ch, op_ch, ch_list_buf,
                   capability, go_timeout, gc_timeout);

    return 0;
}

zt_s32 p2p_check_nego_rsp(nic_info_st *pnic_info,
                          p2p_frame_check_param_st *check_param)
{
    zt_u8 *cont             = NULL;
    zt_u32 cont_len         = 0;
    zt_s32 status           = -1;
    zt_u8 ch_list_buf[128]  = { '\0' };
    zt_s32 op_ch            = -1;
    zt_u8 intent            = 0;
    zt_u8 ch;
    CHANNEL_WIDTH bw;
    HAL_PRIME_CH_OFFSET offset;
    p2p_info_st *p2p_info   = pnic_info->p2p;
    zt_widev_nego_info_t *nego_info = &p2p_info->nego_info;

    if (check_param->is_tx)
    {
        if (zt_p2p_check_buddy_linkstate(pnic_info)
                && p2p_info->full_ch_in_p2p_handshake == 0)
        {
            p2p_adjust_channel(pnic_info,
                               check_param->frame_body,
                               check_param->frame_body_len,
                               1);
        }
    }
    else
    {
        p2p_info->provdisc_req_issued = zt_false;
        if (zt_p2p_check_buddy_linkstate(pnic_info)
                && p2p_check_ch_list_with_buddy(pnic_info,
                                                check_param->frame_body,
                                                check_param->frame_body_len,
                                                1) == zt_false
                && p2p_info->full_ch_in_p2p_handshake == 0)
        {
            P2P_FRAME_DBG(" ch_list has no intersect with buddy\n");
            p2p_change_p2pie_ch_list(pnic_info, check_param->frame_body,
                                     check_param->frame_body_len,
                                     0, 1);
        }
    }

    if ((cont = zt_p2p_get_attr_content(check_param->p2p_ie, check_param->p2p_ielen,
                                        P2P_ATTR_OPERATING_CH, NULL, &cont_len)))
    {
        op_ch = *(cont + 4);
    }
    if ((cont = zt_p2p_get_attr_content(check_param->p2p_ie, check_param->p2p_ielen,
                                        P2P_ATTR_GO_INTENT, NULL, &cont_len)))
    {
        intent = *cont;
    }
    if ((cont = zt_p2p_get_attr_content(check_param->p2p_ie, check_param->p2p_ielen,
                                        P2P_ATTR_STATUS, NULL, &cont_len)))
    {
        status = *cont;
    }

    if (nego_info->token == check_param->dialogToken && nego_info->state == 0
            && zt_memcmp(nego_info->peer_mac,
                         check_param->is_tx ? GetAddr1Ptr(check_param->buf) : GetAddr2Ptr(
                             check_param->buf), ZT_80211_MAC_ADDR_LEN) == 0)
    {
        nego_info->status = (status == -1) ? 0xff : status;
        nego_info->rsp_op_ch = op_ch;
        nego_info->rsp_intent = intent;
        nego_info->state = 1;
        if (status != 0)
        {
            nego_info->token = 0;
        }
    }

    p2p_dump_attr_ch_list(check_param->p2p_ie, check_param->p2p_ielen, ch_list_buf,
                          128, 1);
    zt_hw_info_get_channel_bw_ext(pnic_info, &ch, &bw, &offset);
    P2P_FRAME_INFO("ZT_%s(%d~%d):P2P_GO_NEGO_RESP, dialogToken=%d, intent:%u%s, status:%d, op_ch:%d, ch_list:%s\n",
                   (check_param->is_tx == zt_true) ? "Tx" : "Rx", p2p_info->remain_ch, ch,
                   check_param->dialogToken,
                   (intent >> 1), intent & 0x1 ? "+" : "-", status, op_ch,
                   ch_list_buf);

    return 0;
}


zt_s32 p2p_check_nego_confirm(nic_info_st *pnic_info,
                              p2p_frame_check_param_st *check_param)
{
    zt_u8 *cont             = NULL;
    zt_u32 cont_len         = 0;
    zt_s32 status           = -1;
    zt_u8 ch_list_buf[128]  = { '\0' };
    zt_s32 op_ch            = -1;
    zt_u8 ch;
    CHANNEL_WIDTH bw;
    HAL_PRIME_CH_OFFSET offset;
    p2p_info_st *p2p_info   = pnic_info->p2p;
    zt_widev_nego_info_t *nego_info = &p2p_info->nego_info;
    zt_bool is_go           = zt_false;

    if (check_param->is_tx)
    {
        if (zt_p2p_check_buddy_linkstate(pnic_info) &&
                p2p_info->full_ch_in_p2p_handshake == 0)
        {
            p2p_adjust_channel(pnic_info, check_param->frame_body,
                               check_param->frame_body_len, 1);
        }

    }
    else
    {
    }

    if ((cont = zt_p2p_get_attr_content(check_param->p2p_ie, check_param->p2p_ielen,
                                        P2P_ATTR_OPERATING_CH, NULL, &cont_len)))
    {
        op_ch = *(cont + 4);
    }
    if ((cont = zt_p2p_get_attr_content(check_param->p2p_ie, check_param->p2p_ielen,
                                        P2P_ATTR_STATUS, NULL, &cont_len)))
    {
        status = *cont;
    }

    if (nego_info->token == check_param->dialogToken && nego_info->state == 1 &&
            zt_memcmp(nego_info->peer_mac,
                      check_param->is_tx ? GetAddr1Ptr(check_param->buf) : GetAddr2Ptr(
                          check_param->buf), ZT_80211_MAC_ADDR_LEN) == 0)
    {
        nego_info->status = (status == -1) ? 0xff : status;
        nego_info->conf_op_ch = (op_ch == -1) ? 0 : op_ch;
        nego_info->state = 2;

        if (status == 0)
        {
            if (p2p_compare_nego_intent(nego_info->req_intent, nego_info->rsp_intent,
                                        1) ^ !check_param->is_tx)
            {
                is_go = zt_true;
            }
        }

        nego_info->token = 0;
    }
    p2p_info->link_channel = op_ch;
    p2p_dump_attr_ch_list(check_param->p2p_ie, check_param->p2p_ielen, ch_list_buf,
                          128, 1);
    zt_hw_info_get_channel_bw_ext(pnic_info, &ch, &bw, &offset);
    P2P_FRAME_INFO("ZT_%s(%d~%d):P2P_GO_NEGO_CONF, dialogToken=%d, status:%d, op_ch:%d, ch_list:%s\n",
                   (check_param->is_tx == zt_true) ? "Tx" : "Rx", p2p_info->remain_ch, ch,
                   check_param->dialogToken, status, op_ch, ch_list_buf);

    return 0;
}

zt_s32 p2p_check_invit_req(nic_info_st *pnic_info,
                           p2p_frame_check_param_st *check_param)
{
    zt_u8 *cont             = NULL;
    zt_u32 cont_len         = 0;
    zt_u8 *frame_body       = NULL;
    zt_u8 ch_list_buf[128]  = { '\0' };
    zt_s32 op_ch            = -1;
    zt_u8 ch;
    CHANNEL_WIDTH bw;
    HAL_PRIME_CH_OFFSET offset;
    p2p_info_st *p2p_info   = pnic_info->p2p;
    struct zt_widev_invit_info *invit_info = &p2p_info->invit_info;
    zt_s32 flags = -1;

    if (check_param->is_tx)
    {
        if (zt_p2p_check_buddy_linkstate(pnic_info)  &&
                p2p_info->full_ch_in_p2p_handshake == 0)
        {
            p2p_adjust_channel(pnic_info, check_param->frame_body,
                               check_param->frame_body_len, 1);
        }
    }

    if ((cont = zt_p2p_get_attr_content(check_param->p2p_ie, check_param->p2p_ielen,
                                        P2P_ATTR_INVITATION_FLAGS,
                                        NULL, &cont_len)))
    {
        flags = *cont;
    }
    if ((cont = zt_p2p_get_attr_content(check_param->p2p_ie, check_param->p2p_ielen,
                                        P2P_ATTR_OPERATING_CH, NULL, &cont_len)))
    {
        op_ch = *(cont + 4);
    }

    if (invit_info->token != check_param->dialogToken)
    {
        zt_widev_invit_info_init(invit_info);
    }

    zt_memcpy(invit_info->peer_mac,
              check_param->is_tx ? GetAddr1Ptr(check_param->buf) : GetAddr2Ptr(
                  check_param->buf), ZT_80211_MAC_ADDR_LEN);
    invit_info->active = check_param->is_tx ? 1 : 0;
    invit_info->token = check_param->dialogToken;
    invit_info->flags = (flags == -1) ? 0x0 : flags;
    invit_info->req_op_ch = op_ch;
    invit_info->state = 0;

    p2p_dump_attr_ch_list(check_param->p2p_ie, check_param->p2p_ielen, ch_list_buf,
                          128, 1);
    zt_hw_info_get_channel_bw_ext(pnic_info, &ch, &bw, &offset);
    P2P_FRAME_INFO("ZT_%s(%d~%d):P2P_INVIT_REQ, dialogToken=%d, flags:0x%02x, op_ch:%d, ch_list:%s\n",
                   (check_param->is_tx == zt_true) ? "Tx" : "Rx", p2p_info->remain_ch, ch,
                   check_param->dialogToken, flags, op_ch,
                   ch_list_buf);

    if (!check_param->is_tx)
    {
        if (zt_p2p_check_buddy_linkstate(pnic_info) &&
                p2p_info->full_ch_in_p2p_handshake == 0)
        {
            if (op_ch != -1 && p2p_check_p2pie_op_ch_with_buddy(pnic_info,
                    check_param->frame_body, check_param->frame_body_len,
                    1) == zt_false)
            {
                P2P_FRAME_DBG(" op_ch:%u has no intersect with buddy\n", op_ch);
                p2p_change_p2pie_ch_list(pnic_info, check_param->frame_body,
                                         check_param->frame_body_len,
                                         0, 1);
            }
            else if (p2p_check_ch_list_with_buddy
                     (pnic_info, check_param->frame_body, check_param->frame_body_len,
                      1) == zt_false)
            {
                P2P_FRAME_DBG(" ch_list has no intersect with buddy\n");
                p2p_change_p2pie_ch_list(pnic_info, frame_body,
                                         check_param->frame_body_len,
                                         0, 1);
            }
        }
    }

    return 0;
}

zt_s32 p2p_check_invit_rsp(nic_info_st *pnic_info,
                           p2p_frame_check_param_st *check_param)
{
    zt_u8 *cont             = NULL;
    zt_u32 cont_len         = 0;
    zt_s32 status           = -1;
    zt_u8 ch_list_buf[128]  = { '\0' };
    zt_s32 op_ch            = -1;
    zt_u8 ch;
    CHANNEL_WIDTH bw;
    HAL_PRIME_CH_OFFSET offset;
    p2p_info_st *p2p_info   = pnic_info->p2p;
    zt_widev_invit_info_t *invit_info = &p2p_info->invit_info;

    if (check_param->is_tx)
    {
        if (zt_p2p_check_buddy_linkstate(pnic_info)
                && p2p_info->full_ch_in_p2p_handshake == 0)
            p2p_adjust_channel(pnic_info,
                               check_param->frame_body,
                               check_param->frame_body_len,
                               1);
    }
    else
    {
    }

    if ((cont = zt_p2p_get_attr_content(check_param->p2p_ie, check_param->p2p_ielen,
                                        P2P_ATTR_STATUS, NULL, &cont_len)))
    {
#ifdef CONFIG_P2P_INVITE_IOT
        if (check_param->is_tx && *cont == 7)
        {
            P2P_FRAME_INFO("TX_P2P_INVITE_RESP, status is no common channel, change to unknown group\n");
            *cont = 8;
        }
#endif
        status = *cont;
    }
    if ((cont = zt_p2p_get_attr_content(check_param->p2p_ie, check_param->p2p_ielen,
                                        P2P_ATTR_OPERATING_CH, NULL, &cont_len)))
    {
        op_ch = *(cont + 4);
    }

    if (invit_info->token == check_param->dialogToken && invit_info->state == 0
            && zt_memcmp(invit_info->peer_mac,
                         check_param->is_tx ? GetAddr1Ptr(check_param->buf) : GetAddr2Ptr(
                             check_param->buf), ZT_80211_MAC_ADDR_LEN) == 0)
    {
        invit_info->status = (status == -1) ? 0xff : status;
        invit_info->rsp_op_ch = op_ch;
        invit_info->state = 1;
        invit_info->token = 0;
    }

    p2p_dump_attr_ch_list(check_param->p2p_ie, check_param->p2p_ielen, ch_list_buf,
                          128, 1);
    zt_hw_info_get_channel_bw_ext(pnic_info, &ch, &bw, &offset);
    P2P_FRAME_INFO("ZT_%s(%d~%d):P2P_INVIT_RESP, dialogToken=%d, status:%d, op_ch:%d, ch_list:%s\n",
                   (check_param->is_tx == zt_true) ? "Tx" : "Rx", p2p_info->remain_ch, ch,
                   check_param->dialogToken, status, op_ch, ch_list_buf);

    p2p_info->link_channel = op_ch;

    return 0;
}

zt_s32 zt_p2p_check_frames(nic_info_st *pnic_info, const zt_u8 *buf, zt_u32 len,
                           zt_bool is_tx, zt_u8 flag)
{
    zt_s32 is_p2p_frame = (-1);
    zt_u8 *frame_body   = NULL;
    zt_u8 category;
    zt_u8 action;
    zt_u8 OUI_Subtype;
    zt_u8 dialogToken = 0;
    zt_u8 *p2p_ie = NULL;
    zt_u32 p2p_ielen = 0;
    p2p_frame_check_param_st check_param;
    p2p_info_st *p2p_info = NULL;

    if (NULL == pnic_info || NULL == buf)
    {
        P2P_FRAME_ERR(" input param is null");
        return ZT_RETURN_FAIL;
    }

    p2p_info = pnic_info->p2p;

    frame_body = (zt_u8 *)(buf + sizeof(struct wl_ieee80211_hdr_3addr));
    category = frame_body[0];
    if (category == ZT_WLAN_CATEGORY_PUBLIC)
    {
        P2P_FRAME_DBG("ZT_WLAN_CATEGORY_PUBLIC");
        action = frame_body[1];
        if (action == ZT_WLAN_ACTION_PUBLIC_VENDOR &&
                zt_memcmp(frame_body + 2, P2P_OUI, 4) == 0)
        {
            OUI_Subtype = frame_body[6];
            dialogToken = frame_body[7];
            is_p2p_frame = OUI_Subtype;
            P2P_FRAME_DBG("ACTION_CATEGORY_PUBLIC: ACT_PUBLIC_VENDOR, OUI = 0x%x, OUI_Subtype=%d, dialogToken=%d\n",
                          zt_cpu_to_be32(*((zt_u32 *)(frame_body + 2))), OUI_Subtype, dialogToken);

            p2p_ie = zt_p2p_get_ie((zt_u8 *) buf + sizeof(struct wl_ieee80211_hdr_3addr) +
                                   ZT_PUBLIC_ACTION_IE_OFFSET,
                                   len - sizeof(struct wl_ieee80211_hdr_3addr) - ZT_PUBLIC_ACTION_IE_OFFSET,
                                   NULL, &p2p_ielen);


            check_param.buf             = (zt_u8 *)buf;
            check_param.len             = len;
            check_param.frame_body      = frame_body;
            check_param.frame_body_len  = len - sizeof(struct wl_ieee80211_hdr_3addr);
            check_param.p2p_ie          = p2p_ie;
            check_param.p2p_ielen       = p2p_ielen;
            check_param.is_tx           = is_tx;
            check_param.dialogToken     = dialogToken;
            if (p2p_ie)
            {
                //zt_p2p_dump_attrs(p2p_ie,p2p_ielen);
            }

            switch (OUI_Subtype)
            {

                case P2P_GO_NEGO_REQ:
                {
                    p2p_info->go_negoing |= ZT_BIT(P2P_GO_NEGO_REQ);
                    P2P_FRAME_DBG("P2P_GO_NEGO_REQ");

                    p2p_check_nego_req(pnic_info, &check_param);
                    break;
                }
                case P2P_GO_NEGO_RESP:
                {
                    p2p_info->go_negoing |= ZT_BIT(P2P_GO_NEGO_RESP);
                    P2P_FRAME_DBG("P2P_GO_NEGO_RESP");
                    p2p_check_nego_rsp(pnic_info, &check_param);

                    break;
                }
                case P2P_GO_NEGO_CONF:
                {
                    p2p_info->go_negoing |= ZT_BIT(P2P_GO_NEGO_CONF);
                    P2P_FRAME_DBG("P2P_GO_NEGO_CONF");
                    p2p_check_nego_confirm(pnic_info, &check_param);
                    zt_wlan_mgmt_scan_que_flush(pnic_info);

                    break;
                }
                case P2P_INVIT_REQ:
                {
                    p2p_info->go_negoing |= ZT_BIT(P2P_INVIT_REQ);
                    P2P_FRAME_DBG("P2P_INVIT_REQ");
                    p2p_check_invit_req(pnic_info, &check_param);

                    break;
                }
                case P2P_INVIT_RESP:
                {
                    p2p_info->go_negoing |= ZT_BIT(P2P_INVIT_RESP);
                    P2P_FRAME_DBG("P2P_INVIT_RESP");
                    p2p_check_invit_rsp(pnic_info, &check_param);
                    zt_wlan_mgmt_scan_que_flush(pnic_info);

                    break;
                }
                case P2P_DEVDISC_REQ:
                    P2P_FRAME_DBG("ZT_%s:P2P_DEVDISC_REQ, dialogToken=%d\n",
                                  (is_tx == zt_true) ? "Tx" : "Rx", dialogToken);
                    break;
                case P2P_DEVDISC_RESP:
                {
                    zt_u8 *cont = NULL;
                    zt_s32 cont_len = 0;
                    cont = zt_p2p_get_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_STATUS, NULL,
                                                   &cont_len);
                    P2P_FRAME_DBG("ZT_%s:P2P_DEVDISC_RESP, dialogToken=%d, status:%d\n",
                                  (is_tx == zt_true) ? "Tx" : "Rx", dialogToken, cont ? *cont : -1);
                    break;
                }
                case P2P_PROVISION_DISC_REQ:
                {
                    zt_u32 frame_body_len = len - sizeof(struct wl_ieee80211_hdr_3addr);
                    zt_u8 *p2p_ie;
                    zt_u32 p2p_ielen = 0;
                    zt_u32 contentlen = 0;

                    P2P_FRAME_DBG("ZT_%s:P2P_PROVISION_DISC_REQ, dialogToken=%d\n",
                                  (is_tx == zt_true) ? "Tx" : "Rx", dialogToken);

                    p2p_info->go_negoing |= ZT_BIT(P2P_PROVISION_DISC_REQ);

                    p2p_info->provdisc_req_issued = zt_false;
                    if ((p2p_ie = zt_p2p_get_ie(frame_body + ZT_PUBLIC_ACTION_IE_OFFSET,
                                                frame_body_len - ZT_PUBLIC_ACTION_IE_OFFSET, NULL, &p2p_ielen)))
                    {

                        if (zt_p2p_get_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_GROUP_ID, NULL,
                                                    &contentlen))
                        {
                            p2p_info->provdisc_req_issued = zt_false;
                        }
                        else
                        {
                            P2P_FRAME_DBG("provdisc_req_issued is zt_true\n");
                            p2p_info->provdisc_req_issued = zt_true;
                        }

                    }

                }
                break;
                case P2P_PROVISION_DISC_RESP:
                    P2P_FRAME_DBG("ZT_%s:P2P_PROVISION_DISC_RESP, dialogToken=%d\n",
                                  (is_tx == zt_true) ? "Tx" : "Rx", dialogToken);
                    break;
                default:
                    P2P_FRAME_INFO("ZT_%s:OUI_Subtype=%d, dialogToken=%d\n",
                                   (is_tx == zt_true) ? "Tx" : "Rx", OUI_Subtype, dialogToken);
                    break;
            }

        }

    }
    else if (category == ZT_WLAN_CATEGORY_P2P)
    {
        OUI_Subtype = frame_body[5];
        dialogToken = frame_body[6];

        P2P_FRAME_DBG("ACTION_CATEGORY_P2P: OUI = 0x%x, OUI_Subtype=%d, dialogToken=%d\n",
                      zt_cpu_to_be32(*((zt_u32 *)(frame_body + 1))), OUI_Subtype, dialogToken);

        is_p2p_frame = OUI_Subtype;

        if (flag)
        {
            switch (OUI_Subtype)
            {
                case P2P_NOTICE_OF_ABSENCE:
                    P2P_FRAME_DBG("ZT_%s:P2P_NOTICE_OF_ABSENCE, dialogToken=%d\n",
                                  (is_tx == zt_true) ? "TX" : "RX", dialogToken);
                    break;
                case P2P_PRESENCE_REQUEST:
                    P2P_FRAME_DBG("ZT_%s:P2P_PRESENCE_REQUEST, dialogToken=%d\n",
                                  (is_tx == zt_true) ? "TX" : "RX", dialogToken);
                    break;
                case P2P_PRESENCE_RESPONSE:
                    P2P_FRAME_DBG("ZT_%s:P2P_PRESENCE_RESPONSE, dialogToken=%d\n",
                                  (is_tx == zt_true) ? "TX" : "RX", dialogToken);
                    break;
                case P2P_GO_DISC_REQUEST:
                    P2P_FRAME_DBG("ZT_%s:P2P_GO_DISC_REQUEST, dialogToken=%d\n",
                                  (is_tx == zt_true) ? "TX" : "RX", dialogToken);
                    break;
                default:
                    P2P_FRAME_DBG("ZT_%s:OUI_Subtype=%d, dialogToken=%d\n",
                                  (is_tx == zt_true) ? "TX" : "RX", OUI_Subtype, dialogToken);
                    break;
            }
        }

    }
    else
    {
        is_p2p_frame = 0;
        P2P_FRAME_DBG("ZT_%s:action frame category=%d\n",
                      (is_tx == zt_true) ? "TX" : "RX", category);
    }

    return is_p2p_frame;
}

zt_s32 zt_p2p_recv_public_action(nic_info_st *pnic_info, zt_u8 *pframe,
                                 zt_u16 frame_len)
{
    zt_u32 len = frame_len;
    zt_u8 *frame_body;
    //zt_u32 wps_ielen;
    p2p_info_st *p2p_info = pnic_info->p2p;
    frame_body = (zt_u8 *)(pframe + sizeof(struct wl_ieee80211_hdr_3addr));

    if (zt_p2p_is_valid(pnic_info))
    {

        if (zt_true == p2p_info->scb.init_flag && p2p_info->scb.rx_mgmt)
        {
            P2P_FRAME_DBG("report p2p rx action frame");
            return zt_p2p_rx_action_precess(pnic_info, pframe, len);
        }

    }
    return ZT_RETURN_OK;
}

zt_s32 zt_p2p_ie_valid(void *p2p, zt_u16 len)
{
    zt_80211_p2p_param_ie_t *pie = NULL;
    zt_u32 oui_value = 0;

    if (NULL == p2p  || 0 == len)
    {
        P2P_FRAME_ERR(" NUll point");
        return -1;
    }

    pie = p2p;

    if (!(pie->element_id == ZT_80211_MGMT_EID_VENDOR_SPECIFIC &&
            len >= ZT_OFFSETOF(zt_80211_p2p_param_ie_t, oui) + pie->len))
    {
        P2P_FRAME_ERR(" data corrupt");
        return -2;
    }

    oui_value = (pie->oui[0] << 16) | (pie->oui[1] << 8) | (pie->oui[2] << 0);
    LOG_I("oui_value:0x%x,oui_type:0x%x", oui_value, pie->oui_type);
    if (!(oui_value == ZT_80211_OUI_WFA &&
            pie->oui_type == ZT_80211_OUI_TYPE_WFA_P2P))
    {
        P2P_FRAME_WARN("no p2p element");
        return -3;
    }

    return 0;
}

zt_u8 *zt_p2p_ie_to_str(ZT_P2P_IE_E ie_type)
{
    switch (ie_type)
    {
        case ZT_P2P_IE_BEACON       :
            return to_str(ZT_P2P_IE_BEACON);
        case ZT_P2P_IE_PROBE_REQ    :
            return to_str(ZT_P2P_IE_PROBE_REQ);
        case ZT_P2P_IE_PROBE_RSP    :
            return to_str(ZT_P2P_IE_PROBE_RSP);
        case ZT_P2P_IE_ASSOC_REQ    :
            return to_str(ZT_P2P_IE_ASSOC_REQ);
        case ZT_P2P_IE_ASSOC_RSP    :
            return to_str(ZT_P2P_IE_ASSOC_RSP);
        default:
            return to_str(ZT_P2P_IE_MAX);
    }
    return "unknown ietype";
}
zt_s32 zt_p2p_parse_p2pie(nic_info_st *pnic_info, void *p2p, zt_u16 len,
                          ZT_P2P_IE_E ie_type)
{
    zt_80211_p2p_param_ie_t *pie    = NULL;
    zt_s32 ret                      = 0;
    p2p_info_st *p2p_info           = NULL;

    if (NULL == p2p  || 0 == len || NULL == pnic_info)
    {
        P2P_FRAME_ERR("NUll point");
        return -1;
    }
    if (ZT_P2P_IE_MAX <= ie_type)
    {
        P2P_FRAME_ERR("unknown ie type:%d", ie_type);
        return -2;
    }

    ret = zt_p2p_ie_valid(p2p, len);
    if (ret)
    {
        P2P_FRAME_WARN("no p2p ie");
        return ret;
    }

    P2P_FRAME_DBG("parsing ie:%s", zt_p2p_ie_to_str(ie_type));
    p2p_info = pnic_info->p2p;
    pie  = p2p;

    if (NULL != p2p_info->p2p_ie[ie_type])
    {
        zt_memset(p2p_info->p2p_ie[ie_type], 0, P2P_IE_BUF_LEN);
        p2p_info->p2p_ie_len[ie_type] = 0;
    }

    //P2P_FRAME_ARRAY(pie,2 + pie->len);

    p2p_info->p2p_ie_len[ie_type] = 2 + pie->len;
    zt_memcpy(p2p_info->p2p_ie[ie_type], pie, p2p_info->p2p_ie_len[ie_type]);

    return ret;
}

zt_s32 zt_p2p_proc_assoc_req(nic_info_st *pnic_info, zt_u8 *p2p_ie,
                             zt_u32 p2p_ielen, wdn_net_info_st *pwdn_info, zt_u8 flag)
{
    zt_u8 status_code       = P2P_STATUS_SUCCESS;
    zt_u8 *pbuf             = NULL;
    zt_u8 *pattr_content    = NULL;
    zt_u32 attr_contentlen  = 0;
    zt_u16 cap_attr         = 0;
    p2p_info_st *p2p_info   = pnic_info->p2p;

    if (flag && p2p_info->role != P2P_ROLE_GO)
    {
        return P2P_STATUS_FAIL_REQUEST_UNABLE;
    }

    if (p2p_ie)
    {
        if (zt_p2p_get_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_CAPABILITY,
                                    (zt_u8 *) & cap_attr, (zt_u32 *) & attr_contentlen))
        {
            P2P_FRAME_DBG("Got P2P Capability Attr!!\n");
            cap_attr = zt_le16_to_cpu(cap_attr);
            pwdn_info->dev_cap = cap_attr & 0xff;
        }

        if (zt_p2p_get_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_DEVICE_INFO, NULL,
                                    (zt_u32 *) & attr_contentlen))
        {
            P2P_FRAME_DBG("Got P2P DEVICE INFO Attr!!\n");
            pattr_content = pbuf = zt_kzalloc(attr_contentlen);
            if (pattr_content)
            {
                zt_u8 num_of_secdev_type;
                zt_u16 dev_name_len;

                zt_p2p_get_attr_content(p2p_ie, p2p_ielen,
                                        P2P_ATTR_DEVICE_INFO, pattr_content,
                                        (zt_u32 *) & attr_contentlen);

                zt_memcpy(pwdn_info->dev_addr, pattr_content, ZT_80211_MAC_ADDR_LEN);

                pattr_content += ZT_80211_MAC_ADDR_LEN;

                zt_memcpy(&pwdn_info->config_methods, pattr_content, 2);
                pwdn_info->config_methods = zt_be16_to_cpu(pwdn_info->config_methods);

                pattr_content += 2;

                zt_memcpy(pwdn_info->primary_dev_type, pattr_content, 8);

                pattr_content += 8;

                num_of_secdev_type = *pattr_content;
                pattr_content += 1;

                if (num_of_secdev_type == 0)
                {
                    pwdn_info->num_of_secdev_type = 0;
                }
                else
                {
                    zt_u32 len;
                    pwdn_info->num_of_secdev_type = num_of_secdev_type;
                    len = (sizeof(pwdn_info->secdev_types_list) < (num_of_secdev_type * 8)) ?
                          (sizeof(pwdn_info->secdev_types_list)) : (num_of_secdev_type * 8);
                    zt_memcpy(pwdn_info->secdev_types_list, pattr_content, len);
                    pattr_content += (num_of_secdev_type * 8);
                }

                pwdn_info->dev_name_len = 0;
                if (WPS_ATTR_DEVICE_NAME == zt_be16_to_cpu(*(zt_u16 *) pattr_content))
                {
                    dev_name_len = zt_be16_to_cpu(*(zt_u16 *)(pattr_content + 2));

                    pwdn_info->dev_name_len =
                        (sizeof(pwdn_info->dev_name) < dev_name_len) ? sizeof(pwdn_info->dev_name) :
                        dev_name_len;

                    zt_memcpy(pwdn_info->dev_name, pattr_content + 4, pwdn_info->dev_name_len);
                }

                zt_kfree(pbuf);
            }
        }
    }
    return status_code;
}

zt_s32 zt_p2p_rx_action_precess(nic_info_st *pnic_info, zt_u8 *frame,
                                zt_u32 len)
{
    p2p_info_st *p2p_info = pnic_info->p2p;
    zt_s32 type = -1;
    zt_u8 category = 0 ;
    zt_u8 action   = 0;
    zt_u8 hw_ch;
    CHANNEL_WIDTH cw;
    HAL_PRIME_CH_OFFSET offset;
    zt_widev_nego_info_t *nego_info = NULL;

    type = zt_p2p_check_frames(pnic_info, frame, len, zt_false, 1);
    zt_action_frame_parse(frame, len, &category, &action);
    zt_hw_info_get_channel_bw_ext(pnic_info, &hw_ch, &cw, &offset);
    p2p_info->report_ch = p2p_info->remain_ch;
    P2P_FRAME_INFO("ZT_Rx:nego:0x%x,category(%u), action(%u),type(%d),hw_ch(%d),listen_ch(%d),remain_ch(%d),len:0x%x\n",
                   p2p_info->go_negoing, category, action, type, hw_ch, p2p_info->listen_channel,
                   p2p_info->remain_ch, len);

    zt_scan_wait_done(pnic_info, zt_true, 200);
    zt_scan_wait_done(pnic_info->buddy_nic, zt_true, 200);

    p2p_info->scan_deny = 1;
    nego_info = &p2p_info->nego_info;

    if (P2P_GO_NEGO_CONF == type)
    {
        p2p_info->go_negoing &= ~ ZT_BIT(P2P_GO_NEGO_REQ);
        p2p_info->go_negoing &= ~ ZT_BIT(P2P_GO_NEGO_RESP);
        p2p_info->scan_deny = 0;
    }
    else if (P2P_GO_NEGO_REQ == type)
    {
        if (p2p_info->go_negoing & (ZT_BIT(P2P_GO_NEGO_CONF) | ZT_BIT(P2P_GO_NEGO_RESP)))
        {
            p2p_info->go_negoing &= ~ ZT_BIT(P2P_GO_NEGO_REQ);
        }
    }
    else if (P2P_GO_NEGO_RESP == type)
    {
        if (nego_info->status || 
            (nego_info->state && (p2p_info->go_negoing & ZT_BIT(P2P_GO_NEGO_CONF))))
        {
            p2p_info->go_negoing &= ~ ZT_BIT(P2P_GO_NEGO_REQ);
            p2p_info->go_negoing &= ~ ZT_BIT(P2P_GO_NEGO_RESP);
            p2p_info->scan_deny = 0;
        }
        else if (nego_info->state)
        {
            p2p_info->go_negoing &= ~ ZT_BIT(P2P_GO_NEGO_REQ);
        }
    }
    else if (P2P_PROVISION_DISC_REQ == type)
    {
        if (p2p_info->go_negoing & ZT_BIT(P2P_PROVISION_DISC_RESP))
        {
            p2p_info->go_negoing &= ~ ZT_BIT(P2P_PROVISION_DISC_REQ);
        }
    }
    else if (P2P_PROVISION_DISC_RESP == type)
    {
        p2p_info->go_negoing &= ~ ZT_BIT(P2P_PROVISION_DISC_REQ);
        p2p_info->scan_deny = 0;
    }
    else if (P2P_INVIT_REQ == type)
    {
        if (p2p_info->go_negoing & ZT_BIT(P2P_INVIT_RESP))
        {
            p2p_info->go_negoing &= ~ ZT_BIT(P2P_INVIT_REQ);
        }
    }
    else if (P2P_INVIT_RESP == type)
    {
        p2p_info->go_negoing &= ~ ZT_BIT(P2P_INVIT_REQ);
        if (p2p_info->invit_info.flags & ZT_BIT(0)
            && 0 == p2p_info->invit_info.status)
        {
            p2p_info->scan_deny = 0;
        }
        else
        {
            p2p_info->go_negoing &= ~ ZT_BIT(P2P_INVIT_RESP);
            p2p_info->scan_deny = 0;
        }
    }
    else
    {
        p2p_info->scan_deny = 0;
    }

    if (p2p_info->scb.rx_mgmt)
    {
        P2P_FRAME_DBG("report action to upper");
        p2p_info->scb.rx_mgmt(pnic_info, frame, len);
    }

    if (p2p_info->go_negoing)
    {
        zt_p2p_nego_timer_set(pnic_info, P2P_SCAN_NEGO_TIME);
    }

    return 0;
}

zt_s32 p2p_mgnt_frame_tx(nic_info_st *pnic_info, zt_u8 *buf, zt_s32 len,
                         zt_u8 wait_ack)
{
    struct xmit_buf *pxmit_buf;
    tx_info_st *ptx_info;
    zt_80211_mgmt_t *pmgmt;
    zt_s32 ret = 0;

    /* alloc xmit_buf */
    ptx_info = (tx_info_st *)pnic_info->tx_info;
    pxmit_buf = zt_xmit_extbuf_new(ptx_info);
    if (pxmit_buf == NULL)
    {
        P2P_FRAME_ERR("pxmit_buf is NULL");
        return -1;
    }

    /* clear frame head(txd + 80211head) */
    zt_memset(pxmit_buf->pbuf, 0,
              TXDESC_OFFSET + ZT_OFFSETOF(zt_80211_mgmt_t, beacon));

    /* set frame type */
    pmgmt = (void *)&pxmit_buf->pbuf[TXDESC_OFFSET];
    zt_memcpy(pmgmt, buf, len);
    P2P_FRAME_DBG("send");
    if (wait_ack)
    {
        ret = zt_nic_mgmt_frame_xmit_with_ack(pnic_info, NULL,
                                              pxmit_buf, len, 100);
    }
    else
    {
        ret = zt_nic_mgmt_frame_xmit(pnic_info, NULL, pxmit_buf, len);
    }

    return ret;

}


zt_s32 zt_p2p_tx_action_process(nic_info_st *pnic_info, zt_u8 *frame,
                                zt_u32 len,
                                zt_u8 ch, zt_u8 wait_ack)
{
    zt_s32 type         = 0;
    zt_u8 category = 0;
    zt_u8 action = 0;
    zt_timer_t timer;
    zt_u32 dump_limit   = 1;
    zt_u32 dump_cnt     = 0;
    zt_s32 tx_ret       = 0;
    p2p_info_st *p2p_info = NULL;
    zt_widev_nego_info_t *nego_info = NULL;
    if (NULL == pnic_info)
    {
        return -1;
    }

    p2p_info = pnic_info->p2p;

    zt_scan_wait_done(pnic_info, zt_true, 200);
    zt_scan_wait_done(pnic_info->buddy_nic, zt_true, 200);

    zt_hw_info_set_channel_bw(pnic_info, ch, CHANNEL_WIDTH_20,
                               HAL_PRIME_CHNL_OFFSET_DONT_CARE);
    p2p_info->scan_deny = 1;

    type = zt_p2p_check_frames(pnic_info, frame, len, zt_true, 1);
    if (zt_action_frame_parse((zt_u8 *)frame, len, &category, &action))
    {
        P2P_FRAME_ERR("zt_action_frame_parse error");
        return -3;
    }

    if (P2P_GO_NEGO_RESP == type)
    {
        dump_limit = 5;
    }
    zt_timer_set(&timer, 0);
    while (1)
    {
        zt_u32 sleep_ms = 0;
        dump_cnt++;
        zt_scan_stop(pnic_info);
        zt_scan_stop(pnic_info->buddy_nic);
        tx_ret = p2p_mgnt_frame_tx(pnic_info, frame, len, wait_ack);
        if (ZT_WLAN_ACTION_PUBLIC_GAS_INITIAL_REQ == action ||
                ZT_WLAN_ACTION_PUBLIC_GAS_INITIAL_RSP == action)
        {
            sleep_ms = 50;
            zt_timer_mod(&timer, 500);
        }

        if (P2P_GO_NEGO_RESP == type)
        {
            tx_ret = 1;
            zt_msleep(50);
        }

        if (tx_ret == 0 || (dump_cnt >= dump_limit && zt_timer_expired(&timer)))
        {
            break;
        }
        if (sleep_ms > 0)
        {
            zt_msleep(sleep_ms);
        }
    }

    nego_info = &p2p_info->nego_info;

    P2P_FRAME_INFO("ZT_Tx:nego:0x%x,category(%u), action(%u),type(%d),hw_ch(%d),listen_ch(%d),remain_ch(%d),len:0x%x,tx_ret:%d,cnt:%d\n",
                   p2p_info->go_negoing, category, action, type, ch, p2p_info->listen_channel,
                   p2p_info->remain_ch, (zt_s32)len, tx_ret, dump_cnt);

    if (P2P_GO_NEGO_CONF == type)
    {
        p2p_info->go_negoing &= ~ ZT_BIT(P2P_GO_NEGO_REQ);
        p2p_info->go_negoing &= ~ ZT_BIT(P2P_GO_NEGO_RESP);
        p2p_info->scan_deny = 0;
    }
    else if (P2P_GO_NEGO_REQ == type)
    {
        if (p2p_info->go_negoing & (ZT_BIT(P2P_GO_NEGO_CONF) | ZT_BIT(P2P_GO_NEGO_RESP)))
        {
            p2p_info->go_negoing &= ~ ZT_BIT(P2P_GO_NEGO_REQ);
        }
    }
    else if (P2P_GO_NEGO_RESP == type)
    {
        if (nego_info->status || 
            (nego_info->state && (p2p_info->go_negoing & ZT_BIT(P2P_GO_NEGO_CONF))))
        {
            p2p_info->go_negoing &= ~ ZT_BIT(P2P_GO_NEGO_REQ);
            p2p_info->go_negoing &= ~ ZT_BIT(P2P_GO_NEGO_RESP);
            p2p_info->scan_deny = 0;
        }
        else if (nego_info->state)
        {
            p2p_info->go_negoing &= ~ ZT_BIT(P2P_GO_NEGO_REQ);
        }
    }
    else if (P2P_PROVISION_DISC_REQ == type)
    {
        if (p2p_info->go_negoing & ZT_BIT(P2P_PROVISION_DISC_RESP))
        {
            p2p_info->go_negoing &= ~ ZT_BIT(P2P_PROVISION_DISC_REQ);
        }
    }
    else if (P2P_PROVISION_DISC_RESP == type)
    {
        p2p_info->go_negoing &= ~ ZT_BIT(P2P_PROVISION_DISC_REQ);
        p2p_info->scan_deny = 0;
    }
    else if (P2P_INVIT_REQ == type)
    {
        if (p2p_info->go_negoing & ZT_BIT(P2P_INVIT_RESP))
        {
            p2p_info->go_negoing &= ~ ZT_BIT(P2P_INVIT_REQ);
        }
    }
    else if (P2P_INVIT_RESP == type)
    {
        p2p_info->go_negoing &= ~ ZT_BIT(P2P_INVIT_REQ);
        if (p2p_info->invit_info.flags & ZT_BIT(0)
                && 0 == p2p_info->invit_info.status)
        {
            p2p_info->scan_deny = 0;
        }
        else
        {
            p2p_info->go_negoing &= ~ ZT_BIT(P2P_INVIT_RESP);
            p2p_info->scan_deny = 0;
        }
    }
    else
    {
        p2p_info->scan_deny = 0;
    }

    if (p2p_info->go_negoing)
    {
        zt_p2p_nego_timer_set(pnic_info, P2P_SCAN_NEGO_TIME);
    }

    // zt_lps_deny_cancel(pnic_info,PS_DENY_MGNT_TX);

    return 0;
}

