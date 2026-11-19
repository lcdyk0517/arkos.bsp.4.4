/*
 * nic.h
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
#ifndef __NIC_H__
#define __NIC_H__


typedef enum
{
    NIC_USB     = 1,
    NIC_SDIO    = 2,
} nic_type_e;


typedef struct nic_info
{
    /*hif handle*/
    void *hif_node;
    zt_s32   hif_node_id;

    /*device handle*/
    void *dev;
    /*ndev handle*/
    void *ndev;
    zt_u8 ndev_id;
    zt_u8 ndev_num;
    void *pwiphy;
    void *pwidev;
    void *widev_priv;

    /*nic attr*/
    nic_type_e nic_type;
    zt_bool virNic;
    zt_bool is_surprise_removed;
    zt_bool is_driver_stopped;
    zt_bool is_driver_critical;
    zt_bool is_up;
    zt_bool is_init_commplete;
    zt_os_api_sema_t cmd_sema;
    zt_u32  nic_state;
    zt_u32 setband;

    /*wdn*/
    void *wdn;

    /*nic hw*/
    void *hw_info;

    /*nic local cfg*/
    void *local_info;

    /*nic mlme*/
    void *mlme_info;

    /*nic scan*/
    void *scan_info;

    /*nic auth*/
    void *auth_info;

    /*nic sec */
    void *sec_info;

    /*nic pm*/
    void *pwr_info;

    /*nic assoc*/
    void *assoc_info;

    /*wlan info*/
    void *wlan_mgmt_info;

    /*sta info*/
    void *sta_info;

    /*tx info*/
    void *tx_info;

    /*rx info*/
    void *rx_info;

    /*iw states*/
    void *iwstats;

    /* mp info */
    void *mp_info;

    /*p2p function info*/
    void *p2p;

    /*adhoc info*/
    void *adhoc_info;

    /*nic read/write reg */
    zt_s32(*nic_write)(void *node, zt_u8 flag, zt_u32 addr, zt_s8 *data,
                       zt_s32 datalen);
    zt_s32(*nic_read)(void *node, zt_u8 flag, zt_u32 addr, zt_s8 *data,
                      zt_s32 datalen);

    /*nic write data */
    zt_s32(*nic_tx_queue_insert)(void *node, zt_u8 agg_num, zt_s8 *buff,
                                 zt_u32 buff_len, zt_u32 addr,
                                 zt_s32(*tx_callback_func)(void *tx_info, void *param), void *tx_info,
                                 void *param);
    zt_s32(*nic_tx_queue_empty)(void *node);

    /*nic write cmd */
    zt_s32(*nic_write_cmd)(void *node, zt_u32 cmd, zt_u32 *send_buf,
                           zt_u32 send_len,
                           zt_u32 *recv_buf, zt_u32 recv_len);

    /*nic read cfg txt*/
    zt_s32(*nic_cfg_file_read)(void *pnic_info);

    /* tx wake */
    zt_s32(*nic_tx_wake)(struct nic_info *pnic_info);

    zt_u32 nic_num;
    void *buddy_nic;

    zt_u32 *wdn_id_bitmap;
    zt_u32 *cam_id_bitmap;
    zt_os_api_lock_t *mlme_hw_access_lock;
    zt_os_api_lock_t *mcu_hw_access_lock;
    zt_u8 *hw_ch;
    zt_u8 *hw_bw;
    zt_u8 *hw_offset;
} nic_info_st;

zt_s32 nic_init(nic_info_st *nic_info);
zt_s32 nic_term(nic_info_st *nic_info);
zt_s32 nic_enable(nic_info_st *nic_info);
zt_s32 nic_disable(nic_info_st *nic_info);
zt_s32 nic_suspend(nic_info_st *nic_info);
zt_s32 nic_resume(nic_info_st *nic_info);
zt_s32 nic_shutdown(nic_info_st *nic_info);
zt_u8 *nic_to_local_addr(nic_info_st *nic_info);
zt_inline static void nic_mlme_hw_access_lock(nic_info_st *pnic_info)
{
    if (pnic_info->mlme_hw_access_lock)
    {
        zt_os_api_lock_lock(pnic_info->mlme_hw_access_lock);
    }
}
zt_inline static zt_s32 nic_mlme_hw_access_trylock(nic_info_st *pnic_info)
{
    if (pnic_info->mlme_hw_access_lock)
    {
        return zt_os_api_lock_trylock(pnic_info->mlme_hw_access_lock);
    }

    return 0;
}
zt_inline static void nic_mlme_hw_access_unlock(nic_info_st *pnic_info)
{
    if (pnic_info->mlme_hw_access_lock)
    {
        zt_os_api_lock_unlock(pnic_info->mlme_hw_access_lock);
    }
}

zt_inline static void nic_mcu_hw_access_lock(nic_info_st *pnic_info)
{
    if (pnic_info->mcu_hw_access_lock)
    {
        zt_os_api_lock_lock(pnic_info->mcu_hw_access_lock);
    }
}
zt_inline static zt_s32 nic_mcu_hw_access_trylock(nic_info_st *pnic_info)
{
    if (pnic_info->mcu_hw_access_lock)
    {
        return zt_os_api_lock_trylock(pnic_info->mcu_hw_access_lock);
    }

    return 0;
}
zt_inline static void nic_mcu_hw_access_unlock(nic_info_st *pnic_info)
{
    if (pnic_info->mcu_hw_access_lock)
    {
        zt_os_api_lock_unlock(pnic_info->mcu_hw_access_lock);
    }
}


#define zt_is_surprise_removed(nic_info)    ((nic_info->is_surprise_removed) == zt_true)
#define zt_is_drv_stopped(nic_info)         ((nic_info->is_driver_stopped) == zt_true)

#define ZT_CANNOT_RUN(nic_info)     (zt_is_surprise_removed(nic_info) || zt_is_drv_stopped(nic_info))


#endif

