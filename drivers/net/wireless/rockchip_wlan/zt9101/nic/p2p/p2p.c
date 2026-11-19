/*
 * p2p.c
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
#include "zt_debug.h"

#define P2P_ARRAY(data, len)   zt_log_array(data, len)
#define P2P_DBG(fmt, ...)      LOG_D("P2P[%s:%d][%d]"fmt, __func__,__LINE__, pnic_info->ndev_id, ##__VA_ARGS__)
#define P2P_INFO(fmt, ...)     LOG_I("P2P[%s:%d][%d]"fmt, __func__,__LINE__, pnic_info->ndev_id, ##__VA_ARGS__)
#define P2P_WARN(fmt, ...)     LOG_W("P2P[%s:%d][%d]"fmt, __func__,__LINE__, pnic_info->ndev_id, ##__VA_ARGS__)
#define P2P_ERR(fmt, ...)      LOG_E("P2P[%s:%d][%d]"fmt, __func__,__LINE__, pnic_info->ndev_id, ##__VA_ARGS__)
#define P2P_SHOW_ATTR 0

#define P2P_SHOW_ATTR 0
static zt_u8 wl_basic_rate_cck[4] =
{
    ZT_80211_CCK_RATE_1MB | IEEE80211_BASIC_RATE_MASK,
    ZT_80211_CCK_RATE_2MB | IEEE80211_BASIC_RATE_MASK,
    ZT_80211_CCK_RATE_5MB | IEEE80211_BASIC_RATE_MASK,
    ZT_80211_CCK_RATE_11MB | IEEE80211_BASIC_RATE_MASK
};

static zt_u8 wl_basic_rate_ofdm[3] =
{
    ZT_80211_OFDM_RATE_6MB | IEEE80211_BASIC_RATE_MASK,
    ZT_80211_OFDM_RATE_12MB | IEEE80211_BASIC_RATE_MASK,
    ZT_80211_OFDM_RATE_24MB | IEEE80211_BASIC_RATE_MASK
};

static zt_u8 wl_basic_rate_mix[7] =
{
    ZT_80211_CCK_RATE_1MB | IEEE80211_BASIC_RATE_MASK,
    ZT_80211_CCK_RATE_2MB | IEEE80211_BASIC_RATE_MASK,
    ZT_80211_CCK_RATE_5MB | IEEE80211_BASIC_RATE_MASK,
    ZT_80211_CCK_RATE_11MB | IEEE80211_BASIC_RATE_MASK,
    ZT_80211_OFDM_RATE_6MB | IEEE80211_BASIC_RATE_MASK,
    ZT_80211_OFDM_RATE_12MB | IEEE80211_BASIC_RATE_MASK,
    ZT_80211_OFDM_RATE_24MB | IEEE80211_BASIC_RATE_MASK
};

zt_bool zt_p2p_check_buddy_linkstate(nic_info_st *pnic_info)
{
    zt_bool bconnect = zt_false;

    if (pnic_info->buddy_nic == NULL)
        return bconnect;
    zt_mlme_get_connect(pnic_info->buddy_nic, &bconnect);
    return bconnect;
}
zt_u8 zt_p2p_get_buddy_channel(nic_info_st *pnic_info)
{
    nic_info_st *pbuddy_nic = NULL;
    mlme_info_t *pmlme_info = NULL;
    wdn_net_info_st *pwdn_info = NULL;
    if (NULL == pnic_info)
    {
        P2P_DBG("input param is null");
        return 0;
    }
    pbuddy_nic = pnic_info->buddy_nic;
    if (NULL == pbuddy_nic)
    {
        P2P_DBG("pbuddy_nic is null");
        return 0;
    }
    pmlme_info = pbuddy_nic->mlme_info;
    if (NULL == pmlme_info)
    {
        P2P_DBG("pmlme_info is null");
        return 0;
    }

    pwdn_info = pmlme_info->pwdn_info;
    if (NULL == pwdn_info)
    {
        P2P_DBG("pwdn_info is null");
        return 0;
    }

    return pwdn_info->channel;
}

zt_s8 *p2p_attr_to_str(zt_u8 attr_id)
{
    switch (attr_id)
    {
        case P2P_ATTR_STATUS                 :
        {
            return to_str(P2P_ATTR_STATUS);
        }
        case P2P_ATTR_MINOR_REASON_CODE      :
        {
            return to_str(P2P_ATTR_MINOR_REASON_CODE);
        }
        case P2P_ATTR_CAPABILITY             :
        {
            return to_str(P2P_ATTR_CAPABILITY);
        }
        case P2P_ATTR_DEVICE_ID              :
        {
            return to_str(P2P_ATTR_DEVICE_ID);
        }
        case P2P_ATTR_GO_INTENT              :
        {
            return to_str(P2P_ATTR_GO_INTENT);
        }
        case P2P_ATTR_CONF_TIMEOUT           :
        {
            return to_str(P2P_ATTR_CONF_TIMEOUT);
        }
        case P2P_ATTR_LISTEN_CH              :
        {
            return to_str(P2P_ATTR_LISTEN_CH);
        }
        case P2P_ATTR_GROUP_BSSID            :
        {
            return to_str(P2P_ATTR_GROUP_BSSID);
        }
        case P2P_ATTR_EX_LISTEN_TIMING       :
        {
            return to_str(P2P_ATTR_EX_LISTEN_TIMING);
        }
        case P2P_ATTR_INTENTED_IF_ADDR       :
        {
            return to_str(P2P_ATTR_INTENTED_IF_ADDR);
        }
        case P2P_ATTR_MANAGEABILITY          :
        {
            return to_str(P2P_ATTR_MANAGEABILITY);
        }
        case P2P_ATTR_CH_LIST                :
        {
            return to_str(P2P_ATTR_CH_LIST);
        }
        case P2P_ATTR_NOA                    :
        {
            return to_str(P2P_ATTR_NOA);
        }
        case P2P_ATTR_DEVICE_INFO            :
        {
            return to_str(P2P_ATTR_DEVICE_INFO);
        }
        case P2P_ATTR_GROUP_INFO             :
        {
            return to_str(P2P_ATTR_GROUP_INFO);
        }
        case P2P_ATTR_GROUP_ID               :
        {
            return to_str(P2P_ATTR_GROUP_ID);
        }
        case P2P_ATTR_INTERFACE              :
        {
            return to_str(P2P_ATTR_INTERFACE);
        }
        case P2P_ATTR_OPERATING_CH           :
        {
            return to_str(P2P_ATTR_OPERATING_CH);
        }
        case P2P_ATTR_INVITATION_FLAGS       :
        {
            return to_str(P2P_ATTR_INVITATION_FLAGS);
        }
        default:
            return "unknown p2p attr";
    }
    return NULL;
}
static void p2p_show_attr(zt_u8 *attr)
{
    zt_u8 attr_id = 0;
    zt_u16 attr_data_len = 0;

    attr_id = *attr;
    attr_data_len = *(zt_u16 *)(attr + 1);
#if P2P_SHOW_ATTR
    if (1 == attr_data_len)
    {
        P2P_INFO("-p2p-[%s] %u(%u): 0x%x", p2p_attr_to_str(attr_id), attr_id,
                 attr_data_len, attr[3]);
    }
    else if (2 == attr_data_len)
    {
        P2P_INFO("-p2p-[%s] %u(%u): 0x%x", p2p_attr_to_str(attr_id), attr_id,
                 attr_data_len, *(zt_u16 *)&attr[3]);
    }
    else if (4 == attr_data_len)
    {
        P2P_INFO("-p2p-[%s] %u(%u): 0x%x", p2p_attr_to_str(attr_id), attr_id,
                 attr_data_len, *(zt_u32 *)&attr[3]);
    }
    else
    {
        P2P_INFO("-p2p-[%s] %u(%u):", p2p_attr_to_str(attr_id), attr_id, attr_data_len);
        P2P_ARRAY(&attr[3], attr_data_len);
    }
#else
    if (ZT_DEBUG_LEVEL <= ZT_LOG_LEVEL_DEBUG)
    {
        P2P_ARRAY(&attr[3], attr_data_len);
    }
#endif
}


zt_s32 zt_p2p_dump_attrs(zt_u8 *p2p_ie, zt_u32 p2p_ielen)
{
    zt_u8 *attr_ptr         = NULL;

    if (!p2p_ie || p2p_ielen <= 6 ||
            (p2p_ie[0] != ZT_80211_MGMT_EID_VENDOR_SPECIFIC) ||
            (zt_memcmp(p2p_ie + 2, P2P_OUI, 4) != 0))
    {
        return -1;
    }

    attr_ptr = p2p_ie + 6;

    while ((attr_ptr - p2p_ie + 3) <= p2p_ielen)
    {
        zt_u16 attr_data_len = *(zt_u16 *)(attr_ptr + 1);
        zt_u16 attr_len = attr_data_len + 3;

        p2p_show_attr(attr_ptr);
        if ((attr_ptr - p2p_ie + attr_len) > p2p_ielen)
        {
            break;
        }

        attr_ptr += attr_len;

    }

    return 0;
}

zt_s8 *zt_p2p_role_to_str(P2P_ROLE role)
{
    switch (role)
    {
        case P2P_ROLE_DISABLE:
        {
            return to_str(P2P_ROLE_DISABLE);
        }
        case P2P_ROLE_DEVICE:
        {
            return to_str(P2P_ROLE_DEVICE);
        }
        case P2P_ROLE_CLIENT:
        {
            return to_str(P2P_ROLE_CLIENT);
        }
        case P2P_ROLE_GO :
        {
            return to_str(P2P_ROLE_GO);
        }
        default:
            return "Unknown_p2p_role";
    }
    return NULL;
}
zt_s8 *zt_p2p_state_to_str(P2P_STATE state)
{
    switch (state)
    {
        case P2P_STATE_NONE                        :
        {
            return to_str(P2P_STATE_NONE);
        }
        case P2P_STATE_IDLE                         :
        {
            return to_str(P2P_STATE_IDLE);
        }
        case P2P_STATE_LISTEN                             :
        {
            return to_str(P2P_STATE_LISTEN);
        }
        case P2P_STATE_SCAN                        :
        {
            return to_str(P2P_STATE_SCAN);
        }
        case P2P_STATE_FIND_PHASE_LISTEN             :
        {
            return to_str(P2P_STATE_FIND_PHASE_LISTEN);
        }
        case P2P_STATE_FIND_PHASE_SEARCH            :
        {
            return to_str(P2P_STATE_FIND_PHASE_SEARCH);
        }
        case P2P_STATE_TX_PROVISION_DIS_REQ          :
        {
            return to_str(P2P_STATE_TX_PROVISION_DIS_REQ);
        }
        case P2P_STATE_RX_PROVISION_DIS_RSP         :
        {
            return to_str(P2P_STATE_RX_PROVISION_DIS_RSP);
        }
        case P2P_STATE_RX_PROVISION_DIS_REQ         :
        {
            return to_str(P2P_STATE_RX_PROVISION_DIS_REQ);
        }
        case P2P_STATE_GONEGO_ING                   :
        {
            return to_str(P2P_STATE_GONEGO_ING);
        }
        case P2P_STATE_GONEGO_OK                  :
        {
            return to_str(P2P_STATE_GONEGO_OK);
        }
        case P2P_STATE_GONEGO_FAIL                  :
        {
            return to_str(P2P_STATE_GONEGO_FAIL);
        }
        case P2P_STATE_RECV_INVITE_REQ_MATCH      :
        {
            return to_str(P2P_STATE_RECV_INVITE_REQ_MATCH);
        }
        case P2P_STATE_PROVISIONING_ING          :
        {
            return to_str(P2P_STATE_PROVISIONING_ING);
        }
        case P2P_STATE_PROVISIONING_DONE           :
        {
            return to_str(P2P_STATE_PROVISIONING_DONE);
        }
        case P2P_STATE_TX_INVITE_REQ               :
        {
            return to_str(P2P_STATE_TX_INVITE_REQ);
        }
        case P2P_STATE_RX_INVITE_RESP_OK            :
        {
            return to_str(P2P_STATE_RX_INVITE_RESP_OK);
        }
        case P2P_STATE_RECV_INVITE_REQ_DISMATCH     :
        {
            return to_str(P2P_STATE_RECV_INVITE_REQ_DISMATCH);
        }
        case P2P_STATE_RECV_INVITE_REQ_GO          :
        {
            return to_str(P2P_STATE_RECV_INVITE_REQ_GO);
        }
        case P2P_STATE_RECV_INVITE_REQ_JOIN       :
        {
            return to_str(P2P_STATE_RECV_INVITE_REQ_JOIN);
        }
        case P2P_STATE_RX_INVITE_RESP_FAIL         :
        {
            return to_str(P2P_STATE_RX_INVITE_RESP_FAIL);
        }
        case P2P_STATE_RX_INFOR_NOREADY            :
        {
            return to_str(P2P_STATE_RX_INFOR_NOREADY);
        }
        case P2P_STATE_TX_INFOR_NOREADY            :
        {
            return to_str(P2P_STATE_TX_INFOR_NOREADY);
        }
        default:
            return "Unknown_p2p_state";
    }
    return NULL;
}

static void p2p_do_renew_tx_rate(nic_info_st *pnic_info, zt_u8 wirelessmode)
{
    p2p_info_st *p2p_info           = pnic_info->p2p;
    zt_u8 *supported_rates = NULL;
    zt_u8 support_rate_cnt = 0;
    zt_u16 basic_dr_cfg;
    zt_s32 ret = 0;
    if (P2P_STATE_NONE != p2p_info->p2p_state)
    {
        return;
    }

    if ((wirelessmode & WIRELESS_11B) && (wirelessmode == WIRELESS_11B))
    {
        supported_rates =  wl_basic_rate_cck;
        support_rate_cnt = sizeof(wl_basic_rate_cck) / sizeof(wl_basic_rate_cck[0]);
    }
    else if (wirelessmode & WIRELESS_11B)
    {
        supported_rates =  wl_basic_rate_mix;
        support_rate_cnt = sizeof(wl_basic_rate_mix) / sizeof(wl_basic_rate_mix[0]);
    }
    else
    {
        supported_rates =  wl_basic_rate_ofdm;
        support_rate_cnt = sizeof(wl_basic_rate_ofdm) / sizeof(wl_basic_rate_ofdm[0]);
    }

    if (wirelessmode & WIRELESS_11B)
    {
        p2p_info->mgnt_tx_rate = ZT_80211_CCK_RATE_1MB;
    }
    else
    {
        p2p_info->mgnt_tx_rate = ZT_80211_OFDM_RATE_6MB;
    }

    get_bratecfg_by_support_dates(supported_rates, support_rate_cnt, &basic_dr_cfg);
    ret = zt_mcu_set_basic_rate(pnic_info,  basic_dr_cfg);
    if (ZT_RETURN_OK != ret)
    {
        P2P_WARN("zt_mcu_set_basic_rate failed");
    }
}


static void p2p_info_init(p2p_info_st *p2p_info)
{
    /*  Use the OFDM rate in the P2P probe response frame. ( 6(B), 9(B), 12, 18, 24, 36, 48, 54 )    */
    p2p_info->p2p_support_rate[0] = 0x8c;    /*  6(B) */
    p2p_info->p2p_support_rate[1] = 0x92;    /*  9(B) */
    p2p_info->p2p_support_rate[2] = 0x18;    /*  12 */
    p2p_info->p2p_support_rate[3] = 0x24;    /*  18 */
    p2p_info->p2p_support_rate[4] = 0x30;    /*  24 */
    p2p_info->p2p_support_rate[5] = 0x48;    /*  36 */
    p2p_info->p2p_support_rate[6] = 0x60;    /*  48 */
    p2p_info->p2p_support_rate[7] = 0x6c;    /*  54 */

    p2p_info->p2p_state = P2P_STATE_NONE;
    zt_memcpy((void *)p2p_info->p2p_wildcard_ssid, "DIRECT-",
              P2P_WILDCARD_SSID_LEN);

    zt_memset(&p2p_info->wfd_info, 0, sizeof(wfd_info_st));
    p2p_info->supported_wps_cm = WPS_CONFIG_METHOD_DISPLAY | WPS_CONFIG_METHOD_PBC |
                                 WPS_CONFIG_METHOD_KEYPAD;
    p2p_info->ext_listen_interval    = 1000;
    p2p_info->scan_times = 0;

    zt_widev_invit_info_init(&p2p_info->invit_info);
    zt_widev_nego_info_init(&p2p_info->nego_info);


}

void p2p_nego_timer_handle(zt_os_api_timer_t *timer)
{
    p2p_info_st   *p2p_info = ZT_CONTAINER_OF((zt_os_api_timer_t *)timer,
                              p2p_info_st, nego_timer);
    nic_info_st *pnic_info = NULL;
    if (NULL == p2p_info)
    {
        return;
    }

    pnic_info = p2p_info->nic_info;
    p2p_info->go_negoing = 0;
    p2p_info->scan_deny = 0;
    p2p_info->nego_timer_flag = 0;
    P2P_INFO("doing nego:0x%x", p2p_info->go_negoing);
}

void p2p_ro_ch_timer_handle(zt_os_api_timer_t *timer)
{
    p2p_info_st   *p2p_info = ZT_CONTAINER_OF((zt_os_api_timer_t *)timer,
                              p2p_info_st, remain_ch_timer);
    nic_info_st *pnic_info = NULL;
    if (NULL == p2p_info)
    {
        return;
    }

    pnic_info = p2p_info->nic_info;
    zt_p2p_cannel_remain_on_channel(pnic_info, 0);
    P2P_INFO("doing nego:0x%x", p2p_info->go_negoing);
}

zt_s32 zt_p2p_init(nic_info_st *pnic_info)
{
    p2p_info_st *p2p_info = NULL;
    zt_u8 i = 0;

    P2P_INFO("start");
    if (NULL != pnic_info->p2p)
    {
        P2P_WARN("pnic_info->p2p is not null");
        return 0;
    }

    p2p_info = zt_kzalloc(sizeof(p2p_info_st));
    if (p2p_info == NULL)
    {
        P2P_ERR("zt_kzalloc p2p_info_st failed");
        return ZT_RETURN_FAIL;
    }

    pnic_info->p2p = p2p_info;
    p2p_info->nic_info = pnic_info;
    zt_memcpy(p2p_info->device_addr, nic_to_local_addr(pnic_info),
              ZT_80211_MAC_ADDR_LEN);
    zt_memcpy(p2p_info->interface_addr, nic_to_local_addr(pnic_info),
              ZT_80211_MAC_ADDR_LEN);

    p2p_info_init(p2p_info);
    zt_os_api_timer_reg(&p2p_info->nego_timer, p2p_nego_timer_handle,
                        &p2p_info->nego_timer);
    zt_os_api_timer_reg(&p2p_info->remain_ch_timer, p2p_ro_ch_timer_handle,
                        &p2p_info->remain_ch_timer);
    p2p_info->p2p_enabled = zt_false;
    p2p_info->full_ch_in_p2p_handshake = zt_false;
    for (i = 0; i < ZT_P2P_IE_MAX; i++)
    {
        p2p_info->p2p_ie[i] = zt_kzalloc(P2P_IE_BUF_LEN);
        if (NULL == p2p_info->p2p_ie[i])
        {
            P2P_ERR("zt_kzalloc p2p_ie failed");
            return ZT_RETURN_FAIL;
        }
    }
    if (zt_p2p_wfd_init(pnic_info, 1) != zt_true)
    {
        P2P_WARN("\n Can't init wfd\n");
    }

    return 0;

}



zt_s32 zt_p2p_term(nic_info_st *pnic_info)
{
    p2p_info_st *p2p_info = NULL;
    zt_u8 i = 0;
    wfd_info_st *pwfd_info = NULL;
    P2P_DBG("start");
    p2p_info = pnic_info->p2p;

    if (NULL == p2p_info)
    {
        return -1;
    }

    for (i = 0; i < ZT_P2P_IE_MAX; i++)
    {
        zt_kfree(p2p_info->p2p_ie[i]);
        p2p_info->p2p_ie_len[i] = 0;
        p2p_info->p2p_ie[i] = NULL;
    }

    pwfd_info = &p2p_info->wfd_info;
    for (i = 0; i < ZT_WFD_IE_MAX; i++)
    {
        zt_kfree(pwfd_info->wfd_ie[i]);
        pwfd_info->wfd_ie[i] = NULL;
    }

    zt_os_api_timer_unreg(&p2p_info->nego_timer);
    zt_os_api_timer_unreg(&p2p_info->remain_ch_timer);

    zt_kfree(p2p_info);
    pnic_info->p2p = NULL;

    return 0;
}

zt_s32 zt_p2p_suspend(nic_info_st *pnic_info)
{
    p2p_info_st *p2p_info = pnic_info->p2p;

    P2P_INFO();

    if (NULL == p2p_info)
    {
        return 0;
    }

    zt_p2p_reset(pnic_info);
    zt_p2p_disable(pnic_info);
    zt_os_api_timer_unreg(&p2p_info->nego_timer);
    zt_os_api_timer_unreg(&p2p_info->remain_ch_timer);

    return 0;
}

zt_s32 zt_p2p_resume(nic_info_st *pnic_info)
{
    p2p_info_st *p2p_info = pnic_info->p2p;

    P2P_INFO();

    if (NULL == p2p_info)
    {
        return 0;
    }

    zt_os_api_timer_reg(&p2p_info->nego_timer, p2p_nego_timer_handle,
                        &p2p_info->nego_timer);
    zt_os_api_timer_reg(&p2p_info->remain_ch_timer, p2p_ro_ch_timer_handle,
                        &p2p_info->remain_ch_timer);

    return 0;
}

zt_s32 zt_p2p_reset(nic_info_st *pnic_info)
{
    zt_u8 i = 0;
    p2p_info_st *p2p_info = NULL;
    wfd_info_st *wfd_info = NULL;

    if (NULL == pnic_info)
    {
        return -1;
    }
    if (zt_false == zt_p2p_is_valid(pnic_info))
    {
        return -2;
    }

    p2p_info = pnic_info->p2p;
    p2p_info->role = P2P_ROLE_DEVICE;
    p2p_info->p2p_state = P2P_STATE_NONE;
    p2p_info->pre_p2p_state = P2P_STATE_NONE;
    p2p_info->go_negoing = 0;

    for (i = 0; i < ZT_P2P_IE_MAX; i++)
    {
        zt_memset(p2p_info->p2p_ie[i], 0, P2P_IE_BUF_LEN);
        p2p_info->p2p_ie_len[i] = 0;
    }
    wfd_info = &p2p_info->wfd_info;
    for (i = 0; i < ZT_WFD_IE_MAX; i++)
    {
        zt_memset(wfd_info->wfd_ie[i], 0, P2P_IE_BUF_LEN);
        wfd_info->wfd_ie_len[i] = 0;
    }

    p2p_info->peer_listen_channel = 0;
    p2p_info->listen_channel = 0;
    p2p_info->link_channel = 0;
    p2p_info->report_ch = 0;
    p2p_info->remain_ch = 0;
    p2p_info->ro_ch_duration = 0;

    return 0;
}

zt_inline void zt_p2p_set_role(p2p_info_st *p2p_info, enum P2P_ROLE role)
{
    if (p2p_info->role != role)
    {
        p2p_info->role = role;
    }
}
zt_inline P2P_ROLE p2p_get_role(p2p_info_st *p2p_info)
{
    return p2p_info->role;
}

zt_inline void zt_p2p_set_state(p2p_info_st *p2p_info, enum P2P_STATE state)
{
    if (p2p_info->p2p_state != state)
    {
        zt_p2p_set_pre_state(p2p_info, p2p_info->p2p_state);
        p2p_info->p2p_state = state;
    }
}


zt_inline void zt_p2p_set_pre_state(p2p_info_st *p2p_info, enum P2P_STATE state)
{
    if (p2p_info->pre_p2p_state != state)
    {
        p2p_info->pre_p2p_state = state;
    }
}

static zt_u8 *p2p_wps_attr_ie_parse(nic_info_st *pnic_info, zt_u8 *ie, zt_u32 ie_len, zt_u8 *attr_id)
{
    zt_u8 *temp_wps_ie = ie;
    zt_u16 ie_offset = 0;

    while (zt_memcmp(temp_wps_ie, attr_id, 2))
    {
        ie_offset = temp_wps_ie[3] + 4;
        temp_wps_ie += ie_offset;
        if (temp_wps_ie == NULL)
            break;
    }

    return temp_wps_ie;
}

static zt_s32 p2p_wps_ie_parse(nic_info_st *pnic_info, zt_u8 *wps_ie, zt_u32 ie_len)
{
    zt_u16 ie_offset = 0;
    zt_u8 *temp_wps_ie = wps_ie;
    zt_u8 *temp_ie = NULL;
    zt_u8 *val = NULL;
    zt_u32 temp_wps_len = ie_len;
    p2p_info_st *p2p_info = pnic_info->p2p;
    zt_u8 WPS_DEVICE_NAME[2] = { 0x10, 0x11 };
    zt_u8 WPS_UUID[2] = { 0x10, 0x47 };

    if (!wps_ie)
    {
        P2P_WARN("[%s]wps_ie is null, check", __func__);
        return -1;
    }

    if (ie_len <= 0)
    {
        P2P_WARN("[%s]ie_len is 0, check", __func__);
        return -2;
    }

    ie_offset = 2;
    temp_wps_ie += ie_offset;
    temp_wps_len -= ie_offset;

    if (zt_memcmp(&wps_ie[2], WPS_OUI, 4))
    {
        P2P_WARN("[%s]wps_ie isn't WPS OUI, check", __func__);
        return -3;
    }

    ie_offset = 4;
    temp_wps_ie += ie_offset;
    temp_wps_len -= ie_offset;

    temp_ie = zt_kzalloc(temp_wps_len);
    zt_memcpy(temp_ie, temp_wps_ie, temp_wps_len);
    val = p2p_wps_attr_ie_parse(pnic_info, temp_ie, temp_wps_len, WPS_DEVICE_NAME);
    if (val)
    {
        p2p_info->p2p_device_ssid_len = val[3];
        zt_memcpy(p2p_info->p2p_device_ssid, &val[4], p2p_info->p2p_device_ssid_len);
    }

    zt_memcpy(temp_ie, temp_wps_ie, temp_wps_len);
    val = p2p_wps_attr_ie_parse(pnic_info, temp_ie, temp_wps_len, WPS_UUID);
    if (val)
    {
        zt_memcpy(p2p_info->p2p_uuid, &val[4], val[3]);
    }
    zt_kfree(temp_ie);

    return 0;
}


static zt_s32 p2p_scan_ie_parse(nic_info_st *pnic_info, zt_s8 *buf, zt_u32 len)
{
    zt_s32 ret = 0;
    zt_u32 wps_ielen = 0;
    zt_u8 *wps_ie = NULL;
    zt_u32 p2p_ielen = 0;
    zt_u8 *p2p_ie   = NULL;

    zt_u32 wfd_ielen = 0;
    zt_u8 *wfd_ie;

    mlme_info_t *pmlme_info = (mlme_info_t *)pnic_info->mlme_info;
    p2p_info_st *p2p_info    = pnic_info->p2p;

    P2P_DBG("start! ielen = %d", len);

    if (len > 0)
    {
        if ((wps_ie = zt_wlan_get_wps_ie((zt_u8 *)buf, len, NULL, &wps_ielen)))
        {
            P2P_DBG("probereq_wps_ie_len : %d", wps_ielen);
            zt_memcpy(&pmlme_info->probereq_wps_ie[0], wps_ie, wps_ielen);
            pmlme_info->wps_ie_len = wps_ielen;
            p2p_wps_ie_parse(pnic_info, wps_ie, wps_ielen);
        }

        if ((p2p_ie = zt_p2p_get_ie((zt_u8 *)buf, len, NULL, &p2p_ielen)))
        {
            zt_u32 attr_contentlen = 0;
            zt_u8 listen_ch_attr[5];

            P2P_DBG("probereq p2p_ielen : %d", p2p_ielen);
            zt_p2p_dump_attrs(p2p_ie, p2p_ielen);
            zt_p2p_parse_p2pie(pnic_info, p2p_ie, p2p_ielen, ZT_P2P_IE_PROBE_REQ);
            if (zt_p2p_get_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_LISTEN_CH,
                                        (zt_u8 *) listen_ch_attr, (zt_u32 *) & attr_contentlen)
                    && attr_contentlen == 5)
            {
                if (p2p_info->listen_channel != listen_ch_attr[4])
                {
                    P2P_INFO(" listen channel - country:%c%c%c, class:%u, ch:%u\n",
                             listen_ch_attr[0],
                             listen_ch_attr[1], listen_ch_attr[2],
                             listen_ch_attr[3], listen_ch_attr[4]);
                    p2p_info->listen_channel = listen_ch_attr[4];
                }
            }
        }

        wfd_ie = zt_p2p_wfd_get_ie(1, (zt_u8 *)buf, len, NULL, &wfd_ielen);
        if (wfd_ie)
        {
            P2P_DBG("probe_req_wfdielen=%d", wfd_ielen);
            if (zt_p2p_wfd_update_ie(pnic_info, ZT_WFD_IE_PROBE_REQ, wfd_ie, wfd_ielen,
                                     1) != zt_true)
            {
                return -1;
            }


        }

    }
    /* this func is mainly build for p2p*/

    return ret;
}

zt_s32 zt_p2p_scan_entry(nic_info_st *pnic_info, zt_u8 social_channel,
                         zt_u8 *ies, zt_s32 ieslen)
{
    p2p_info_st *p2p_info = pnic_info->p2p;

    P2P_DBG("%s", zt_p2p_state_to_str(p2p_info->p2p_state));
    if (p2p_info->p2p_state != P2P_STATE_NONE /*&& p2p_info->p2p_state != P2P_STATE_IDLE*/)
    {
        zt_p2p_set_state(p2p_info, P2P_STATE_FIND_PHASE_SEARCH);
        //do_network_queue_unnew(pwadptdata, _TRUE, 1);
    }

    /*p2p wps ie get func*/
    if (ies && ieslen > 0)
    {
        p2p_scan_ie_parse(pnic_info, (zt_s8 *)ies, ieslen);
    }

    return 0;
}


zt_s32 zt_p2p_connect_entry(nic_info_st *pnic_info, zt_u8 *ie, zt_u32 ie_len)
{

    zt_u32 p2p_ielen = 0;
    zt_u8 *p2p_ie = NULL;
    zt_u32 wfd_ielen = 0;
    zt_u8 *wfd_ie = NULL;

    p2p_ie = zt_p2p_get_ie(ie, ie_len, NULL, &p2p_ielen);
    if (NULL == p2p_ie)
    {
        return -1;
    }

    P2P_DBG("p2p_assoc_req_ielen=%d\n", p2p_ielen);
    zt_p2p_dump_attrs(p2p_ie, p2p_ielen);
    zt_p2p_parse_p2pie(pnic_info, p2p_ie, p2p_ielen, ZT_P2P_IE_ASSOC_REQ);

    wfd_ie = zt_p2p_wfd_get_ie(1, ie, ie_len, NULL, &wfd_ielen);
    if (wfd_ie)
    {
        P2P_INFO(" wfd_assoc_req_ielen=%d\n", wfd_ielen);
        if (zt_p2p_wfd_update_ie(pnic_info, ZT_WFD_IE_ASSOC_REQ, wfd_ie, wfd_ielen,
                                 1) != zt_true)
        {
            P2P_INFO("wfd_assoc_req_ie update failed\n");
        }
    }

    return 0;
}

zt_s32 zt_p2p_enable(nic_info_st *pnic_info, P2P_ROLE role)
{

    zt_s32 ret = ZT_RETURN_OK;
    p2p_info_st *p2p_info = pnic_info->p2p;
    hw_info_st *hw_info = pnic_info->hw_info;
    nic_info_st *buddy_nic                 = pnic_info->buddy_nic;
    zt_wlan_mgmt_info_t *pother_wlan_info        = NULL;
    zt_wlan_network_t *pother_cur_network   = NULL;
    p2p_info_st *other_p2p_info = NULL;
    zt_bool other_bconnect = zt_false;
    
    /* In P2P scenarios, configure the bandwidth to 20M to 
    avoid interference and improve screen casting quality. */
    hw_info->cbw40_support = zt_false;
    
    P2P_DBG("%s", zt_p2p_role_to_str(role));
    if (role == P2P_ROLE_DEVICE || role == P2P_ROLE_CLIENT || role == P2P_ROLE_GO)
    {

        if (buddy_nic)
        {
            other_p2p_info      = buddy_nic->p2p;
            pother_wlan_info    = buddy_nic->wlan_mgmt_info;
            pother_cur_network  = &pother_wlan_info->cur_network;

            if (zt_p2p_is_valid(buddy_nic))
            {
                return ret;
            }
            zt_mlme_get_connect(buddy_nic, &other_bconnect);
            if (other_bconnect == zt_true &&
                    (1 == pother_cur_network->channel || 6 == pother_cur_network->channel ||
                     11 == pother_cur_network->channel))
            {
                p2p_info->listen_channel = pother_cur_network->channel;
            }
            else
            {
                p2p_info->listen_channel = 11;
            }
        }
        else
        {
            p2p_info->listen_channel = 11;
        }

        p2p_info->p2p_enabled = zt_true;
        p2p_info->wfd_info.wfd_enable = zt_true;
        p2p_do_renew_tx_rate(pnic_info, WIRELESS_11G_24N);

        if (role == P2P_ROLE_DEVICE)
        {
            zt_p2p_set_role(p2p_info, P2P_ROLE_DEVICE);
            if (other_bconnect == zt_true && buddy_nic && zt_p2p_is_valid(buddy_nic))
            {
                zt_p2p_set_state(p2p_info, P2P_STATE_IDLE);
            }
            else
            {
                zt_p2p_set_state(p2p_info, P2P_STATE_LISTEN);
            }

            p2p_info->intent = 1;
            zt_p2p_set_pre_state(p2p_info, P2P_STATE_LISTEN);
        }
        else if (role == P2P_ROLE_CLIENT)
        {
            zt_p2p_set_role(p2p_info, P2P_ROLE_CLIENT);
            zt_p2p_set_state(p2p_info, P2P_STATE_GONEGO_OK);
            p2p_info->intent = 1;
            zt_p2p_set_pre_state(p2p_info, P2P_STATE_GONEGO_OK);
        }
        else if (role == P2P_ROLE_GO)
        {
            zt_p2p_set_role(p2p_info, P2P_ROLE_GO);
            zt_p2p_set_state(p2p_info, P2P_STATE_GONEGO_OK);
            p2p_info->intent = 15;
            zt_p2p_set_pre_state(p2p_info, P2P_STATE_GONEGO_OK);
        }

        zt_mcu_msg_body_sync(pnic_info, HAL_MSG_P2P_STATE, zt_true);

        if (zt_p2p_wfd_is_valid(pnic_info))
        {
            p2p_info->supported_wps_cm = WPS_CONFIG_METHOD_DISPLAY | WPS_CONFIG_METHOD_PBC | WPS_CONFIG_METHOD_KEYPAD;
            zt_mcu_msg_body_sync(pnic_info, HAL_MSG_WIFI_DISPLAY_STATE, zt_true);
        }
    }

    return ret;

}

zt_s32 zt_p2p_disable(nic_info_st *pnic_info)
{
    p2p_info_st *p2p_info = pnic_info->p2p;

    p2p_info->p2p_enabled = zt_false;

    if (p2p_info->p2p_state != P2P_STATE_NONE)
    {
        zt_p2p_set_state(p2p_info, P2P_STATE_NONE);
        zt_p2p_set_pre_state(p2p_info, P2P_STATE_NONE);
        zt_p2p_set_role(p2p_info, P2P_ROLE_DISABLE);
    }

    zt_mcu_msg_body_sync(pnic_info, HAL_MSG_P2P_STATE, zt_false);

    if (zt_p2p_wfd_is_valid(pnic_info))
    {
        zt_mcu_msg_body_sync(pnic_info, HAL_MSG_WIFI_DISPLAY_STATE, zt_false);
    }
#if 0
    if (_FAIL == wl_pwr_wakeup(pwadptdata))
    {
        ret = _FAIL;
        goto exit;
    }
#endif
    p2p_do_renew_tx_rate(pnic_info, WIRELESS_11BG_24N);

    return 0;
}

zt_bool zt_p2p_is_valid(nic_info_st *pnic_info)
{
    p2p_info_st *p2p_info   = pnic_info->p2p;

    if (p2p_info)
    {
        if (P2P_STATE_NONE != p2p_info->p2p_state)
        {
            return zt_true;
        }
    }

    return zt_false;
}


/* do not use the beacon ie of this func parse */
static zt_s32 p2p_beacon_parse(nic_info_st *pnic_info, zt_s8 *buf, zt_s32 len)
{
    zt_u8 *wps_ie       = NULL;
    zt_u32 wps_ielen    = 0;
    zt_u8 *wfd_ie       = NULL;
    zt_u32 wfd_ielen    = 0;

    zt_u8 *p2p_ie       = NULL;
    zt_u32 p2p_ielen    = 0;
    mlme_info_t *pmlme_info = (mlme_info_t *)pnic_info->mlme_info;

    P2P_DBG("start set beacon wps p2pie! ielen = %d", len);

    if (len <= 0)
    {
        return -1;
    }

    if ((wps_ie = zt_wlan_get_wps_ie((zt_u8 *)buf, len, NULL, &wps_ielen)))
    {
        P2P_DBG("bcn_wps_ielen=%d\n", wps_ielen);
        zt_memcpy(&pmlme_info->wps_beacon_ie[0], wps_ie, wps_ielen);
        pmlme_info->wps_beacon_ie_len = wps_ielen;
#ifdef CONFIG_IOCTL_CFG80211
#ifdef CFG_ENABLE_AP_MODE
        zt_ap_update_beacon(pnic_info, ZT_80211_MGMT_EID_VENDOR_SPECIFIC, WPS_OUI,
                            zt_true);
#endif
#endif
    }

    if ((p2p_ie = zt_p2p_get_ie((zt_u8 *)buf, len, NULL, &p2p_ielen)))
    {
        P2P_DBG("bcn_p2p_ielen=%d\n", p2p_ielen);
        zt_p2p_dump_attrs(p2p_ie, p2p_ielen);
        zt_p2p_parse_p2pie(pnic_info, p2p_ie, p2p_ielen, ZT_P2P_IE_BEACON);
    }


    if (zt_p2p_wfd_is_valid(pnic_info))
    {
        wfd_ie = zt_p2p_wfd_get_ie(1, (zt_u8 *)buf, len, NULL, &wfd_ielen);
        if (wfd_ie)
        {
            P2P_DBG("bcn_wfd_ielen=%d\n", wfd_ielen);

            if (zt_p2p_wfd_update_ie(pnic_info, ZT_WFD_IE_BEACON, wfd_ie, wfd_ielen,
                                     1) != zt_true)
            {
                return -1;
            }
        }
    }

    return 0;
}

static zt_bool st_check_nic_state(nic_info_st *pnic_info, zt_u32 check_state)
{
    zt_bool ret = zt_false;
    if (pnic_info->nic_state & check_state)
    {
        ret = zt_true;
    }
    return ret;
}


static zt_s32 p2p_probe_rsp_parse(nic_info_st *pnic_info, zt_s8 *buf,
                                  zt_s32 len)
{
    zt_s32 ret = 0;
    zt_u32 wps_ielen    = 0;
    zt_u8 *wps_ie       = NULL;
    zt_u32 p2p_ielen    = 0;
    zt_u8 *p2p_ie           = NULL;
    p2p_info_st *p2p_info    = pnic_info->p2p;
    zt_u32 wfd_ielen = 0;
    zt_u8 *wfd_ie    = NULL;
    mlme_info_t *pmlme_info = (mlme_info_t *)pnic_info->mlme_info;

    P2P_DBG(" ielen=%d\n", len);
    //P2P_ARRAY(buf,  len);
    if (len > 0)
    {
        if ((wps_ie = zt_wlan_get_wps_ie((zt_u8 *)buf, len, NULL, &wps_ielen)))
        {
            zt_u32 attr_contentlen = 0;
            zt_u16 uconfig_method, *puconfig_method = NULL;


            P2P_DBG("probe_resp_wps_ielen=%d\n", wps_ielen);

            if (st_check_nic_state(pnic_info, WIFI_UNDER_WPS))
            {
                zt_u8 sr = 0;
                zt_wlan_get_wps_attr_content(1, wps_ie, wps_ielen, WPS_ATTR_SELECTED_REGISTRAR,
                                             (zt_u8 *)(&sr), NULL);

                if (sr != 0)
                {
                    P2P_INFO("got sr\n");
                }
                else
                {
                    P2P_INFO("GO mode process WPS under site-survey,  sr no set\n");
                    return ret;
                }
            }

            if ((puconfig_method =
                        (zt_u16 *) zt_wlan_get_wps_attr_content(1, wps_ie, wps_ielen,
                                WPS_ATTR_CONF_METHOD, NULL,
                                &attr_contentlen)) != NULL)
            {
                if (zt_p2p_is_valid(pnic_info))
                {
                    if (p2p_info->role == P2P_ROLE_GO)
                    {
                        uconfig_method = WPS_CM_PUSH_BUTTON;
                        uconfig_method = zt_cpu_to_be16(uconfig_method);
                        *puconfig_method &= ~uconfig_method;
                    }
                }
            }

            zt_memcpy(&pmlme_info->wps_probe_resp_ie[0], wps_ie, wps_ielen);
            pmlme_info->wps_probe_resp_ie_len = wps_ielen;

        }

        if ((p2p_ie = zt_p2p_get_ie((zt_u8 *)buf, len, NULL, &p2p_ielen)))
        {
            if (zt_false == zt_p2p_is_valid(pnic_info))
            {
                P2P_DBG("enable p2p func\n");
                zt_p2p_enable(pnic_info, P2P_ROLE_DEVICE);
            }
            zt_p2p_parse_p2pie(pnic_info, p2p_ie, p2p_ielen, ZT_P2P_IE_PROBE_RSP);
            P2P_DBG("probe_resp_p2p_ielen=%d\n", p2p_ielen);
            zt_p2p_dump_attrs(p2p_ie, p2p_ielen);

        }
        //P2P_ARRAY(p2p_info->p2p_ie[ZT_P2P_IE_PROBE_RSP],p2p_info->p2p_ie_len[ZT_P2P_IE_PROBE_RSP]);

        wfd_ie = zt_p2p_wfd_get_ie(1, (zt_u8 *)buf, len, NULL, &wfd_ielen);
        P2P_DBG("probe_resp_wfd_ielen=%d , wfd_ie=%p\n", wfd_ielen, wfd_ie);
        if (wfd_ie)
        {
            if (zt_p2p_wfd_update_ie(pnic_info, ZT_WFD_IE_PROBE_RSP, wfd_ie, wfd_ielen,
                                     1) != zt_true)
            {
                return -1;
            }
        }

    }
    else
    {
        P2P_INFO("reset p2p key info");
        zt_p2p_reset(pnic_info);
    }

    return ret;

}


static zt_s32 p2p_assoc_rsp_parse(nic_info_st *pnic_info, zt_s8 *buf,
                                  zt_s32 len)
{
    zt_u8 *wps_ie = NULL;
    zt_u32 wps_ielen = 0;
    zt_u8 *p2p_ie = NULL;
    zt_u32 p2p_ielen = 0;
    zt_u8 *wfd_ie = NULL;
    zt_u32 wfd_ielen = 0;

    mlme_info_t *pmlme_info = (mlme_info_t *)pnic_info->mlme_info;

    if (len <= 0)
    {
        return -1;
    }
    if ((wps_ie = zt_wlan_get_wps_ie((zt_u8 *)buf, len, NULL, &wps_ielen)))
    {
        P2P_DBG("assoc_resp_wps_ie_len : %d", wps_ielen);
        zt_memcpy(&pmlme_info->wps_assoc_resp_ie[0], wps_ie, wps_ielen);
        pmlme_info->wps_assoc_resp_ie_len = wps_ielen;
    }

    if ((p2p_ie = zt_p2p_get_ie((zt_u8 *)buf, len, NULL, &p2p_ielen)))
    {
        P2P_DBG("assoc_resp_p2p_ie_len : %d", p2p_ielen);
        zt_p2p_dump_attrs(p2p_ie, p2p_ielen);
        zt_p2p_parse_p2pie(pnic_info, p2p_ie, p2p_ielen, ZT_P2P_IE_ASSOC_RSP);
    }

    if (zt_p2p_wfd_is_valid(pnic_info))
    {
        wfd_ie = zt_p2p_wfd_get_ie(1, (zt_u8 *)buf, len, NULL, &wfd_ielen);
        if (zt_p2p_wfd_update_ie(pnic_info, ZT_WFD_IE_ASSOC_RSP, wfd_ie, wfd_ielen,
                                 1) != zt_true)
        {
            return -1;
        }
    }

    return 0;
}


zt_s32 zt_p2p_parse_ie(nic_info_st *pnic_info, zt_u8 *buf, zt_s32 len,
                       zt_s32 type)
{
    zt_s32 ret = 0;
    zt_u32 wps_ielen = 0;
    zt_u32 p2p_ielen = 0;

    P2P_DBG(" ielen=%d\n", len);
    if ((zt_wlan_get_wps_ie(buf, len, NULL, &wps_ielen) && (wps_ielen > 0))
            || (zt_p2p_get_ie(buf, len, NULL, &p2p_ielen) && (p2p_ielen > 0))
       )
    {
        if (pnic_info != NULL)
        {
            switch (type)
            {
                case 0x1:
                    ret = p2p_beacon_parse(pnic_info, (zt_s8 *)buf, len);
                    break;
                case 0x2:
                    ret = p2p_probe_rsp_parse(pnic_info, (zt_s8 *)buf, len);
                    break;
                case 0x4:
                    ret = p2p_assoc_rsp_parse(pnic_info, (zt_s8 *)buf, len);
                    break;
            }
        }
    }

    return ret;

}

