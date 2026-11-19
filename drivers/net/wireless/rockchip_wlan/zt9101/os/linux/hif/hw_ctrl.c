/*
 * hw_ctrl.c
 *
 * used for M0 init
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
#include "hif.h"

/* macro */
#define HW_CTRL_DBG(fmt, ...)       LOG_D("[%s:%d]"fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define HW_CTRL_INFO(fmt, ...)      LOG_I("[%s:%d]"fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define HW_CTRL_WARN(fmt, ...)      LOG_W("[%s:%d]"fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define HW_CTRL_ERROR(fmt, ...)     LOG_E("[%s:%d]"fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__)

zt_s32 zt_hw_mcu_disable(hif_node_st *hif_node)
{
    zt_s8 value8;
    zt_s32 ret;

    ret = hif_io_read(hif_node, 0, 0x94, &value8, sizeof(value8));
    if (ret)
    {
        HW_CTRL_ERROR("ZT_CLK_ADDR failed,check!!!");
        return ZT_RETURN_FAIL;
    }

    value8 &= 0x18;

    ret = hif_io_write(hif_node, 0, 0x94, &value8, sizeof(value8));
    if (ret)
    {
        HW_CTRL_ERROR("ZT_CLK_ADDR failed, check!!!");
        return ZT_RETURN_FAIL;
    }

    return ZT_RETURN_OK;
}

zt_s32 zt_hw_mcu_enable(hif_node_st *hif_node)
{
    zt_s8 value8;
    zt_s32 ret;

    ret = hif_io_read(hif_node, 0, 0x94, &value8, sizeof(value8));
    if (ret)
    {
        HW_CTRL_ERROR("ZT_CLK_ADDR failed, check!!!");
        return ZT_RETURN_FAIL;
    }

    value8 |= 0x6;

    ret = hif_io_write(hif_node, 0, 0x94, &value8, sizeof(value8));
    if (ret)
    {
        HW_CTRL_ERROR("ZT_CLK_ADDR failed, check!!!");
        return ZT_RETURN_FAIL;
    }

    return ZT_RETURN_OK;
}

zt_s32 zt_hw_mcu_startup(hif_node_st *hif_node)
{
    zt_s8 value8;
    zt_s32 ret;

    ret = hif_io_read(hif_node, 0, 0x94, &value8, sizeof(value8));
    if (ret)
    {
        HW_CTRL_ERROR("ZT_CLK_ADDR failed, check!!!");
        return ZT_RETURN_FAIL;
    }

    value8 |= 0x1;

    ret = hif_io_write(hif_node, 0, 0x94, &value8, sizeof(value8));
    if (ret)
    {
        HW_CTRL_ERROR("ZT_CLK_ADDR failed, check!!!");
        return ZT_RETURN_FAIL;
    }

    return ZT_RETURN_OK;
}

