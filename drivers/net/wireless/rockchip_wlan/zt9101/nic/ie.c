/*
 * ie.c
 *
 * used for implement the basic operation interface of IEEE80211 management
 * frame element id
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

zt_u8 *set_ie(zt_u8 *pbuf, zt_u8 index, zt_u8 len, zt_u8 *source, zt_u32 *frlen)
{
    *pbuf = index;
    *(pbuf + 1) = len;
    if (len > 0)
    {
        zt_memcpy((void *)(pbuf + 2), (void *)source, len);
    }

    *frlen = *frlen + (len + 2);
    return (pbuf + len + 2);
}

zt_u8 *set_fixed_ie(zt_u8 *pbuf, zt_u32 len, zt_u8 *source, zt_u16 *frlen)
{
    zt_memcpy((void *)pbuf, (void *)source, len);
    *frlen = *frlen + len;
    return (pbuf + len);
}





zt_s32 have_cckrates(zt_u8 *rate, zt_s32 ratelen)
{
    zt_s32 i;

    for (i = 0; i < ratelen; i++)
    {
        if ((((rate[i]) & 0x7f) == 2) || (((rate[i]) & 0x7f) == 4) ||
                (((rate[i]) & 0x7f) == 11) || (((rate[i]) & 0x7f) == 22))
        {
            return 1;
        }
    }

    return -1;

}

zt_s32 only_cckrates(zt_u8 *rate, zt_s32 ratelen)
{
    zt_s32 i;

    for (i = 0; i < ratelen; i++)
    {
        if ((((rate[i]) & 0x7f) != 2) && (((rate[i]) & 0x7f) != 4) &&
                (((rate[i]) & 0x7f) != 11) && (((rate[i]) & 0x7f) != 22))
        {
            return -1;
        }
    }

    return 1;
}

void do_set_mcs_by_flag(zt_u8 *mcs_set, zt_u32 mask)
{
    zt_u8 mcs_rate_1r = (zt_u8)(mask & 0xff);
    zt_u8 mcs_rate_2r = (zt_u8)((mask >> 8) & 0xff);
    zt_u8 mcs_rate_3r = (zt_u8)((mask >> 16) & 0xff);
    zt_u8 mcs_rate_4r = (zt_u8)((mask >> 24) & 0xff);

    mcs_set[0] &= mcs_rate_1r;
    mcs_set[1] &= mcs_rate_2r;
    mcs_set[2] &= mcs_rate_3r;
    mcs_set[3] &= mcs_rate_4r;
}


zt_s32 zt_ie_cap_info_update(nic_info_st *nic_info, wdn_net_info_st *wdn_info,
                             zt_u16 cap_info)
{
    if (cap_info & ZT_80211_MGMT_CAPAB_ESS)
    {
        wdn_info->ess_net = zt_true;
    }
    else
    {
        wdn_info->ess_net = zt_false;
    }

    if (cap_info & ZT_80211_MGMT_CAPAB_IBSS)
    {
        wdn_info->ibss_net = zt_true;
    }
    else
    {
        wdn_info->ibss_net = zt_false;
    }

    if (cap_info & ZT_80211_MGMT_CAPAB_PRIVACY)
    {
        wdn_info->privacy = zt_true;
    }
    else
    {
        wdn_info->privacy = zt_false;
    }

    if (cap_info & ZT_80211_MGMT_CAPAB_SHORT_PREAMBLE)
    {
        wdn_info->short_preamble = zt_true;
    }
    else
    {
        wdn_info->short_preamble = zt_false;
    }

    if (cap_info & ZT_80211_MGMT_CAPAB_SHORT_SLOT_TIME)
    {
        wdn_info->short_slot = zt_true;
    }
    else
    {
        wdn_info->short_slot = zt_false;
    }

    if (cap_info & ZT_80211_MGMT_CAPAB_RADIO_MEASURE)
    {
        wdn_info->radio_measure = zt_true;
    }
    else
    {
        wdn_info->radio_measure = zt_false;
    }

    /* update cap_info ,  can't support radio_measure*/
    wdn_info->cap_info = 0;

    if (wdn_info->ess_net)
    {
        wdn_info->cap_info |= ZT_80211_MGMT_CAPAB_ESS;
    }

    if (wdn_info->ibss_net)
    {
        wdn_info->cap_info |= ZT_80211_MGMT_CAPAB_IBSS;
    }

    if (wdn_info->privacy)
    {
        wdn_info->cap_info |= ZT_80211_MGMT_CAPAB_PRIVACY;
        wdn_info->auth_algo = dot11AuthAlgrthm_Shared;
    }
    else
    {
        wdn_info->auth_algo = dot11AuthAlgrthm_Open;
    }

    if (wdn_info->short_preamble)
    {
        wdn_info->cap_info |= ZT_80211_MGMT_CAPAB_SHORT_PREAMBLE;
    }

    if (wdn_info->short_slot)
    {
        wdn_info->cap_info |= ZT_80211_MGMT_CAPAB_SHORT_SLOT_TIME;
    }

    return ZT_RETURN_OK;
}



zt_s32 zt_ie_ssid_update(nic_info_st *nic_info, wdn_net_info_st *wdn_info,
                         zt_u8 *pie_data, zt_u8 len)
{
    if (len > ZT_80211_MAX_SSID_LEN || len == 0)
    {
        LOG_D("invalid SSID length");
        return ZT_RETURN_FAIL;
    }
    else
    {
        zt_memset(wdn_info->ssid, '\0', len + 1);
        zt_memcpy(wdn_info->ssid, pie_data, len);
        wdn_info->ssid_len = len;
    }

    return ZT_RETURN_OK;
}


zt_s32 zt_ie_supported_rates_update(nic_info_st *nic_info,
                                    wdn_net_info_st *wdn_info, zt_u8 *pie_data, zt_u8 len)
{
    zt_u8 i, j;
    hw_info_st *hw_info = nic_info->hw_info;
    if (len == 0 || len > 8)
    {
        return ZT_RETURN_FAIL;
    }

    /* set supported rates */
    wdn_info->datarate_len = 0;
    for (i = 0; i < len; i++)
    {
        if (pie_data[i] == 0)
        {
            continue;
        }

        for (j = 0; j < ZT_RATES_NUM; j++)
        {
            if ((pie_data[i] & (~IEEE80211_BASIC_RATE_MASK)) == (hw_info->datarate[j] &
                    (~IEEE80211_BASIC_RATE_MASK)))
            {
                wdn_info->datarate[wdn_info->datarate_len++] = pie_data[i];

                break;
            }
        }
    }

    /* get network type */
    if ((only_cckrates(wdn_info->datarate, wdn_info->datarate_len)) == 1)
    {
        wdn_info->network_type |= WIRELESS_11B;
    }
    else if ((have_cckrates(wdn_info->datarate, wdn_info->datarate_len)) == 1)
    {
        wdn_info->network_type |= WIRELESS_11BG;
    }
    else
    {
        wdn_info->network_type |= WIRELESS_11G;
    }

    return ZT_RETURN_OK;
}


zt_s32 zt_ie_extend_supported_rates_update(nic_info_st *nic_info,
        wdn_net_info_st *wdn_info, zt_u8 *pie_data, zt_u8 len)
{
    zt_u8 i, j;
    hw_info_st *hw_info = nic_info->hw_info;

    if (len == 0)
    {
        return ZT_RETURN_FAIL;
    }

    /* set supported rates */
    wdn_info->ext_datarate_len = 0;
    for (i = 0; i < len; i++)
    {
        if (pie_data[i] == 0)
        {
            continue;
        }

        for (j = 0; j < ZT_RATES_NUM; j++)
        {
            if ((pie_data[i] & (~IEEE80211_BASIC_RATE_MASK)) == (hw_info->datarate[j] &
                    (~IEEE80211_BASIC_RATE_MASK)))
            {
                wdn_info->ext_datarate[wdn_info->ext_datarate_len++] = hw_info->datarate[j];
                break;
            }
        }
    }

    /* get network type */
    if ((only_cckrates(wdn_info->datarate, wdn_info->datarate_len)) == 1)
    {
        wdn_info->network_type |= WIRELESS_11B;
    }
    else if ((have_cckrates(wdn_info->datarate, wdn_info->datarate_len)) == 1)
    {
        wdn_info->network_type |= WIRELESS_11BG;
    }
    else
    {
        wdn_info->network_type |= WIRELESS_11G;
    }

    return ZT_RETURN_OK;
}


zt_s32 zt_ie_wmm_update(nic_info_st *nic_info, wdn_net_info_st *wdn_info,
                        zt_u8 *pie_data, zt_u8 len)
{
    if (len == 0)
    {
        return ZT_RETURN_FAIL;
    }

    wdn_info->wmm_enable = zt_true;
    zt_memcpy(&wdn_info->wmm_info, pie_data, len);

    return ZT_RETURN_OK;
}

zt_s32 zt_ie_wpa_update(nic_info_st *nic_info, wdn_net_info_st *wdn_info,
                        zt_u8 *pie_data, zt_u8 len)
{
    if (len == 0)
    {
        return ZT_RETURN_FAIL;
    }

    wdn_info->wpa_enable = zt_true;
    wdn_info->auth_algo = dot11AuthAlgrthm_Open;

    return ZT_RETURN_OK;
}


zt_s32 zt_ie_rsn_update(nic_info_st *nic_info, wdn_net_info_st *wdn_info,
                        zt_u8 *pie_data, zt_u8 len)
{
    if (len == 0)
    {
        return ZT_RETURN_FAIL;
    }

    wdn_info->rsn_enable = zt_true;
    wdn_info->auth_algo = dot11AuthAlgrthm_Open;

    return ZT_RETURN_OK;
}


zt_s32 zt_ie_ht_capability_update(nic_info_st *nic_info,
                                  wdn_net_info_st *wdn_info,
                                  zt_u8 *pie_data, zt_u8 len)
{
    zt_u32 max_ampdu_sz = 0;
    zt_80211_mgmt_ht_cap_t ht_cap_temp;
    hw_info_st *hw_info = nic_info->hw_info;
    local_info_st *local_info = nic_info->local_info;
    zt_wlan_mgmt_info_t *pwlan_info = nic_info->wlan_mgmt_info;
    zt_wlan_network_t *pcur_network = &pwlan_info->cur_network;

    if (len == 0)
    {
        return ZT_RETURN_FAIL;
    }

    zt_memcpy(&ht_cap_temp, pie_data, len);

    wdn_info->ht_enable = zt_true;
    wdn_info->network_type |= WIRELESS_11_24N;

    wdn_info->ht_cap.cap_info = 0;

    /* parse capability Info */
    if (ht_cap_temp.cap_info & ZT_80211_MGMT_HT_CAP_LDPC_CODING)
    {
        wdn_info->htpriv.mcu_ht.ldpc_cap = zt_true;
    }
    else
    {
        wdn_info->htpriv.mcu_ht.ldpc_cap = zt_false;
    }

    if (ht_cap_temp.cap_info & ZT_80211_MGMT_HT_CAP_SUP_WIDTH_20_40)
    {
        wdn_info->htpriv.both_20m_40m = hw_info->cbw40_support;
        /*sta support both 20M and 40Mhz so set 40M band width*/
        zt_ie_ht_operation_info_update(nic_info, wdn_info, (zt_u8 *)&pcur_network->pht_oper, sizeof(zt_80211_mgmt_ht_operation_t));
    }
    else
    {
        wdn_info->htpriv.both_20m_40m = zt_false;
    }

    wdn_info->htpriv.sm_ps_mode = (ht_cap_temp.cap_info &
                                   ZT_80211_MGMT_HT_CAP_SM_PS) >> 2;
    if (wdn_info->htpriv.sm_ps_mode == 3)
    {
        wdn_info->htpriv.mcu_ht.smps_cap = zt_false;
    }
    else
    {
        wdn_info->htpriv.mcu_ht.smps_cap = zt_true;
    }

    if (ht_cap_temp.cap_info & ZT_80211_MGMT_HT_CAP_SGI_20)
    {
        wdn_info->htpriv.mcu_ht.sgi_20m = zt_true;
    }
    else
    {
        wdn_info->htpriv.mcu_ht.sgi_20m = zt_false;
    }

    if (ht_cap_temp.cap_info & ZT_80211_MGMT_HT_CAP_SGI_40)
    {
        wdn_info->htpriv.mcu_ht.sgi_40m = hw_info->cbw40_support;
    }
    else
    {
        wdn_info->htpriv.mcu_ht.sgi_40m = zt_false;
    }

    if (ht_cap_temp.cap_info & ZT_80211_MGMT_HT_CAP_DSSSCCK40)
    {
        wdn_info->htpriv.dssck_40m = hw_info->cbw40_support;
    }
    else
    {
        wdn_info->htpriv.dssck_40m = zt_false;
    }

    if (ht_cap_temp.cap_info & ZT_80211_MGMT_HT_CAP_RX_STBC)
    {
        wdn_info->htpriv.rx_stbc = zt_true;
    }
    else
    {
        wdn_info->htpriv.rx_stbc = zt_false;
    }

    if (ht_cap_temp.cap_info & ZT_80211_MGMT_HT_CAP_TX_STBC)
    {
        wdn_info->htpriv.mcu_ht.stbc_cap = zt_true;
    }
    else
    {
        wdn_info->htpriv.mcu_ht.stbc_cap = zt_false;
    }

    /* parse AMPDU Parameters */
    wdn_info->htpriv.mcu_ht.ampdu_enable = zt_true;
    max_ampdu_sz =
        ZT_MIN(ht_cap_temp.ampdu_params_info & ZT_80211_MGMT_HT_AMPDU_PARM_FACTOR,
               local_info->max_ampdu_len_ulimit);
    wdn_info->htpriv.mcu_ht.rx_ampdu_maxlen = max_ampdu_sz;
    wdn_info->htpriv.mcu_ht.rx_ampdu_min_spacing =
        (ht_cap_temp.ampdu_params_info &
         ZT_80211_MGMT_HT_AMPDU_PARM_DENSITY) >> 2;

    wdn_info->qos_option = zt_true;

    /* update ht cap_info*/
    if (wdn_info->htpriv.mcu_ht.sgi_20m == zt_true)
    {
        wdn_info->ht_cap.cap_info |= ZT_80211_MGMT_HT_CAP_SGI_20;
    }

    if (wdn_info->htpriv.mcu_ht.sgi_40m == zt_true)
    {
        wdn_info->ht_cap.cap_info |= ZT_80211_MGMT_HT_CAP_SGI_40;
    }

    if (wdn_info->htpriv.dssck_40m == zt_true)
    {
        wdn_info->ht_cap.cap_info |= ZT_80211_MGMT_HT_CAP_DSSSCCK40;
    }

    if (wdn_info->htpriv.mcu_ht.ampdu_enable == zt_true)
    {
        wdn_info->ht_cap.cap_info |= ZT_80211_MGMT_HT_CAP_MAX_AMSDU;
    }

    if ((wdn_info->htpriv.mcu_ht.ldpc_cap == zt_true) && hw_info->ldpc_support)
    {
        wdn_info->ht_cap.cap_info |= ZT_80211_MGMT_HT_CAP_LDPC_CODING;
        wdn_info->ldpc = 1;
    }
    else
    {
        wdn_info->ldpc = 0;
    }

    if (hw_info->tx_stbc_support)
    {
        wdn_info->ht_cap.cap_info |= ZT_80211_MGMT_HT_CAP_TX_STBC;
    }

    if (hw_info->rx_stbc_support)
    {
        if (hw_info->rx_stbc_num == 1)
        {
            wdn_info->ht_cap.cap_info |= ZT_80211_MGMT_HT_CAP_RX_STBC & 0x100;
            wdn_info->stbc = hw_info->rx_stbc_num;
        }
    }

    if (hw_info->max_recvbuf_sz - hw_info->rx_packet_offset >= (8191 - 256))
    {
        wdn_info->ht_cap.cap_info |= ZT_80211_MGMT_HT_CAP_MAX_AMSDU;
    }

    if (wdn_info->htpriv.mcu_ht.smps_cap == zt_false)
    {
        wdn_info->ht_cap.cap_info |= ZT_80211_MGMT_HT_CAP_SM_PS;
    }
    else
    {
        if (hw_info->sm_ps_support == zt_true)
        {
            wdn_info->ht_cap.cap_info |= wdn_info->htpriv.sm_ps_mode << 2;
        }
        else
        {
            wdn_info->ht_cap.cap_info |= ZT_80211_MGMT_HT_CAP_SM_PS;
        }
    }

    if (wdn_info->htpriv.both_20m_40m == zt_true)
    {
        if (hw_info->cbw40_support == zt_true &&
                wdn_info->bw_mode == CHANNEL_WIDTH_40)
        {
            wdn_info->ht_cap.cap_info |= ZT_80211_MGMT_HT_CAP_SUP_WIDTH_20_40;
        }
    }

    /* update ht ampdu info*/
    wdn_info->ht_cap.ampdu_params_info = hw_info->max_rx_ampdu_factor & 0x03;
    wdn_info->ht_cap.ampdu_params_info |= (ZT_80211_MGMT_HT_AMPDU_PARM_DENSITY &
                                           (hw_info->best_ampdu_density << 2));

//    zt_memcpy(wdn_info->ht_cap.supp_mcs_set, hw_info->default_supported_mcs_set,
//              ZT_MCS_NUM);
//    do_set_mcs_by_flag(wdn_info->ht_cap.supp_mcs_set, MCS_RATE_1R);
    zt_memcpy(wdn_info->ht_cap.supp_mcs_set, ht_cap_temp.supp_mcs_set, ZT_MCS_NUM);
    do_set_mcs_by_flag(wdn_info->ht_cap.supp_mcs_set, MCS_RATE_1R);

    return ZT_RETURN_OK;
}



zt_s32 zt_ie_ht_operation_info_update(nic_info_st *nic_info,
                                      wdn_net_info_st *wdn_info, zt_u8 *pie_data, zt_u8 len)
{
    struct wl_ieee80211_ht_oper_info ht_oper_temp;
    hw_info_st *hw_info = nic_info->hw_info;

    if (len == 0)
    {
        return ZT_RETURN_FAIL;
    }

    wdn_info->htpriv.mcu_ht.ht_option = zt_true;

    zt_memcpy(&ht_oper_temp, pie_data, len);

    if (hw_info->cbw40_support == zt_true)
    {
        if (ht_oper_temp.ht_param & ZT_BIT(2)) /* Set to 0 for a 20 MHz channel width */
        {
            wdn_info->bw_mode = CHANNEL_WIDTH_40;

            if ((ht_oper_temp.ht_param & 0x3) == 3)
            {
                wdn_info->channle_offset = HAL_PRIME_CHNL_OFFSET_UPPER;
            }
            else if ((ht_oper_temp.ht_param & 0x3) == 1)
            {
                wdn_info->channle_offset = HAL_PRIME_CHNL_OFFSET_LOWER;
            }
            else
            {
                wdn_info->channle_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
            }
        }
        else
        {
            wdn_info->bw_mode = CHANNEL_WIDTH_20;
            wdn_info->channle_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
        }
    }
    else
    {
        wdn_info->bw_mode = CHANNEL_WIDTH_20;
        wdn_info->channle_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
    }

    wdn_info->HT_protection = ht_oper_temp.operation_mode & 0x3;

    return ZT_RETURN_OK;
}

zt_s32 zt_ie_erp_update(nic_info_st *nic_info, wdn_net_info_st *wdn_info,
                        zt_u8 *pie_data, zt_u8 len)
{
    if (len == 0)
    {
        return ZT_RETURN_FAIL;
    }

    wdn_info->erp_enable = zt_true;
    wdn_info->erp_flag = pie_data[0];

    if (wdn_info->erp_flag & ZT_BIT(1))
    {
        wdn_info->cts2self = 0;
        wdn_info->rtsen = 1;
    }
    else
    {
        wdn_info->cts2self = 0;
        wdn_info->rtsen = 0;
    }

    return ZT_RETURN_OK;
}





