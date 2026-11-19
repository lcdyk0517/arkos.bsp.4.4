/*
 * power_hw_rich200.c
 *
 * used for .....
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
#include "fw_download.h"

#define PWR_DBG(fmt, ...)      LOG_D("[%s:%d]"fmt, __func__, __LINE__, ##__VA_ARGS__)
#define PWR_ARRAY(data, len)   zt_log_array(data, len)
#define PWR_INFO(fmt, ...)     LOG_I("[%s:%d]"fmt, __func__, __LINE__, ##__VA_ARGS__)
#define PWR_WARN(fmt, ...)     LOG_W("[%s:%d]"fmt, __func__, __LINE__, ##__VA_ARGS__)
#define PWR_ERROR(fmt, ...)    LOG_E("[%s:%d]"fmt, __func__, __LINE__, ##__VA_ARGS__)


zt_s32 side_road_cfg(struct hif_node_ *node)
{
    zt_u8  value8;

    value8 = hif_io_read8(node, 0xac, NULL);
    value8 |= 0x02;
    hif_io_write8(node, 0xac, value8);

    return 0;
}

zt_s32 power_off(struct hif_node_ *node)
{
    zt_s32 ret = 0;
    zt_u8  value8 = 0;
    zt_u16 value16 = 0;
    zt_u32 value32 = 0;

    if (hm_get_mod_removed() == zt_false && node->dev_removed == zt_true)
    {
        return ZT_RETURN_OK;
    }

    if (node->drv_ops->driver_flag)
    {
        value8 = hif_io_read8(node, 0x94, NULL);
        ret = hif_io_write8(node, 0x94, value8 & 0x18);
        if (ZT_RETURN_FAIL == ret)
        {
            LOG_E("[%s] ZT_CLK_ADDR failed,check!!!", __func__);
            return ZT_RETURN_FAIL;
        }
    }

    switch (node->hif_type)
    {
        case HIF_USB:
        {
            value32 = hif_io_read32(node, 0xac, NULL);
            value32 |= ZT_BIT(22);
            ret = hif_io_write32(node, 0xac, value32);
            if (ZT_RETURN_FAIL == ret)
            {
                LOG_E("[%s] 0xac bit 22 set 1 failed, check!!!", __func__);
                return ret;
            }

            value32 &= ~((zt_u32)ZT_BIT(11));
            ret = hif_io_write32(node, 0xac, value32);
            if (ZT_RETURN_FAIL == ret)
            {
                LOG_E("[%s] 0xac failed, check!!!", __func__);
                return ret;
            }

            value32 &= ~((zt_u32)ZT_BIT(10));
            ret = hif_io_write32(node, 0xac, value32);
            if (ZT_RETURN_FAIL == ret)
            {
                LOG_E("[%s] 0xac failed, check!!!", __func__);
                return ret;
            }
            value32 |= ZT_BIT(10);
            ret = hif_io_write32(node, 0xac, value32);
            if (ZT_RETURN_FAIL == ret)
            {
                LOG_E("[%s] 0xac failed, check!!!", __func__);
                return ret;
            }

            zt_msleep(10);
            value16 = 0;
            while (1)
            {
                value32 = hif_io_read32(node, 0xac, NULL);
                if (value32 & ZT_BIT(11))
                {
                    break;
                }
                zt_msleep(1);
                value16++;
                if (value16 > 10)
                {
                    break;
                }
            }

            if (value16 > 10)
            {
                LOG_E("[%s] failed!!!", __func__);
                return ZT_RETURN_FAIL;
            }
        }
        break;

        case HIF_SDIO:
        {
            value8 = hif_io_read8(node, 0xac + 2, NULL);
            value8 |= ZT_BIT(6);
            ret = hif_io_write8(node, 0xac + 2, value8);
            if (ZT_RETURN_FAIL == ret)
            {
                LOG_E("[%s] 0xac failed, check!!!", __func__);
                return ret;
            }

            value8 = hif_io_read8(node, 0x9094, NULL);
            value8 &= 0xFE;
            ret = hif_io_write8(node, 0x9094, value8);
            if (ZT_RETURN_FAIL == ret)
            {
                LOG_E("[%s] 0x9094 failed, check!!!", __func__);
                return ret;
            }

            value8 = hif_io_read8(node, 0xac + 1, NULL);
            value8 &= ~(ZT_BIT(2));
            ret = hif_io_write8(node, 0xac + 1, value8);
            if (ZT_RETURN_FAIL == ret)
            {
                LOG_E("[%s] 0xac failed, check!!!", __func__);
                return ret;
            }
            value8 |= ZT_BIT(2);
            ret = hif_io_write8(node, 0xac + 1, value8);
            if (ZT_RETURN_FAIL == ret)
            {
                LOG_E("[%s] 0xac failed, check!!!", __func__);
                return ret;
            }

            zt_msleep(10);
            value16 = 0;
            while (1)
            {
                value8 = hif_io_read8(node, 0x9094, NULL);
                if (value8 & ZT_BIT(0))
                {
                    break;
                }
                zt_msleep(1);
                value16++;
                if (value16 > 100)
                {
                    break;
                }
            }

            if (value16 > 100)
            {
                LOG_E("[%s] failed!!!", __func__);
                return ZT_RETURN_FAIL;
            }
        }
        break;

        default:
        {
            LOG_E("Error Nic type");
            return ZT_RETURN_FAIL;
        }
    }

    ret = hif_io_write8(node, 0xac, 0);
    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] 0xac failed, check!!!", __func__);
        return ret;
    }

    return ZT_RETURN_OK;
}

zt_s32 power_on(struct hif_node_ *node)
{
    zt_s32 ret = 0;
    zt_bool initSuccess = zt_false;
    zt_u8  value8 = 0;
    zt_u16 value16 = 0;

    LOG_I("[%s] start", __func__);

#ifdef CONFIG_CLOCK_24MHZ
    if (HIF_SDIO == node->hif_type)
    {
        value8 = hif_io_read8(node, 0xAC, NULL);
        value8 |= ZT_BIT(1);
        hif_io_write8(node, 0xAC, value8);

        value8 = hif_io_read8(node, 0xF8, NULL);
        value8 |= ZT_BIT(4);
        hif_io_write8(node, 0xF8, value8);
        LOG_I("[0xF8]=0x%02x",  hif_io_read8(node, 0xF8, NULL));

        hif_io_write8(node, 0x98, 0x09);

        /* clear reg[0x1c] bit8 = 0 */
        value8 = hif_io_read8(node, 0x1d, NULL);
        value8 &= ~ZT_BIT(0);
        hif_io_write8(node, 0x1d, value8);

        value8 = hif_io_read8(node, 0x2C, NULL);
        value8 &= 0xF0;
        value8 |= 0x0E;
        hif_io_write8(node, 0x2C, value8);
        LOG_I("[0x98]=0x%02x, [0x2C]=0x%02x, [0x1c]=0x%02x%02x%02x%02x",
              hif_io_read8(node, 0x98, NULL),  hif_io_read8(node, 0x2C, NULL),
              hif_io_read8(node, 0x1F, NULL), hif_io_read8(node, 0x1E, NULL),
              hif_io_read8(node, 0x1D, NULL), hif_io_read8(node, 0x1C, NULL));
    }
#endif

    // check chip status first
    value8 = hif_io_read8(node, 0xac, NULL);
    if (value8 & 0x10)
    {
        value16 = hif_io_read16(node, 0xec, NULL);
        LOG_D("[%s] power on status 0xec:0x%x", __func__, value16);

        power_off(node);
    }
    else
    {
        LOG_D("[%s] power off status", __func__);
    }

    //set 0x_00AC  bit 4 д0
    value8 = hif_io_read8(node, 0xac, NULL);
    value8 &= 0xEF;
    ret = hif_io_write8(node, 0xac, value8);
    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] 0xac failed, check!!!", __func__);
        return ret;
    }
    //set 0x_00AC  bit 0 д0
    value8 &= 0xFE;
    ret = hif_io_write8(node, 0xac, value8);
    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] 0xac failed, check!!!", __func__);
        return ret;
    }
    //set 0x_00AC  bit 0 д1
    value8 |= 0x01;
    ret = hif_io_write8(node, 0xac, value8);
    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] 0xac failed, check!!!", __func__);
        return ret;
    }
    zt_msleep(10);
    // waiting for power on
    value16 = 0;

    while (1)
    {
        value8 = hif_io_read8(node, 0xac, NULL);
        if (value8 & 0x10)
        {
            initSuccess = zt_true;
            break;
        }
        value16++;
        if (value16 > 1000)
        {
            break;
        }
    }

    if (initSuccess == zt_false)
    {
        LOG_E("[%s] failed!!!", __func__);
        return ZT_RETURN_FAIL;
    }

    LOG_I("[%s] success", __func__);

    return ZT_RETURN_OK;

}


zt_s32 power_suspend(struct hif_node_ *node)
{
    hif_io_write32(node, 0x4, 0x20030a02);
    PWR_DBG("reg 0x4(%x)", hif_io_read32(node, 0x4, NULL));

    return 0;
}


zt_s32 power_resume(struct hif_node_ *node)
{
    zt_s32 ret = 0;
    zt_bool initSuccess = zt_false;
    zt_u8  value8 = 0;
    zt_u16 value16 = 0;

    PWR_WARN("resume start\n");

    node->dev_removed = zt_false;

    zt_msleep(100);

    value8 = hif_io_read8(node, 0xac, NULL);
    value8 &= 0xEF;
    ret = hif_io_write8(node, 0xac, value8);
    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] 0xac failed, check!!!", __func__);
        return ret;
    }

    value8 &= 0xFE;
    ret = hif_io_write8(node, 0xac, value8);
    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] 0xac failed, check!!!", __func__);
        return ret;
    }

    value8 |= 0x01;
    ret = hif_io_write8(node, 0xac, value8);
    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] 0xac failed, check!!!", __func__);
        return ret;
    }
    zt_msleep(10);
    value16 = 0;

    while (1)
    {
        value8 = hif_io_read8(node, 0xac, NULL);
        if (value8 & 0x10)
        {
            initSuccess = zt_true;
            break;
        }
        value16++;
        if (value16 > 1000)
        {
            break;
        }
    }

    if (initSuccess == zt_false)
    {
        LOG_E("[%s] failed!!!", __func__);
        return ZT_RETURN_FAIL;
    }

    PWR_DBG("zt_power_on success");

    side_road_cfg(node);

    if (HIF_SDIO == node->hif_type) {
#ifndef CONFIG_USB_FLAG
        zt_sdioh_config(node);
        zt_sdioh_interrupt_enable(node);
#endif
    }

    {
        data_queue_mngt_st *data_queue_mngt = &node->trx_pipe;
        skb_queue_head_init(&data_queue_mngt->rx_queue);
        skb_queue_head_init(&data_queue_mngt->free_rx_queue_skb);
    }

    ret = zt_hif_queue_enable(node);
    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] zt_hif_queue_enable failed", __func__);
        return -1;
    }

    /*ndev reg*/
    PWR_DBG("<< add nic to hif_node >>");
    PWR_DBG("   node_id    :%d", node->node_id);
    PWR_DBG("   hif_type   :%d  [1:usb  2:sdio]", node->hif_type);

    /* fw download */
    if (zt_fw_download(node))
    {
        LOG_E("===>zt_fw_download error, exit!!");
        return ZT_RETURN_FAIL;
    }

    if (node->drv_ops->driver_flag)
    {
        zt_u32 version;
        zt_mcu_disable_fw_dbginfo(node->nic_info[0]);
        if (zt_mcu_get_chip_version(node->nic_info[0], &version) < 0)
        {
            LOG_E("===>zt_mcu_get_chip_version error");
            return ZT_RETURN_FAIL;
        }
        LOG_I("version:%d", version);
    }

    return 0;
}


