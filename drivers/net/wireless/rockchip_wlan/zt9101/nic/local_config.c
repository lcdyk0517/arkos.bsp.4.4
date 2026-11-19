/*
 * local_config.c
 *
 * used for local information
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
#include "hif.h"

static local_info_st default_cfg[ZT_MODE_MAX] =
{
    [ZT_AUTO_MODE] =
    {
        .work_mode  = ZT_AUTO_MODE,
        .channel    = 1,
        .bw         = CHANNEL_WIDTH_20,
#ifdef CFG_ENABLE_ADHOC_MODE
        .adhoc_master   = zt_false,
#endif
#ifdef CFG_ENABLE_AP_MODE
        .ssid        = "ZTOP-AUTO",
#endif
        .channel_plan = ZT_CHPLAN_CHINA,
        .ba_enable_tx = 1,
        .ba_enable_rx = 1,

        .scan_ch_to = 50,
        .scan_prb_times = 3,
        .scan_que_deep = 64,
        .scan_que_node_ttl = 20,
        .ars_policy = 0,
        .max_ampdu_len_ulimit = 3,
        .wlan_guard = 1,
        .rf_power = E_RADIO_POWER_LEVEL_M,
        .vco_cur = 0xFF,
    },
#ifdef CFG_ENABLE_ADHOC_MODE
    [ZT_ADHOC_MODE] =
    {
        .work_mode  = ZT_ADHOC_MODE,
        .channel    = 1,
        .bw         = CHANNEL_WIDTH_20,
#ifdef CFG_ENABLE_ADHOC_MODE
        .adhoc_master   = zt_false,
#endif
#ifdef CFG_ENABLE_AP_MODE
        .ssid        = "ZTOP-ADHOC",
#endif
        .channel_plan = ZT_CHPLAN_CHINA,
        .ba_enable_tx = 1,
        .ba_enable_rx = 1,

        .scan_ch_to = 50,
        .scan_prb_times = 3,
        .scan_que_deep = 64,
        .scan_que_node_ttl = 20,
        .ars_policy = 0,
        .max_ampdu_len_ulimit = 3,
        .wlan_guard = 1,
        .rf_power = E_RADIO_POWER_LEVEL_M,
        .vco_cur = 0xFF,
    },
#endif
    [ZT_INFRA_MODE] =
    {
        .work_mode  = ZT_INFRA_MODE,
        .channel    = 1,
        .bw         = CHANNEL_WIDTH_20,
#ifdef CFG_ENABLE_ADHOC_MODE
        .adhoc_master   = zt_false,
#endif
        .channel_plan = ZT_CHPLAN_CHINA,
        .ba_enable_tx = 1,
        .ba_enable_rx = 1,

        .scan_ch_to = 50,
        .scan_prb_times = 2,
        .scan_que_deep = 64,
        .scan_que_node_ttl = 20,
        .ars_policy = 0,
        .max_ampdu_len_ulimit = 3,
        .wlan_guard = 1,
        .rf_power = E_RADIO_POWER_LEVEL_M,
        .vco_cur = 0xFF,
    },
#ifdef CFG_ENABLE_AP_MODE
    [ZT_MASTER_MODE] =
    {
        .work_mode  = ZT_MASTER_MODE,
        .channel    = 1,
        .bw         = CHANNEL_WIDTH_20,
#ifdef CFG_ENABLE_ADHOC_MODE
        .adhoc_master   = zt_false,
#endif
        .ssid        = "ZTOP-AP",
        .channel_plan = ZT_CHPLAN_CHINA,
        .ba_enable_tx = 1,
        .ba_enable_rx = 1,

        .scan_ch_to = 50,
        .scan_prb_times = 3,
        .scan_que_deep = 64,
        .scan_que_node_ttl = 20,
        .ars_policy = 0,
        .max_ampdu_len_ulimit = 3,
        .wlan_guard = 1,
        .rf_power = E_RADIO_POWER_LEVEL_M,
        .vco_cur = 0xFF,
    },
#endif
    [ZT_REPEAT_MODE] =
    {
        .work_mode  = ZT_REPEAT_MODE,
        .channel    = 1,
        .bw         = CHANNEL_WIDTH_20,
#ifdef CFG_ENABLE_ADHOC_MODE
        .adhoc_master   = zt_false,
#endif
#ifdef CFG_ENABLE_AP_MODE
        .ssid        = "ZTOP-REPEAT",
#endif
        .channel_plan = ZT_CHPLAN_CHINA,
        .ba_enable_tx = 1,
        .ba_enable_rx = 1,

        .scan_ch_to = 50,
        .scan_prb_times = 3,
        .scan_que_deep = 64,
        .scan_que_node_ttl = 20,
        .ars_policy = 0,
        .max_ampdu_len_ulimit = 3,
        .wlan_guard = 1,
        .rf_power = E_RADIO_POWER_LEVEL_M,
        .vco_cur = 0xFF,
    },
    [ZT_SECOND_MODES] =
    {
        .work_mode  = ZT_SECOND_MODES,
        .channel    = 1,
        .bw         = CHANNEL_WIDTH_20,
#ifdef CFG_ENABLE_ADHOC_MODE
        .adhoc_master   = zt_false,
#endif
#ifdef CFG_ENABLE_AP_MODE
        .ssid        = "ZTOP-SECOND",
#endif
        .channel_plan = ZT_CHPLAN_CHINA,
        .ba_enable_tx = 1,
        .ba_enable_rx = 1,

        .scan_ch_to = 50,
        .scan_prb_times = 3,
        .scan_que_deep = 64,
        .scan_que_node_ttl = 20,
        .ars_policy = 0,
        .max_ampdu_len_ulimit = 3,
        .wlan_guard = 1,
        .rf_power = E_RADIO_POWER_LEVEL_M,
        .vco_cur = 0xFF,
    },
#ifdef CFG_ENABLE_MONITOR_MODE
    [ZT_MONITOR_MODE] =
    {
        .work_mode  = ZT_MONITOR_MODE,
        .channel    = 1,
        .bw         = CHANNEL_WIDTH_20,
        .channel_plan = ZT_CHPLAN_CHINA,
        .ba_enable_tx = 1,
        .ba_enable_rx = 1,

        .scan_ch_to = 50,
        .scan_prb_times = 3,
        .scan_que_deep = 64,
        .scan_que_node_ttl = 20,
        .ars_policy = 0,
        .max_ampdu_len_ulimit = 3,
        .wlan_guard = 1,
        .rf_power = E_RADIO_POWER_LEVEL_M,
        .vco_cur = 0xFF,
    },
#endif
    [ZT_MESH_MODE] =
    {
        .work_mode  = ZT_MESH_MODE,
        .channel    = 1,
        .bw         = CHANNEL_WIDTH_20,
#ifdef CFG_ENABLE_AP_MODE
        .ssid        = "ZTOP-MESH",
#endif
        .channel_plan = ZT_CHPLAN_CHINA,
        .ba_enable_tx = 1,
        .ba_enable_rx = 1,

        .scan_ch_to = 50,
        .scan_prb_times = 3,
        .scan_que_deep = 64,
        .scan_que_node_ttl = 20,
        .ars_policy = 0,
        .max_ampdu_len_ulimit = 3,
        .wlan_guard = 1,
        .rf_power = E_RADIO_POWER_LEVEL_M,
        .vco_cur = 0xFF,
    },

};

zt_s32 zt_local_cfg_init(nic_info_st *nic_info)
{
    nic_info->local_info = (local_info_st *)zt_kzalloc(sizeof(local_info_st));
    if (nic_info->local_info == NULL)
    {
        return -1;
    }

    zt_memcpy(nic_info->local_info, &default_cfg[ZT_INFRA_MODE],
              sizeof(local_info_st));

    return 0;
}

zt_s32 zt_local_cfg_term(nic_info_st *nic_info)
{
    if (nic_info->local_info != NULL)
    {
        zt_kfree(nic_info->local_info);
    }

    return 0;
}


zt_s32 zt_local_cfg_get_default(nic_info_st *nic_info)
{
    if (nic_info->nic_cfg_file_read != NULL)
    {
        nic_info->nic_cfg_file_read((void *)nic_info);
    }

    return 0;
}

static zt_s32 rx_config_agg(nic_info_st *nic_info)
{
    zt_u8 agg_en;
    zt_s32 ret = 0;

#ifdef CONFIG_SOFT_RX_AGGREGATION
    agg_en = 1;
#else
    agg_en = 0;
#endif
    if (nic_info->nic_type == NIC_USB)
    {
        ret = zt_mcu_set_agg_param(nic_info, 0x4, 0x5, agg_en, 0);
    }
    else
    {
        ret = zt_mcu_set_agg_param(nic_info, 0x8, 0x5, 1, 0);
    }
    if (ret != ZT_RETURN_OK)
    {
        return ZT_RETURN_FAIL;
    }

    return 0;
}

zt_s32 zt_local_cfg_set_default(nic_info_st *nic_info)
{
    local_info_st *local_info = nic_info->local_info;
    hif_node_st *hif_node = nic_info->hif_node;
    zt_s32 ret = 0;

    LOG_D("[LOCAL_CFG] work_mode: %d", local_info->work_mode);
    LOG_D("[LOCAL_CFG] channel: %d", local_info->channel);
    LOG_D("[LOCAL_CFG] bw: %d", local_info->bw);
    LOG_D("[LOCAL_CFG] ssid: %s", local_info->ssid);

    ret = zt_hw_info_set_channel_bw(nic_info, local_info->channel, local_info->bw,
                                     HAL_PRIME_CHNL_OFFSET_DONT_CARE);
    if (ret != ZT_RETURN_OK)
    {
        return ZT_RETURN_FAIL;
    }

    if (hif_node->drv_ops->driver_flag)
    {
        ret = zt_mcu_handle_rf_lck_calibrate(nic_info);
        if (ret != ZT_RETURN_OK)
        {
            return ZT_RETURN_FAIL;
        }

    }
    ret = zt_mcu_handle_rf_iq_calibrate(nic_info, local_info->channel);
    if (ret != ZT_RETURN_OK)
    {
        return ZT_RETURN_FAIL;
    }

    ret = zt_mcu_update_thermal(nic_info);
    if (ret != ZT_RETURN_OK)
    {
        return ZT_RETURN_FAIL;
    }

    // cfg sta/ap/adhoc/monitor mode
    ret = zt_mcu_set_op_mode(nic_info, local_info->work_mode);
    if (ret != ZT_RETURN_OK)
    {
        return ZT_RETURN_FAIL;
    }

    rx_config_agg(nic_info);

    return ZT_RETURN_OK;
}


sys_work_mode_e zt_local_cfg_get_work_mode(nic_info_st *pnic_info)
{
    local_info_st *plocal = (local_info_st *)pnic_info->local_info;
    return plocal->work_mode;
}

void zt_local_cfg_set_work_mode(nic_info_st *pnic_info, sys_work_mode_e mode)
{
    if (NULL == pnic_info)
    {
        LOG_E("param is null");
        return;
    }

    if (ZT_AUTO_MODE > mode || mode > ZT_MESH_MODE)
    {
        LOG_E("[%s] mode(%d) is not support", __func__, mode);
        return;
    }

    zt_memcpy(pnic_info->local_info, &default_cfg[mode],
              sizeof(local_info_st));
}



