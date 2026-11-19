/*
 * iw_func.c
 *
 * used for wext framework interface
 *
 * Author: houchuang
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
#include "ndev_linux.h"
#include "iw_func.h"
#include <linux/decompress/mm.h>

#define IW_FUNC_DBG(fmt, ...)       LOG_D("[%s:%d]"fmt, __func__, __LINE__, ##__VA_ARGS__)
#define IW_FUNC_ARRAY(data, len)    zt_log_array(data, len)
#define IW_FUNC_INFO(fmt, ...)      LOG_I("[%s:%d]"fmt, __func__, __LINE__, ##__VA_ARGS__)
#define IW_FUNC_WARN(fmt, ...)      LOG_W("[%s:%d]"fmt, __func__, __LINE__, ##__VA_ARGS__)
#define IW_FUNC_ERROR(fmt, ...)     LOG_E("[%s:%d]"fmt, __func__, __LINE__, ##__VA_ARGS__)

#define MIN_FRAG_THRESHOLD     256U
#define MAX_FRAG_THRESHOLD     2346U

#define MAX_WPA_IE_LEN          (ZT_FIELD_SIZEOF(zt_wlan_mgmt_scan_que_node_t, wpa_ie))
#define MAX_RSN_IE_LEN          (ZT_FIELD_SIZEOF(zt_wlan_mgmt_scan_que_node_t, rsn_ie))

#define AUTH_ALG_OPEN_SYSTEM        0x1
#define AUTH_ALG_SHARED_KEY         0x2
#define AUTH_ALG_LEAP               0x4


const channel_info_st chan_info_tab[14] =
{
    { 1,  2412 },
    { 2,  2417 },
    { 3,  2422 },
    { 4,  2427 },
    { 5,  2432 },
    { 6,  2437 },
    { 7,  2442 },
    { 8,  2447 },
    { 9,  2452 },
    { 10, 2457 },
    { 11, 2462 },
    { 12, 2467 },
    { 13, 2472 },
    { 14, 2484 },
};
wireless_info_st def_wireless_info =
{
#define RATE_COUNT              4
#define MIN_FRAG_THRESHOLD      256U
#define MAX_FRAG_THRESHOLD      2346U
    .throughput = 5 * 1000 * 1000, /* ~5 Mb/s real (802.11b) */

    /* percent values between 0 and 100. */
    .max_qual.qual = 100,
    .max_qual.level = 100,
    .max_qual.noise = 100,
    .max_qual.updated = IW_QUAL_ALL_UPDATED, /* Updated all three */

    .avg_qual.qual = 92, /* > 8% missed beacons is 'bad' */
    /* TODO: Find real 'good' to 'bad' threshol value for RSSI */
    .avg_qual.level = (zt_u8) - 70, /* -70dbm */
    .avg_qual.noise = (zt_u8) - 256,
    .avg_qual.updated = IW_QUAL_ALL_UPDATED, /* Updated all three */

    .num_bitrates = RATE_COUNT,
    .bitrate = { 1000000, 2000000, 5500000, 11000000, },

    .min_frag = MIN_FRAG_THRESHOLD,
    .max_frag = MAX_FRAG_THRESHOLD,

    .num_channels = ARRAY_SIZE(chan_info_tab),
    .pchannel_tab = (channel_info_st *)chan_info_tab,

    /*  The following code will proivde the security capability to network manager. */
    /*  If the driver doesn't provide this capability to network manager, */
    /*  the WPA/WPA2 routers can't be chosen in the network manager. */
#if WIRELESS_EXT > 17
    .enc_capa = IW_ENC_CAPA_WPA | IW_ENC_CAPA_WPA2 |
    IW_ENC_CAPA_CIPHER_TKIP | IW_ENC_CAPA_CIPHER_CCMP,
#endif

#ifdef IW_SCAN_CAPA_ESSID
    .scan_capa = IW_SCAN_CAPA_ESSID | IW_SCAN_CAPA_TYPE | IW_SCAN_CAPA_BSSID |
    IW_SCAN_CAPA_CHANNEL | IW_SCAN_CAPA_MODE | IW_SCAN_CAPA_RATE,
#endif
};

struct iw_crypt
{
    zt_80211_addr_t sta_addr;
    zt_u16 alg;
    zt_u8 set_tx;
    zt_u8 idx;
    zt_u16 key_len;
    zt_u8 key[0];
} ;

static zt_s32 set_encryption(struct net_device *dev,
                             struct iw_crypt *param, zt_u32 param_len)
{
    zt_u32 wep_key_idx, wep_key_len;
    ndev_priv_st *pndev_priv = netdev_priv(dev);
    nic_info_st *pnic_info = pndev_priv->nic;
    sec_info_st *psec_info = pnic_info->sec_info;
    wdn_net_info_st *pwdn_info;
    zt_s32 res = 0;

    IW_FUNC_DBG("mac addr : "ZT_MAC_FMT, ZT_MAC_ARG(nic_to_local_addr(pnic_info)));

    if (pnic_info->is_driver_critical)
    {
        IW_FUNC_WARN("driver enter crital");
        return -EINVAL;
    }

    if (param_len !=
            ZT_OFFSETOF(struct iw_crypt, key) + param->key_len)
    {
        IW_FUNC_ERROR("param_len invalid !!!!!!!");
        res = -EINVAL;
        goto exit;
    }

    if (zt_80211_is_bcast_addr(param->sta_addr))
    {
        if (param->idx >= ZT_80211_WEP_KEYS)
        {
            res = -EINVAL;
            goto exit;
        }
    }
    else
    {
        res = -EINVAL;
        goto exit;
    }

    if (param->alg == IW_ENCODE_ALG_WEP)
    {
        wep_key_idx = param->idx;
        wep_key_len = param->key_len;

        if ((wep_key_idx > ZT_80211_WEP_KEYS) || (wep_key_len == 0))
        {
            res = -EINVAL;
            goto exit;
        }

        psec_info->ndisencryptstatus = zt_ndis802_11Encryption1Enabled;

        wep_key_len = wep_key_len <= 5 ? 5 : 13; /* 5B for wep40 and 13B for wep104 */
        if (wep_key_len == 13)
        {
            psec_info->dot11PrivacyAlgrthm = _WEP104_;
        }
        else
        {
            psec_info->dot11PrivacyAlgrthm = _WEP40_;
        }

        if (param->set_tx)
        {
            psec_info->dot11PrivacyKeyIndex = wep_key_idx;
        }
        zt_memcpy(psec_info->dot11DefKey[wep_key_idx].skey,
                  param->key, wep_key_len);
        psec_info->dot11DefKeylen[wep_key_idx] = wep_key_len;
        psec_info->key_mask |= ZT_BIT(wep_key_idx);
    }
    else if (psec_info->dot11AuthAlgrthm == dot11AuthAlgrthm_8021X) /* 802_1x */
    {
        local_info_st *plocal_info = pnic_info->local_info;
        if (plocal_info->work_mode == ZT_INFRA_MODE) /* sta mode */
        {
            pwdn_info = zt_wdn_find_info(pnic_info,
                                         zt_wlan_get_cur_bssid(pnic_info));
            if (pwdn_info == NULL)
            {
                goto exit;
            }

            if (param->alg != IW_ENCODE_ALG_NONE)
            {
                pwdn_info->ieee8021x_blocked = zt_false;
            }

            if (psec_info->ndisencryptstatus == zt_ndis802_11Encryption2Enabled ||
                    psec_info->ndisencryptstatus == zt_ndis802_11Encryption3Enabled)
            {
                pwdn_info->dot118021XPrivacy = psec_info->dot11PrivacyAlgrthm;
            }
            IW_FUNC_DBG("pwdn_info->dot118021XPrivacy = %d", pwdn_info->dot118021XPrivacy);

            zt_mcu_set_sec_cfg(pnic_info,
                               psec_info->dot11AuthAlgrthm == dot11AuthAlgrthm_8021X ? 0xcf : 0xcc);

            /* PTK: param->u.crypt.key */
            if (param->set_tx == 1) /* pairwise key */
            {
                IW_FUNC_DBG("set unicastkey");
                /* KCK PTK0~127 */
                zt_memcpy(pwdn_info->dot118021x_UncstKey.skey, param->key,
                          min_t(zt_u16, param->key_len, 16));

                if (param->alg == IW_ENCODE_ALG_TKIP) /* set mic key */
                {
                    /* KEK PTK128~255 */
                    zt_memcpy(pwdn_info->dot11tkiptxmickey.skey,
                              &(param->key[16]), 8); /* PTK128~191 */
                    zt_memcpy(pwdn_info->dot11tkiprxmickey.skey,
                              &(param->key[24]), 8); /* PTK192~255 */
                    psec_info->busetkipkey = zt_true;
                }
                if (param->alg == IW_ENCODE_ALG_CCMP)
                {
                    IW_FUNC_DBG("sta_hw_set_unicast_key");
                    zt_sec_sta_set_unicast_key(pnic_info, &pwdn_info->unicast_cam_id,
                                               pwdn_info->dot118021XPrivacy,
                                               pwdn_info->mac, pwdn_info->dot118021x_UncstKey.skey);
                }
            }
            else /* group key */
            {
                IW_FUNC_DBG("set groupkey");
                zt_memcpy(psec_info->dot118021XGrpKey[param->idx].skey,
                          param->key,
                          min_t(zt_u16, param->key_len, 16));
                zt_memcpy(psec_info->dot118021XGrptxmickey[param->idx].skey,
                          &param->key[16], 8);
                zt_memcpy(psec_info->dot118021XGrprxmickey[param->idx].skey,
                          &param->key[24], 8);
                psec_info->binstallGrpkey = zt_true;
                psec_info->dot118021XGrpKeyid = param->idx;
                if (psec_info->dot118021XGrpPrivacy == _AES_)
                {
                    IW_FUNC_DBG("sta_hw_set_group_key");
                    zt_sec_sta_set_group_key(pnic_info, &pwdn_info->group_cam_id, pwdn_info->bssid);
                }
            }
        }
    }

exit:
    return res;
}


static char *translate_scan_info(nic_info_st *pnic_info,
                                 struct iw_request_info *pinfo,
                                 zt_wlan_mgmt_scan_que_node_t *pscan_que_node,
                                 char *pstart, char *pstop)
{
    char *pstart_last = pstart;
    struct iw_event *piwe;
    zt_u16 max_rate = 0, rate;
    zt_u16 i = 0;
    zt_u8 ch;
    zt_u8 *p;
    zt_80211_mgmt_ie_t *pie;
    zt_u32 pie_len;
    char *buf = NULL;

    piwe = zt_vmalloc(sizeof(struct iw_event));
    if (piwe == NULL)
    {
        IW_FUNC_ERROR("\"struct iw_event\" malloc fail !!!");
        goto error;
    }

    /* AP MAC ADDRESS */
    piwe->cmd = SIOCGIWAP;
    piwe->u.ap_addr.sa_family = ARPHRD_ETHER;

    zt_memcpy(piwe->u.ap_addr.sa_data, pscan_que_node->bssid, ETH_ALEN);
    pstart = iwe_stream_add_event(pinfo, pstart, pstop, piwe, IW_EV_ADDR_LEN);

    /* add the ESSID */
    piwe->cmd = SIOCGIWESSID;
    piwe->u.data.flags = 1;
    piwe->u.data.length = (zt_u16)pscan_que_node->ssid.length;
    pstart = iwe_stream_add_point(pinfo, pstart, pstop, piwe,
                                  pscan_que_node->ssid.data);

    /* Add the protocol name */
    piwe->cmd = SIOCGIWNAME;
    switch (pscan_que_node->name)
    {
        case ZT_WLAN_BSS_NAME_IEEE80211_B :
            zt_snprintf(piwe->u.name, IFNAMSIZ, "IEEE 802.11b");
            break;
        case ZT_WLAN_BSS_NAME_IEEE80211_G :
            zt_snprintf(piwe->u.name, IFNAMSIZ, "IEEE 802.11g");
            break;
        case ZT_WLAN_BSS_NAME_IEEE80211_BG :
            zt_snprintf(piwe->u.name, IFNAMSIZ, "IEEE 802.11bg");
            break;
        case ZT_WLAN_BSS_NAME_IEEE80211_BN :
            zt_snprintf(piwe->u.name, IFNAMSIZ, "IEEE 802.11bn");
            break;
        case ZT_WLAN_BSS_NAME_IEEE80211_GN :
            zt_snprintf(piwe->u.name, IFNAMSIZ, "IEEE 802.11gn");
            break;
        case ZT_WLAN_BSS_NAME_IEEE80211_BGN :
            zt_snprintf(piwe->u.name, IFNAMSIZ, "IEEE 802.11bgn");
            break;
        case ZT_WLAN_BSS_NAME_IEEE80211_A :
        case ZT_WLAN_BSS_NAME_IEEE80211_AN :
        default :
            break;
    }
    pstart = iwe_stream_add_event(pinfo, pstart, pstop, piwe, IW_EV_CHAR_LEN);

    /* add mode */
    piwe->cmd = SIOCGIWMODE;
    if (pscan_que_node->opr_mode != ZT_WLAN_OPR_MODE_MESH)
    {
        piwe->u.mode = pscan_que_node->opr_mode;
        pstart = iwe_stream_add_event(pinfo, pstart, pstop, piwe, IW_EV_UINT_LEN);
    }

    /* Add frequency/channel */
    piwe->cmd = SIOCGIWFREQ;
    ch = pscan_que_node->channel;
    if (ch >= 1 && ch <= 14)
    {
        if (ch == 14)
        {
            piwe->u.freq.m = 2484 * 100000;
        }
        else if (ch < 14)
        {
            piwe->u.freq.m = (2407 + ch * 5) * 100000;
        }
    }
    piwe->u.freq.e = 1;
    piwe->u.freq.i = pscan_que_node->channel;
    pstart = iwe_stream_add_event(pinfo, pstart, pstop, piwe, IW_EV_FREQ_LEN);

    /* Add encryption capability */
    piwe->cmd = SIOCGIWENCODE;
    if (pscan_que_node->cap_privacy)
    {
        piwe->u.data.flags = IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
    }
    else
    {
        piwe->u.data.flags = IW_ENCODE_DISABLED;
    }
    piwe->u.data.length = 0;
    pstart = iwe_stream_add_point(pinfo, pstart, pstop, piwe,
                                  pscan_que_node->ssid.data);

    /*Add basic and extended rates */
    max_rate = 0;
    while (pscan_que_node->spot_rate[i] != 0)
    {
        rate = pscan_que_node->spot_rate[i] & 0x7F;
        if (rate > max_rate)
        {
            max_rate = rate;
        }
        i++;
    }
    if (pscan_que_node->mcs & 0x8000)  /* MCS15 */
    {
        max_rate = pscan_que_node->bw_40mhz ?
                   (pscan_que_node->short_gi ? 300 : 270) :
                   (pscan_que_node->short_gi ? 144 : 130);
    }
    else if (pscan_que_node->mcs & 0x0080)     /* MCS7 */
    {
    }
    else     /* default MCS7 */
    {
        max_rate = (pscan_que_node->bw_40mhz) ?
                   (pscan_que_node->short_gi ? 150 : 135) :
                   (pscan_que_node->short_gi ? 72 : 65);
    }
    max_rate = max_rate * 2; /* Mbps/2; */

    piwe->cmd = SIOCGIWRATE;
    piwe->u.bitrate.fixed = 0;
    piwe->u.bitrate.disabled = 0;
    piwe->u.bitrate.value = max_rate * 500000;
    pstart = iwe_stream_add_event(pinfo, pstart, pstop, piwe, IW_EV_PARAM_LEN);

#define BUF_SIZE     (10 + ZT_MAX(MAX_WPA_IE_LEN, MAX_RSN_IE_LEN) * 2) /* 2 for hex to string translate */
    buf = zt_vmalloc(BUF_SIZE);
    if (buf == NULL)
    {
        IW_FUNC_ERROR("zt_vmalloc fail !!!");
        goto error;
    }

    if (pscan_que_node->cap_privacy)
    {
        /* parsing WPA/WPA2 */
        pie = (zt_80211_mgmt_ie_t *)pscan_que_node->wpa_ie;
        pie_len = ZT_OFFSETOF(zt_80211_mgmt_ie_t, data) + pie->len;
        if (pie->len > 0)
        {
            zt_memset(buf, '\0', BUF_SIZE);
            p = buf;
            p += zt_sprintf(p, "wpa_ie=");
            for (i = 0; i < pie_len; i++)
            {
                p += zt_sprintf(p, "%02x", ((zt_u8 *)pie)[i]);
            }

            zt_memset(piwe, 0, sizeof(struct iw_event));
            piwe->cmd = IWEVCUSTOM;
            piwe->u.data.length = zt_strlen(buf);
            pstart = iwe_stream_add_point(pinfo, pstart, pstop, piwe, buf);

            zt_memset(piwe, 0, sizeof(struct iw_event));
            piwe->cmd = IWEVGENIE;
            piwe->u.data.length = pie_len;
            pstart =
                iwe_stream_add_point(pinfo, pstart, pstop, piwe, (char *)pie);
        }

        /* parsing rsn */
        pie = (zt_80211_mgmt_ie_t *)pscan_que_node->rsn_ie;
        pie_len = ZT_OFFSETOF(zt_80211_mgmt_ie_t, data) + pie->len;
        if (pie->len > 0)
        {
            zt_memset(buf, '\0', BUF_SIZE);
            p = buf;
            p += zt_sprintf(p, "rsn_ie=");
            for (i = 0; i < pie_len; i++)
            {
                p += zt_sprintf(p, "%02x", ((zt_u8 *)pie)[i]);
            }

            zt_memset(piwe, 0, sizeof(struct iw_event));
            piwe->cmd = IWEVCUSTOM;
            piwe->u.data.length = zt_strlen(buf);
            pstart = iwe_stream_add_point(pinfo, pstart, pstop, piwe, buf);

            zt_memset(piwe, 0, sizeof(struct iw_event));
            piwe->cmd = IWEVGENIE;
            piwe->u.data.length = pie_len;
            pstart =
                iwe_stream_add_point(pinfo, pstart, pstop, piwe, (char *)pie);
        }

        /* parsing WPS */
        pie = (zt_80211_mgmt_ie_t *)pscan_que_node->wps_ie;
        pie_len = ZT_OFFSETOF(zt_80211_mgmt_ie_t, data) + pie->len;
        if (pie->len)
        {
            piwe->cmd = IWEVGENIE;
            piwe->u.data.length = pie_len;
            pstart =
                iwe_stream_add_point(pinfo, pstart, pstop, piwe, (char *)pie);
        }
    }

    /* Add rssi statistics */
    zt_memset(buf, '\0', BUF_SIZE);
    p = buf;
    p += zt_sprintf(p, "rssi=");
    p += zt_sprintf(p, "%d dbm",
                    translate_percentage_to_dbm(pscan_que_node->signal_strength));
    zt_memset(piwe, 0, sizeof(struct iw_event));
    piwe->cmd = IWEVCUSTOM;
    piwe->u.data.length = zt_strlen(buf);
    pstart = iwe_stream_add_point(pinfo, pstart, pstop, piwe, buf);

    /* Add quality statistics */
    piwe->cmd = IWEVQUAL;
    piwe->u.qual.updated =
        IW_QUAL_QUAL_UPDATED | IW_QUAL_LEVEL_UPDATED | IW_QUAL_NOISE_INVALID;
    piwe->u.qual.level = pscan_que_node->signal_strength_scale;
    piwe->u.qual.qual = pscan_que_node->signal_qual; /*  signal quality */
    piwe->u.qual.noise = 0; /*  noise level */
    pstart = iwe_stream_add_event(pinfo, pstart, pstop, piwe, IW_EV_QUAL_LEN);

    zt_vfree(piwe);
    zt_vfree(buf);

    return pstart;

error :
    if (piwe)
    {
        zt_vfree(piwe);
    }
    if (buf)
    {
        zt_vfree(buf);
    }

    return pstart_last;
}

zt_s32 zt_iw_getName(struct net_device *ndev, struct iw_request_info *info,
                     union iwreq_data *wrqu, char *extra)
{
    ndev_priv_st *pndev_priv = netdev_priv(ndev);
    nic_info_st *pnic_info = pndev_priv->nic;
    zt_bool is_connected = zt_false;
    zt_wlan_mgmt_scan_que_node_t *pscan_que_node;
    zt_wlan_mgmt_scan_que_for_rst_e scan_que_for_rst;
    zt_wlan_mgmt_info_t *pwlan_info = pnic_info->wlan_mgmt_info;
    zt_wlan_network_t *pcur_network = &pwlan_info->cur_network;

    IW_FUNC_DBG("mac addr : "ZT_MAC_FMT, ZT_MAC_ARG(nic_to_local_addr(pnic_info)));

    zt_mlme_get_connect(pnic_info, &is_connected);
    if (is_connected == zt_true)
    {
#ifdef CFG_ENABLE_ADHOC_MODE
        if (zt_local_cfg_get_work_mode(pnic_info) == ZT_ADHOC_MODE)
        {
            zt_snprintf(wrqu->name, IFNAMSIZ, "UNKNOWN");
            return 0;
        }
        else
#endif
#ifdef CFG_ENABLE_AP_MODE
            if (zt_local_cfg_get_work_mode(pnic_info) == ZT_MASTER_MODE)
            {
                switch (pcur_network->cur_wireless_mode)
                {
                    case WIRELESS_11B :
                        zt_snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11b");
                        break;
                    case WIRELESS_11G :
                        zt_snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11g");
                        break;
                    case WIRELESS_11BG :
                        zt_snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11bg");
                        break;
                    case WIRELESS_11B_24N :
                        zt_snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11bn");
                        break;
                    case WIRELESS_11G_24N :
                        zt_snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11gn");
                        break;
                    case WIRELESS_11BG_24N :
                        zt_snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11bgn");
                        break;
                    case ZT_WLAN_BSS_NAME_IEEE80211_A :
                    case ZT_WLAN_BSS_NAME_IEEE80211_AN :
                    default :
                        break;
                }
                return 0;
            }
            else
#endif
            {
                zt_wlan_mgmt_scan_que_for_begin(pnic_info, pscan_que_node)
                {
                    if (!zt_memcmp(pcur_network->bssid, pscan_que_node->bssid, ETH_ALEN))
                    {
                        switch (pscan_que_node->name)
                        {
                            case ZT_WLAN_BSS_NAME_IEEE80211_B :
                                zt_snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11b");
                                break;
                            case ZT_WLAN_BSS_NAME_IEEE80211_G :
                                zt_snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11g");
                                break;
                            case ZT_WLAN_BSS_NAME_IEEE80211_BG :
                                zt_snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11bg");
                                break;
                            case ZT_WLAN_BSS_NAME_IEEE80211_BN :
                                zt_snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11bn");
                                break;
                            case ZT_WLAN_BSS_NAME_IEEE80211_GN :
                                zt_snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11gn");
                                break;
                            case ZT_WLAN_BSS_NAME_IEEE80211_BGN :
                                zt_snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11bgn");
                                break;
                            case ZT_WLAN_BSS_NAME_IEEE80211_A :
                            case ZT_WLAN_BSS_NAME_IEEE80211_AN :
                            default :
                                break;
                        }
                        break;
                    }
                }
                zt_wlan_mgmt_scan_que_for_end(scan_que_for_rst);

                if (scan_que_for_rst == ZT_WLAN_MGMT_SCAN_QUE_FOR_RST_BREAK)
                {
                    return 0;
                }
            }
    }

    zt_snprintf(wrqu->name, IFNAMSIZ, "unassociated");

    return 0;
}

zt_s32 zt_iw_ch_set_search_ch(zt_channel_info_t *ch_set, const zt_u32 ch)
{
    zt_s32 i;
    for (i = 0; ch_set[i].channel_num != 0; i++)
    {
        if (ch == ch_set[i].channel_num)
        {
            break;
        }
    }

    if (i >= ch_set[i].channel_num)
    {
        return -1;
    }

    return i;
}

zt_s32 zt_iw_setFrequency(struct net_device *ndev, struct iw_request_info *info,
                          union iwreq_data *wrqu, char *extra)
{
    ndev_priv_st *pndev_priv = netdev_priv(ndev);
    nic_info_st *pnic_info = pndev_priv->nic;
    hw_info_st *phw_info = pnic_info->hw_info;
    local_info_st *local_info = pnic_info->local_info;
    zt_s32 exp = 1, freq = 0, div = 0;
    zt_bool is_connected = zt_false;
    zt_u8 channel;

    if (pnic_info->is_driver_critical)
    {
        IW_FUNC_WARN("driver enter crital");
        return -EINVAL;
    }

    if (wrqu->freq.m <= 1000)
    {
        if (wrqu->freq.flags == IW_FREQ_AUTO)
        {
            if (zt_iw_ch_set_search_ch(phw_info->channel_set, wrqu->freq.m) > 0)
            {
                IW_FUNC_DBG("channel is auto, set to channel %d", wrqu->freq.m);
                channel = wrqu->freq.m;
            }
            else
            {
                channel = 1;
                IW_FUNC_DBG("channel is auto, channelset not match just set to channel 1");
            }
        }
        else
        {
            channel = wrqu->freq.m;
            IW_FUNC_DBG("channel is't auto, set to channel == %d", channel);
        }
    }
    else
    {
        while (wrqu->freq.e)
        {
            exp *= 10;
            wrqu->freq.e--;
        }

        freq = wrqu->freq.m;

        while (!(freq % 10))
        {
            freq /= 10;
            exp *= 10;
        }

        div = 1000000 / exp;

        if (div)
        {
            freq /= div;
        }
        else
        {
            div = exp / 1000000;
            freq *= div;
        }
        if (freq == 2484)
        {
            channel = 14;
        }
        else if (freq < 2484)
        {
            channel = ((freq - 2407) / 5);
        }
        else
        {
            channel = 0;
        }
    }

    zt_mlme_get_connect(pnic_info, &is_connected);
    if (zt_local_cfg_get_work_mode(pnic_info) == ZT_INFRA_MODE &&
            is_connected == zt_false)
    {
        local_info->channel = channel;
    }
    else
    {
        zt_wlan_set_cur_channel(pnic_info, channel);
    }
    zt_hw_info_set_channel_bw(pnic_info, channel, CHANNEL_WIDTH_20,
                               HAL_PRIME_CHNL_OFFSET_DONT_CARE);

    return 0;
}


zt_s32 zt_iw_getFrequency(struct net_device *ndev, struct iw_request_info *info,
                          union iwreq_data *wrqu, char *extra)
{
    ndev_priv_st *pndev_priv = netdev_priv(ndev);
    nic_info_st *pnic_info = pndev_priv->nic;
    local_info_st *local_info = pnic_info->local_info;
    zt_bool is_connected = zt_false;
    zt_u8 cur_channel;

    zt_mlme_get_connect(pnic_info, &is_connected);

    if (zt_local_cfg_get_work_mode(pnic_info) == ZT_INFRA_MODE &&
            is_connected == zt_false)
    {
        cur_channel = local_info->channel;
    }
    else
    {
        cur_channel = zt_wlan_get_cur_channel(pnic_info);
    }

    if (cur_channel == 14)
    {
        wrqu->freq.m = 2484 * 100000;
    }
    else
    {
        wrqu->freq.m = (2407 + cur_channel * 5) * 100000;
    }

    wrqu->freq.e = 1;
    wrqu->freq.i = cur_channel;

    return 0;
}

zt_s32 zt_iw_setOperationMode(struct net_device *ndev,
                              struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
    ndev_priv_st *pndev_priv = netdev_priv(ndev);
    nic_info_st *pnic_info = pndev_priv->nic;
    local_info_st *plocal = (local_info_st *)pnic_info->local_info;
    zt_bool bConnect = zt_false;
#ifdef CFG_ENABLE_AP_MODE
    zt_wlan_mgmt_info_t *wlan_mgmt_info = pnic_info->wlan_mgmt_info;
    zt_wlan_network_t *cur_network = &wlan_mgmt_info->cur_network;
#endif

    IW_FUNC_DBG("OpMode:%d", wrqu->mode);
    IW_FUNC_DBG("[zt_iw_setOperationMode]:mac:"ZT_MAC_FMT,
                ZT_MAC_ARG(ndev->dev_addr));

    if (pnic_info->is_driver_critical)
    {
        IW_FUNC_WARN("driver enter crital");
        return -EINVAL;
    }

#ifdef CONFIG_LPS
    if (ZT_RETURN_FAIL == zt_lps_wakeup(pnic_info, LPS_CTRL_SCAN, 0))
    {
        return -1;
    }
#endif
    if (plocal->work_mode == wrqu->mode)
    {
        return 0;
    }

    zt_local_cfg_set_work_mode(pnic_info, wrqu->mode);

    zt_mlme_get_connect(pnic_info, &bConnect);
    if (bConnect)
    {
        zt_mlme_deauth(pnic_info, zt_true, ZT_80211_REASON_DEAUTH_LEAVING);
    }

    zt_mcu_set_op_mode(pnic_info, wrqu->mode);

    ndev->type = ARPHRD_ETHER;

    switch (wrqu->mode)
    {
#ifdef CFG_ENABLE_AP_MODE
        case ZT_MASTER_MODE :
            cur_network->join_res = -1;
            zt_mlme_abort(pnic_info);
            break;
#endif

#ifdef CFG_ENABLE_MONITOR_MODE
        case ZT_MONITOR_MODE :
            ndev->type = ARPHRD_IEEE80211_RADIOTAP;
            zt_mlme_abort(pnic_info);
            IW_FUNC_DBG("ZT_MONITOR_MODE");
            break;
#endif

        case ZT_INFRA_MODE :
        case ZT_AUTO_MODE :
        default :
            break;
    }

#ifdef CFG_ENABLE_MONITOR_MODE
    if (zt_local_cfg_get_work_mode(pnic_info) == ZT_MONITOR_MODE)
    {
        zt_os_api_ind_connect(pnic_info, ZT_MLME_FRAMEWORK_WEXT);
    }
#endif

    return 0;
}

zt_s32 zt_iw_getOperationMode(struct net_device *ndev,
                              struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
    ndev_priv_st *pndev_priv = netdev_priv(ndev);
    nic_info_st *pnic_info = pndev_priv->nic;
    local_info_st *local_info = pnic_info->local_info;

    wrqu->mode = local_info->work_mode;

    return 0;
}

zt_s32 zt_iw_getSensitivity(struct net_device *ndev,
                            struct iw_request_info *info,
                            union iwreq_data *wrqu, char *extra)
{
    IW_FUNC_DBG("mac addr : "ZT_MAC_FMT,
                ZT_MAC_ARG(nic_to_local_addr(((ndev_priv_st *)netdev_priv(ndev))->nic)));
    wrqu->sens.value = 0;
    wrqu->sens.fixed = 0;
    wrqu->sens.disabled = 1;

    return 0;
}

zt_s32 zt_iw_getRange(struct net_device *ndev, struct iw_request_info *info,
                      union iwreq_data *wrqu, char *extra)
{
    struct iw_range *range = (struct iw_range *)extra;
    wireless_info_st *pwirl = NULL;
    zt_s32 i;

    pwirl = &def_wireless_info;

    IW_FUNC_DBG("mac addr : "ZT_MAC_FMT,
                ZT_MAC_ARG(nic_to_local_addr(((ndev_priv_st *)netdev_priv(ndev))->nic)));

    wrqu->data.length = sizeof(*range);
    zt_memset(range, 0, sizeof(*range));

    range->throughput = pwirl->throughput;

    range->max_qual.qual = pwirl->max_qual.qual;
    range->max_qual.level = pwirl->max_qual.level;
    range->max_qual.noise = pwirl->max_qual.noise;

    range->avg_qual.qual = pwirl->avg_qual.qual;
    range->avg_qual.level = pwirl->avg_qual.level;
    range->avg_qual.noise = pwirl->avg_qual.noise;

    range->num_bitrates = pwirl->num_bitrates;
    for (i = 0; i < pwirl->num_bitrates && i < IW_MAX_BITRATES; i++)
    {
        range->bitrate[i] = pwirl->bitrate[i];
    }

    range->min_frag = pwirl->min_frag;
    range->max_frag = pwirl->max_frag;

    range->pm_capa = 0;

    range->we_version_compiled = WIRELESS_EXT;
    range->we_version_source = 16;

    for (i = 0; i < pwirl->num_channels; i++)
    {
        range->freq[i].i = pwirl->pchannel_tab[i].num;
        range->freq[i].m = pwirl->pchannel_tab[i].freq * 100000;
        range->freq[i].e = 1;

        if (i == IW_MAX_FREQUENCIES)
        {
            break;
        }
    }

    range->num_channels = i;
    range->num_frequency = i;

    /*  The following code will proivde the security capability to network manager. */
    /*  If the driver doesn't provide this capability to network manager, */
    /*  the WPA/WPA2 routers can't be chosen in the network manager. */
#if WIRELESS_EXT > 17
    range->enc_capa = pwirl->enc_capa;
#endif

#ifdef IW_SCAN_CAPA_ESSID
    range->scan_capa = pwirl->scan_capa;
#endif

    return 0;
}


zt_s32 zt_iw_setPriv(struct net_device *ndev, struct iw_request_info *info,
                     union iwreq_data *wrqu, char *extra)
{
    IW_FUNC_DBG("mac addr : "ZT_MAC_FMT,
                ZT_MAC_ARG(nic_to_local_addr(((ndev_priv_st *)netdev_priv(ndev))->nic)));

    return 0;
}

zt_s32 zt_iw_getWirelessStats(struct net_device *ndev,
                              struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
    ndev_priv_st *pndev_priv = netdev_priv(ndev);
    nic_info_st *pnic_info = pndev_priv->nic;
    struct iw_statistics *piwstats = pnic_info->iwstats;

    IW_FUNC_DBG("mac addr : "ZT_MAC_FMT, ZT_MAC_ARG(nic_to_local_addr(pnic_info)));

    piwstats->qual.qual = 0;
    piwstats->qual.level = 0;
    piwstats->qual.noise = 0;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,14))
    piwstats->qual.updated = IW_QUAL_ALL_UPDATED;
#else
    piwstats->qual.updated = 0x0f;
#endif

#ifdef CONFIG_SIGNAL_DISPLAY_DBM
    piwstats->qual.updated = piwstats->qual.updated | IW_QUAL_DBM;
#endif

    return 0;
}

zt_s32 zt_iw_setWap(struct net_device *ndev, struct iw_request_info *info,
                    union iwreq_data *wrqu, char *extra)
{
    ndev_priv_st *pndev_priv = netdev_priv(ndev);
    nic_info_st *pnic_info = pndev_priv->nic;
    struct sockaddr *awrq = (struct sockaddr *)wrqu;

    IW_FUNC_DBG("mac addr : "ZT_MAC_FMT, ZT_MAC_ARG(nic_to_local_addr(pnic_info)));

    if (pnic_info->buddy_nic)
    {
        mlme_state_e state;
        zt_mlme_get_state((nic_info_st *)(pnic_info->buddy_nic), &state);
        if (state == MLME_STATE_SCAN ||
                state == MLME_STATE_CONN_SCAN ||
                state == MLME_STATE_AUTH ||
                state == MLME_STATE_ASSOC)
        {
            IW_FUNC_INFO("buddy interface is under linking !");
            return -EINVAL;
        }
    }

    if (awrq->sa_family != ARPHRD_ETHER)
    {
        return -EINVAL;
    }

    if (!zt_80211_is_valid_bssid((zt_u8 *)awrq->sa_data))
    {
        IW_FUNC_DBG("clear bssid");
        zt_mlme_conn_abort(pnic_info, zt_false, ZT_80211_REASON_DEAUTH_LEAVING);
        goto exit;
    }

    {
        zt_bool is_connected;
        zt_wlan_mgmt_scan_que_for_rst_e scan_que_for_rst;
        zt_wlan_mgmt_scan_que_node_t *pscan_que_node;
        zt_wlan_ssid_t ssid;
        zt_u8 *pbssid = awrq->sa_data;

        zt_mlme_get_connect(pnic_info, &is_connected);
        if (is_connected)
        {
            if (zt_80211_is_same_addr(pbssid, zt_wlan_get_cur_bssid(pnic_info)))
            {
                IW_FUNC_DBG("the bssid as same as the current associate bssid");
                zt_os_api_ind_connect(pnic_info, ZT_MLME_FRAMEWORK_WEXT);
                goto exit;
            }
        }

        IW_FUNC_DBG("bssid: "ZT_MAC_FMT, ZT_MAC_ARG(pbssid));

        zt_wlan_mgmt_scan_que_for_begin(pnic_info, pscan_que_node)
        {
            if (!zt_memcmp(pbssid, pscan_que_node->bssid, ETH_ALEN))
            {
                ssid.length = pscan_que_node->ssid.length;
                zt_memcpy(ssid.data, pscan_que_node->ssid.data, ssid.length);
                break;
            }
        }
        zt_wlan_mgmt_scan_que_for_end(scan_que_for_rst);

        if (scan_que_for_rst == ZT_WLAN_MGMT_SCAN_QUE_FOR_RST_BREAK)
        {
            /* start connect */
            zt_mlme_conn_start(pnic_info, pbssid, &ssid,
                               ZT_MLME_FRAMEWORK_WEXT, zt_true);
        }
        else if (scan_que_for_rst == ZT_WLAN_MGMT_SCAN_QUE_FOR_RST_END)
        {
            /* start connect */
            zt_mlme_conn_start(pnic_info, pbssid, NULL,
                               ZT_MLME_FRAMEWORK_WEXT, zt_true);
        }
    }

exit:
    return 0;
}

zt_s32 zt_iw_getWap(struct net_device *ndev, struct iw_request_info *info,
                    union iwreq_data *wrqu, char *extra)
{
    ndev_priv_st *pndev_priv = netdev_priv(ndev);
    nic_info_st *pnic_info = pndev_priv->nic;
    zt_wlan_mgmt_info_t *pwlan_mgmt_info = pnic_info->wlan_mgmt_info;
    zt_bool is_connected;
    zt_u8 *curBssid;
    wdn_net_info_st *pwdn_info;

    IW_FUNC_DBG("mac addr : "ZT_MAC_FMT, ZT_MAC_ARG(nic_to_local_addr(pnic_info)));

    wrqu->ap_addr.sa_family = ARPHRD_ETHER;

    zt_mlme_get_connect(pnic_info, &is_connected);
    if (is_connected)
    {
#if defined CFG_ENABLE_AP_MODE || defined CFG_ENABLE_ADHOC_MODE
        if (
#ifdef CFG_ENABLE_AP_MODE
            zt_local_cfg_get_work_mode(pnic_info) == ZT_MASTER_MODE
#endif
#ifdef CFG_ENABLE_ADHOC_MODE
#ifdef CFG_ENABLE_AP_MODE
            || zt_local_cfg_get_work_mode(pnic_info) == ZT_ADHOC_MODE
#else
            zt_local_cfg_get_work_mode(pnic_info) == ZT_ADHOC_MODE
#endif
#endif
        )
        {
            curBssid = zt_wlan_get_cur_bssid(pnic_info);
            zt_memcpy(wrqu->ap_addr.sa_data, curBssid, ETH_ALEN);
            IW_FUNC_DBG("bssid: "ZT_MAC_FMT, ZT_MAC_ARG(curBssid));
            return 0;
        }
        else
#endif
            if (zt_local_cfg_get_work_mode(pnic_info) == ZT_INFRA_MODE)
            {
                /*check bssid in wdn */
                curBssid = pwlan_mgmt_info->cur_network.mac_addr;
                pwdn_info = zt_wdn_find_info(pnic_info, curBssid);
                if (pwdn_info == NULL)
                {
                    IW_FUNC_ERROR("connection establishment, but can't find in wdn_info");
                    return -1;
                }

                zt_memcpy(wrqu->ap_addr.sa_data, curBssid, ETH_ALEN);
                IW_FUNC_DBG("bssid: "ZT_MAC_FMT, ZT_MAC_ARG(curBssid));
                return 0;
            }
    }

    zt_memset(wrqu->ap_addr.sa_data, 0, ETH_ALEN);

    return 0;
}

zt_s32 zt_iw_setMlme(struct net_device *ndev, struct iw_request_info *info,
                     union iwreq_data *wrqu, char *extra)
{
    struct iw_mlme *mlme = (struct iw_mlme *)extra;
    ndev_priv_st *pndev_priv = netdev_priv(ndev);
    nic_info_st *pnic_info = pndev_priv->nic;

    if (pnic_info->is_driver_critical)
    {
        IW_FUNC_WARN("driver enter crital");
        return -EINVAL;
    }

    if (mlme == NULL)
    {
        return -1;
    }

    IW_FUNC_DBG("cmd=%d, reason=%d", mlme->cmd, zt_le16_to_cpu(mlme->reason_code));

    /* cleap up sec info */
    zt_memset(pnic_info->sec_info, 0x0, sizeof(sec_info_st));

#ifdef CONFIG_LPS
    if (ZT_RETURN_FAIL == zt_lps_wakeup(pnic_info, LPS_CTRL_SCAN, 0))
    {
        return -1;
    }
#endif

    switch (mlme->cmd)
    {
        case IW_MLME_DEAUTH:
            IW_FUNC_DBG("IW_MLME_DEAUTH");
            zt_mlme_conn_abort(pnic_info, zt_false, ZT_80211_REASON_DEAUTH_LEAVING);
            break;

        case IW_MLME_DISASSOC:
            IW_FUNC_DBG("IW_MLME_DISASSOC");
            zt_mlme_conn_abort(pnic_info, zt_false, ZT_80211_REASON_DEAUTH_LEAVING);
            break;

        default:
            return -EOPNOTSUPP;
    }

    return 0;
}

zt_s32 zt_iw_setScan(struct net_device *ndev, struct iw_request_info *info,
                     union iwreq_data *wrqu, char *extra)
{
    ndev_priv_st *pndev_priv = netdev_priv(ndev);
    nic_info_st *pnic_info = pndev_priv->nic;
    zt_bool is_connected, is_busy;

    IW_FUNC_DBG("mac addr : "ZT_MAC_FMT, ZT_MAC_ARG(nic_to_local_addr(pnic_info)));

    if (pnic_info->buddy_nic)
    {
        mlme_state_e state;
        zt_mlme_get_state((nic_info_st *)(pnic_info->buddy_nic), &state);
        if (state == MLME_STATE_CONN_SCAN ||
                state == MLME_STATE_AUTH ||
                state == MLME_STATE_ASSOC)
        {
            IW_FUNC_INFO("interface or buddy interface is under linking !");
            zt_os_api_ind_scan_done(pnic_info, zt_true, ZT_MLME_FRAMEWORK_WEXT);
            return 0;
        }
    }

    zt_mlme_get_connect(pndev_priv->nic, &is_connected);
    if (is_connected)
    {
        zt_mlme_get_traffic_busy(pndev_priv->nic, &is_busy);
        if (is_busy)
        {
            zt_os_api_ind_scan_done(pnic_info, zt_true, ZT_MLME_FRAMEWORK_WEXT);

            return 0;
        }
        zt_mlme_scan_start(pnic_info, SCAN_TYPE_ACTIVE,
                           NULL, 0, NULL, 0,
                           ZT_MLME_FRAMEWORK_WEXT);
    }
    else
    {
        zt_wlan_ssid_t ssids[ZT_SCAN_REQ_SSID_NUM];
        zt_memset(ssids, 0, sizeof(ssids));
        if (wrqu->data.length == sizeof(struct iw_scan_req))
        {
            struct iw_scan_req *req = (struct iw_scan_req *)extra;
            if (wrqu->data.flags & IW_SCAN_THIS_ESSID)
            {
                zt_s32 len = min((zt_s32)req->essid_len, IW_ESSID_MAX_SIZE);
                zt_memcpy(ssids[0].data, req->essid, len);
                ssids[0].length = len;

                IW_FUNC_DBG("ssid = %s, ssid_len = %d", ssids[0].data, ssids[0].length);

                zt_mlme_scan_start(pnic_info, SCAN_TYPE_ACTIVE,
                                   ssids, 1, NULL, 0,
                                   ZT_MLME_FRAMEWORK_WEXT);
            }
        }
        else
        {
            zt_mlme_scan_start(pnic_info, SCAN_TYPE_ACTIVE,
                               NULL, 0, NULL, 0,
                               ZT_MLME_FRAMEWORK_WEXT);
        }
    }

    return 0;
}

zt_s32 zt_iw_getScan(struct net_device *ndev, struct iw_request_info *info,
                     union iwreq_data *wrqu, char *extra)
{
    ndev_priv_st *pndev_priv = netdev_priv(ndev);
    nic_info_st *pnic_info = pndev_priv->nic;
    zt_wlan_mgmt_scan_que_node_t *pscan_que_node = NULL;
    char *ev = extra;
    char *stop = ev + wrqu->data.length;
    zt_u32 res = 0;
    zt_wlan_mgmt_scan_que_for_rst_e scan_que_for_rst;
    zt_u16 apCount = 0;

    IW_FUNC_DBG("mac addr : "ZT_MAC_FMT, ZT_MAC_ARG(nic_to_local_addr(pnic_info)));

    if (zt_is_scanning(pnic_info))
    {
        return -EAGAIN;
    }
    /* Check if there is space for one more entry */
    zt_wlan_mgmt_scan_que_for_begin(pnic_info, pscan_que_node)
    {
        if ((stop - ev) < ZT_FIELD_SIZEOF(zt_wlan_mgmt_scan_que_node_t, ies))
        {
            res = -E2BIG;
            break;
        }
        ev = translate_scan_info(pnic_info, info, pscan_que_node, ev, stop);
        apCount++;
    }
    zt_wlan_mgmt_scan_que_for_end(scan_que_for_rst);

    wrqu->data.length = ev - extra;
    wrqu->data.flags = 0;

    if (scan_que_for_rst == ZT_WLAN_MGMT_SCAN_QUE_FOR_RST_FAIL)
    {
        res = -EAGAIN;
    }
    else
    {
        IW_FUNC_DBG("<ap count = %d / scaned_list count=%d>", apCount,
                    zt_que_count(&((zt_wlan_mgmt_info_t *)
                                   pnic_info->wlan_mgmt_info)->scan_que.ready));
    }

    return res;
}

static zt_bool is_8021x_auth(zt_80211_mgmt_ie_t *pies, zt_u16 ies_len)
{
    zt_80211_mgmt_ie_t *pie;
    zt_u16 offset_len = 0;
    zt_u32 pmulticast_cipher, punicast_cipher;

    pie = pies;

    while (offset_len < ies_len)
    {
        switch (pie->element_id)
        {
            case ZT_80211_MGMT_EID_RSN:
                IW_FUNC_DBG("RSN");
                return zt_true;
            case ZT_80211_MGMT_EID_VENDOR_SPECIFIC:
                if (!zt_80211_mgmt_wpa_survey(pie,
                                              ZT_OFFSETOF(zt_80211_mgmt_ie_t, data) + pie->len,
                                              NULL, NULL,
                                              &pmulticast_cipher,
                                              &punicast_cipher))
                {
                    IW_FUNC_INFO("WPA");
                    return zt_true;
                }
                break;
            default:
                break;
        }

        offset_len += pie->len + ZT_OFFSETOF(zt_80211_mgmt_ie_t, data);
        pie = (zt_80211_mgmt_ie_t *)(pie->data + pie->len);
    }

    IW_FUNC_INFO("NO 8021X");
    return zt_false;
}

zt_s32 zt_iw_setEssid(struct net_device *ndev, struct iw_request_info *info,
                      union iwreq_data *wrqu, char *extra)
{
    ndev_priv_st *pndev_priv = netdev_priv(ndev);
    nic_info_st *pnic_info = pndev_priv->nic;
    sec_info_st *psec_info = pnic_info->sec_info;
    zt_u8 len;

    IW_FUNC_DBG("mac addr : "ZT_MAC_FMT, ZT_MAC_ARG(nic_to_local_addr(pnic_info)));

    if (pnic_info->buddy_nic)
    {
        mlme_state_e state;
        zt_mlme_get_state((nic_info_st *)(pnic_info->buddy_nic), &state);
        if (state == MLME_STATE_SCAN ||
                state == MLME_STATE_CONN_SCAN ||
                state == MLME_STATE_AUTH ||
                state == MLME_STATE_ASSOC)
        {
            IW_FUNC_INFO("buddy interface is under linking !");
            return -EINVAL;
        }
    }

#ifdef CFG_ENABLE_AP_MODE
    if (zt_local_cfg_get_work_mode(pnic_info) == ZT_MASTER_MODE)
    {
        IW_FUNC_ERROR("ap no support set_essid");
        return -EPERM;
    }
#endif

#if WIRELESS_EXT <= 20
    len = wrqu->essid.length - 1;
#else
    len = wrqu->essid.length;
#endif
    if (len > IW_ESSID_MAX_SIZE)
    {
        IW_FUNC_ERROR("ssid length %d too long", len);
        return -E2BIG;
    }

    if (len == IW_ESSID_MAX_SIZE)
    {
        IW_FUNC_DBG("clear essid");
        goto exit;
    }

    if (wrqu->essid.flags && wrqu->essid.length)
    {
        zt_wlan_mgmt_scan_que_for_rst_e scan_que_for_rst;
        zt_wlan_mgmt_scan_que_node_t *pscan_que_node;
        zt_wlan_ssid_t ssid;
        zt_u8 *pbssid = NULL;

        /* retrive ssid */
        if (len >= sizeof(ssid.data))
        {
            return -EINVAL;
        }
        zt_memcpy(ssid.data, extra, ssid.length = len);
        ssid.data[ssid.length] = '\0';

        zt_wlan_mgmt_scan_que_for_begin(pnic_info, pscan_que_node)
        {
            if (zt_wlan_is_same_ssid(&pscan_que_node->ssid, &ssid))
            {
                pbssid = pscan_que_node->bssid;
                break;
            }
        }
        zt_wlan_mgmt_scan_que_for_end(scan_que_for_rst);

        if (scan_que_for_rst == ZT_WLAN_MGMT_SCAN_QUE_FOR_RST_FAIL)
        {
            IW_FUNC_WARN("scan queue for each fail");
            return -EAGAIN;
        }
        else
        {
            zt_bool is_connected;
            zt_mlme_get_connect(pnic_info, &is_connected);
            if (is_connected)
            {
                if (pbssid &&
                        zt_80211_is_same_addr(zt_wlan_get_cur_bssid(pnic_info), pbssid))
                {
                    IW_FUNC_INFO("the essid as same as the current associate ssid");
                    zt_os_api_ind_connect(pnic_info, ZT_MLME_FRAMEWORK_WEXT);
                    goto exit;
                }
            }

            if (psec_info->dot11PrivacyAlgrthm == _NO_PRIVACY_)
            {
                zt_memset(pnic_info->sec_info, 0x0, sizeof(sec_info_st));
            }
            else if (scan_que_for_rst == ZT_WLAN_MGMT_SCAN_QUE_FOR_RST_BREAK)
            {
                zt_80211_mgmt_ie_t *pies =
                    (void *)((struct beacon_ie *)pscan_que_node->ies)->variable;
                zt_u16 ies_len = pscan_que_node->ie_len -
                                 ZT_OFFSETOF(struct beacon_ie, variable);
                if (!is_8021x_auth(pies, ies_len) &&
                        psec_info->dot11AuthAlgrthm == dot11AuthAlgrthm_8021X)
                {
                    IW_FUNC_DBG("clean sec info!!!");
                    zt_memset(pnic_info->sec_info, 0x0, sizeof(sec_info_st));
                }
            }

            /* start connection */
            zt_mlme_conn_start(pnic_info, pbssid, &ssid,
                               ZT_MLME_FRAMEWORK_WEXT, zt_true);
        }
    }

exit :
    return 0;
}

zt_s32 zt_iw_getEssid(struct net_device *ndev, struct iw_request_info *info,
                      union iwreq_data *wrqu, char *extra)
{
    ndev_priv_st *pndev_priv = netdev_priv(ndev);
    nic_info_st *pnic_info = pndev_priv->nic;
    zt_bool is_connected = zt_false;
    zt_wlan_ssid_t *curSsid;

    IW_FUNC_DBG("mac addr : "ZT_MAC_FMT, ZT_MAC_ARG(nic_to_local_addr(pnic_info)));

    zt_mlme_get_connect(pnic_info, &is_connected);
    if (is_connected == zt_true)
    {
#ifdef CFG_ENABLE_ADHOC_MODE
        wdn_net_info_st *pwdn_info;
        /*check bssid in wdn */
        if (zt_local_cfg_get_work_mode(pnic_info) != ZT_ADHOC_MODE)
        {
            zt_u8 *curBssid = zt_wlan_get_cur_bssid(pnic_info);
            pwdn_info = zt_wdn_find_info(pnic_info, curBssid);
            if (pwdn_info == NULL)
            {
                return -1;
            }
            IW_FUNC_DBG("<ssid:%s>", pwdn_info->ssid);

            wrqu->essid.flags = 1;
            wrqu->essid.length = pwdn_info->ssid_len;
            zt_memcpy(extra, pwdn_info->ssid, wrqu->essid.length);
        }
        else
#endif
        {
            curSsid = zt_wlan_get_cur_ssid(pnic_info);

            wrqu->essid.flags = 1;
            wrqu->essid.length = curSsid->length;
            zt_memcpy(extra, curSsid->data, wrqu->essid.length);
            IW_FUNC_DBG("<ssid:%s>", extra);
        }
    }
    else
    {
        IW_FUNC_DBG("<ssid:NULL>");
        return -1;
    }

    return 0;
}

zt_s32 zt_iw_getNick(struct net_device *ndev, struct iw_request_info *info,
                     union iwreq_data *wrqu, char *extra)
{
    if (extra)
    {
        wrqu->data.length = 12;
        wrqu->data.flags = 1;
        zt_memcpy(extra, "<WIFI@ZTOP>", 12);
    }

    return 0;
}

zt_s32 zt_iw_setRate(struct net_device *ndev, struct iw_request_info *info,
                     union iwreq_data *wrqu, char *extra)
{
    zt_u8 datarates[13];
    zt_u32 target_rate = wrqu->bitrate.value;
    zt_u32 fixed = wrqu->bitrate.fixed;
    zt_u32 ratevalue = 0;
    zt_u8 mpdatarate[13] = { 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 0xff };
    zt_u32 i = 0;
    zt_u32 res = 0;

    IW_FUNC_DBG("target_rate:%d  fixed:%d", target_rate, fixed);

    if (target_rate == -1)
    {
        ratevalue = 11;
        goto set_rate;
    }
    target_rate = target_rate / 100000;

    switch (target_rate)
    {
        case 10:
            ratevalue = 0;
            break;
        case 20:
            ratevalue = 1;
            break;
        case 55:
            ratevalue = 2;
            break;
        case 60:
            ratevalue = 3;
            break;
        case 90:
            ratevalue = 4;
            break;
        case 110:
            ratevalue = 5;
            break;
        case 120:
            ratevalue = 6;
            break;
        case 180:
            ratevalue = 7;
            break;
        case 240:
            ratevalue = 8;
            break;
        case 360:
            ratevalue = 9;
            break;
        case 480:
            ratevalue = 10;
            break;
        case 540:
            ratevalue = 11;
            break;
        default:
            ratevalue = 11;
            break;
    }

set_rate:

    for (i = 0; i < 13; i++)
    {
        if (ratevalue == mpdatarate[i])
        {
            datarates[i] = mpdatarate[i];
            if (fixed == 0)
            {
                break;
            }
        }
        else
        {
            datarates[i] = 0xff;
        }

    }

    return res;
}


zt_s32 zt_iw_getRate(struct net_device *ndev, struct iw_request_info *info,
                     union iwreq_data *wrqu, char *extra)
{
    zt_u16 max_rate = 0;
    zt_s32 ret = 0;
    ndev_priv_st *pndev_priv = netdev_priv(ndev);
    nic_info_st *pnic_info = pndev_priv->nic;

    ret = zt_wlan_get_max_rate(pnic_info, zt_wlan_get_cur_bssid(pnic_info),
                               &max_rate);
    if (-1 == ret)
    {
        return -EPERM;
    }

    if (max_rate == 0)
    {
        return -EPERM;
    }

    wrqu->bitrate.fixed = 0;
    wrqu->bitrate.value = max_rate * 100000;


    return 0;
}

zt_s32 zt_iw_setRts(struct net_device *ndev, struct iw_request_info *info,
                    union iwreq_data *wrqu, char *extra)
{
    ndev_priv_st *pndev_priv  = netdev_priv(ndev);
    nic_info_st *pnic_info = pndev_priv->nic;
    hw_info_st *rts_priv = (hw_info_st *)pnic_info->hw_info;

    IW_FUNC_DBG("mac addr : "ZT_MAC_FMT, ZT_MAC_ARG(nic_to_local_addr(pnic_info)));


    if (wrqu->rts.disabled)
    {
        rts_priv->rts_thresh = 2347;
    }
    else
    {
        if ((wrqu->rts.value < 0) ||
                (wrqu->rts.value > 2347))
        {
            return -EINVAL;
        }

        rts_priv->rts_thresh = wrqu->rts.value;
    }

    IW_FUNC_DBG("rts_thresh=%d", rts_priv->rts_thresh);

    return 0;
}

zt_s32 zt_iw_getRts(struct net_device *ndev, struct iw_request_info *info,
                    union iwreq_data *wrqu, char *extra)
{
    ndev_priv_st *pndev_priv  = netdev_priv(ndev);
    hw_info_st *rts_priv = pndev_priv->nic->hw_info;

    IW_FUNC_DBG("mac addr : "ZT_MAC_FMT,
                ZT_MAC_ARG(nic_to_local_addr(pndev_priv->nic)));

    IW_FUNC_DBG("rts_thresh=%d", rts_priv->rts_thresh);
    wrqu->rts.value = rts_priv->rts_thresh;
    wrqu->rts.fixed = 0;

    return 0;
}

zt_s32 zt_iw_setFragmentation(struct net_device *ndev,
                              struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
    ndev_priv_st *pndev_priv = netdev_priv(ndev);
    nic_info_st *pnic_info = pndev_priv->nic;
    hw_info_st *frag_priv = (hw_info_st *)pnic_info->hw_info;

    IW_FUNC_DBG("mac addr : "ZT_MAC_FMT, ZT_MAC_ARG(nic_to_local_addr(pnic_info)));

    if (wrqu->frag.disabled)
    {
        frag_priv->frag_thresh = MAX_FRAG_THRESHOLD;
    }
    else
    {
        if (wrqu->frag.value < MIN_FRAG_THRESHOLD ||
                wrqu->frag.value > MAX_FRAG_THRESHOLD)
        {
            return  -EINVAL;
        }

        frag_priv->frag_thresh = wrqu->frag.value & ~0x1;
    }

    IW_FUNC_DBG("frag_len=%d", frag_priv->frag_thresh);

    return 0;
}

zt_s32 zt_iw_getFragmentation(struct net_device *ndev,
                              struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
    ndev_priv_st *pndev_priv = netdev_priv(ndev);
    hw_info_st *frag_priv = pndev_priv->nic->hw_info;

    IW_FUNC_DBG("mac addr : "ZT_MAC_FMT,
                ZT_MAC_ARG(nic_to_local_addr(pndev_priv->nic)));

    IW_FUNC_DBG("frag_len=%d", frag_priv->frag_thresh);
    wrqu->frag.value = frag_priv->frag_thresh;
    wrqu->frag.fixed = 0;

    return 0;
}

zt_s32 zt_iw_getRetry(struct net_device *ndev, struct iw_request_info *info,
                      union iwreq_data *wrqu, char *extra)
{
    IW_FUNC_DBG("mac addr : "ZT_MAC_FMT,
                ZT_MAC_ARG(nic_to_local_addr(((ndev_priv_st *)netdev_priv(ndev))->nic)));

    wrqu->retry.value = 7;
    wrqu->retry.fixed = 0;
    wrqu->retry.disabled = 1;

    return 0;
}

zt_s32 zt_iw_setEnc(struct net_device *ndev, struct iw_request_info *info,
                    union iwreq_data *wrqu, char *extra)
{
    zt_u32 key;
    zt_u32 keyindex_provided;
    zt_s32 keyid;
    wl_ndis_802_11_wep_st wep;
    zt_ndis_802_11_auth_mode_e authmode;

    struct iw_point *erq = &(wrqu->encoding);
    ndev_priv_st *pndev_priv = netdev_priv(ndev);
    nic_info_st *pnic_info = pndev_priv->nic;
    sec_info_st *sec_info = pnic_info->sec_info;

    IW_FUNC_DBG("mac addr : "ZT_MAC_FMT, ZT_MAC_ARG(nic_to_local_addr(pnic_info)));
    IW_FUNC_DBG(",flags=0x%x", erq->flags);

    zt_memset(&wep, 0, sizeof(wl_ndis_802_11_wep_st));

    key = erq->flags & IW_ENCODE_INDEX;


    if (erq->flags & IW_ENCODE_DISABLED)
    {
        IW_FUNC_DBG("EncryptionDisabled");
        sec_info->ndisencryptstatus = zt_ndis802_11EncryptionDisabled;
        sec_info->dot11PrivacyAlgrthm = _NO_PRIVACY_;
        sec_info->dot118021XGrpPrivacy = _NO_PRIVACY_;
        sec_info->dot11AuthAlgrthm = dot11AuthAlgrthm_Open;
        authmode = zt_ndis802_11AuthModeOpen;
        sec_info->ndisauthtype = authmode;

        return 0;
    }

    if (key)
    {
        if (key > ZT_80211_WEP_KEYS)
        {
            return -EINVAL;
        }
        key--;
        keyindex_provided = 1;
    }
    else
    {
        keyindex_provided = 0;
        key = sec_info->dot11PrivacyKeyIndex;
        IW_FUNC_DBG(", key=%d", key);
    }

    if (erq->flags & IW_ENCODE_OPEN)
    {
        IW_FUNC_DBG("IW_ENCODE_OPEN");
        sec_info->ndisencryptstatus = zt_ndis802_11Encryption1Enabled;

        sec_info->dot11AuthAlgrthm = dot11AuthAlgrthm_Open;

        sec_info->dot11PrivacyAlgrthm = _NO_PRIVACY_;
        sec_info->dot118021XGrpPrivacy = _NO_PRIVACY_;
        authmode = zt_ndis802_11AuthModeOpen;
        sec_info->ndisauthtype = authmode;
    }
    else if (erq->flags & IW_ENCODE_RESTRICTED)
    {
        IW_FUNC_DBG("IW_ENCODE_RESTRICTED");
        sec_info->ndisencryptstatus = zt_ndis802_11Encryption1Enabled;

        sec_info->dot11AuthAlgrthm = dot11AuthAlgrthm_Shared;

        sec_info->dot11PrivacyAlgrthm = _WEP40_;
        sec_info->dot118021XGrpPrivacy = _WEP40_;
        authmode = zt_ndis802_11AuthModeShared;
        sec_info->ndisauthtype = authmode;
    }
    else
    {
        IW_FUNC_DBG(",erq->flags=0x%x", erq->flags);

        sec_info->ndisencryptstatus = zt_ndis802_11Encryption1Enabled;
        sec_info->dot11AuthAlgrthm = dot11AuthAlgrthm_Open;
        sec_info->dot11PrivacyAlgrthm = _NO_PRIVACY_;
        sec_info->dot118021XGrpPrivacy = _NO_PRIVACY_;
        authmode = zt_ndis802_11AuthModeOpen;
        sec_info->ndisauthtype = authmode;
    }

    wep.KeyIndex = key;
    if (erq->length > 0)
    {
        wep.KeyLength = erq->length <= 5 ? 5 : 13;

        wep.Length = wep.KeyLength + ZT_OFFSETOF(wl_ndis_802_11_wep_st, KeyMaterial);
    }
    else
    {
        wep.KeyLength = 0;

        if (keyindex_provided == 1)
        {
            sec_info->dot11PrivacyKeyIndex = key;

            IW_FUNC_DBG(",(keyindex_provided == 1), keyid=%d, key_len=%d", key,
                        sec_info->dot11DefKeylen[key]);

            switch (sec_info->dot11DefKeylen[key])
            {
                case 5:
                    sec_info->dot11PrivacyAlgrthm = _WEP40_;
                    break;
                case 13:
                    sec_info->dot11PrivacyAlgrthm = _WEP104_;
                    break;
                default:
                    sec_info->dot11PrivacyAlgrthm = _NO_PRIVACY_;
                    break;
            }

            return 0;

        }

    }

    wep.KeyIndex |= 0x80000000;

    zt_memcpy(wep.KeyMaterial, extra, wep.KeyLength);

    keyid = wep.KeyIndex & 0x3fffffff;

    if (keyid >= 4)
    {
        IW_FUNC_ERROR("keyid >= 4,false");
        return -1;
    }

    switch (wep.KeyLength)
    {
        case 5:
            sec_info->dot11PrivacyAlgrthm = _WEP40_;
            break;
        case 13:
            sec_info->dot11PrivacyAlgrthm = _WEP104_;
            break;
        default:
            sec_info->dot11PrivacyAlgrthm = _NO_PRIVACY_;
            break;
    }

    zt_memcpy(&(sec_info->dot11DefKey[keyid].skey[0]),
              &(wep.KeyMaterial), wep.KeyLength);

    sec_info->dot11DefKeylen[keyid] = wep.KeyLength;

    sec_info->dot11PrivacyKeyIndex = keyid;

    return 0;
}

zt_s32 zt_iw_getEnc(struct net_device *ndev, struct iw_request_info *info,
                    union iwreq_data *wrqu, char *extra)
{
    zt_u32 key;
    ndev_priv_st *pndev_priv = netdev_priv(ndev);
    nic_info_st *pnic_info = pndev_priv->nic;
    struct iw_point *erq = &(wrqu->encoding);
    sec_info_st *sec_info = pnic_info->sec_info;
    IW_FUNC_DBG("mac addr : "ZT_MAC_FMT, ZT_MAC_ARG(nic_to_local_addr(pnic_info)));

    key = erq->flags & IW_ENCODE_INDEX;

    if (key)
    {
        if (key > ZT_80211_WEP_KEYS)
        {
            return -EINVAL;
        }
        key--;
    }
    else
    {
        key = sec_info->dot11PrivacyKeyIndex;
    }

    erq->flags = key + 1;

    switch (sec_info->ndisencryptstatus)
    {
        case zt_ndis802_11EncryptionNotSupported:
        case zt_ndis802_11EncryptionDisabled:

            erq->length = 0;
            erq->flags |= IW_ENCODE_DISABLED;

            break;

        case zt_ndis802_11Encryption1Enabled:

            erq->length = sec_info->dot11DefKeylen[key];

            if (erq->length)
            {
                zt_memcpy(extra, sec_info->dot11DefKey[key].skey,
                          sec_info->dot11DefKeylen[key]);

                erq->flags |= IW_ENCODE_ENABLED;

                if (sec_info->ndisauthtype == zt_ndis802_11AuthModeOpen)
                {
                    erq->flags |= IW_ENCODE_OPEN;
                }
                else if (sec_info->ndisauthtype ==
                         zt_ndis802_11AuthModeShared)
                {
                    erq->flags |= IW_ENCODE_RESTRICTED;
                }
            }
            else
            {
                erq->length = 0;
                erq->flags |= IW_ENCODE_DISABLED;
            }

            break;

        case zt_ndis802_11Encryption2Enabled:
        case zt_ndis802_11Encryption3Enabled:

            erq->length = 16;
            erq->flags |= (IW_ENCODE_ENABLED | IW_ENCODE_OPEN | IW_ENCODE_NOKEY);

            break;

        default:
            erq->length = 0;
            erq->flags |= IW_ENCODE_DISABLED;

            break;

    }

    return 0;
}

zt_s32 zt_iw_getPower(struct net_device *ndev, struct iw_request_info *info,
                      union iwreq_data *wrqu, char *extra)
{
    IW_FUNC_DBG("mac addr : "ZT_MAC_FMT,
                ZT_MAC_ARG(nic_to_local_addr(((ndev_priv_st *)netdev_priv(ndev))->nic)));

    wrqu->power.value = 0;
    wrqu->power.fixed = 0;
    wrqu->power.disabled = 1;

    return 0;
}

zt_s32 zt_iw_set_wpa_ie(nic_info_st *pnic_info, zt_u8 *pie, size_t ielen)
{
    sec_info_st *sec_info = pnic_info->sec_info;
    zt_u8 *buf = NULL;
    zt_s32 group_cipher = 0, pairwise_cipher = 0;
    zt_u16 cnt = 0;
    zt_u8 eid, wps_oui[4] = { 0x0, 0x50, 0xf2, 0x04 };
    zt_s32 res = 0;

    IW_FUNC_DBG("mac addr : "ZT_MAC_FMT, ZT_MAC_ARG(nic_to_local_addr(pnic_info)));

    if (pnic_info->is_driver_critical)
    {
        IW_FUNC_WARN("driver enter crital");
        return -EINVAL;
    }

    if (pie == NULL)
    {
        goto exit;
    }

    if (ielen > ZT_MAX_WPA_IE_LEN)
    {
        res = -EINVAL;
        goto exit;
    }

    if (ielen)
    {
        buf = zt_kzalloc(ielen);
        if (buf == NULL)
        {
            res = -ENOMEM;
            goto exit;
        }
        zt_memcpy(buf, pie, ielen);

        if (ielen < ZT_RSN_HD_LEN)
        {
            IW_FUNC_ERROR("Ie len too short(%d)", (zt_u16)ielen);
            res = -EINVAL;
            goto exit;
        }

        {
            void *pdata;
            zt_u16 data_len;

            if (!zt_80211_mgmt_wpa_survey(buf, ielen, &pdata, &data_len,
                                          &group_cipher, &pairwise_cipher))
            {
                sec_info->dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;
                sec_info->ndisauthtype = zt_ndis802_11AuthModeWPAPSK;
                sec_info->wpa_enable = zt_true;
                zt_memcpy(sec_info->supplicant_ie, pdata, data_len);
            }
            else if (!zt_80211_mgmt_rsn_survey(buf, ielen, &pdata, &data_len,
                                               &group_cipher, &pairwise_cipher))
            {
                sec_info->dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;
                sec_info->ndisauthtype = zt_ndis802_11AuthModeWPA2PSK;
                sec_info->rsn_enable = zt_true;
                zt_memcpy(sec_info->supplicant_ie, pdata, data_len);
            }
        }

        switch (group_cipher)
        {
            case ZT_CIPHER_SUITE_TKIP:
                sec_info->dot118021XGrpPrivacy = _TKIP_;
                sec_info->ndisencryptstatus = zt_ndis802_11Encryption2Enabled;
                IW_FUNC_DBG("dot118021XGrpPrivacy=_TKIP_");
                break;
            case ZT_CIPHER_SUITE_CCMP:
                sec_info->dot118021XGrpPrivacy = _AES_;
                sec_info->ndisencryptstatus = zt_ndis802_11Encryption3Enabled;
                IW_FUNC_DBG("dot118021XGrpPrivacy=_AES_");
                break;
        }

        switch (pairwise_cipher)
        {
            case ZT_CIPHER_SUITE_NONE:
                break;
            case ZT_CIPHER_SUITE_TKIP:
                sec_info->dot11PrivacyAlgrthm = _TKIP_;
                sec_info->ndisencryptstatus = zt_ndis802_11Encryption2Enabled;
                IW_FUNC_DBG("dot11PrivacyAlgrthm=_TKIP_");
                break;
            case ZT_CIPHER_SUITE_CCMP:
                sec_info->dot11PrivacyAlgrthm = _AES_;
                sec_info->ndisencryptstatus = zt_ndis802_11Encryption3Enabled;
                IW_FUNC_DBG("dot11PrivacyAlgrthm=_AES_");
                break;
        }

        while (cnt < ielen)
        {
            eid = buf[cnt];
            if (eid == ZT_80211_MGMT_EID_VENDOR_SPECIFIC &&
                    !zt_memcmp(&buf[cnt + 2], wps_oui, 4))
            {
                IW_FUNC_DBG("SET WPS_IE");
                sec_info->wps_ie_len = ZT_MIN(buf[cnt + 1] + 2, 512);
                zt_memcpy(sec_info->wps_ie, &buf[cnt], sec_info->wps_ie_len);
                cnt += buf[cnt + 1] + 2;
                break;
            }
            else
            {
                cnt += buf[cnt + 1] + 2;
            }
        }

        zt_mcu_set_on_rcr_am(pnic_info, zt_false);
        //        zt_mcu_set_hw_invalid_all(pnic_info);
    }

exit :
    if (buf)
    {
        zt_kfree(buf);
    }
    return res;
}

zt_s32 zt_iw_setGenIe(struct net_device *ndev, struct iw_request_info *info,
                      union iwreq_data *wrqu, char *extra)
{
    ndev_priv_st *pndev_priv = netdev_priv(ndev);
    nic_info_st *pnic_info = pndev_priv->nic;
    zt_s32 res = 0;

    LOG_D("VALUE:%s", extra);
    LOG_D("len:%d", wrqu->data.length);
    res = zt_iw_set_wpa_ie(pnic_info, extra, wrqu->data.length);

    return res;
}

static zt_s32 wpa_set_auth_algs(struct net_device *ndev, zt_u32 value)
{
    ndev_priv_st *pndev_priv = netdev_priv(ndev);
    nic_info_st *pnic_info = pndev_priv->nic;
    sec_info_st *psec_info = pnic_info->sec_info;
    zt_s32 res = 0;

    if ((value & AUTH_ALG_SHARED_KEY) && (value & AUTH_ALG_OPEN_SYSTEM))
    {
        IW_FUNC_DBG("AUTH_ALG_SHARED_KEY and  AUTH_ALG_OPEN_SYSTEM [value:0x%x]",
                    value);
        psec_info->ndisencryptstatus = zt_ndis802_11Encryption1Enabled;
        psec_info->ndisauthtype = zt_ndis802_11AuthModeAutoSwitch;
        psec_info->dot11AuthAlgrthm = dot11AuthAlgrthm_Auto;
    }
    else if (value & AUTH_ALG_SHARED_KEY)
    {
        IW_FUNC_DBG("AUTH_ALG_SHARED_KEY  [value:0x%x]", value);
        psec_info->ndisencryptstatus = zt_ndis802_11Encryption1Enabled;
        psec_info->ndisauthtype = zt_ndis802_11AuthModeShared;
        psec_info->dot11AuthAlgrthm = dot11AuthAlgrthm_Shared;
    }
    else if (value & AUTH_ALG_OPEN_SYSTEM)
    {
        IW_FUNC_DBG("AUTH_ALG_OPEN_SYSTEM  [value:0x%x]", value);
        psec_info->ndisauthtype = zt_ndis802_11AuthModeOpen;
        psec_info->dot11AuthAlgrthm = dot11AuthAlgrthm_Open;
    }
    else if (value & AUTH_ALG_LEAP)
    {
        IW_FUNC_DBG("AUTH_ALG_LEAP  [value:0x%x]", value);
    }
    else
    {
        IW_FUNC_DBG("error!");
        res = -EINVAL;
    }

    return res;
}

zt_s32 zt_iw_setAuth(struct net_device *ndev, struct iw_request_info *info,
                     union iwreq_data *wrqu, char *extra)
{
    ndev_priv_st *pndev_priv = netdev_priv(ndev);
    nic_info_st *pnic_info = pndev_priv->nic;
    sec_info_st *psec_info = pnic_info->sec_info;
    struct iw_param *param = (struct iw_param *)&wrqu->param;
    zt_s32 res = 0;

    IW_FUNC_DBG("mac addr : "ZT_MAC_FMT, ZT_MAC_ARG(nic_to_local_addr(pnic_info)));

    switch (param->flags & IW_AUTH_INDEX)
    {
        case IW_AUTH_MFP:
            IW_FUNC_DBG("IW_AUTH_MFP");
            break;

        case IW_AUTH_WPA_VERSION:
            IW_FUNC_DBG("IW_AUTH_WPA_VERSION");
            break;

        case IW_AUTH_CIPHER_PAIRWISE:
            IW_FUNC_DBG("IW_AUTH_CIPHER_PAIRWISE");
            break;

        case IW_AUTH_CIPHER_GROUP:
            IW_FUNC_DBG("IW_AUTH_CIPHER_GROUP");
            break;

        case IW_AUTH_KEY_MGMT:
            IW_FUNC_DBG("IW_AUTH_KEY_MGMT");
            break;

        case IW_AUTH_TKIP_COUNTERMEASURES:
            IW_FUNC_DBG("IW_AUTH_TKIP_COUNTERMEASURES");
            if (param->value)
            {
                /*  wpa_supplicant is enabling the tkip countermeasure. */
                psec_info->btkip_countermeasure = true;
            }
            else
            {
                /*  wpa_supplicant is disabling the tkip countermeasure. */
                psec_info->btkip_countermeasure = false;
            }
            break;

        case IW_AUTH_DROP_UNENCRYPTED:
            IW_FUNC_DBG("IW_AUTH_DROP_UNENCRYPTED");
            /* HACK:
             *
             * wpa_supplicant calls set_wpa_enabled when the driver
             * is loaded and unloaded, regardless of if WPA is being
             * used.  No other calls are made which can be used to
             * determine if encryption will be used or not prior to
             * association being expected.  If encryption is not being
             * used, drop_unencrypted is set to false, else true -- we
             * can use this to determine if the CAP_PRIVACY_ON bit should
             * be set.
             */

            if (psec_info->ndisencryptstatus == zt_ndis802_11Encryption1Enabled)
            {
                break;/* it means init value, or using wep,
                         ndisencryptstatus = zt_ndis802_11Encryption1Enabled, */
            }

            /*  then it needn't reset it; */
            if (param->value)
            {
                psec_info->ndisauthtype = zt_ndis802_11AuthModeOpen;
                psec_info->ndisencryptstatus = zt_ndis802_11EncryptionDisabled;
                psec_info->dot11AuthAlgrthm = dot11AuthAlgrthm_Open;
                psec_info->dot11PrivacyAlgrthm = _NO_PRIVACY_;
                psec_info->dot118021XGrpPrivacy = _NO_PRIVACY_;
            }
            break;

        case IW_AUTH_80211_AUTH_ALG:
            IW_FUNC_DBG("IW_AUTH_80211_AUTH_ALG");
            res = wpa_set_auth_algs(ndev, (zt_u32)param->value);
            break;

        case IW_AUTH_WPA_ENABLED:
            IW_FUNC_DBG("IW_AUTH_WPA_ENABLED");
            break;

        case IW_AUTH_RX_UNENCRYPTED_EAPOL:
            IW_FUNC_DBG("IW_AUTH_RX_UNENCRYPTED_EAPOL");
            break;

        case IW_AUTH_PRIVACY_INVOKED:
            IW_FUNC_DBG("IW_AUTH_PRIVACY_INVOKED");
            break;

        default:
            res = -EOPNOTSUPP;
    }

    return res;
}

zt_s32 zt_iw_getAuth(struct net_device *ndev, struct iw_request_info *info,
                     union iwreq_data *wrqu, char *extra)
{
    IW_FUNC_DBG("mac addr : "ZT_MAC_FMT,
                ZT_MAC_ARG(nic_to_local_addr(((ndev_priv_st *)netdev_priv(ndev))->nic)));

    return 0;
}

zt_s32 zt_iw_setEncExt(struct net_device *ndev,
                       struct iw_request_info *info,
                       union iwreq_data *wrqu, char *extra)
{
    zt_u32 param_len;
    struct iw_crypt *param = NULL;
    struct iw_point *pencoding = &wrqu->encoding;
    struct iw_encode_ext *pext = (struct iw_encode_ext *)extra;
    zt_s32 res = 0;

    IW_FUNC_DBG("mac addr : "ZT_MAC_FMT,
                ZT_MAC_ARG(nic_to_local_addr(((ndev_priv_st *)netdev_priv(ndev))->nic)));

    param_len = ZT_OFFSETOF(struct iw_crypt, key) + pext->key_len;
    param = (struct iw_crypt *)zt_vmalloc(param_len);
    if (param == NULL)
    {
        LOG_E("[%s]: no memory for param", __func__);
        res = -EPERM;
        goto exit;
    }
    zt_memset(param, 0, param_len);
    zt_memset(param->sta_addr, 0xff, ETH_ALEN);

    param->alg = pext->alg;
    if (pext->ext_flags & IW_ENCODE_EXT_SET_TX_KEY)
    {
        param->set_tx = 1;
    }

    if (pext->alg != IW_ENCODE_ALG_WEP &&
            pext->ext_flags & IW_ENCODE_EXT_GROUP_KEY)
    {
        param->set_tx = 0;
    }

    param->idx = (pencoding->flags & 0x00FF) - 1;
    IW_FUNC_DBG("iw_crypt.idx=%d", param->idx);

    if (pext->key_len)
    {
        param->key_len = pext->key_len;
        zt_memcpy(param->key, pext->key, pext->key_len);
        IW_FUNC_DBG("iw_crypt.key=");
        IW_FUNC_ARRAY(param->key, pext->key_len);
    }

    res = set_encryption(ndev, param, param_len);

exit :
    if (param)
    {
        zt_vfree((zt_u8 *)param);
    }

    return res;
}

zt_s32 zt_iw_getEncExt(struct net_device *ndev, struct iw_request_info *info,
                       union iwreq_data *wrqu, char *extra)
{
    IW_FUNC_DBG("mac addr : "ZT_MAC_FMT,
                ZT_MAC_ARG(nic_to_local_addr(((ndev_priv_st *)netdev_priv(ndev))->nic)));

    return 0;
}

/*
 *  There are the BSSID information in the bssid.sa_data array.
 *  If cmd is IW_PMKSA_FLUSH, it means the wpa_supplicant wants to clear
 *  all the PMKID information. If cmd is IW_PMKSA_ADD, it means the
 *  wpa_supplicant wants to add a PMKID/BSSID to driver.
 *  If cmd is IW_PMKSA_REMOVE, it means the wpa_supplicant wants to
 *  remove a PMKID/BSSID from driver.
 */
zt_s32 zt_iw_setPmkid(struct net_device *ndev, struct iw_request_info *info,
                      union iwreq_data *wrqu, char *extra)
{
    IW_FUNC_DBG("mac addr : "ZT_MAC_FMT,
                ZT_MAC_ARG(nic_to_local_addr(((ndev_priv_st *)netdev_priv(ndev))->nic)));

    return 0;
}

