/*
 * mcu_cmd.c
 *
 * used for cmd Interactive command
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

#define MCU_LINKED           1
#define MCU_UNLINKED         0

#define ZT_SECURITY_CAM_SIZE        28
#define ZT_SECURITY_KEY_SIZE        16


static zt_s32 mcu_get_buddy_mlmestate(nic_info_st *pnic_info)
{
    nic_info_st *buddy_nic = NULL;
    sys_work_mode_e work_mode;


    if (NULL == pnic_info)
    {
        return FW_STATE_NO_EXIST;
    }

    buddy_nic = pnic_info->buddy_nic;
    if (NULL == buddy_nic)
    {
        return FW_STATE_NO_EXIST;
    }

    work_mode = zt_local_cfg_get_work_mode(buddy_nic);
#ifdef CFG_ENABLE_ADHOC_MODE
    if (work_mode == ZT_ADHOC_MODE)
    {
        return FW_STATE_ADHOC;
    }
    else
#endif
        if (work_mode == ZT_INFRA_MODE)
        {
            return FW_STATE_STATION;
        }
#ifdef CFG_ENABLE_AP_MODE
        else if (work_mode == ZT_MASTER_MODE)
        {
            return FW_STATE_AP;
        }
#endif

    return FW_STATE_NO_EXIST;
}

static zt_s32 mcu_get_buddy_fwstate(nic_info_st *pnic_info)
{
    nic_info_st *pvir_nic = pnic_info->buddy_nic;
    if (pnic_info == NULL)
    {
        return WIFI_FW_NO_EXIST;
    }
    if (pvir_nic == NULL)
    {
        return WIFI_FW_NO_EXIST;
    }

    return pvir_nic->nic_state;
}


static zt_s32 mcu_msg_sta_info_pars(wdn_net_info_st *wdn_net_info,
                                    mcu_msg_sta_info_st *msg_sta, zt_u8 sta)
{
    msg_sta->bUsed = sta;
    msg_sta->mac_id = wdn_net_info->wdn_id;

    zt_memcpy(msg_sta->hwaddr, wdn_net_info->mac, ZT_80211_MAC_ADDR_LEN);
    zt_memcpy(&msg_sta->htpriv, &wdn_net_info->htpriv.mcu_ht,
              sizeof(struct wdn_ht_priv));
    zt_memcpy(&msg_sta->htpriv.ht_cap, &wdn_net_info->ht_cap,
              sizeof(wdn_net_info->ht_cap));

    return 0;
}


static zt_s32 mcu_media_status_set(nic_info_st *nic_info, zt_bool opmode,
                                   zt_bool miracast, zt_bool miracast_sink, zt_u8 role,
                                   zt_u8 macid, zt_bool macid_ind, zt_u8 macid_end)
{
    zt_u32 buf[MEDIA_RPT_LEN + 2] = {0};
    zt_s32 ret = 0;

    buf[0] = MEDIA_RPT;
    buf[1] = MEDIA_RPT_LEN;
    buf[2] = opmode;
    buf[3] = macid_ind;
    buf[4] = miracast;
    buf[5] = miracast_sink;
    buf[6] = role;
    buf[7] = macid;
    buf[8] = macid_end;

    ret = mcu_cmd_communicate(nic_info, UMSG_OPS_HAL_CONTROL_ARS_CMD, buf,
                              MEDIA_RPT_LEN + 2, NULL, 0);

    return ret;
}

zt_s32 zt_mcu_media_connect_set(nic_info_st *nic_info,
                                wdn_net_info_st *wdn_net_info, zt_bool opmode)
{
    zt_s32 ret = 0;
    zt_u8 role;
    sys_work_mode_e mode = zt_local_cfg_get_work_mode(nic_info);

    switch (mode)
    {
        case ZT_INFRA_MODE:
            role = CONTROL_ARS_AP;
            break;
#ifdef CFG_ENABLE_AP_MODE
        case ZT_MASTER_MODE:
            role = CONTROL_ARS_STA;
            break;
#endif
#ifdef CFG_ENABLE_ADHOC_MODE
        case ZT_ADHOC_MODE:
            role = CONTROL_ARS_ADHOC;
            break;
#endif
        default:
            return -1;
    }

    ret = mcu_media_status_set(nic_info, opmode, zt_false, zt_false,
                               role, wdn_net_info->wdn_id, zt_false, 0);
    return ret;
}


static zt_s32 mcu_bit_value_from_ieee_value_to_get_func(zt_u8 val, zt_u8 flag)
{
    zt_u8 dot11_rate_table[] = { ZT_80211_CCK_RATE_1MB, ZT_80211_CCK_RATE_2MB,
                                 ZT_80211_CCK_RATE_5MB, ZT_80211_CCK_RATE_11MB,
                                 ZT_80211_OFDM_RATE_6MB, ZT_80211_OFDM_RATE_9MB,
                                 ZT_80211_OFDM_RATE_12MB, ZT_80211_OFDM_RATE_18MB,
                                 ZT_80211_OFDM_RATE_24MB, ZT_80211_OFDM_RATE_36MB,
                                 ZT_80211_OFDM_RATE_48MB, ZT_80211_OFDM_RATE_54MB, 0
                               };

    zt_s32 i = 0;
    if (flag)
    {
        while (dot11_rate_table[i] != 0)
        {
            if (dot11_rate_table[i] == val)
            {
                return ZT_BIT(i);
            }
            i++;
        }
    }
    return 0;
}


static zt_s32 mcu_set_rate_bitmap(nic_info_st *nic_info,
                                  wdn_net_info_st *wdn_net_info)
{
    zt_s32 ret = 0;
    zt_u32 buf[6] = {0};
    zt_u32 ra_mask = 0;
    zt_u8 sgi = 0;
    zt_s32 i = 0;
    zt_u32 rate_bitmap;
    hw_info_st *hw_info = (hw_info_st *) nic_info->hw_info;

    if (NULL == wdn_net_info)
    {
        LOG_E("[%s] param is null, check!!!", __func__);
        return ZT_RETURN_OK;
    }

    /*calc ra_mask*/
    for (i = 0; i < wdn_net_info->datarate_len; i++)
    {
        if (wdn_net_info->datarate[i])
        {
            ra_mask |= mcu_bit_value_from_ieee_value_to_get_func(wdn_net_info->datarate[i] &
                       0x7f, 1);
        }
    }

    for (i = 0; i < wdn_net_info->ext_datarate_len; i++)
    {
        if (wdn_net_info->ext_datarate[i])
        {
            ra_mask |= mcu_bit_value_from_ieee_value_to_get_func(
                           wdn_net_info->ext_datarate[i] & 0x7f, 1);
        }
    }

    for (i = 0; i < 8; i++)
    {
        if (wdn_net_info->ht_cap.supp_mcs_set[i / 8] & ZT_BIT(i % 8))
        {
            ra_mask |= ZT_BIT(i + 12);
        }
    }

    if (wdn_net_info->bw_mode == CHANNEL_WIDTH_40)
    {
        sgi = wdn_net_info->htpriv.mcu_ht.sgi_40m;
    }
    else
    {
        sgi = wdn_net_info->htpriv.mcu_ht.sgi_20m;
    }

    buf[0] = wdn_net_info->wdn_id;
    buf[1] = wdn_net_info->raid;
    buf[2] = wdn_net_info->bw_mode;
    buf[3] = sgi;
    buf[4] = ra_mask;
    buf[5] = hw_info->ars_policy;

    LOG_D("[%s] MacID:%d  RaID:%d  BW:%d  SGI:%d ra_mask: 0x%x retry_policy: 0x%x",
          __func__, buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);

    ret = mcu_cmd_communicate(nic_info, UMSG_OPS_MSG_SET_RATE_BITMAP, buf, 6,
                              &rate_bitmap, 1);
    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] failed", __func__);
        return ret;
    }
    else if (ZT_RETURN_CMD_BUSY == ret)
    {
        LOG_W("[%s] cmd busy, try again if need!", __func__);
        return ret;
    }

    LOG_D("[%s] Rate Bitmap:0x%x", __func__,  rate_bitmap);

    return ZT_RETURN_OK;
}

zt_s32 translate_percentage_to_dbm(zt_u32 SignalStrengthIndex)
{
    zt_s32 SignalPower;

    SignalPower = SignalStrengthIndex - 100;

    return SignalPower; /* in dBM.raw data */
}


zt_s32 signal_scale_mapping(zt_s32 current_sig)
{
    zt_s32 result_sig = 0;

    if (current_sig >= 51 && current_sig <= 100)
    {
        result_sig = 100;
    }
    else if (current_sig >= 41 && current_sig <= 50)
    {
        result_sig = 80 + ((current_sig - 40) * 2);
    }
    else if (current_sig >= 31 && current_sig <= 40)
    {
        result_sig = 66 + (current_sig - 30);
    }
    else if (current_sig >= 21 && current_sig <= 30)
    {
        result_sig = 54 + (current_sig - 20);
    }
    else if (current_sig >= 10 && current_sig <= 20)
    {
        result_sig = 42 + (((current_sig - 10) * 2) / 3);
    }
    else if (current_sig >= 5 && current_sig <= 9)
    {
        result_sig = 22 + (((current_sig - 5) * 3) / 2);
    }
    else if (current_sig >= 1 && current_sig <= 4)
    {
        result_sig = 6 + (((current_sig - 1) * 3) / 2);
    }
    else
    {
        result_sig = current_sig;
    }

    return result_sig;

}

zt_s32 zt_mcu_check_tx_buff(nic_info_st *nic_info, zt_u8 *rempty)
{
    zt_s32 ret = 0;
    union
    {
        zt_u32 value[3];
        struct
        {
            zt_u8 is_empty;
            zt_u32 reg_200;
            zt_u32 reg_204;
        };
    } r;

    ret = mcu_cmd_communicate(nic_info, UMSG_OPS_HAL_CHECK_TXBUFF_EMPTY,
                              NULL, 0, (void *)&r, ZT_ARRAY_SIZE(r.value));
    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] failed", __func__);
        return ret;
    }
    else if (ZT_RETURN_CMD_BUSY == ret)
    {
        LOG_W("[%s] cmd busy, try again if need!", __func__);
        return ret;
    }
    //    LOG_D("reg_200=%08x, reg_204=%08x", r.reg_200, r.reg_204);
    *rempty = r.is_empty;

    return 0;
}

//Currently a full power down will cause interface anomalies, but to meet power consumption requirements, only power down rf and bb
zt_s32 zt_mcu_power_off(nic_info_st *nic_info)
{
    zt_s32 ret;
    zt_u8 i;
    zt_u32 tmp = 0;
    zt_u32 rf_poweroff = ZT_BIT(2);
    zt_u32 bb_poweroff = ZT_BIT(1);
    tmp = rf_poweroff | bb_poweroff;
    LOG_D("power down start");
    if (!nic_info)
    {
        LOG_D("nic_info is null");
        return -1;
    }
    ret = mcu_cmd_communicate(nic_info, UMSG_OPS_POWER_OFF,
                              &tmp, 1, NULL, 0);
    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] failed", __func__);
        return ret;
    }
    else if (ZT_RETURN_CMD_BUSY == ret)
    {
        LOG_W("[%s] cmd busy, try again if need!", __func__);
        return ret;
    }

    return 0;
}

zt_s32 zt_mcu_set_macaddr(nic_info_st *nic_info, zt_u8 *val)
{
    zt_u8 idx = 0;
    zt_s32 ret = 0;
    zt_u32 var[7] = { 0 };

    var[0] = nic_info->nic_num;
    for (idx = 0; idx < 6; idx++)
    {
        var[idx + 1] = val[idx];
    }

    ret =
        mcu_cmd_communicate(nic_info, UMSG_OPS_HAL_SET_MAC, var, 7, NULL, 0);
    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] failed", __func__);
        return ret;
    }
    else if (ZT_RETURN_CMD_BUSY == ret)
    {
        LOG_W("[%s] cmd busy, try again if need!", __func__);
        return ret;
    }
    return 0;
}

zt_s32 zt_mcu_disable_fw_dbginfo(nic_info_st *pnic_info)
{
    zt_s32 ret = 0;
    zt_u32 inbuff[2] = {0, 0};

    ret = mcu_cmd_communicate(pnic_info, UMSG_OPS_HAL_DBGLOG_CONFIG, inbuff, 2,
                              NULL, 0);
    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] failed", __func__);
        return ret;
    }
    else if (ZT_RETURN_CMD_BUSY == ret)
    {
        LOG_W("[%s] cmd busy, try again if need!", __func__);
        return ret;
    }

    return 0;

}

/*************************************************
 * Function     : zt_mcu_get_chip_version
 * Description  : To get the version of this chip
 * Input        : nic_info
 * Output       : version
 * Return       : 1. ZT_RETURN_FAIL
                  2. ZT_RETURN_OK
 *************************************************/

zt_s32 zt_mcu_get_chip_version(nic_info_st *nic_info, zt_u32 *version)
{
    zt_s32 ret   = 0;
    zt_timer_t timeout;
    hif_node_st *hif_node = (hif_node_st *)nic_info->hif_node;
    zt_u32 chip_val = hif_node->drv_ops->driver_flag;

    zt_timer_set(&timeout, 2000);
    do
    {
        ret = mcu_cmd_communicate(nic_info, UMSG_OPS_READ_VERSION, &chip_val, 1,
                                  version, 1);
        if (ZT_RETURN_FAIL == ret)
        {
            LOG_W("[%s] failed, try again!", __func__);
        }
        else if (ZT_RETURN_CMD_BUSY == ret)
        {
            LOG_W("[%s] cmd busy, try again!", __func__);
        }
        else if (ZT_RETURN_OK == ret)
        {
            return ZT_RETURN_OK;
        }
        zt_msleep(1);
    } while (!zt_timer_expired(&timeout));

    return ZT_RETURN_OK;
}



zt_s32 zt_mcu_set_op_mode(nic_info_st *nic_info, zt_u32 mode)
{
    zt_s32 ret = 0;
    zt_u32 tmp[5] = { 0 };
    zt_bool bConnect;
    zt_u32 mlmeState;
    zt_u32 fwState;

    if ((nic_info->is_driver_stopped == zt_true) ||
            (nic_info->is_surprise_removed == zt_true))
    {
        return ZT_RETURN_FAIL;
    }

    zt_mlme_get_connect(nic_info, &bConnect);
    if (bConnect == zt_true)
    {
        mlmeState = FW_STATE_STATION;
        fwState = WIFI_STATION_STATE | WIFI_ASOC_STATE;
    }
    else
    {
        mlmeState = FW_STATE_NO_EXIST;
        fwState =  WIFI_FW_NO_EXIST;
    }

    switch (mode)
    {
        /* STA mode */
        case ZT_AUTO_MODE:
        case ZT_INFRA_MODE:
            tmp[0] = FW_STATE_STATION;
            break;

#ifdef CFG_ENABLE_ADHOC_MODE
        /* AdHoc mode */
        case ZT_ADHOC_MODE:
            tmp[0] = FW_STATE_ADHOC;
            break;
#endif

#ifdef CFG_ENABLE_AP_MODE
        /* AP mode */
        case ZT_MASTER_MODE:
            tmp[0] = FW_STATE_AP;
            break;
#endif

#ifdef CFG_ENABLE_MONITOR_MODE
        /* Sniffer mode */
        case ZT_MONITOR_MODE:
            tmp[0] = FW_STATE_MONITOR;
            tmp[4] = ZT_BIT(1) | ZT_BIT(2) | ZT_BIT(3) | ZT_BIT(29) |
                     ZT_BIT(13) | ZT_BIT(14) | ZT_BIT(30) | ZT_BIT(28);


            tmp[4] |= ZT_BIT(8);

            break;
#endif
        case ZT_REPEAT_MODE:
        case ZT_SECOND_MODES:
        case ZT_MESH_MODE:
        default:
        {
            LOG_E("Unsupport Mode!!");
            return ZT_RETURN_FAIL;
        }
        break;
    }

    tmp[1] = nic_info->nic_num; //iface: 0 or 1
    tmp[2] = mcu_get_buddy_mlmestate(nic_info); //get mlme state
    tmp[3] = mcu_get_buddy_fwstate(nic_info);
    tmp[4] = ZT_BIT(0) | ZT_BIT(2) | ZT_BIT(31);
    ret = mcu_cmd_communicate(nic_info, UMSG_OPS_HW_SET_OP_MODE, tmp, 5, NULL, 0);
    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] failed", __func__);
        return ret;
    }
    else if (ZT_RETURN_CMD_BUSY == ret)
    {
        LOG_W("[%s] cmd busy, try again if need!", __func__);
        return ret;
    }

    return ZT_RETURN_OK;

}


zt_s32 zt_mcu_set_ch_bw(nic_info_st *nic_info, zt_u32 *args, zt_u32 arg_len)
{
    zt_s32 ret = 0;

    ret = mcu_cmd_communicate(nic_info, UMSG_OPS_HAL_CHNLBW_MODE, args, arg_len,
                              NULL, 0);
    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] failed", __func__);
        return ret;
    }
    else if (ZT_RETURN_CMD_BUSY == ret)
    {
        LOG_W("[%s] cmd busy, try again if need!", __func__);
        return ret;
    }

    /*some kind of register operation*/
    //LOG_I("To do");

    return ZT_RETURN_OK;
}


zt_s32 zt_mcu_get_ch_bw(nic_info_st *nic_info, zt_u8 *channel,
                        CHANNEL_WIDTH *cw,
                        HAL_PRIME_CH_OFFSET *offset)
{
    zt_s32 ret = 0;
    zt_u32 value[3];

    ret = mcu_cmd_communicate(nic_info, UMSG_OPS_HAL_GET_CHNLBW_MODE, NULL, 0,
                              value, 3);
    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] failed", __func__);
        return ret;
    }
    else if (ZT_RETURN_CMD_BUSY == ret)
    {
        LOG_W("[%s] cmd busy, try again if need!", __func__);
        return ret;
    }

    *channel    = (zt_u8)value[0];
    *cw         = (CHANNEL_WIDTH)value[1];
    *offset     = (HAL_PRIME_CH_OFFSET)value[2];

    return ZT_RETURN_OK;
}

zt_s32 zt_mcu_reset_bb(nic_info_st *nic_info)
{
    zt_s32 ret = 0;
    zt_u32 arg;

    /* stop BB */
    arg = 1;
    ret = mcu_cmd_communicate(nic_info, UMSG_OPS_HAL_GATE_BB, &arg, 1, NULL, 0);
    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] failed", __func__);
        return ret;
    }
    else if (ZT_RETURN_CMD_BUSY == ret)
    {
        LOG_W("[%s] cmd busy, try again if need!", __func__);
        return ret;
    }

    /* start BB */
    arg = 0;
    ret = mcu_cmd_communicate(nic_info, UMSG_OPS_HAL_GATE_BB, &arg, 1, NULL, 0);
    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] failed", __func__);
        return ret;
    }
    else if (ZT_RETURN_CMD_BUSY == ret)
    {
        LOG_W("[%s] cmd busy, try again if need!", __func__);
        return ret;
    }

    return ZT_RETURN_OK;
}

zt_s32  zt_mcu_set_hw_reg(nic_info_st *nic_info, zt_u32 *value, zt_u32 len)
{
    zt_s32  ret = ZT_RETURN_OK;

    if (len > MAILBOX_MAX_TXLEN)
    {
        LOG_E("%s len = %d is bigger than MAILBOX_MAX_TXLEN(%d), check!!!! ", __func__,
              len, MAILBOX_MAX_TXLEN);
        return ZT_RETURN_FAIL;
    }

    ret = mcu_cmd_communicate(nic_info, UMSG_OPS_HAL_SET_HWREG, value, len, NULL,
                              0);
    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] failed", __func__);
        return ret;
    }
    else if (ZT_RETURN_CMD_BUSY == ret)
    {
        LOG_W("[%s] cmd busy, try again if need!", __func__);
        return ret;
    }

    return ret;
}


zt_s32 zt_mcu_set_hw_invalid_all(nic_info_st *nic_info)
{
    zt_u32 arg[2] = {0};
    hw_info_st *hw_info = (hw_info_st *)nic_info->hw_info;
    arg[0] = WLAN_HAL_VALUE_CAM_INVALID_ALL;
    arg[1] = 0;
    hw_info->hw_reg.cam_invalid = arg[1];

    return zt_mcu_set_hw_reg(nic_info, arg, 2);
}


zt_s32 zt_mcu_set_config_xmit(nic_info_st *nic_info, zt_s32 event, zt_u32 val)
{
    zt_s32 ret = 0;
    zt_u32 buf[2];
    buf[0] = event;
    buf[1] = val;

    ret = mcu_cmd_communicate(nic_info, UMSG_OPS_HAL_CONFIG_XMIT, buf, 2, NULL, 0);

    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] failed", __func__);
        return ret;
    }
    else if (ZT_RETURN_CMD_BUSY == ret)
    {
        LOG_W("[%s] cmd busy, try again if need!", __func__);
        return ret;
    }

    return ZT_RETURN_OK;
}

// use for control ARS
zt_s32  zt_mcu_set_user_info(nic_info_st *nic_info, zt_bool state)
{
    zt_u32 var;
    zt_s32 ret = 0;
    if (state)
    {
        var = 0x000000001;
    }
    else
    {
        var = 0x00000000;
    }

    if ((zt_mlme_check_mode(nic_info, ZT_INFRA_MODE) == zt_true))
    {
        var |= 0x000000008;
    }
#ifdef CFG_ENABLE_AP_MODE
    else if ((zt_mlme_check_mode(nic_info, ZT_MASTER_MODE) == zt_true))
    {
        var |= 0x000000010;
    }
#endif
#ifdef CFG_ENABLE_ADHOC_MODE
    else if (zt_mlme_check_mode(nic_info, ZT_ADHOC_MODE) == zt_true)
    {
        var |= 0x000000020;
    }
#endif
    else
    {
        LOG_E("[%s]:not support work mode", __func__);
        return ZT_RETURN_FAIL;
    }

    ret = mcu_cmd_communicate(nic_info, UMSG_OPS_MP_USER_INFO, &var, 1, NULL, 0);

    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] failed", __func__);
        return ret;
    }
    else if (ZT_RETURN_CMD_BUSY == ret)
    {
        LOG_W("[%s] cmd busy, try again if need!", __func__);
        return ret;
    }

    return ZT_RETURN_OK;
}



static zt_s32 hw_sta_obtain(nic_info_st *nic_info, zt_u32 *fw_sta,
                            zt_u32 *link_sta)
{
    sys_work_mode_e work_mode = zt_local_cfg_get_work_mode(nic_info);
    zt_u32 fwState;
    zt_bool bConnect;

#ifdef CFG_ENABLE_ADHOC_MODE
    if (work_mode == ZT_ADHOC_MODE)
    {
        fwState = WIFI_ADHOC_STATE | WIFI_ADHOC_MASTER_STATE;
    }
    else
#endif
        if (work_mode == ZT_INFRA_MODE)
        {
            fwState = WIFI_STATION_STATE | WIFI_SITE_MONITOR;
        }
#ifdef CFG_ENABLE_MONITOR_MODE
        else if (work_mode == ZT_MONITOR_MODE)
        {
            fwState = WIFI_SITE_MONITOR;
        }
#endif
#ifdef CFG_ENABLE_AP_MODE
        else if (work_mode == ZT_MASTER_MODE)
        {
            fwState = WIFI_AP_STATE;
        }
#endif
        else
        {
            LOG_E("unknow fw state!!!");
            return -1;
        }

    zt_mlme_get_connect(nic_info, &bConnect);
    if (bConnect == zt_true)
    {
        *fw_sta = WIFI_ASOC_STATE | fwState;
        *link_sta = MCU_LINKED;
    }
    else
    {
        *fw_sta = fwState;
        *link_sta = MCU_UNLINKED;
    }
#ifdef CFG_ENABLE_AP_MODE
    if (work_mode == ZT_MASTER_MODE)
    {
        *fw_sta = WIFI_ASOC_STATE | fwState;
        *link_sta = MCU_LINKED;
    }
#endif
    return 0;
}

zt_s32 zt_mcu_set_mlme_scan(nic_info_st *nic_info, zt_bool enable)
{
    nic_info_st *nic_real_info, *nic_vir_info;
    zt_u32 arg[7] = {0};
    zt_s32 ret = 0;

    if (nic_info == NULL)
    {
        return ZT_RETURN_FAIL;
    }

    if (nic_info->virNic)
    {
        nic_vir_info = nic_info;
        nic_real_info = nic_vir_info->buddy_nic;
    }
    else
    {
        nic_real_info = nic_info;
        nic_vir_info = nic_real_info->buddy_nic;
    }

    arg[0] = enable;
    arg[1] = nic_info->nic_num;
#ifdef CFG_ENABLE_AP_MODE
    arg[2] = zt_ap_get_num(nic_info);
#else
    arg[2] = 0;
#endif
    if (nic_real_info)
    {
        hw_sta_obtain(nic_real_info, &arg[3], &arg[4]);
        nic_real_info->nic_state = arg[3];
    }
    else
    {
        arg[3] = WIFI_FW_NO_EXIST;
        arg[4] = 0;
    }
    if (nic_vir_info)
    {
        hw_sta_obtain(nic_vir_info, &arg[5], &arg[6]);
        nic_vir_info->nic_state = arg[5];
    }
    else
    {
        arg[5] = WIFI_FW_NO_EXIST;
        arg[6] = 0;
    }

    ret = mcu_cmd_communicate(nic_info, UMSG_OPS_HW_SET_MLME_SITE, arg, 7, NULL, 0);
    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] failed", __func__);
        return ret;
    }
    else if (ZT_RETURN_CMD_BUSY == ret)
    {
        LOG_W("[%s] cmd busy, try again if need!", __func__);
        return ret;
    }

    return 0;
}




zt_s32 zt_mcu_set_mlme_join(nic_info_st *nic_info, zt_u8 type)
{
    zt_u32 param[5] = { 0 };
    zt_s32 ret = 0;
    zt_u32 mlmeState;
    zt_u32 fwState;
    sys_work_mode_e work_mode = zt_local_cfg_get_work_mode(nic_info);

#ifdef CFG_ENABLE_ADHOC_MODE
    if (work_mode == ZT_ADHOC_MODE)
    {
        mlmeState = FW_STATE_ADHOC;
        fwState = WIFI_ADHOC_STATE | WIFI_ADHOC_MASTER_STATE;
    }
    else
#endif
        if (work_mode == ZT_INFRA_MODE)
        {
            mlmeState = FW_STATE_STATION;
            fwState = WIFI_STATION_STATE | WIFI_ASOC_STATE;
        }
        else
        {
            LOG_E("unknow fw state!!!");
            return -1;
        }

    param[0] = type;
    param[1] = nic_info->nic_num;  //iface0 or iface1
    param[2] = fwState;
    param[3] = mcu_get_buddy_mlmestate(nic_info);
    param[4] = mcu_get_buddy_fwstate(nic_info);

    ret = mcu_cmd_communicate(nic_info, UMSG_OPS_HW_SET_MLME_JOIN, param, 5, NULL,
                              0);
    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] failed", __func__);
        return ret;
    }
    else if (ZT_RETURN_CMD_BUSY == ret)
    {
        LOG_W("[%s] cmd busy, try again if need!", __func__);
        return ret;
    }

    return 0;
}




zt_s32 zt_mcu_set_bssid(nic_info_st *nic_info, zt_u8 *bssid)
{
    zt_u8 idx = 0;
    zt_u32 var[7] = { 0 };
    zt_s32 ret = 0;

    var[0] = nic_info->nic_num;

    if (bssid != NULL)
    {
        for (idx = 0; idx < 6; idx++)
        {
            var[idx + 1] = bssid[idx];
        }
    }
    else
    {
        for (idx = 0; idx < 6; idx++)
        {
            var[idx + 1] = 0;
        }
    }

    ret = mcu_cmd_communicate(nic_info, UMSG_OPS_HAL_SET_BSSID, var, 7, NULL, 0);
    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] failed", __func__);
        return ret;
    }
    else if (ZT_RETURN_CMD_BUSY == ret)
    {
        LOG_W("[%s] cmd busy, try again if need!", __func__);
        return ret;
    }

    return 0;
}



zt_s32 zt_mcu_set_sifs(nic_info_st *nic_info)
{
    zt_u32 arg;

    arg = WLAN_HAL_VALUE_RESP_SIFS;

    return zt_mcu_set_hw_reg(nic_info, &arg, 1);
}

zt_s32 zt_mcu_set_basic_rate(nic_info_st *nic_info, zt_u16 br_cfg)
{
    zt_s32 ret = 0;
    zt_u32 BrateCfg;
    zt_u16 rrsr_2g_force_mask = ZT_80211_CCK_RATE_1MB_MASK |
                                ZT_80211_CCK_RATE_2MB_MASK |
                                ZT_80211_CCK_RATE_5MB_MASK |
                                ZT_80211_CCK_RATE_11MB_MASK;
    zt_u16 rrsr_2g_allow_mask = ZT_80211_OFDM_RATE_24MB_MASK |
                                ZT_80211_OFDM_RATE_12MB_MASK |
                                ZT_80211_OFDM_RATE_6MB_MASK |
                                rrsr_2g_force_mask;

    BrateCfg = rrsr_2g_force_mask | br_cfg;
    BrateCfg &= rrsr_2g_allow_mask;

    LOG_D("[%s] br_cfg = 0x%x", __func__, br_cfg);

    ret = mcu_cmd_communicate(nic_info, UMSG_OPS_HW_SET_BASIC_RATE, &BrateCfg, 1,
                              NULL, 0);
    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] failed", __func__);
        return ret;
    }
    else if (ZT_RETURN_CMD_BUSY == ret)
    {
        LOG_W("[%s] cmd busy, try again if need!", __func__);
        return ret;
    }

    return ZT_RETURN_OK;
}


zt_s32 zt_mcu_set_preamble(nic_info_st *nic_info, zt_u8 short_val)
{
    zt_u32 arg[2] = {0};

    arg[0] = WLAN_HAL_VALUE_ACK_PREAMBLE;
    arg[1] = short_val;

    return zt_mcu_set_hw_reg(nic_info, arg, 2);
}

zt_s32 zt_mcu_set_wmm_para_disable(nic_info_st *nic_info,
                                   wdn_net_info_st *wdn_info)
{
    zt_u8 AIFS, ECWMin, ECWMax, aSifsTime;
    zt_u32 acParm;
    zt_u16 TXOP;
    zt_u32 arg[2] = {0};

    if (wdn_info->network_type & WIRELESS_11_24N)
    {
        aSifsTime = 16;
    }
    else
    {
        aSifsTime = 10;
    }
    AIFS = aSifsTime + (2 * SHORT_SLOT_TIME);
    ECWMax = 10;
    ECWMin = 4;
    TXOP = 0;
    acParm = AIFS | (ECWMin << 8) | (ECWMax << 12) | (TXOP << 16);

    arg[0] = WLAN_HAL_VALUE_AC_PARAM_BE;
    arg[1] = acParm;
    if (zt_mcu_set_hw_reg(nic_info, arg, 2) != 0)
    {
        return -1;
    }

    arg[0] = WLAN_HAL_VALUE_AC_PARAM_BK;
    if (zt_mcu_set_hw_reg(nic_info, arg, 2) != 0)
    {
        return -1;
    }

    arg[0] = WLAN_HAL_VALUE_AC_PARAM_VI;
    if (zt_mcu_set_hw_reg(nic_info, arg, 2) != 0)
    {
        return -1;
    }

    TXOP = 0x2f;
    ECWMax = 3;
    ECWMin = 2;
    acParm = AIFS | (ECWMin << 8) | (ECWMax << 12) | (TXOP << 16);
    arg[0] = WLAN_HAL_VALUE_AC_PARAM_VO;
    arg[1] = acParm;
    if (zt_mcu_set_hw_reg(nic_info, arg, 2) != 0)
    {
        return -1;
    }
    return 0;

}

zt_s32 zt_mcu_set_wmm_para_enable(nic_info_st *nic_info,
                                  wdn_net_info_st *wdn_info)
{
    zt_u8 ACI, ACM, AIFS, ECWMin, ECWMax, aSifsTime;
    zt_u16 TXOP;
    zt_u32 acParm;
    zt_s32 i = 0;
    zt_wmm_para_st *wmm_info = &wdn_info->wmm_info;
    zt_u32 arg[2] = {0};

    wdn_info->acm_mask = 0;
    if (wdn_info->network_type & WIRELESS_11_24N)
    {
        aSifsTime = 16;
    }
    else
    {
        aSifsTime = 10;
    }
    for (i = 0 ; i < 4; i++)
    {
        ACI = (wmm_info->ac[i].ACI >> 5) & 0x03;
        ACM = (wmm_info->ac[i].ACI >> 4) & 0x01;
        AIFS = (wmm_info->ac[i].ACI & 0x0f) * SHORT_SLOT_TIME + aSifsTime;

        ECWMin = (wmm_info->ac[i].ECW & 0x0f);
        ECWMax = (wmm_info->ac[i].ECW & 0xf0) >> 4;
        TXOP = zt_le16_to_cpu(wmm_info->ac[i].TXOP_limit);

        aSifsTime = 16;
        acParm = AIFS | (ECWMin << 8) | (ECWMax << 12) | (TXOP << 16);
        switch (ACI)
        {
            case 0x00:
                arg[0] = WLAN_HAL_VALUE_AC_PARAM_BE;
                wdn_info->acm_mask |= (ACM ? ZT_BIT(1) : 0);
                break;
            case 0x01:
                arg[0] = WLAN_HAL_VALUE_AC_PARAM_BK;
                break;
            case 0x02:
                arg[0] = WLAN_HAL_VALUE_AC_PARAM_VI;
                wdn_info->acm_mask |= (ACM ? ZT_BIT(2) : 0);
                break;
            case 0x03:
                arg[0] = WLAN_HAL_VALUE_AC_PARAM_VO;
                wdn_info->acm_mask |= (ACM ? ZT_BIT(3) : 0);
                break;
        }

        arg[1] = acParm;

        zt_mcu_set_hw_reg(nic_info, arg, 2);

        LOG_D("acParm:0x%x   acm_mask:0x%x", acParm, wdn_info->acm_mask);
    }

    return 0;
}

#ifdef CONFIG_MP_MODE
zt_s32 zt_mcu_set_usb_agg_normal(nic_info_st *nic_info, zt_u8 cur_wireless_mode)
{
    zt_s32 ret = ZT_RETURN_FAIL;
    zt_u32 mbox1[1] = { 0 };

    if (NIC_USB != nic_info->nic_type)
    {
        return ret;
    }

    mbox1[0] = cur_wireless_mode;
    ret = mcu_cmd_communicate(nic_info, UMSG_OPS_HAL_SET_USB_AGG_NORMAL, mbox1, 1,
                              NULL, 0);
    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] failed", __func__);
        return ret;
    }
    else if (ZT_RETURN_CMD_BUSY == ret)
    {
        LOG_W("[%s] cmd busy, try again if need!", __func__);
        return ret;
    }
    return ZT_RETURN_OK;
}
#endif

zt_s32 zt_mcu_set_bcn_intv(nic_info_st *nic_info, zt_u16 val)
{
    zt_u32 arg[2] = {0};

    arg[0] = WLAN_HAL_VALUE_BEACON_INTERVAL;
    arg[1] = val;
    return zt_mcu_set_hw_reg(nic_info, arg, 2);
}


zt_s32 zt_mcu_set_slot_time(nic_info_st *nic_info, zt_u32 slotTime)
{
    zt_u32 arg[2] = {0};

    arg[0] = WLAN_HAL_VALUE_SLOT_TIME;
    arg[1] = slotTime;

    return zt_mcu_set_hw_reg(nic_info, arg, 2);
}

zt_s32 zt_mcu_set_media_status(nic_info_st *nic_info, zt_u32 status)
{
    zt_u32 arg[2] = {0};

    if (nic_info->nic_num == 1)
    {
        arg[0] = WLAN_HAL_VALUE_MEDIA_STATUS1;
    }
    else
    {
        arg[0] = WLAN_HAL_VALUE_MEDIA_STATUS;
    }

    arg[1] = status;

    return zt_mcu_set_hw_reg(nic_info, arg, 2);
}


zt_s32 zt_mcu_cca_config(nic_info_st *nic_info)
{
    zt_s32 ret = 0;

    if (NIC_USB == nic_info->nic_type)
    {
        //do nothing
    }
    else
    {
        ret = mcu_cmd_communicate(nic_info, UMSG_OPS_HAL_CCA_CONFIG, NULL, 0, NULL, 0);
    }

    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] failed", __func__);
        return ret;
    }
    else if (ZT_RETURN_CMD_BUSY == ret)
    {
        LOG_W("[%s] cmd busy, try again if need!", __func__);
        return ret;
    }

    return ZT_RETURN_OK;
}


zt_s32 zt_mcu_set_on_rcr_am(nic_info_st *nic_info, zt_bool on_off)
{
    zt_u32 buf[2];

    buf[0] = (zt_u32)on_off ? WLAN_HAL_VALUE_ON_RCR_AM : WLAN_HAL_VALUE_OFF_RCR_AM;
    buf[1] = 0;
    zt_mcu_set_hw_reg(nic_info, buf, 2);

    return ZT_RETURN_OK;
}

zt_s32 zt_mcu_set_dk_cfg(nic_info_st *nic_info, zt_u32 auth_algrthm,
                         zt_bool dk_en)
{
    zt_u32 buf[2];
    zt_s32 ret = 0;
    buf[0] = dk_en;
    buf[1] = auth_algrthm;
    ret = mcu_cmd_communicate(nic_info, UMSG_OPS_HW_SET_DK_CFG, buf, 2, NULL, 0);
    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] failed", __func__);
        return ret;
    }
    else if (ZT_RETURN_CMD_BUSY == ret)
    {
        LOG_W("[%s] cmd busy, try again if need!", __func__);
        return ret;
    }

    return ZT_RETURN_OK;
}

zt_s32 zt_mcu_set_sec_cfg(nic_info_st *nic_info, zt_u8 val)
{
    zt_u32 buf[5];

    buf[0] = WLAN_HAL_VALUE_SEC_CFG;
    buf[1] = zt_false;
    buf[2] = zt_false;
    buf[3] = val;
    buf[4] = zt_true;
    zt_mcu_set_hw_reg(nic_info, buf, 5);

    return ZT_RETURN_OK;
}

zt_s32 zt_mcu_set_sec_cam(nic_info_st *nic_info, struct cam_param *pcam_param)
{
    zt_s32 ret = 0;
    zt_s32 i   = 0;
    zt_u32 buff[ZT_SECURITY_CAM_SIZE] = {0};

    buff[0] = pcam_param->cam_id;
    buff[1] = pcam_param->privacy;
    buff[2] = pcam_param->keyid;
    buff[3] = pcam_param->is_sta;
    buff[4] = pcam_param->is_group;
    buff[5] = pcam_param->is_clean;

    for (i = 0; i < ZT_80211_MAC_ADDR_LEN; i++)
    {
        buff[i + 6] = pcam_param->macaddr[i];
    }

    for (i = 0; i < ZT_SECURITY_KEY_SIZE; i++)
    {
        buff[i + 6 + ZT_80211_MAC_ADDR_LEN] = pcam_param->key[i];
    }

    ret = mcu_cmd_communicate(nic_info, UMSG_OPS_HAL_SEC_WRITE_CAM,
                              buff, ZT_SECURITY_CAM_SIZE, NULL, 0);

    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] failed", __func__);
        return ret;
    }
    else if (ZT_RETURN_CMD_BUSY == ret)
    {
        LOG_W("[%s] cmd busy, try again if need!", __func__);
        return ret;
    }

    return ZT_RETURN_OK;
}

zt_s32 zt_mcu_set_max_ampdu_len(nic_info_st *pnic_info, zt_u8 max_ampdu_len)
{
    zt_u32 arg[2] = {0};

    arg[0] = WLAN_HAL_VALUE_AMPDU_FACTIONOR;
    arg[1] = max_ampdu_len;

    return zt_mcu_set_hw_reg(pnic_info, arg, 2);
}


zt_s32 zt_mcu_set_agg_param(nic_info_st *nic_info, zt_u8 agg_size,
                            zt_u8 agg_timeout, zt_u8 agg_dma_enable, zt_u8 agg_dma_mode)
{
    zt_s32 ret = ZT_RETURN_FAIL;
    zt_u32 mbox[4] = { 0 };

    mbox[0] = agg_size;
    mbox[1] = agg_timeout;
    mbox[2] = agg_dma_enable;
    mbox[3] = agg_dma_mode;
    ret = mcu_cmd_communicate(nic_info, UMSG_OPS_HAL_SET_USB_AGG_CUSTOMER, mbox, 4,
                              NULL, 0);
    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] failed", __func__);
        return ret;
    }
    else if (ZT_RETURN_CMD_BUSY == ret)
    {
        LOG_W("[%s] cmd busy, try again if need!", __func__);
        return ret;
    }

    return ZT_RETURN_OK;
}


zt_s32 zt_mcu_reset_chip(nic_info_st *nic_info)
{
    zt_s32 ret = 0;
    if (NIC_USB == nic_info->nic_type)
    {
        ret =  mcu_cmd_communicate(nic_info, UMSG_OPS_RESET_CHIP, NULL, 0, NULL, 0);
    }
    else
    {
        return 0;
    }

    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] failed", __func__);
        return ret;
    }
    else if (ZT_RETURN_CMD_BUSY == ret)
    {
        LOG_W("[%s] cmd busy, try again if need!", __func__);
        return ret;
    }

    return 0;
}

zt_s32 zt_mcu_set_ac_vo(nic_info_st *pnic_info)
{
    zt_u32 acparm;
    zt_u32 arg[2] = {0};

    if (pnic_info->buddy_nic)
    {
        acparm = 1;
    }
    else
    {
        acparm = 0;
    }

    arg[0] = WLAN_HAL_VALUE_AC_PARAM_VO;
    arg[1] = acparm;

    return zt_mcu_set_hw_reg(pnic_info, arg, 2);
}

zt_s32 zt_mcu_set_ac_vi(nic_info_st *pnic_info)
{
    zt_u32 arg = 0;

    arg = WLAN_HAL_VALUE_AC_PARAM_VI;

    return zt_mcu_set_hw_reg(pnic_info, &arg, 1);
}

zt_s32 zt_mcu_set_ac_be(nic_info_st *pnic_info)
{
    zt_u32 arg = 0;

    arg = WLAN_HAL_VALUE_AC_PARAM_BE;

    return zt_mcu_set_hw_reg(pnic_info, &arg, 1);
}

zt_s32 zt_mcu_set_ac_bk(nic_info_st *pnic_info)
{
    zt_u32 arg = 0;

    arg = WLAN_HAL_VALUE_AC_PARAM_BK;


    return zt_mcu_set_hw_reg(pnic_info, &arg, 1);
}

zt_s32 zt_mcu_set_bcn_queue(nic_info_st *pnic_info, zt_bool bcn_que_on)
{
    zt_s32 ret = 0;
    zt_u32 arg[2] = {0};

    arg[0] = bcn_que_on;
    arg[1] = pnic_info->nic_num;

    ret = mcu_cmd_communicate(pnic_info, UMSG_OPS_HAL_SET_BCN, arg, 2, NULL, 0);
    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] failed", __func__);
        return ret;
    }
    else if (ZT_RETURN_CMD_BUSY == ret)
    {
        LOG_W("[%s] cmd busy, try again if need!", __func__);
        return ret;
    }

    return 0;
}

zt_s32 zt_mcu_set_bcn_valid(nic_info_st *pnic_info)
{
    zt_u32 arg[2] = {0};

    arg[0] = WLAN_HAL_VALUE_BCN_VALID;
    arg[1] = pnic_info->nic_num;

    return  zt_mcu_set_hw_reg(pnic_info, arg, 2);
}

zt_s32 zt_mcu_get_bcn_valid(nic_info_st *pnic_info, zt_u32 *val32)
{
    zt_s32 ret = 0;

    zt_u32 arg[1] = {0};

    if (pnic_info->nic_num == 1)
    {
        arg[0] = WLAN_HAL_VALUE_BCN_VALID1;
    }
    else
    {
        arg[0] = WLAN_HAL_VALUE_BCN_VALID;
    }

    ret = mcu_cmd_communicate(pnic_info, UMSG_OPS_HAL_GET_HWREG, arg, 1, val32, 1);
    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] failed", __func__);
        return ret;
    }
    else if (ZT_RETURN_CMD_BUSY == ret)
    {
        LOG_W("[%s] cmd busy, try again if need!", __func__);
        return ret;
    }

    return 0;
}


zt_s32 zt_mcu_set_bcn_sel(nic_info_st *pnic_info)
{
    zt_u32 arg[2] = {0};

    arg[0] = WLAN_HAL_VALUE_DL_BCN_SEL;
    arg[1] = pnic_info->ndev_id;

    return zt_mcu_set_hw_reg(pnic_info, arg, 2);
}

zt_s32 zt_mcu_update_thermal(nic_info_st *nic_info)
{
    zt_s32 ret = 0;
    if (NIC_USB == nic_info->nic_type)
    {
        ret = mcu_cmd_communicate(nic_info, UMSG_OPS_HAL_UPDATE_THERMAL, NULL, 0, NULL,
                                  0);
    }
    else
    {
        // todo
    }

    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] failed", __func__);
        return ret;
    }
    else if (ZT_RETURN_CMD_BUSY == ret)
    {
        LOG_W("[%s] cmd busy, try again if need!", __func__);
        return ret;
    }

    return ZT_RETURN_OK;
}

zt_s32 zt_mcu_handle_rf_lck_calibrate(nic_info_st *nic_info)
{
    zt_s32 ret          = 0;
    zt_u32 outbuf;

    ret = mcu_cmd_communicate(nic_info, UMSG_OPS_HAL_CALI_LLC, NULL, 0, &outbuf, 1);
    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] failed", __func__);
        return ret;
    }
    else if (ZT_RETURN_CMD_BUSY == ret)
    {
        LOG_W("[%s] cmd busy, try again if need!", __func__);
        return ret;
    }

    if (outbuf == 0)
    {
        LOG_E("LOCK FAIL");
    }
    else if (outbuf == 1)
    {
        LOG_D("LOCK success");
    }

    return ZT_RETURN_OK;
}

zt_s32  zt_mcu_handle_rf_iq_calibrate(nic_info_st *nic_info, zt_u8 channel)
{
    zt_s32 ret = 0;
    zt_u32 buff[2] = { 0 };
    zt_s32 len = 2;

    buff[0] = 0;
    buff[1] = channel;

    ret = mcu_cmd_communicate(nic_info, UMSG_OPS_HAL_PHY_IQ_CALIBRATE, buff, len,
                              NULL,  0);
    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] failed", __func__);
        return ret;
    }
    else if (ZT_RETURN_CMD_BUSY == ret)
    {
        LOG_W("[%s] cmd busy, try again if need!", __func__);
        return ret;
    }

    return ZT_RETURN_OK;
}




zt_s32 zt_mcu_msg_body_get(nic_info_st *nic_info, mcu_msg_body_st *mcu_msg)
{
    zt_s32 ret = 0;
    ret = mcu_cmd_communicate(nic_info, UMSG_OPS_HAL_READVAR_MSG, NULL, 0,
                              (zt_u32 *)mcu_msg, sizeof(mcu_msg_body_st) / 4);
    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] failed", __func__);
        return ret;
    }
    else if (ZT_RETURN_CMD_BUSY == ret)
    {
        LOG_W("[%s] cmd busy, try again if need!", __func__);
        return ret;
    }

    return ZT_RETURN_OK;
}

zt_s32 zt_mcu_msg_body_set(nic_info_st *nic_info, mcu_msg_body_st *mcu_msg)
{
    zt_s32 ret = 0;

    ret = mcu_cmd_communicate(nic_info, UMSG_OPS_HAL_WRITEVAR_MSG,
                              (zt_u32 *) mcu_msg, sizeof(mcu_msg_body_st) / 4, NULL, 0);
    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] failed", __func__);
        return ret;
    }
    else if (ZT_RETURN_CMD_BUSY == ret)
    {
        LOG_W("[%s] cmd busy, try again if need!", __func__);
        return ret;
    }

    return ZT_RETURN_OK;
}

zt_s32 zt_mcu_msg_body_sync(nic_info_st *nic_info, MSG_BODY_VARIABLE ops,
                            zt_u32 val)
{
    mcu_msg_body_st mcu_msg;

    zt_mcu_msg_body_get(nic_info, &mcu_msg);
    switch (ops)
    {
        case HAL_MSG_STA_INFO:
        {
            break;
        }
        case HAL_MSG_P2P_STATE:
        {
            mcu_msg.wifi_direct = val;
            break;
        }
        case HAL_MSG_WIFI_DISPLAY_STATE:
        {
            mcu_msg.wifi_display = val;
            break;
        }
        default:
        {
            break;
        }
    }

    zt_mcu_msg_body_set(nic_info, &mcu_msg);

    return ZT_RETURN_OK;
}


zt_s32 zt_mcu_msg_sta_info_set(nic_info_st *nic_info,
                               wdn_net_info_st *wdn_net_info,
                               zt_u8 sta)
{
    zt_s32 ret = 0;
    zt_u32 *pbuf  = NULL;
    zt_s32 len = 0;

    len = ZT_RND4(sizeof(mcu_msg_sta_info_st));

    pbuf = (zt_u32 *) zt_kzalloc(len);
    if (!pbuf)
    {
        LOG_E("[%s] failed", __func__);
        return ZT_RETURN_FAIL;
    }

    mcu_msg_sta_info_pars(wdn_net_info, (void *)pbuf, sta);

    ret = mcu_cmd_communicate(nic_info, UMSG_OPS_HAL_SYNC_MSG_STA_INFO, pbuf,
                              len / 4, NULL, 0);
    zt_kfree(pbuf);
    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] failed", __func__);
        return ret;
    }
    else if (ZT_RETURN_CMD_BUSY == ret)
    {
        LOG_W("[%s] cmd busy, try again if need!", __func__);
        return ret;
    }

    return ZT_RETURN_OK;
}


zt_s32 zt_mcu_rate_table_update(nic_info_st *nic_info,
                                wdn_net_info_st *wdn_net_info)
{
    zt_s32 ret = 0;

    ret = zt_mcu_msg_sta_info_set(nic_info, wdn_net_info, zt_true);
    if (ret == ZT_RETURN_FAIL)
    {
        return ret;
    }

    ret = mcu_set_rate_bitmap(nic_info, wdn_net_info);
    if (ret == ZT_RETURN_FAIL)
    {
        return ret;
    }

    ret = zt_mcu_media_connect_set(nic_info, wdn_net_info, zt_true);
    if (ret == ZT_RETURN_FAIL)
    {
        return ret;
    }

    return ZT_RETURN_OK;
}

zt_s32 zt_mcu_rf_power_init(nic_info_st *nic_info)
{
    zt_s32 ret = 0;
    local_info_st *local_info = nic_info->local_info;
    hif_node_st *hif_node = (hif_node_st *)nic_info->hif_node;

    zt_u8 rf_pwr_ofs =
        local_info->rf_power < E_RADIO_POWER_LEVEL_MAX ?
        local_info->rf_power : E_RADIO_POWER_LEVEL_M;

    union
    {
        zt_u32 dw[6];
        struct
        {
            zt_u8 ofs[20];
            zt_u8 cal;
        };
    } pwr[2][E_RADIO_POWER_LEVEL_MAX] =
    {
        {
            /*
            |-mode-|-------------radio power offset----------|
            |   b  | 1M 2M 5.5M 11M                          |
            |   g  | 6M 9M  12M 18M 24M 36M 48M 54M          |
            |   n  | mcs0 mcs1 mcs2 mcs3 mcs4 mcs5 mcs6 mcs7 |
            */
            [E_RADIO_POWER_LEVEL_L] =
            {
                .ofs =
                {
                    0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                },
                .cal = local_info->vco_cur,
            },
            [E_RADIO_POWER_LEVEL_M] =
            {
                .ofs =
                {
                    0x0a, 0x0a, 0x0a, 0x0a,
                    0x0e, 0x0e, 0x0e, 0x0c, 0x0a, 0x08, 0x02, 0x00,
                    0x0e, 0x0e, 0x0c, 0x0a, 0x08, 0x04, 0x02, 0x00,
                },
                .cal = local_info->vco_cur,
            },
            [E_RADIO_POWER_LEVEL_H] =
            {
                .ofs =
                {
                    0x0a, 0x0a, 0x0a, 0x0a,
                    0x0e, 0x0e, 0x0e, 0x0c, 0x0a, 0x08, 0x02, 0x00,
                    0x0e, 0x0e, 0x0c, 0x0a, 0x08, 0x04, 0x02, 0x00,
                },
                .cal = local_info->vco_cur,
            }
        },
        {
            /*
            |-mode-|-------------radio power offset----------|
            |   b  | 1M 2M 5.5M 11M                          |
            |   g  | 6M 9M  12M 18M 24M 36M 48M 54M          |
            |   n  | mcs0 mcs1 mcs2 mcs3 mcs4 mcs5 mcs6 mcs7 |
            */
            [E_RADIO_POWER_LEVEL_L] =
            {
                .ofs =
                {
                    0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                },
                .cal = 0xff,
            },
            [E_RADIO_POWER_LEVEL_M] =
            {
                .ofs =
                {
                    0x0a, 0x0a, 0x0a, 0x0a,
                    0x0e, 0x0e, 0x0e, 0x0c, 0x0a, 0x08, 0x02, 0x00,
                    0x0e, 0x0e, 0x0c, 0x0a, 0x08, 0x04, 0x02, 0x00,
                },
                .cal = 0xff,
            },
            [E_RADIO_POWER_LEVEL_H] =
            {
                .ofs =
                {
                    0x0e, 0x0e, 0x0c, 0x0c,
                    0x0e, 0x0e, 0x0a, 0x0a, 0x08, 0x08, 0x06, 0x06,
                    0x0e, 0x0e, 0x0a, 0x0a, 0x08, 0x08, 0x06, 0x06,
                },
                .cal = 0xff,
            }
        }
    };

    if (hif_node->drv_ops->driver_flag > 1) {
        LOG_E("[%s]: drv_ops->driver_flag error", __func__);
        return 0;
    }

    ret = mcu_cmd_communicate(nic_info, UMSG_OPS_HAL_RF_PWR,
                              pwr[hif_node->drv_ops->driver_flag][rf_pwr_ofs].dw,
                              ARRAY_SIZE(pwr[hif_node->drv_ops->driver_flag][rf_pwr_ofs].dw),
                              NULL, 0);

    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] failed", __func__);
        return ret;
    }
    else if (ZT_RETURN_CMD_BUSY == ret)
    {
        LOG_W("[%s] cmd busy, try again if need!", __func__);
        return ret;
    }

    return ZT_RETURN_OK;
}

zt_s32 zt_mcu_hw_init(nic_info_st *nic_info, hw_param_st *param)
{
    zt_s32 ret = 0;
    zt_u32 arg[9] = {0};

    arg[0] = param->work_mode;
    arg[1] = param->mac_addr[0];
    arg[2] = param->mac_addr[1];
    arg[3] = param->mac_addr[2];
    arg[4] = param->mac_addr[3];
    arg[5] = param->mac_addr[4];
    arg[6] = param->mac_addr[5];
    arg[7] = param->concurrent_mode;
    arg[8] = param->rx_agg_enable;

    ret = mcu_cmd_communicate(nic_info, UMSG_OPS_HAL_FW_INIT, arg, 9,
                              NULL, 0);

    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] failed", __func__);
        return ret;
    }
    else if (ZT_RETURN_CMD_BUSY == ret)
    {
        LOG_W("[%s] cmd busy, try again if need!", __func__);
        return ret;
    }

    return ZT_RETURN_OK;
}

zt_s32 zt_mcu_ars_init(nic_info_st *nic_info)
{
    zt_s32 ret = 0;
    hw_info_st *hw_info = nic_info->hw_info;
    ret = mcu_cmd_communicate(nic_info, UMSG_OPS_HAL_ARS_INIT,
                              (zt_u32 *)&hw_info->Regulation2_4G, 1, NULL, 0);
    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] failed", __func__);
        return ret;
    }
    else if (ZT_RETURN_CMD_BUSY == ret)
    {
        LOG_W("[%s] cmd busy, try again if need!", __func__);
        return ret;
    }

    return 0;
}

zt_s32 zt_mcu_ars_switch(nic_info_st *nic_info, zt_u32 open)
{
    zt_s32 ret = 0;
    ret = mcu_cmd_communicate(nic_info, UMSG_OPS_ARS_SWITCH,
                              (zt_u32 *)&open, 1, NULL, 0);
    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] failed", __func__);
        return ret;
    }
    else if (ZT_RETURN_CMD_BUSY == ret)
    {
        LOG_W("[%s] cmd busy, try again if need!", __func__);
        return ret;
    }

    return 0;
}

zt_s32 zt_mcu_periodic_lck_switch(nic_info_st *nic_info, zt_u32 open)
{
    zt_s32 ret = 0;
    ret = mcu_cmd_communicate(nic_info, UMSG_OPS_PERIODIC_LCK_SWITCH,
                              (zt_u32 *)&open, 1, NULL, 0);
    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] failed", __func__);
        return ret;
    }
    else if (ZT_RETURN_CMD_BUSY == ret)
    {
        LOG_W("[%s] cmd busy, try again if need!", __func__);
        return ret;
    }

    return 0;
}

#ifdef CONFIG_LPS
zt_s32 zt_mcu_set_lps_opt(nic_info_st *pnic_info, zt_u32 data)
{
    zt_u32 arg[1];
    zt_u32 val;
    zt_s32 ret = 0;

    if (data == 0)
    {
        arg[0] = data;
        ret = mcu_cmd_communicate(pnic_info, UMSG_OPS_HAL_LPS_OPT, arg, 1, &val, 1);
    }
    else
    {
        arg[0] = data;
        ret = mcu_cmd_communicate(pnic_info, UMSG_OPS_HAL_LPS_OPT, arg, 1, NULL, 0);
    }


    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] UMSG_OPS_HAL_LPS_OPT failed", __func__);
        return ZT_RETURN_FAIL;
    }

    return ZT_RETURN_OK;

}

zt_s32 zt_mcu_set_lps_config(nic_info_st *pnic_info)
{
    zt_u32 arg[2];
    pwr_info_st *pwr_info = pnic_info->pwr_info;
    zt_s32 ret = 0;

    arg[0] = pwr_info->smart_lps;
    arg[1] = pwr_info->pwr_mgnt;

    ret = mcu_cmd_communicate(pnic_info, UMSG_OPS_HAL_LPS_CONFIG, arg, 2, NULL, 0);
    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] failed", __func__);
        return ret;
    }
    else if (ZT_RETURN_CMD_BUSY == ret)
    {
        LOG_W("[%s] cmd busy, try again if need!", __func__);
        return ret;
    }

    return 0;
}

zt_s32 zt_mcu_set_fw_lps_config(nic_info_st *pnic_info)
{
    zt_u32 aid = 0;
    zt_wlan_mgmt_info_t *wlan_mgmt_info = (zt_wlan_mgmt_info_t *)
                                          pnic_info->wlan_mgmt_info;
    zt_wlan_network_t *cur_network = &(wlan_mgmt_info->cur_network);
    zt_s32 ret = 0;

    aid = (zt_u32)cur_network->aid;

    ret = mcu_cmd_communicate(pnic_info, UMSG_OPS_HAL_LPS_SET, &aid, 1, NULL, 0);
    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] failed", __func__);
        return ret;
    }
    else if (ZT_RETURN_CMD_BUSY == ret)
    {
        LOG_W("[%s] cmd busy, try again if need!", __func__);
        return ret;
    }

    return 0;
}

zt_s32 zt_mcu_set_fw_lps_get(nic_info_st *pnic_info)
{
    zt_s32 ret = ZT_RETURN_OK;
    zt_u32 arg[1];

    arg[0] = zt_false;

    ret = mcu_cmd_communicate(pnic_info, UMSG_OPS_HAL_LPS_GET, arg, 1, NULL, 0);
    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] UMSG_OPS_HAL_LPS_GET failed", __func__);
        return ZT_RETURN_FAIL;
    }

    return ZT_RETURN_OK;
}

zt_s32 zt_mcu_set_rsvd_page_loc(nic_info_st *nic_info, void *rsvdpage)
{
    zt_u32 buf[RSVD_PAGE_len + 2] = {0};
    lps_prsvdpage rsvdpageloc = (lps_prsvdpage)rsvdpage;
    zt_s32 ret = 0;

    buf[0] = RSVD_PAGE;
    buf[1] = RSVD_PAGE_len;
    buf[2] = rsvdpageloc->lps_probe_rsp;
    buf[3] = rsvdpageloc->lps_poll;
    buf[4] = rsvdpageloc->lps_null_data;
    buf[5] = rsvdpageloc->lps_qos_data;
    buf[6] = rsvdpageloc->lps_bt_qos;

    ret = mcu_cmd_communicate(nic_info, UMSG_OPS_HAL_CONTROL_ARS_CMD, buf,
                              RSVD_PAGE_len + 2, NULL, 0);

    return ret;
}
#endif

#ifdef CFG_ENABLE_AP_MODE
zt_s32 zt_mcu_set_ap_mode(nic_info_st *pnic_info)
{
    zt_u32 arg[2] = {0};

    LOG_D("[set AP role] %s", __func__);

    if (pnic_info->nic_num == 1)
    {
        arg[0] = WLAN_HAL_VALUE_MEDIA_STATUS1;
    }
    else
    {
        arg[0] = WLAN_HAL_VALUE_MEDIA_STATUS;
    }

    arg[1] = WIFI_FW_AP_STATE;

    return zt_mcu_set_hw_reg(pnic_info, arg, 2);
}
#endif

zt_u32 zt_mcu_get_ack_rpt(nic_info_st *nic_info)
{
    zt_u32 cnt = 0;
    zt_s32 ret = 0;
    ret = mcu_cmd_communicate(nic_info, UMSG_OPS_HAL_GET_ACK_RPT_CNT,
                              NULL, 0,
                              &cnt, 1);
    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] failed", __func__);
        return -1;
    }
    else if (ZT_RETURN_CMD_BUSY == ret)
    {
        LOG_W("[%s] cmd busy, try again if need!", __func__);
        return -1;
    }

    return cnt;
}

