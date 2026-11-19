/*
 * zt_os_api_mix.c
 *
 * used for .....
 *
 * Author: zenghua
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
/* include */
#include "common.h"
#ifdef CONFIG_IOCTL_CFG80211
#include "zt_cfg80211.h"
#endif
#include "hif.h"

/* macro */

/* type */

/* function declaration */

void zt_os_api_ind_scan_done(void *arg, zt_bool arg1, zt_u8 arg2)
{
    nic_info_st *pnic_info = arg;
#ifdef CONFIG_IOCTL_CFG80211
    zt_u8 babort = arg1;
#endif
    zt_mlme_framework_e framework = arg2;

#ifdef CONFIG_WIRELESS_EXT
    if (framework == ZT_MLME_FRAMEWORK_WEXT)
    {
        union iwreq_data wrqu;

        zt_memset(&wrqu, 0, sizeof(union iwreq_data));
        wireless_send_event(pnic_info->ndev, SIOCGIWSCAN, &wrqu, NULL);
    }
#endif
#ifdef CONFIG_IOCTL_CFG80211
    if (framework == ZT_MLME_FRAMEWORK_NETLINK)
    {
        if (zt_cfg80211_scan_complete(pnic_info)){
            zt_cfg80211_scan_done_event_up(pnic_info, babort);
        }
    }
#endif

    /* Restore the p2p state before scanning */
    if (zt_p2p_is_valid(pnic_info))
    {
        p2p_info_st *p2p_info = pnic_info->p2p;
        zt_p2p_set_state(p2p_info, p2p_info->pre_p2p_state);
    }
}

void zt_os_api_ind_connect(void *arg, zt_u8 arg1)
{
    nic_info_st *pnic_info = arg;
    zt_mlme_framework_e framework = arg1;

#ifdef CONFIG_WIRELESS_EXT
    if (framework == ZT_MLME_FRAMEWORK_WEXT)
    {
        zt_wlan_mgmt_info_t *wlan_mgmt_info = pnic_info->wlan_mgmt_info;
        zt_wlan_network_t *pcur_network = &wlan_mgmt_info->cur_network;
        union iwreq_data wrqu;

        zt_memset(&wrqu, 0, sizeof(union iwreq_data));

        wrqu.ap_addr.sa_family = ARPHRD_ETHER;
        zt_memcpy(wrqu.ap_addr.sa_data, pcur_network->bssid, ETH_ALEN);
        wireless_send_event(pnic_info->ndev, SIOCGIWAP, &wrqu, NULL);
    }
#endif
#ifdef CONFIG_IOCTL_CFG80211
    if (framework == ZT_MLME_FRAMEWORK_NETLINK)
    {
#ifdef CFG_ENABLE_ADHOC_MODE
        if (zt_local_cfg_get_work_mode(pnic_info) == ZT_ADHOC_MODE)
        {
            zt_cfg80211_ibss_indicate_connect(pnic_info);
        }
        else
#endif
        {
            zt_cfg80211_indicate_connect(pnic_info);
        }
    }
#endif
}

void zt_os_api_ind_disconnect(void *arg, zt_u8 arg1)
{
    nic_info_st *pnic_info = arg;
    zt_mlme_framework_e framework = arg1;

#ifdef CONFIG_WIRELESS_EXT
    if (framework == ZT_MLME_FRAMEWORK_WEXT)
    {
        union iwreq_data wrqu;

        zt_memset(&wrqu, 0, sizeof(union iwreq_data));

        wrqu.ap_addr.sa_family = ARPHRD_ETHER;
        zt_memset(wrqu.ap_addr.sa_data, 0, ETH_ALEN);
        wireless_send_event(pnic_info->ndev, SIOCGIWAP, &wrqu, NULL);

        zt_os_api_disable_all_data_queue(pnic_info->ndev);
    }
#endif
#ifdef CONFIG_IOCTL_CFG80211
    if (framework == ZT_MLME_FRAMEWORK_NETLINK)
    {
        zt_cfg80211_indicate_disconnect(pnic_info);
    }
#endif
}

#ifdef CFG_ENABLE_ADHOC_MODE
void zt_os_api_cfg80211_unlink_ibss(void *arg)
{
#ifdef CONFIG_IOCTL_CFG80211
    nic_info_st *pnic_info = arg;
    zt_cfg80211_unlink_ibss(pnic_info);
#endif
}
#endif


#ifdef CFG_ENABLE_AP_MODE
void zt_os_api_ap_ind_assoc(void *arg, void *arg1, void *arg2, zt_u8 arg3)
{
    nic_info_st *pnic_info = arg;
#ifdef CONFIG_WIRELESS_EXT
    wdn_net_info_st *pwdn_info = arg1;
#endif
    zt_mlme_framework_e framework = arg3;

#ifdef CONFIG_WIRELESS_EXT
    if (framework == ZT_MLME_FRAMEWORK_WEXT)
    {
        union iwreq_data wrqu;

        wrqu.addr.sa_family = ARPHRD_ETHER;
        zt_memcpy(wrqu.addr.sa_data, pwdn_info->mac, ZT_80211_MAC_ADDR_LEN);
        wireless_send_event(pnic_info->ndev, IWEVREGISTERED, &wrqu, NULL);
    }
#endif
#ifdef CONFIG_IOCTL_CFG80211
    if (framework == ZT_MLME_FRAMEWORK_NETLINK)
    {
        zt_ap_msg_t *pmsg = arg2;

        zt_ap_cfg80211_assoc_event_up(pnic_info, (zt_u8 *)&pmsg->mgmt, pmsg->len);
    }
#endif
}

void zt_os_api_ap_ind_disassoc(void *arg, void *arg1, zt_u8 arg2)
{
    nic_info_st *pnic_info = arg;
    wdn_net_info_st *pwdn_info = arg1;
    zt_mlme_framework_e framework = arg2;

#ifdef CONFIG_WIRELESS_EXT
    if (framework == ZT_MLME_FRAMEWORK_WEXT)
    {
        union iwreq_data wrqu;

        wrqu.addr.sa_family = ARPHRD_ETHER;
        zt_memcpy(wrqu.addr.sa_data, pwdn_info->mac, ZT_80211_MAC_ADDR_LEN);
        wireless_send_event(pnic_info->ndev, IWEVEXPIRED, &wrqu, NULL);
    }
#endif
#ifdef CONFIG_IOCTL_CFG80211
    if (framework == ZT_MLME_FRAMEWORK_NETLINK)
    {
        zt_ap_cfg80211_disassoc_event_up(pnic_info, pwdn_info);
    }
#endif
}
#endif


void zt_os_api_enable_all_data_queue(void *arg)
{
    struct net_device *ndev = arg;

    netif_carrier_on(ndev);

#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,35))
    netif_tx_start_all_queues(ndev);
#else
    netif_start_queue(ndev);
#endif
    LOG_W("The netif carrier on");
}

void zt_os_api_disable_all_data_queue(void *arg)
{
    struct net_device *ndev = arg;

#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,35))
    netif_tx_stop_all_queues(ndev);
#else
    netif_stop_queue(ndev);
#endif

    netif_carrier_off(ndev);
    LOG_W("The netif carrier off");
}

zt_u32 zt_os_api_rand32(void)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,11,0))
    return get_random_u32();
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3,8,0))
    return prandom_u32();
#elif (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,18))
    zt_u32 random_int;
    get_random_bytes(&random_int, 4);
    return random_int;
#else
    return random32();
#endif
}

zt_s32 zt_os_api_get_cpu_id(void)
{
    return smp_processor_id();
}

