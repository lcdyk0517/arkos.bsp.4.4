/*
 * fw_download.c
 *
 * used for fireware download after system power on
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
#include "hw_ctrl.h"
#include "fw_download.h"

/* macro */
#define FWDL_DBG(fmt, ...)      LOG_D("[%s:%d]"fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define FWDL_INFO(fmt, ...)     LOG_I("[%s:%d]"fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define FWDL_WARN(fmt, ...)     LOG_W("[%s:%d]"fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define FWDL_ERROR(fmt, ...)    LOG_E("[%s:%d]"fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__)

/* function declaration */
static zt_s32 fwdl_cmd_get_status(hif_node_st *hif_node)
{
    /*test base on hisilicon platform, it would need 25000*/
    zt_u32 ret = 0;
    zt_u32 data = 0;
    zt_u32 tryCnt = 0;
    zt_timer_t timer;
    zt_u32 t_delta = 0;

    // set mailbox zt_s32 finish
    ret = hif_io_write32(hif_node, ZT_MAILBOX_INT_FINISH, 0x12345678);
    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] ZT_MAILBOX_INT_FINISH failed, check!!!", __func__);
        return ret;
    }

    // set mailbox triger zt_s32
    ret = hif_io_write8(hif_node, ZT_MAILBOX_REG_INT, 1);
    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] ZT_MAILBOX_REG_INT failed, check!!!", __func__);
        return ret;
    }

    do
    {
        zt_s32 err = 0;

        data = hif_io_read32(hif_node, ZT_MAILBOX_INT_FINISH, &err);
        if (err)
        {
            LOG_E("[%s] read failed,err:%d", __func__, err);
            break;
        }
        if (HIF_USB == hif_node->hif_type && 0x55 == data)
        {
            return ZT_RETURN_OK;

        }
        else if (HIF_SDIO == hif_node->hif_type && 0x000000aa == data)
        {
            return ZT_RETURN_OK;
        }

        zt_timer_set(&timer, t_delta += (tryCnt++ < 3));
        while (!zt_timer_expired(&timer));
    } while ((tryCnt - 1) * 3 < 1000); /* totall time(ms) = (x-1)*3 */

    LOG_I("timeout !!!  data:0x%x", data);
    return ZT_RETURN_FAIL;
}


static zt_s32 fwdl_wait_fw_startup(hif_node_st *hif_node)
{
    /* get mcu feedback */
    if (fwdl_cmd_get_status(hif_node) < 0)
    {
        LOG_E("===>zt_mcu_cmd_get_status error, exit");
        return ZT_RETURN_FAIL;
    }

    return ZT_RETURN_OK;
}

zt_s32 zt_fw_download(void *node)
{
    hif_node_st *hif_node = node;
    struct hif_firmware_info firmware_info;
    zt_u8 re_dw_cnt;
    int ret;
    zt_u8 value8;

    re_dw_cnt = 0;
    DW_ALL_FW_FAIL_RETRY:

    ret = hif_firmware_read_get(hif_node->drv_ops->firmware_no, &firmware_info);
    if (ret < 0)
    {
        LOG_E("Can't get firmware for downloading");
        return -1;
    }

    FWDL_INFO("start");
    zt_hw_mcu_disable(hif_node);
    zt_hw_mcu_enable(hif_node);

    hif_io_read(hif_node, 0, 0xf4, &value8, sizeof(value8));
    if (value8 & ZT_BIT(0))
    {
        FWDL_ERROR("efuse setting error!");
        hif_firmware_read_free(&firmware_info);
        return -1;
    }

    {
        zt_timer_t timer;

        FWDL_INFO("fw downloading.....");
        zt_timer_set(&timer, 0);

        if (hif_write_firmware(hif_node, 0,
                               (zt_u8 *)firmware_info.fw0, firmware_info.fw0_size))
        {
            re_dw_cnt++;
            if(re_dw_cnt < 3) {
                goto DW_ALL_FW_FAIL_RETRY;
            } else {
                goto DW_ALL_FW_FAIL_RETURN;
            }
        }

        if (hif_write_firmware(hif_node, 1,
                               (zt_u8 *)firmware_info.fw1, firmware_info.fw1_size))
        {
            re_dw_cnt++;
            if(re_dw_cnt < 3) {
                goto DW_ALL_FW_FAIL_RETRY;
            } else {
                goto DW_ALL_FW_FAIL_RETURN;
            }
        }

        FWDL_DBG("===>fw download elapsed: %d ms", zt_timer_elapsed(&timer));
    }

    hif_firmware_read_free(&firmware_info);

    /* fw startup */
    if (zt_hw_mcu_startup(hif_node) != ZT_RETURN_OK)
    {
        FWDL_ERROR("===>zt_hw_mcu_startup error, exit!!");
        return ZT_RETURN_FAIL;
    }

    /* wait fw status */
    if (fwdl_wait_fw_startup(hif_node))
    {
        FWDL_ERROR("===>fw startup fail, exit!!");
        return -2;
    }

    FWDL_INFO("end");

    return 0;

DW_ALL_FW_FAIL_RETURN:
    hif_firmware_read_free(&firmware_info);
    return -1;
}


