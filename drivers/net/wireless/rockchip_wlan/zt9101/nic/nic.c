/*
 * nic.c
 *
 * used for Initialization logic
 *
 * Author: pansiwei
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

static zt_s32 prv_hardware_init(nic_info_st *pnic_info)
{
    zt_s32 ret = 0;

    LOG_D("[NIC] prv_hardware_init - entry");

    pnic_info->is_driver_stopped = zt_false;
    pnic_info->is_surprise_removed = zt_false;
    pnic_info->is_driver_critical = zt_false;

    /* tx init */
    if (zt_tx_info_init(pnic_info) < 0)
    {
        LOG_E("===>zt_tx_info_init error");
        return ZT_RETURN_FAIL;
    }

    /* rx init */
    if (zt_rx_init(pnic_info))
    {
        LOG_E("===>zt_rx_init error");
        return ZT_RETURN_FAIL;
    }

    /* local info init */
    if (zt_local_cfg_init(pnic_info) < 0)
    {
        LOG_E("===>zt_local_cfg_init error");
        return ZT_RETURN_FAIL;
    }

    /* get local default cfg */
    if (zt_local_cfg_get_default(pnic_info) < 0)
    {
        LOG_E("===>zt_local_cfg_get_default error");
        return ZT_RETURN_FAIL;
    }

    /* hw info init */
    if (zt_hw_info_init(pnic_info) < 0)
    {
        LOG_E("===>zt_hw_info_init error");
        return ZT_RETURN_FAIL;
    }

    /* get hw default cfg */
    if (zt_hw_info_get_default_cfg(pnic_info) < 0)
    {
        LOG_E("===>zt_hw_info_get_default_cfg error");
        return ZT_RETURN_FAIL;
    }

    if (pnic_info->virNic == zt_false)
    {
        /* init hardware by default cfg */
        if (zt_hw_info_set_default_cfg(pnic_info) < 0)
        {
            LOG_E("===>zt_hw_info_set_default_cfg error");
            return ZT_RETURN_FAIL;
        }

        /* configure */
        if (zt_local_cfg_set_default(pnic_info) < 0)
        {
            LOG_E("===>zt_local_cfg_set_default error");
            return ZT_RETURN_FAIL;
        }
    }

    pnic_info->ndev_num++;

    LOG_D("[NIC] prv_hardware_init - exit");

    return ret;
}


static zt_s32 prv_hardware_term(nic_info_st *pnic_info)
{
    LOG_D("[NIC] prv_hardware_term - entry");

    /* rx term */
    if (zt_rx_term(pnic_info) < 0)
    {
        LOG_E("===>zt_rx_term error");
        return ZT_RETURN_FAIL;
    }

    /* tx term */
    if (zt_tx_info_term(pnic_info) < 0)
    {
        LOG_E("===>zt_tx_info_term error");
        return ZT_RETURN_FAIL;
    }


    /* hw info term */
    if (zt_hw_info_term(pnic_info) < 0)
    {
        LOG_E("===>zt_hw_info_term error");
    }

    /* local configure term */
    if (zt_local_cfg_term(pnic_info) < 0)
    {
        LOG_E("===>zt_local_cfg_term error");
        return ZT_RETURN_FAIL;
    }

    pnic_info->ndev_num--;

    LOG_D("[NIC] prv_hardware_term - exit");

    return ZT_RETURN_OK;
}

zt_s32 nic_init(nic_info_st *pnic_info)
{
    LOG_D("[NIC] nic_init - start");

    zt_os_api_sema_init(&pnic_info->cmd_sema, 1);

    /* hardware init by chip */
    if (prv_hardware_init(pnic_info) < 0)
    {
        LOG_E("===>prv_hardware_init error");
        return ZT_RETURN_FAIL;
    }

    /*p2p*/
#ifdef ZT_CONFIG_P2P
    if (zt_p2p_init(pnic_info) < 0)
    {
        LOG_E("===>zt_p2p_init error");
        return ZT_RETURN_FAIL;
    }
#endif

#ifdef CFG_ENABLE_ADHOC_MODE
    if (zt_adhoc_init(pnic_info) < 0)
    {
        LOG_E("===>zt_adhoc_init error");
        return ZT_RETURN_FAIL;
    }
#endif

    /*wdn init*/
    if (zt_wdn_init(pnic_info) < 0)
    {
        LOG_E("===>zt_wdn_init error");
        return ZT_RETURN_FAIL;
    }

    /* scan init */
    if (zt_scan_init(pnic_info) < 0)
    {
        LOG_E("===>zt_scan_info_init error");
        return ZT_RETURN_FAIL;
    }

    /* auth init */
    if (zt_auth_init(pnic_info) < 0)
    {
        LOG_E("===>zt_auth_init error");
        return ZT_RETURN_FAIL;
    }

    /* assoc init */
    if (zt_assoc_init(pnic_info) < 0)
    {
        LOG_E("===>zt_assoc_init error");
        return ZT_RETURN_FAIL;
    }

    /* sec init */
    if (zt_sec_info_init(pnic_info) < 0)
    {
        LOG_E("===>zt_sec_info_init error");
        return ZT_RETURN_FAIL;
    }

    /* pwr_info init  */
    if (zt_lps_init(pnic_info) < 0)
    {
        LOG_E("===>zt_lps_init error");
        return ZT_RETURN_FAIL;
    }

    /* wlan init */
    if (zt_wlan_mgmt_init(pnic_info) < 0)
    {
        LOG_E("===>zt_wlan_init error");
        return ZT_RETURN_FAIL;
    }

#ifdef CFG_ENABLE_AP_MODE
    /* ap init */
    if (zt_ap_init(pnic_info) < 0)
    {
        LOG_E("===>zt_ap_init error");
        return ZT_RETURN_FAIL;
    }
#endif

    /* mlme init */
    if (zt_mlme_init(pnic_info) < 0)
    {
        LOG_E("===>zt_mlme_init error");
        return ZT_RETURN_FAIL;
    }

    pnic_info->is_init_commplete = zt_true;
    LOG_D("[NIC] nic_init - end");

    return ZT_RETURN_OK;
}



zt_s32 nic_term(nic_info_st *pnic_info)
{
    LOG_D("[NIC] nic_term - start");

    pnic_info->is_init_commplete = zt_false;

    /* mlme term */
    if (zt_mlme_term(pnic_info) < 0)
    {
        LOG_E("===>zt_mlme_term error");
        return ZT_RETURN_FAIL;
    }

#ifdef CFG_ENABLE_AP_MODE
    if (zt_ap_term(pnic_info) < 0)
    {
        LOG_E("===>zt_ap_work_stop error");
        return ZT_RETURN_FAIL;
    }
#endif

    /* wlan term */
    if (zt_wlan_mgmt_term(pnic_info) < 0)
    {
        LOG_E("===>zt_wlan_term error");
        return ZT_RETURN_FAIL;
    }

    /* pwr_info term  */
    if (zt_lps_term(pnic_info) < 0)
    {
        LOG_E("===>zt_lps_term error");
        return ZT_RETURN_FAIL;
    }

    /* sec term */
    if (zt_sec_info_term(pnic_info) < 0)
    {
        LOG_E("===>zt_sec_info_term error");
        return ZT_RETURN_FAIL;
    }

    /* assoc term */
    if (zt_assoc_term(pnic_info) < 0)
    {
        LOG_E("===>zt_assoc_term error");
        return ZT_RETURN_FAIL;
    }


    /* auth term */
    if (zt_auth_term(pnic_info) < 0)
    {
        LOG_E("===>zt_auth_term error");
        return ZT_RETURN_FAIL;
    }

    /* scan term */
    if (zt_scan_term(pnic_info) < 0)
    {
        LOG_E("===>zt_scan_term error");
        return ZT_RETURN_FAIL;
    }

    /*wdn term*/
    if (zt_wdn_term(pnic_info) < 0)
    {
        LOG_E("===>zt_wdn_term error");
        return ZT_RETURN_FAIL;
    }

#ifdef CFG_ENABLE_ADHOC_MODE
    if (zt_adhoc_term(pnic_info) < 0)
    {
        LOG_E("===>zt_adhoc_term error");
        return ZT_RETURN_FAIL;
    }
#endif

#ifdef ZT_CONFIG_P2P
    if (zt_p2p_term(pnic_info) < 0)
    {
        LOG_E("===>zt_p2p_term error");
        return ZT_RETURN_FAIL;
    }
#endif

    zt_mcu_reset_chip(pnic_info);

    /* hardware term */
    if (prv_hardware_term(pnic_info) < 0)
    {
        LOG_E("prv_hardware_term, fail!");
        return ZT_RETURN_FAIL;
    }

    LOG_D("[NIC] nic_term - end");
    return ZT_RETURN_OK;
}


zt_s32 nic_enable(nic_info_st *pnic_info)
{
    //zt_s32 ret = 0;
    if (NULL == pnic_info)
    {
        LOG_I("[NIC] pnic_info is null");
        return ZT_RETURN_OK;
    }

    LOG_I("[NIC] nic_enable :"ZT_MAC_FMT, ZT_MAC_ARG(nic_to_local_addr(pnic_info)));

    if (0 == pnic_info->is_up)
    {
        pnic_info->is_up = zt_true;
        zt_p2p_resume(pnic_info);
    }

    return ZT_RETURN_OK;
}


zt_s32 nic_disable(nic_info_st *pnic_info)
{
    zt_s32 ret = ZT_RETURN_FAIL;

    LOG_D("[%d] nic_disable", pnic_info->ndev_id);

    if (pnic_info->is_up)
    {
        zt_p2p_suspend(pnic_info);
        zt_mlme_abort(pnic_info);
#ifdef CFG_ENABLE_AP_MODE
        zt_ap_work_stop(pnic_info);
#endif
        pnic_info->is_up = zt_false;
        ret = ZT_RETURN_OK;
    }

    return ret;
}

zt_s32 nic_suspend(nic_info_st *pnic_info)
{
    LOG_D("[NIC] nic_suspend - begin");

    if (zt_mlme_suspend(pnic_info))
    {
        LOG_E("mlme suspend error!");
        return ZT_RETURN_FAIL;
    }

#ifdef CFG_ENABLE_AP_MODE
    if (zt_ap_suspend(pnic_info))
    {
        LOG_E("ap suspend error!");
        return ZT_RETURN_FAIL;
    }
#endif

#ifdef ZT_CONFIG_P2P
    if (zt_p2p_suspend(pnic_info))
    {
        LOG_E("p2p suspend error!");
        return ZT_RETURN_FAIL;
    }
#endif

    if (zt_tx_suspend(pnic_info))
    {
        LOG_E("tx suspend error!");
        return ZT_RETURN_FAIL;
    }

    if (zt_rx_suspend(pnic_info))
    {
        LOG_E("rx suspend error!");
        return ZT_RETURN_FAIL;
    }

    LOG_D("[NIC] nic_suspend - end");

    return ZT_RETURN_OK;
}

zt_s32 nic_resume(nic_info_st *pnic_info)
{
    LOG_D("[NIC] nic_resume - begin");

    if (pnic_info->virNic == zt_false)
    {
        /* init hardware by default cfg */
        if (zt_hw_info_set_default_cfg(pnic_info) < 0)
        {
            LOG_E("===>zt_hw_info_set_default_cfg error");
            return ZT_RETURN_FAIL;
        }

        /* configure */
        if (zt_local_cfg_set_default(pnic_info) < 0)
        {
            LOG_E("===>zt_local_cfg_set_default error");
            return ZT_RETURN_FAIL;
        }
    }

    if (zt_rx_resume(pnic_info))
    {
        LOG_E("rx suspend error!");
        return ZT_RETURN_FAIL;
    }

    if (zt_tx_resume(pnic_info))
    {
        LOG_E("tx suspend error!");
        return ZT_RETURN_FAIL;
    }

#ifdef ZT_CONFIG_P2P
    if (zt_p2p_resume(pnic_info))
    {
        LOG_E("p2p resume error!");
        return ZT_RETURN_FAIL;
    }
#endif

#ifdef CFG_ENABLE_AP_MODE
    if (zt_ap_resume(pnic_info))
    {
        LOG_E("ap resume error!");
        return ZT_RETURN_FAIL;
    }
#endif

    if (zt_mlme_resume(pnic_info))
    {
        LOG_E("mlme resume error!");
        return ZT_RETURN_FAIL;
    }

    LOG_D("[NIC] nic_resume - end");

    return ZT_RETURN_OK;
}

zt_s32 nic_shutdown(nic_info_st *pnic_info)
{
    pnic_info->is_driver_stopped = zt_true;

    zt_scan_stop(pnic_info);


    return ZT_RETURN_OK;
}

zt_u8 *nic_to_local_addr(nic_info_st *pnic_info)
{
    hw_info_st *hw_info = pnic_info->hw_info;

    if (hw_info == NULL)
    {
        return NULL;
    }

    return hw_info->macAddr;
}

