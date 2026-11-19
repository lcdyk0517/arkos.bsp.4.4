/*
 * zt_80211.c
 *
 * used for implement the basic operation interface of IEEE80211 management
 * frame
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

#define _80211_DBG(fmt, ...)      LOG_D("[%s:%d]"fmt, __func__, __LINE__, ##__VA_ARGS__)
#define _80211_WARN(fmt, ...)     LOG_W("[%s:%d]"fmt, __func__, __LINE__, ##__VA_ARGS__)
#define _80211_ERROR(fmt, ...)    LOG_E("[%s:%d]"fmt, __func__, __LINE__, ##__VA_ARGS__)
#define _80211_ARRAY(data, len)   zt_log_array(data, len)

static zt_u8 SNAP_ETH_TYPE_IPX[2] = { 0x81, 0x37 };

static zt_u8 SNAP_ETH_TYPE_APPLETALK_AARP[2] = { 0x80, 0xf3 }; /* AppleTale ARP */


static zt_u8 wl_rfc1042_header[] = { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00 };
static zt_u8 wl_bridge_tunnel_header[] = { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0xf8 };

zt_s32 zt_80211_mgmt_ies_search(void *pvar, zt_u16 var_len,
                                zt_u8 cmp_id,
                                zt_80211_mgmt_ie_t **pout)
{
    zt_80211_mgmt_ie_t *pie = NULL;
    zt_u8 *pstart = pvar;
    zt_u16 offset = 0;

    if (pvar == NULL || pout == NULL)
    {
        return ZT_RETURN_FAIL;
    }

    while (1)
    {
        pie = (zt_80211_mgmt_ie_t *)(pstart + offset);
        if (pie->element_id == cmp_id)
        {
            *pout = pie;
            return ZT_RETURN_OK;
        }

        offset += 2 + pie->len;
        if (offset >= var_len)
        {
            return ZT_RETURN_FAIL;
        }
    }
}

zt_s32 zt_80211_mgmt_ies_search_with_oui(void *pies, zt_u16 ies_len,
        zt_u8 cmp_id, zt_u8 *oui,
        zt_80211_mgmt_ie_t **ppie)
{
    zt_80211_mgmt_ie_t *pie;
    zt_u8 *pstart = pies;
    zt_u16 offset = 0;

    if (pies == NULL || ppie == NULL)
    {
        return ZT_RETURN_FAIL;
    }

    while (1)
    {
        pie = (zt_80211_mgmt_ie_t *)(pstart + offset);
        if (pie->element_id == cmp_id)
        {
            if (zt_memcmp(pie->data, oui, 4) == 0)
            {
                *ppie = pie;
                return ZT_RETURN_OK;
            }
        }

        offset += 2 + pie->len;
        if (offset >= ies_len)
        {
            return ZT_RETURN_FAIL;
        }
    }
}

zt_u8 *zt_80211_set_fixed_ie(zt_u8 *pbuf, zt_u32 len, zt_u8 *source,
                             zt_u16 *frlen)
{
    zt_memcpy((void *)pbuf, (void *)source, len);
    *frlen = *frlen + len;

    return (pbuf + len);
}


zt_bool zt_80211_is_snap_hdr(zt_u8 *phdr)
{
    zt_u8 *psnap = phdr;
    zt_u8 *psnap_type;
    psnap_type = phdr + ZT_80211_SNAP_HDR_SIZE;

    if ((!zt_memcmp(psnap, wl_rfc1042_header, ZT_80211_SNAP_HDR_SIZE) &&
            zt_memcmp(psnap_type, SNAP_ETH_TYPE_IPX, 2) &&
            zt_memcmp(psnap_type, SNAP_ETH_TYPE_APPLETALK_AARP, 2)) ||
            !zt_memcmp(psnap, wl_bridge_tunnel_header, ZT_80211_SNAP_HDR_SIZE))
    {
        return zt_true;
    }
    else
    {
        return zt_false;
    }
}

static zt_inline zt_u32 rsne_cipher_suite_parse(zt_u32 cipher_suite)
{
    if (cipher_suite == ZT_80211_RSN_CIPHER_SUITE_USE_GROUP)
    {
        return ZT_CIPHER_SUITE_NONE;
    }
    if (cipher_suite == ZT_80211_RSN_CIPHER_SUITE_WEP40)
    {
        return ZT_CIPHER_SUITE_WEP40;
    }
    if (cipher_suite == ZT_80211_RSN_CIPHER_SUITE_WEP104)
    {
        return ZT_CIPHER_SUITE_WEP104;
    }
    if (cipher_suite == ZT_80211_RSN_CIPHER_SUITE_TKIP)
    {
        return ZT_CIPHER_SUITE_TKIP;
    }
    if (cipher_suite == ZT_80211_RSN_CIPHER_SUITE_CCMP)
    {
        return ZT_CIPHER_SUITE_CCMP;
    }

    return 0;
}

zt_s32 zt_80211_mgmt_rsn_parse(void *prsn, zt_u16 len,
                               zt_u32 *pgroup_cipher, zt_u32 *pairwise_cipher)
{
    zt_80211_mgmt_ie_t *pie;
    zt_u16 left;
    zt_u8 *pos;
    zt_u16 version;
    zt_u16 cipher_suite_cnt;
    zt_s32 ret = 0;

    if (prsn == NULL || len == 0)
    {
        _80211_WARN("NULL point");
        ret = -1;
        goto exit;
    }

    pie = prsn;
    if (!(pie->element_id == ZT_80211_MGMT_EID_RSN &&
            len >= ZT_OFFSETOF(zt_80211_mgmt_ie_t, data) + pie->len))
    {
        _80211_DBG("no rsn element");
        ret = -2;
        goto exit;
    }
    pos = pie->data;
    left = len - ZT_OFFSETOF(zt_80211_mgmt_ie_t, data);

#define RSNE_VERSION_SIZE                       2
#define RSNE_GROUP_DATA_CIPHER_SUITE_SIZE       4
#define RSNE_PARIWISE_CIPHER_SUITE_COUNT_SIZE   2
#define RSNE_PARIWISE_CIPHER_SUITE_LIST_SIZE    4

    /* check version field */
    if (left < RSNE_VERSION_SIZE)
    {
        _80211_WARN("no version field");
        ret = -3;
        goto exit;
    }
    version = zt_le16_to_cpu(*(zt_u16 *)pos);
    if (version != 1)
    {
        _80211_WARN("no support version");
        ret = -4;
        goto exit;
    }
    left -= RSNE_VERSION_SIZE;
    pos += RSNE_VERSION_SIZE;
    if (left == 0)
    {
        _80211_DBG("no any option field");
        goto exit;
    }

    /* get group data cipher suite */
    if (left < RSNE_GROUP_DATA_CIPHER_SUITE_SIZE)
    {
        _80211_DBG("no group data cipher suite");
        ret = -5;
        goto exit;
    }
    if (pgroup_cipher)
    {
        *pgroup_cipher = rsne_cipher_suite_parse(zt_be32_to_cpu(*(zt_u32 *)pos));
    }
    left -= RSNE_GROUP_DATA_CIPHER_SUITE_SIZE;
    pos  += RSNE_GROUP_DATA_CIPHER_SUITE_SIZE;
    if (left == 0)
    {
        _80211_DBG("only group data cipher suite field");
        goto exit;
    }

    /* get pairwise cipher suite count */
    if (left < RSNE_PARIWISE_CIPHER_SUITE_COUNT_SIZE)
    {
        _80211_DBG("no cipher suite count filed");
        ret = -6;
        goto exit;
    }
    cipher_suite_cnt = zt_le16_to_cpu(*(zt_u16 *)pos);
    left -= RSNE_PARIWISE_CIPHER_SUITE_COUNT_SIZE;
    pos  += RSNE_PARIWISE_CIPHER_SUITE_COUNT_SIZE;
    /* get pairwise cipher suite */
    if (cipher_suite_cnt == 0 ||
            left < cipher_suite_cnt * RSNE_PARIWISE_CIPHER_SUITE_LIST_SIZE)
    {
        _80211_WARN("cipher suite count(%d) error", cipher_suite_cnt);
        ret = -7;
        goto exit;
    }
    if (pairwise_cipher)
    {
        *pairwise_cipher = 0x0;
        do
        {
            *pairwise_cipher |= rsne_cipher_suite_parse(zt_be32_to_cpu(*(zt_u32 *)pos));
            pos += RSNE_PARIWISE_CIPHER_SUITE_LIST_SIZE;
        } while (--cipher_suite_cnt);
    }

    return 0;
exit :
    if (pairwise_cipher)
    {
        *pairwise_cipher = 0x0;
    }
    if (pgroup_cipher)
    {
        *pgroup_cipher = 0x0;
    }
    return ret;
}

zt_s32 zt_80211_mgmt_rsn_survey(void *data, zt_u16 data_len,
                                void **prsn_ie, zt_u16 *prsn_ie_len,
                                zt_u32 *pgroup_cipher, zt_u32 *pairwise_cipher)
{
    zt_s32 ret;

    zt_80211_mgmt_ie_t *pie = data;
    zt_u16 ie_len = data_len;

    do
    {
        ret = zt_80211_mgmt_rsn_parse(pie, sizeof(*pie) + pie->len,
                                      pgroup_cipher, pairwise_cipher);
        if (!ret)
        {
            if (prsn_ie)
            {
                *prsn_ie = pie;
            }
            if (prsn_ie_len)
            {
                *prsn_ie_len = ie_len;
            }
            break;
        }

        ie_len -= sizeof(*pie) + pie->len;
        if (ie_len <= sizeof(*pie))
        {
            break;
        }
        pie = (zt_80211_mgmt_ie_t *)&pie->data[pie->len];
    } while (1);

    return ret;
}

static zt_inline zt_u32 get_wpa_cipher_suite(zt_u32 cipher_suite)
{
    zt_u32 oui;
    zt_u8 type;

    oui = cipher_suite >> 8;
    if (oui != ZT_80211_OUI_MICROSOFT)
    {
        return 0;
    }

    type = (zt_u8)cipher_suite;
    switch (type)
    {
        case 0 :
            return ZT_CIPHER_SUITE_NONE;
        case 1 :
            return ZT_CIPHER_SUITE_WEP40;
        case 2 :
            return ZT_CIPHER_SUITE_TKIP;
        case 4 :
            return ZT_CIPHER_SUITE_CCMP;
        case 5 :
            return ZT_CIPHER_SUITE_WEP104;
    }

    return 0;
}


zt_s32 zt_80211_mgmt_wpa_parse(void *pwpa, zt_u16 len,
                               zt_u32 *pmulticast_cipher, zt_u32 *punicast_cipher)
{
    zt_u16 left;
    zt_u8 *pos;
    zt_80211_mgmt_ie_t *pie;
    zt_u32 oui_type;
    zt_u16 version;
    zt_u16 cipher_suite_cnt;
    zt_s32 ret = 0;

    if (pwpa == NULL || len == 0)
    {
        _80211_WARN("invalid parameter");
        ret = -1;
        goto exit;
    }

    pie = pwpa;
    if (!(pie->element_id == ZT_80211_MGMT_EID_VENDOR_SPECIFIC &&
            len >= ZT_OFFSETOF(zt_80211_mgmt_ie_t, data) + pie->len))
    {
        _80211_DBG("data corrupt");
        ret = -2;
        goto exit;
    }
    pos = pie->data;
    left = len - ZT_OFFSETOF(zt_80211_mgmt_ie_t, data);

#define WPA_OUI_TYPE_SIZE                       4
#define WPA_VERSION_SIZE                        2
#define WPA_MULTICAST_CIPHER_SUITE_SIZE         4
#define WPA_UNICAST_CIPHER_SUITE_COUNT_SIZE     2
#define WPA_UNICAST_CIPHER_SUITE_LIST_SIZE      4

    /* check OUT-type field */
    if (left < WPA_OUI_TYPE_SIZE)
    {
        _80211_WARN("no oui-type field");
        ret = -3;
        goto exit;
    }
    oui_type = zt_be32_to_cpu(*(zt_u32 *)pos);
    if (oui_type !=
            ((ZT_80211_OUI_MICROSOFT << 8) | ZT_80211_OUI_TYPE_MICROSOFT_WPA))
    {
        _80211_DBG("no wpa element");
        ret = -4;
        goto exit;
    }
    left -= WPA_OUI_TYPE_SIZE;
    pos += WPA_OUI_TYPE_SIZE;

    /* check version field */
    if (left < WPA_VERSION_SIZE)
    {
        _80211_WARN("no version field");
        ret = -3;
        goto exit;
    }
    version = zt_le16_to_cpu(*(zt_u16 *)pos);
    if (version != 1)
    {
        _80211_WARN("no support version");
        ret = -4;
        goto exit;
    }
    left -= WPA_VERSION_SIZE;
    pos += WPA_VERSION_SIZE;
    if (left == 0)
    {
        _80211_DBG("no any option field");
        goto exit;
    }

    /* get mulitcast cipher suite */
    if (left < WPA_MULTICAST_CIPHER_SUITE_SIZE)
    {
        _80211_WARN("no mulitcast cipher suite");
        ret = -5;
        goto exit;
    }
    if (pmulticast_cipher)
    {
        *pmulticast_cipher = get_wpa_cipher_suite(zt_be32_to_cpu(*(zt_u32 *)pos));
    }
    left -= WPA_MULTICAST_CIPHER_SUITE_SIZE;
    pos += WPA_MULTICAST_CIPHER_SUITE_SIZE;
    if (left == 0)
    {
        _80211_DBG("only mulitcast cipher suite field");
        goto exit;
    }

    /* get unicast cipher suite count */
    if (left < WPA_UNICAST_CIPHER_SUITE_COUNT_SIZE)
    {
        _80211_WARN("no unicast cipher suite count filed");
        ret = -6;
        goto exit;
    }
    cipher_suite_cnt = zt_le16_to_cpu(*(zt_u16 *)pos);

    left -= WPA_UNICAST_CIPHER_SUITE_COUNT_SIZE;
    pos += WPA_UNICAST_CIPHER_SUITE_COUNT_SIZE;
    /* get unicast cipher suite */
    if (cipher_suite_cnt == 0 ||
            left < cipher_suite_cnt * WPA_UNICAST_CIPHER_SUITE_LIST_SIZE)
    {
        _80211_WARN("cipher suite count(%d) error", cipher_suite_cnt);
        ret = -7;
        goto exit;
    }
    if (punicast_cipher)
    {
        *punicast_cipher = 0x0;
        do
        {
            *punicast_cipher |= get_wpa_cipher_suite(zt_be32_to_cpu(*(zt_u32 *)pos));
            pos += WPA_UNICAST_CIPHER_SUITE_LIST_SIZE;
        } while (--cipher_suite_cnt);
    }

    return 0;
exit :
    if (punicast_cipher)
    {
        *punicast_cipher = 0x0;
    }
    if (pmulticast_cipher)
    {
        *pmulticast_cipher = 0x0;
    }
    return ret;
}

zt_s32 zt_80211_mgmt_wpa_survey(void *data, zt_u16 data_len,
                                void **pwpa_ie, zt_u16 *pwpa_ie_len,
                                zt_u32 *pmulticast_cipher, zt_u32 *punicast_cipher)
{
    zt_s32 ret;

    zt_80211_mgmt_ie_t *pie = data;
    zt_u16 ie_len = data_len;

    do
    {
        ret = zt_80211_mgmt_wpa_parse(pie, sizeof(*pie) + pie->len,
                                      pmulticast_cipher, punicast_cipher);
        if (!ret)
        {
            if (pwpa_ie)
            {
                *pwpa_ie = pie;
            }
            if (pwpa_ie_len)
            {
                *pwpa_ie_len = ie_len;
            }
            break;
        }

        ie_len -= sizeof(*pie) + pie->len;
        if (ie_len <= sizeof(*pie))
        {
            break;
        }
        pie = (zt_80211_mgmt_ie_t *)&pie->data[pie->len];
    } while (1);

    return ret;
}

zt_s32 zt_80211_mgmt_wmm_parse(void *pwmm, zt_u16 len)
{
    zt_80211_wmm_param_ie_t *pie;
    zt_u64 oui_type;
    zt_s32 ret = 0;

    if (pwmm == NULL || len == 0)
    {
        _80211_WARN("NUll point");
        ret = -1;
        goto exit;
    }

    pie = pwmm;
    if (!(pie->element_id == ZT_80211_MGMT_EID_VENDOR_SPECIFIC &&
            len >= ZT_OFFSETOF(zt_80211_wmm_param_ie_t, oui) + pie->len))
    {
        _80211_WARN("data corrupt");
        ret = -2;
        goto exit;
    }

    oui_type = (pie->oui[0] << 16) | (pie->oui[1] << 8) | (pie->oui[2] << 0);
    if (!(oui_type == ZT_80211_OUI_MICROSOFT &&
            pie->oui_type == ZT_80211_OUI_TYPE_MICROSOFT_WMM))
    {
        _80211_DBG("no wmm element");
        ret = -3;
        goto exit;
    }
    // if (!(pie->oui_subtype == 0 && pie->version == 1))
    // {
    //     _80211_WARN("unknow subtype(%d) and version(%d)",
    //                 pie->oui_subtype, pie->version);
    //     ret = -4;
    //     goto exit;
    // }

exit :
    return ret;
}

zt_s32 zt_wlan_get_sec_ie(zt_u8 *in_ie, zt_u32 in_len,
                          zt_u8 *rsn_ie, zt_u16 *rsn_len,
                          zt_u8 *wpa_ie, zt_u16 *wpa_len,
                          zt_u8 flag)
{
    zt_u8 authmode, sec_idx, i;
    zt_u8 wpa_oui[4] = { 0x0, 0x50, 0xf2, 0x01 };
    zt_u32 cnt;

    cnt = (_TIMESTAMP_ + _BEACON_ITERVAL_ + _CAPABILITY_);

    sec_idx = 0;
    if (flag)
    {
        while (cnt < in_len)
        {
            authmode = in_ie[cnt];

            if (authmode == _WPA_IE_ID_ && !zt_memcpy(&in_ie[cnt + 2], &wpa_oui[0], 4))
            {
#if 0
                LOG_E("\n wpa_ie_to_get_func: sec_idx=%d in_ie[cnt+1]+2=%d\n",
                      sec_idx, in_ie[cnt + 1] + 2);
#endif

                if (wpa_ie)
                {
                    zt_memcpy(wpa_ie, &in_ie[cnt], in_ie[cnt + 1] + 2);

                    for (i = 0; i < (in_ie[cnt + 1] + 2); i = i + 8)
                    {

                    }
                }

                *wpa_len = in_ie[cnt + 1] + 2;
                cnt += in_ie[cnt + 1] + 2;
            }
            else
            {
                if (authmode == _WPA2_IE_ID_)
                {
#if 0
                    LOG_E("\n get_rsn_ie: sec_idx=%d in_ie[cnt+1]+2=%d\n",
                          sec_idx, in_ie[cnt + 1] + 2);
#endif

                    if (rsn_ie)
                    {
                        zt_memcpy(rsn_ie, &in_ie[cnt], in_ie[cnt + 1] + 2);

                        for (i = 0; i < (in_ie[cnt + 1] + 2); i = i + 8)
                        {

                        }
                    }

                    *rsn_len = in_ie[cnt + 1] + 2;
                    cnt += in_ie[cnt + 1] + 2;
                }
                else
                {
                    cnt += in_ie[cnt + 1] + 2;
                }
            }

        }
    }

    return (*rsn_len + *wpa_len);
}

zt_u8 *zt_wlan_get_ie(zt_u8 *pbuf, zt_s32 index, zt_s32 *len, zt_s32 limit)
{
    zt_s32 tmp, i;
    zt_u8 *p;

    if (limit < 1)
    {
        return NULL;
    }

    p = pbuf;
    i = 0;
    *len = 0;
    while (1)
    {
        if (*p == index)
        {
            *len = *(p + 1);
            return (p);
        }
        else
        {
            tmp = *(p + 1);
            p += (tmp + 2);
            i += (tmp + 2);
        }
        if (i >= limit)
        {
            break;
        }
    }
    return NULL;
}


zt_u8 *zt_wlan_get_wps_ie(zt_u8 *temp_ie, zt_u32 temp_len, zt_u8 *wps_ie,
                          zt_u32 *ie_len)
{
    zt_u32 count = 0;
    zt_u8 *temp_wps_ie = NULL;
    zt_u8 eid, wps_oui[4] = {0x00, 0x50, 0xf2, 0x04};

    if (ie_len)
    {
        *ie_len = 0;
    }

    if (!temp_ie)
    {
        _80211_WARN("[%s]temp_ie isn't null, check", __func__);
        *ie_len = 0;
        return temp_wps_ie;
    }

    if (temp_len <= 0)
    {
        _80211_WARN("[%s]ie_len is 0, check", __func__);
        *ie_len = 0;
        return temp_wps_ie;
    }

    while (count + 1 + 4 < temp_len)
    {
        eid = temp_ie[count];
        if (eid == ZT_80211_MGMT_EID_VENDOR_SPECIFIC &&
                zt_memcmp(&temp_ie[count + 2], wps_oui, 4) == zt_false)
        {
            temp_wps_ie = temp_ie + count;
            if (wps_ie)
            {
                zt_memcpy(wps_ie, &temp_ie[count], temp_ie[count + 1 + 2]);
            }

            if (ie_len)
            {
                *ie_len = temp_ie[count + 1] + 2;
            }
            break;
        }
        else
        {
            count += temp_ie[count + 1] + 2;
        }
    }
    return temp_wps_ie;
}

zt_u8 *zt_wlan_get_wps_attr(zt_u8 *wps_ie, zt_u32 wps_ielen,
                            zt_u16 target_attr_id, zt_u8 *buf_attr, zt_u32 *len_attr, zt_u8 flag)
{
    zt_u8 *attr_ptr = NULL;
    zt_u8 *target_attr_ptr = NULL;
    zt_u8 wps_oui[4] = { 0x00, 0x50, 0xF2, 0x04 };

    if (flag)
    {
        if (len_attr)
        {
            *len_attr = 0;
        }
    }
    if ((wps_ie[0] != ZT_80211_MGMT_EID_VENDOR_SPECIFIC) ||
            (zt_memcmp(wps_ie + 2, wps_oui, 4) != zt_true))
    {
        return attr_ptr;
    }

    attr_ptr = wps_ie + 6;

    while (attr_ptr - wps_ie < wps_ielen)
    {
        zt_u16 attr_id = ZT_GET_BE16(attr_ptr);
        zt_u16 attr_data_len = ZT_GET_BE16(attr_ptr + 2);
        zt_u16 attr_len = attr_data_len + 4;

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


zt_u8 *zt_wlan_get_wps_attr_content(zt_u8 flag, zt_u8 *wps_ie, zt_u32 wps_ielen,
                                    zt_u16 target_attr_id, zt_u8 *buf_content, zt_u32 *len_content)
{
    zt_u8 *attr_ptr;
    zt_u32 attr_len;

    if (flag)
    {
        if (len_content)
        {
            *len_content = 0;
        }
    }
    attr_ptr =
        zt_wlan_get_wps_attr(wps_ie, wps_ielen, target_attr_id, NULL, &attr_len, 1);

    if (attr_ptr && attr_len)
    {
        if (buf_content)
        {
            zt_memcpy(buf_content, attr_ptr + 4, attr_len - 4);
        }

        if (len_content)
        {
            *len_content = attr_len - 4;
        }

        return attr_ptr + 4;
    }

    return NULL;
}


zt_s32 zt_ch_2_freq(zt_s32 ch)
{
    if (ch >= 1 && ch <= 13)
    {
        return 2407 + ch * 5;
    }
    else if (ch == 14)
    {
        return 2484;
    }
    else if (ch >= 36 && ch <= 177)
    {
        return 5000 + ch * 5;
    }

    return 0; /* not supported */
}

zt_s32 freq_2_ch(zt_s32 freq)
{
    /* see 802.11 17.3.8.3.2 and Annex J */
    if (freq < 2484)
    {
        return (freq - 2407) / 5;
    }
    else if (freq == 2484)
    {
        return 14;
    }
    else if (freq >= 4910 && freq <= 4980)
    {
        return (freq - 4000) / 5;
    }
    else if (freq <= 45000) /* DMG band lower limit */
    {
        return (freq - 5000) / 5;
    }
    else if (freq >= 58320 && freq <= 64800)
    {
        return (freq - 56160) / 2160;
    }
    else
    {
        return 0;    /* err param */
    }
}


