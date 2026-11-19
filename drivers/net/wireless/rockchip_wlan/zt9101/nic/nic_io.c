/*
 * nic_io.c
 *
 * used for nic io read or write
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

#include "common.h"

zt_u8 zt_io_read8(const nic_info_st *nic_info, zt_u32 addr, zt_s32 *err)
{
    zt_u8 value;
    zt_s32 ret = 0;
    ZT_ASSERT(nic_info != NULL);

    ret = nic_info->nic_read(nic_info->hif_node, 0, addr, (zt_s8 *)&value,
                             sizeof(value));
    if (err)
    {
        *err = ret;
    }

    return value;
}

zt_u16 zt_io_read16(const nic_info_st *nic_info, zt_u32 addr, zt_s32 *err)
{
    zt_u16 value;
    zt_s32 ret = 0;
    ZT_ASSERT(nic_info != NULL);

    ret = nic_info->nic_read(nic_info->hif_node, 0, addr, (zt_s8 *)&value,
                             sizeof(value));
    if (err)
    {
        *err = ret;
    }

    return value;
}

zt_u32 zt_io_read32(const nic_info_st *nic_info, zt_u32 addr, zt_s32 *err)
{
    zt_u32 value = 0;
    zt_s32 ret = 0;
    ZT_ASSERT(nic_info != NULL);

    ret = nic_info->nic_read(nic_info->hif_node, 0, addr, (zt_s8 *)&value,
                             sizeof(value));
    if (err)
    {
        *err = ret;
    }

    return value;
}

zt_s32 zt_io_write8(const nic_info_st *nic_info, zt_u32 addr, zt_u8 value)
{
    ZT_ASSERT(nic_info != NULL);

    return nic_info->nic_write(nic_info->hif_node, 0, addr, (zt_s8 *)&value,
                               sizeof(value));
}

zt_s32 zt_io_write16(const nic_info_st *nic_info, zt_u32 addr, zt_u16 value)
{
    ZT_ASSERT(nic_info != NULL);

    return nic_info->nic_write(nic_info->hif_node, 0, addr, (zt_s8 *)&value,
                               sizeof(value));
}

zt_s32 zt_io_write32(const nic_info_st *nic_info, zt_u32 addr, zt_u32 value)
{
    ZT_ASSERT(nic_info != NULL);

    return nic_info->nic_write(nic_info->hif_node, 0, addr, (zt_s8 *)&value,
                               sizeof(value));
}


zt_s32 zt_io_write_data(const nic_info_st *nic_info, zt_u8 agg_num, zt_s8 *pbuf,
                        zt_u32 len, zt_u32 addr,
                        zt_s32(*callback_func)(void *tx_info, void *param), void *tx_info, void *param)
{
    zt_s32 ret = 0;

    ZT_ASSERT(nic_info != NULL);

    if (nic_info->nic_tx_queue_insert == NULL)
    {
        LOG_E("nic_tx_queue_insert is not register, please check!!");
        return -1;
    }

    ret = nic_info->nic_tx_queue_insert(nic_info->hif_node, agg_num, pbuf, len,
                                        addr,
                                        callback_func, tx_info, param);

    return ret;
}


zt_s32 zt_io_write_data_queue_check(const nic_info_st *nic_info)
{
    ZT_ASSERT(nic_info != NULL);
    if (nic_info->nic_tx_queue_empty == NULL)
    {
        LOG_E("nic_tx_queue_empty is not register, please check!!");
        return -1;
    }

    return nic_info->nic_tx_queue_empty(nic_info->hif_node);
}

zt_s32 zt_io_tx_xmit_wake(const nic_info_st *nic_info)
{
    ZT_ASSERT(nic_info != NULL);
    if (nic_info->nic_tx_wake == NULL)
    {
        LOG_E("nic_tx_wake is not register, please check!!");
        return -1;
    }

    return nic_info->nic_tx_wake((nic_info_st *)nic_info);
}

zt_s32 zt_io_write_cmd_by_txd(nic_info_st *nic_info, zt_u32 cmd,
                              zt_u32 *send_buf, zt_u32 send_len,
                              zt_u32 *recv_buf, zt_u32 recv_len)
{
    zt_s32 ret;

    if (ZT_CANNOT_RUN(nic_info))
    {
        return ZT_RETURN_OK;
    }

    nic_mcu_hw_access_lock(nic_info);
#ifdef CFG_ENABLE_AP_MODE
    tx_work_pause(nic_info->ndev);
#endif
    ret =  nic_info->nic_write_cmd(nic_info->hif_node, cmd,
                                   send_buf, send_len,
                                   recv_buf, recv_len);
#ifdef CFG_ENABLE_AP_MODE
    tx_work_resume(nic_info->ndev);
#endif
    nic_mcu_hw_access_unlock(nic_info);

    return ret;
}

