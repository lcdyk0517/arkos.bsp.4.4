/*
 * hif.c
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
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/types.h>
#include <linux/utsname.h>
#include <linux/list.h>
#include <linux/usb.h>
#include <linux/time.h>

#include "common.h"
#include "hif.h"
#include "fw_download.h"
#include "power.h"
#include "proc.h"
#include "cfg_parse.h"
#include "power.h"
#include "rx_linux.h"

#define TX_CMD_PARAM_LENGTH    12
#define TXDESC_OFFSET_NEW      20
#define TXDESC_PACK_LEN        4
#define RX_CMD_PARAM_LENGTH    8
#define RXDESC_OFFSET_NEW      16
#define RXDESC_PACK_LEN        4
#define TX_RX_REG_MAX_SIZE     28
#define FIRMWARE_BLOCK_SIZE    (512 -  TXDESC_OFFSET_NEW - TXDESC_PACK_LEN)
#define HIF_HW_RECOVER_TH_CMD   3
#define HIF_HW_DEV_REINSERT     3


static hif_mngent_st *gl_hif = NULL;
static zt_bool gl_mode_removed = zt_true;

static char *cfg = "./wifi.cfg";
module_param(cfg, charp, 0644);

zt_bool hm_get_mod_removed(void)
{
    return gl_mode_removed;
}

zt_list_t *hm_get_list_head()
{
    if (NULL != gl_hif)
    {
        return &(gl_hif->hif_usb_sdio);
    }

    LOG_E("gl_hif is null");
    return 0;
}

static zt_s32 hif_create_id(zt_u64 *id_map, zt_u8 *id)
{
    zt_u8 i = 0;
    zt_s32 bit_mask = 0;
    for (i = 0; i < 64; i++)
    {
        bit_mask = ZT_BIT(i);
        if (!(*id_map & bit_mask))
        {
            *id_map |= bit_mask;
            *id = i;
            return ZT_RETURN_OK;
        }
    }

    return ZT_RETURN_FAIL;
}

static zt_s32 hif_destory_id(zt_u64 *id_map, zt_u8  id)
{
    if (id >= 64)
    {
        return ZT_RETURN_FAIL;
    }

    *id_map &= ~ ZT_BIT(id);
    return ZT_RETURN_OK;
}

zt_u8 hm_new_id(zt_s32 *err)
{
    zt_s32 ret = 0;
    zt_u8  id  = 0;
    if (NULL != gl_hif)
    {
        ret = hif_create_id(&gl_hif->id_bitmap, &id);
        if (err)
        {
            *err = ret;
        }
        return id;
    }

    return 0xff;
}

zt_u8 hm_del_id(zt_u8 id)
{
    return hif_destory_id(&gl_hif->id_bitmap, id);
}


zt_u8 hm_new_usb_id(zt_s32 *err)
{
    zt_s32 ret = 0;
    zt_u8  id  = 0;
    if (NULL != gl_hif)
    {
        ret = hif_create_id(&gl_hif->usb_id_bitmap, &id);
        if (err)
        {
            *err = ret;
        }
        return id;
    }
    return 0xff;
}
zt_u8 hm_del_usb_id(zt_u8 id)
{
    return hif_destory_id(&gl_hif->usb_id_bitmap, id);
}

zt_u8 hm_new_sdio_id(zt_s32 *err)
{
    zt_s32 ret = 0;
    zt_u8  id  = 0;
    if (NULL != gl_hif)
    {
        ret = hif_create_id(&gl_hif->sdio_id_bitmap, &id);
        if (err)
        {
            *err = ret;
        }

        return id;
    }

    return 0xff;
}

zt_u8 hm_del_sdio_id(zt_u8 id)
{
    return hif_destory_id(&gl_hif->sdio_id_bitmap, id);
}


zt_os_api_lock_t *hm_get_lock(void)
{
    if (NULL != gl_hif)
    {
        return &gl_hif->lock_mutex;
    }

    LOG_E("gl_hif is null");
    return 0;
}


zt_u8 hm_set_num(HM_OPERATION op)
{
    if (NULL != gl_hif)
    {
        if (HM_ADD == op)
        {
            return gl_hif->hif_num++;
        }

        else if (HM_SUB == op)
        {
            return gl_hif->hif_num--;
        }
    }

    LOG_E("gl_hif is null");
    return 0;
}

zt_u8 hm_set_usb_num(HM_OPERATION op)
{
    if (NULL != gl_hif)
    {
        if (HM_ADD == op)
        {
            return gl_hif->usb_num++;
        }
        else if (HM_SUB == op)
        {
            return gl_hif->usb_num--;
        }
    }

    LOG_E("gl_hif is null");
    return 0;
}

zt_u8 hm_set_sdio_num(HM_OPERATION op)
{
    if (NULL != gl_hif)
    {
        if (HM_ADD == op)
        {
            return gl_hif->sdio_num++;
        }
        else if (HM_SUB == op)
        {
            return gl_hif->sdio_num--;
        }
    }

    LOG_E("gl_hif is null");
    return 0;
}

static zt_inline zt_s32 hif_nic_tx_wake(nic_info_st *nic_info)
{
    tx_work_wake(nic_info->ndev);
    return 0;
}

hif_mngent_st *hif_mngent_get(void)
{
    return gl_hif;
}

static void hif_stop_bcn_queue(nic_info_st *nic_info[], zt_u8 nic_num)
{
    zt_u8 i;

    for (i = 0; i < nic_num; i++)
    {
        nic_info_st *pnic_info = nic_info[i];
        if (!pnic_info)
        {
            continue;
        }

        if (zt_local_cfg_get_work_mode(pnic_info) == ZT_MASTER_MODE)
        {
            zt_mcu_set_bcn_queue(pnic_info, zt_false);
        }
    }
}

static void dump_kernel_version(void)
{
    zt_s32 a = LINUX_VERSION_CODE >> 16;
    zt_s32 c = (0x0000ffff & LINUX_VERSION_CODE) >> 8;
    zt_s32 e = 0x000000ff & LINUX_VERSION_CODE;

    ZT_UNUSED(a);
    ZT_UNUSED(c);
    ZT_UNUSED(e);

    LOG_I("Kernel_version: %d.%d.%d", a, c, e);
}

#ifndef CONFIG_FW_FILE
#include "./../../../wifi_cfg.c"
#include "./../../../fw/zt9101_fw_data.c"
#include "./../../../fw/zt9101V30_fw_data.c"
#endif

int hif_firmware_read_get(zt_u8 firmware_no,
                          struct hif_firmware_info *firmware_info)
{
#ifdef CONFIG_FW_FILE
    const char *fw_full_puth;
    zt_file *file;
    zt_s32 i;
    loff_t pos;
    fw_file_header_t fw_file_head;
    fw_header_t fw_head;
    hif_mngent_st *hif_mngent = hif_mngent_get();

    if (firmware_no > 1)
    {
        LOG_E("firmware no error, exit");
        return -1;
    }

    fw_full_puth = hif_mngent->fw_full_path[firmware_no];
    LOG_I("download firmware fullpath:%s", fw_full_puth);
    file = zt_os_api_file_open(fw_full_puth);
    if (file == NULL)
    {
        LOG_E("firmware open failed");
        return -2;
    }
    pos = 0;
    zt_os_api_file_read(file, pos, (zt_u8 *)&fw_file_head,
                        sizeof(fw_file_head));
    if ((fw_file_head.magic_number != 0xaffa) ||
            (fw_file_head.interface_type != 0x9188))
    {
        LOG_E("firmware format error, magic:0x%x, type:0x%x",
              fw_file_head.magic_number, fw_file_head.interface_type);
        zt_os_api_file_close(file);
        return -2;
    }

    firmware_info->fw_rom_type = fw_file_head.rom_type;
    pos += sizeof(fw_file_head);
    for (i = 0; i < fw_file_head.firmware_num; i++)
    {
        zt_memset(&fw_head, 0, sizeof(fw_head));
        zt_os_api_file_read(file, pos, (zt_u8 *)&fw_head, sizeof(fw_head));
        if (fw_head.type == 0)
        {
            LOG_D("FW0 Ver: %d.%d.%d.%d, size:%dBytes",
                  fw_head.version & 0xFF, (fw_head.version >> 8) & 0xFF,
                  (fw_head.version >> 16) & 0xFF, (fw_head.version >> 24) & 0xFF,
                  fw_head.length);
            firmware_info->fw0_size = fw_head.length;
            firmware_info->fw0 = zt_kzalloc(fw_head.length);
            if (NULL == firmware_info->fw0)
            {
                LOG_E("firmware0 zt_kzalloc failed");
                zt_os_api_file_close(file);
                return -3;
            }
            zt_os_api_file_read(file, fw_head.offset, (zt_u8 *)firmware_info->fw0,
                                fw_head.length);
        }
        else
        {
            LOG_D("FW1 Ver: %d.%d.%d.%d, size:%dBytes",
                  fw_head.version & 0xFF, (fw_head.version >> 8) & 0xFF,
                  (fw_head.version >> 16) & 0xFF, (fw_head.version >> 24) & 0xFF,
                  fw_head.length);
            fw_head.length -= 32;
            firmware_info->fw1_size = fw_head.length;
            firmware_info->fw1 = zt_kzalloc(fw_head.length);
            if (NULL == firmware_info->fw1)
            {
                LOG_E("firmware1 zt_kzalloc failed");
                zt_kfree(firmware_info->fw0);
                firmware_info->fw0 = NULL;
                zt_os_api_file_close(file);
                return -3;
            }
            zt_os_api_file_read(file, fw_head.offset + 32, (zt_u8 *)firmware_info->fw1,
                                fw_head.length);
        }
        pos += sizeof(fw_head);
    }
    zt_os_api_file_close(file);
#else
    const uint8_t *fw_data;
    zt_s32 i;
    loff_t pos;
    fw_file_header_t fw_file_head;
    fw_header_t fw_head;

    if (firmware_no > 1)
    {
        LOG_E("firmware no error, exit");
        return -1;
    }

    if (firmware_no == 0) {
        fw_data = (const uint8_t *)zt9101_fw_data;
        LOG_I("download firmware zt9101_fw_data");
    } else {
        fw_data = (const uint8_t *)zt9101V30_fw_data;
        LOG_I("download firmware zt9101V30_fw_data");
    }

    pos = 0;
    zt_memcpy((zt_u8 *)&fw_file_head, &fw_data[pos], sizeof(fw_file_head));
    if ((fw_file_head.magic_number != 0xaffa) ||
            (fw_file_head.interface_type != 0x9188))
    {
        LOG_E("firmware format error, magic:0x%x, type:0x%x",
              fw_file_head.magic_number, fw_file_head.interface_type);
        return -2;
    }

    firmware_info->fw_rom_type = fw_file_head.rom_type;
    pos += sizeof(fw_file_head);
    for (i = 0; i < fw_file_head.firmware_num; i++)
    {
        zt_memset(&fw_head, 0, sizeof(fw_head));
        zt_memcpy((zt_u8 *)&fw_head, &fw_data[pos], sizeof(fw_head));
        if (fw_head.type == 0)
        {
            LOG_D("FW0 Ver: %d.%d.%d.%d, size:%dBytes",
                  fw_head.version & 0xFF, (fw_head.version >> 8) & 0xFF,
                  (fw_head.version >> 16) & 0xFF, (fw_head.version >> 24) & 0xFF,
                  fw_head.length);
            firmware_info->fw0_size = fw_head.length;
            firmware_info->fw0 = (zt_s8 *)&fw_data[fw_head.offset];
        }
        else
        {
            LOG_D("FW1 Ver: %d.%d.%d.%d, size:%dBytes",
                  fw_head.version & 0xFF, (fw_head.version >> 8) & 0xFF,
                  (fw_head.version >> 16) & 0xFF, (fw_head.version >> 24) & 0xFF,
                  fw_head.length);
            fw_head.length -= 32;
            firmware_info->fw1_size = fw_head.length;
            firmware_info->fw1 = (zt_s8 *)&fw_data[fw_head.offset + 32];
        }
        pos += sizeof(fw_head);
    }
#endif

    return 0;
}

void hif_firmware_read_free(struct hif_firmware_info *firmware_info)
{
    firmware_info->fw_rom_type = 0;
    firmware_info->fw0_size = 0;
    firmware_info->fw1_size = 0;
#ifdef CONFIG_FW_FILE
    zt_kfree(firmware_info->fw0);
    zt_kfree(firmware_info->fw1);
#endif
}

static zt_s32 __init hif_init(void)
{
#ifdef CONFIG_FW_FILE
    zt_file *file = NULL;
#endif
    zt_s32 ret = 0;
    LOG_D("\n\n     <ZTOP WIFI DRV INSMOD> \n\n");
    LOG_D("************HIF INIT*************");
#ifdef COMPILE_TIME
    LOG_I("Driver Ver:%s, Compile time:%s", ZT_VERSION, COMPILE_TIME);
#else
    LOG_I("Driver Ver:%s", ZT_VERSION);
#endif
    dump_kernel_version();

    gl_hif = zt_kzalloc(sizeof(hif_mngent_st));
    if (NULL == gl_hif)
    {
        LOG_E("zt_kzalloc failed");
        return -1;
    }
#ifdef CONFIG_FW_FILE
    file = zt_os_api_file_open(cfg);
    if (file == NULL)
    {
        LOG_E("can't open cfg file");
        gl_hif->cfg_size = 0;
        gl_hif->cfg_content = NULL;
    }
    else
    {
        gl_hif->cfg_size = zt_os_api_file_size(file);
        if (gl_hif->cfg_size <= 0)
        {
            gl_hif->cfg_size = 0;
            gl_hif->cfg_content = NULL;
        }
        else
        {
            gl_hif->cfg_content = zt_kzalloc(gl_hif->cfg_size);
            if (gl_hif->cfg_content == NULL)
            {
                gl_hif->cfg_size = 0;
            }
            else
            {
                zt_os_api_file_read(file, 0, gl_hif->cfg_content, gl_hif->cfg_size);
            }
        }

        zt_os_api_file_close(file);
    }

    {
        gl_hif->fw_path = zt_kzalloc(513);
        gl_hif->fw1_path = zt_kzalloc(513);
        gl_hif->ifname = zt_kzalloc(64);
        gl_hif->if2name = zt_kzalloc(64);
        if (!gl_hif->fw_path || !gl_hif->fw1_path || !gl_hif->ifname || !gl_hif->ifname)
        {
            LOG_E("zt_kzalloc failed");
            goto exit;
        }
        zt_cfg_file_preparse(gl_hif);
    }

    gl_hif->fw_full_path[0] = zt_os_api_file_getfullpath(gl_hif->fw_path);
    gl_hif->fw_full_path[1] = zt_os_api_file_getfullpath(gl_hif->fw1_path);
    if ((gl_hif->fw_full_path[0] == NULL) || (gl_hif->fw_full_path[1] == NULL))
    {
        LOG_E("can't zt_kalloc fullpath for fw path");
        goto exit;
    }
#else
    gl_hif->cfg_content = (zt_s8 *)wifi_cfg_data;
    gl_hif->cfg_size = sizeof(wifi_cfg_data);

    {
        gl_hif->fw_path = zt_kzalloc(513);
        gl_hif->fw1_path = zt_kzalloc(513);
        gl_hif->ifname = zt_kzalloc(64);
        gl_hif->if2name = zt_kzalloc(64);
        if (!gl_hif->fw_path || !gl_hif->ifname || !gl_hif->ifname)
        {
            LOG_E("zt_kzalloc failed");
            goto exit;
        }
        zt_cfg_file_preparse(gl_hif);
    }
#endif

    zt_list_init(&gl_hif->hif_usb_sdio);
    gl_hif->usb_num     = 0;
    gl_hif->sdio_num    = 0;
    gl_hif->hif_num     = 0;

    zt_os_api_lock_init(hm_get_lock(), ZT_LOCK_TYPE_MUTEX);

    gl_mode_removed = zt_false;

    ndev_notifier_register();
#if defined(CONFIG_USB_FLAG)
    ret = usb_init();
    if (ret)
    {
        ndev_notifier_unregister();
        return ret;
    }
#elif defined(CONFIG_SDIO_FLAG)
    ret = sdio_init();
    if (ret)
    {
        ndev_notifier_unregister();
        return ret;
    }
#else
    ret = usb_init();
    if (ret)
    {
        ndev_notifier_unregister();
        return ret;
    }

    ret = sdio_init();
    if (ret)
    {
        ndev_notifier_unregister();
        return ret;
    }
#endif

    return 0;

exit :
    if (gl_hif->fw_path)
    {
        zt_kfree(gl_hif->fw_path);
    }
    if (gl_hif->fw1_path)
    {
        zt_kfree(gl_hif->fw1_path);
    }
    if (gl_hif->ifname)
    {
        zt_kfree(gl_hif->ifname);
    }
    if (gl_hif->if2name)
    {
        zt_kfree(gl_hif->if2name);
    }
    if (gl_hif->fw_full_path[0])
    {
        zt_kfree(gl_hif->fw_full_path[0]);
    }
    if (gl_hif->fw_full_path[1])
    {
        zt_kfree(gl_hif->fw_full_path[1]);
    }
    zt_kfree(gl_hif);
    return -1;
}

static void __exit hif_exit(void)
{
    {
        zt_list_t *pos;
        zt_list_for_each(pos, &gl_hif->hif_usb_sdio)
        {
            hif_node_st *phif_info = ZT_CONTAINER_OF(pos, hif_node_st, next);
            /* stop hw beacon queue */
            hif_stop_bcn_queue(phif_info->nic_info, phif_info->nic_number);
            zt_mcu_power_off(phif_info->nic_info[0]);
        }
    }

    LOG_I("[%s] start ", __func__);
    gl_mode_removed = zt_true;

    LOG_D("notifier unregister");
    ndev_notifier_unregister();

    {
        zt_list_t *pos;
        zt_list_for_each(pos, &gl_hif->hif_usb_sdio)
        {
            hif_node_st *phif_info = ZT_CONTAINER_OF(pos, hif_node_st, next);
            /* ndev modules unregister */
            ndev_unregister_all(phif_info->nic_info, phif_info->nic_number);
        }
    }

    /* exit */
#if defined(CONFIG_USB_FLAG)
    usb_exit();
#elif defined(CONFIG_SDIO_FLAG)
    sdio_exit();
#else
    usb_exit();
    sdio_exit();
#endif

#ifdef CONFIG_FW_FILE
    if (gl_hif->cfg_content != NULL)
    {
        zt_kfree(gl_hif->cfg_content);
        gl_hif->cfg_content = NULL;
    }
#endif
    gl_hif->cfg_size = 0;

    zt_os_api_lock_term(hm_get_lock());
    if (gl_hif->fw_path)
    {
        zt_kfree(gl_hif->fw_path);
    }
    if (gl_hif->fw1_path)
    {
        zt_kfree(gl_hif->fw1_path);
    }
    if (gl_hif->fw_full_path[0])
    {
        zt_kfree(gl_hif->fw_full_path[0]);
    }
    if (gl_hif->fw_full_path[1])
    {
        zt_kfree(gl_hif->fw_full_path[1]);
    }
    if (gl_hif->ifname)
    {
        zt_kfree(gl_hif->ifname);
    }
    if (gl_hif->if2name)
    {
        zt_kfree(gl_hif->if2name);
    }
    zt_kfree(gl_hif);
    gl_hif = NULL;

    LOG_I("[%s] end", __func__);
}



static zt_s32 hif_add_nic(hif_node_st *hif_info, zt_s32 num)
{
    struct sdio_func *psdio_func_tmp = hif_info->u.sdio.func;
    struct usb_interface *pusb_intf_tmp = hif_info->u.usb.pusb_intf;

    hif_info->nic_info[num] = zt_kzalloc(sizeof(nic_info_st));
    if (hif_info->nic_info[num] == NULL)
    {
        LOG_E("[hif_dev_insert] malloc nic_info failed");
        return -1;
    }

    hif_info->nic_info[num]->hif_node       = hif_info;
    hif_info->nic_info[num]->hif_node_id    = hif_info->node_id;
    hif_info->nic_info[num]->ndev_id        = num;
    hif_info->nic_info[num]->is_up          = 0;
    hif_info->nic_info[num]->virNic         = num ? zt_true : zt_false;
    if (hif_info->hif_type == HIF_USB)
    {
        hif_info->nic_info[num]->nic_type   = NIC_USB;
        hif_info->nic_info[num]->dev        = &pusb_intf_tmp->dev;
    }
    else
    {
        hif_info->nic_info[num]->nic_type   = NIC_SDIO;
        hif_info->nic_info[num]->dev        = &psdio_func_tmp->dev;
    }

    hif_info->nic_info[num]->nic_read               = hif_io_read;
    hif_info->nic_info[num]->nic_write              = hif_io_write;
    hif_info->nic_info[num]->nic_cfg_file_read      = zt_cfg_file_parse;
    hif_info->nic_info[num]->nic_tx_queue_insert    =
        hif_info->ops->hif_tx_queue_insert;
    hif_info->nic_info[num]->nic_tx_queue_empty     =
        hif_info->ops->hif_tx_queue_empty;
    hif_info->nic_info[num]->nic_tx_wake            = hif_nic_tx_wake;
    hif_info->nic_info[num]->nic_write_cmd          = hif_write_cmd;

    hif_info->nic_info[num]->wdn_id_bitmap          = &hif_info->wdn_id_bitmap;
    hif_info->nic_info[num]->cam_id_bitmap          = &hif_info->cam_id_bitmap;
    hif_info->nic_info[num]->mlme_hw_access_lock    =
        &hif_info->mlme_hw_access_lock;
    hif_info->nic_info[num]->mcu_hw_access_lock     = &hif_info->mcu_hw_access_lock;
    hif_info->nic_info[num]->hw_ch                  = &hif_info->hw_ch;
    hif_info->nic_info[num]->hw_bw                  = &hif_info->hw_bw;
    hif_info->nic_info[num]->hw_offset              = &hif_info->hw_offset;

    hif_info->nic_number++;
    hif_info->nic_info[num]->nic_num = num;

    hif_info->nic_info[num]->buddy_nic = NULL;
    if (hif_info->nic_number == 2)
    {
        /* for buddy */
        hif_info->nic_info[0]->buddy_nic = hif_info->nic_info[1];
        hif_info->nic_info[1]->buddy_nic = hif_info->nic_info[0];
    }

    return 0;
}

zt_inline static void hif_hw_access_init(hif_node_st *hif_info)
{
    /* mlme hardware access hw lock */
    zt_os_api_lock_init(&hif_info->mlme_hw_access_lock, ZT_LOCK_TYPE_MUTEX);

    /* mcu hardware access hw lock */
    zt_os_api_lock_init(&hif_info->mcu_hw_access_lock, ZT_LOCK_TYPE_MUTEX);
}

zt_s32 hif_exception_handle(void)
{
    zt_list_t *tmp   = NULL;
    zt_list_t *next  = NULL;
    hif_node_st *hif_info       = NULL;
    zt_u8 i = 0;
    nic_info_st *pnic_info;

    /* todo: unregiest netif, this exception will case application software
    lost wlan interface, notify the upper level. */
    zt_list_for_each_safe(tmp, next, hm_get_list_head())
    {
        hif_info = zt_list_entry(tmp, hif_node_st, next);
        if (NULL != hif_info)
        {
            for (i = 0; i < hif_info->nic_number; i++)
            {
                pnic_info = hif_info->nic_info[i];
                if (NULL == pnic_info)
                {
                    LOG_W("[%s] pnic_info is null", __func__);
                    continue;
                }

                ndev_shutdown(pnic_info);
                ndev_unregister(pnic_info);
            }
        }
    }

    return 0;
}

zt_s32 hif_chip_reset(hif_node_st *hif_info)
{
    zt_s32 i;

    /* power on */
    LOG_D("%s: chip reset", __func__);
    i = 0;
    while (power_on(hif_info) < 0)
    {
        i++;
        LOG_E("===>power on error, times->%d", i);
        if (i >= 5)
        {
            LOG_E("===>power_on error, exit!!");
            hif_exception_handle();
            return ZT_RETURN_FAIL;
        }
        zt_msleep(500);
    }

    LOG_D("zt_power_on success");
    side_road_cfg(hif_info);

    if (HIF_SDIO == hif_info->hif_type)
    {
        // cmd53 is ok, next for side-road configue
#ifndef CONFIG_USB_FLAG
        zt_sdioh_config(hif_info);
        zt_sdioh_interrupt_enable(hif_info);
#endif
    }

    /* fw download */
    if (zt_fw_download(hif_info))
    {
        LOG_E("%s: ===>zt_fw_download error, exit!!", __func__);
        return ZT_RETURN_FAIL;
    }

    {
        zt_u32 version;
        if (zt_mcu_get_chip_version(hif_info->nic_info[0], &version) < 0)
        {
            LOG_E("===>zt_mcu_get_chip_version error");
            return ZT_RETURN_FAIL;
        }
    }

    /* init hardware by default cfg */
    if (zt_hw_info_set_default_cfg(hif_info->nic_info[0]) < 0)
    {
        LOG_E("===>zt_hw_info_set_default_cfg error");
        return ZT_RETURN_FAIL;
    }

    /* configure */
    if (zt_local_cfg_set_default(hif_info->nic_info[0]) < 0)
    {
        LOG_E("===>zt_local_cfg_set_default error");
        return ZT_RETURN_FAIL;
    }

    /* close the fw debug info */
    zt_mcu_disable_fw_dbginfo(hif_info->nic_info[0]);

    return 0;
}

zt_s32 hif_dev_insert(hif_node_st *hif_info)
{
    zt_s32 ret = 0;
    zt_s32 i   = 0;
    zt_s32 nic_num = 0;
    zt_u8 re_insert_cnt = 0;

DEV_REINSERT:
    hif_info->dev_removed = zt_false;

    /* power on */
    LOG_D("************HIF DEV INSERT*************");
    LOG_D("<< Power on >>");
    i = 0;
    while (power_on(hif_info) < 0)
    {
        i++;
        LOG_E("===>power on error, times->%d", i);
        if (i >= 5)
        {
            LOG_E("===>power_on error, exit!!");
            return -1;
        }
        zt_msleep(500);
    }

    LOG_D("zt_power_on success");
    side_road_cfg(hif_info);

    if (HIF_SDIO == hif_info->hif_type)
    {
        // cmd53 is ok, next for side-road configue
#ifndef CONFIG_USB_FLAG
        zt_sdioh_config(hif_info);
        zt_sdioh_interrupt_enable(hif_info);
#endif
    }

    /*create hif trx func*/
    LOG_D("<< create hif tx/rx queue >>");
    ret = zt_data_queue_mngt_init(hif_info);
    if (ret)
    {
        LOG_E("[%s] zt_data_queue_mngt_init failed", __func__);
        return -1;
    }

    ret = zt_hif_bulk_enable(hif_info);
    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] zt_hif_bulk_enable failed", __func__);
        goto err_qmngt;
    }

    ret = zt_hif_queue_enable(hif_info);
    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] zt_hif_queue_enable failed", __func__);
        goto err_bulk;
    }

    /*ndev reg*/
    LOG_D("<< add nic to hif_node >>");
    LOG_D("   node_id    :%d", hif_info->node_id);
    LOG_D("   hif_type   :%d  [1:usb  2:sdio]", hif_info->hif_type);

    /* fw download */
    ret = zt_fw_download(hif_info);
    if (ret)
    {
        re_insert_cnt++;
        if(re_insert_cnt < HIF_HW_DEV_REINSERT && ret == -2) {
            hif_dev_removed(hif_info);
            LOG_I("===>zt_fw_download error, retry %d!!", re_insert_cnt);
            goto DEV_REINSERT;
        } else {
            LOG_E("===>zt_fw_download error, exit!!");
            ret = ZT_RETURN_FAIL;
            goto err_queue;
        }
    }

#ifdef CONFIG_STA_AND_AP_MODE
    nic_num = 2;
#else
    nic_num = 1;
#endif

    hif_hw_access_init(hif_info);

    for (i = 0; i < nic_num; i++)
    {
        ret = hif_add_nic(hif_info, i);
        if (ret != 0)
        {
            LOG_E("[%s] ndev_register nic[%d] failed", __func__, i);
            goto err_hw;
        }

        LOG_D("<< ndev_%d register >>", i);
        ret = ndev_register(hif_info->nic_info[i]);
        if (ret != 0)
        {
            LOG_E("[%s] ndev_register nic[%d] failed", __func__, i);
            goto err_nic;
        }
    }

    /* debug init */
    if (zt_proc_init(hif_info) < 0)
    {
        LOG_E("===>zt_proc_init error");
        ret = ZT_RETURN_FAIL;
        goto err_reg;
    }

    /* close the fw debug info */
    zt_mcu_disable_fw_dbginfo(hif_info->nic_info[0]);

    return 0;

err_nic:
    while (i > 0)
    {
        i--;
        ndev_unregister(hif_info->nic_info[i]);
        zt_kfree(hif_info->nic_info[i]);
        hif_info->nic_info[i] = NULL;
    }
err_reg:
    i = nic_num;
err_hw:
    while (i > 0)
    {
        i--;
        ndev_unregister(hif_info->nic_info[i]);
        zt_kfree(hif_info->nic_info[i]);
        hif_info->nic_info[i] = NULL;
    }
err_queue:
    zt_hif_queue_disable(hif_info);
err_bulk:
    zt_hif_bulk_disable(hif_info);
err_qmngt:
    zt_data_queue_mngt_term(hif_info);
    return ret;
}


void  hif_dev_removed(hif_node_st *hif_info)
{
    hif_info->dev_removed = zt_true;

    LOG_D("************HIF DEV REMOVE [NODE:%d TYPE:%d]*************",
          hif_info->node_id, hif_info->hif_type);

    zt_hif_bulk_cmd_post_exit(hif_info);

    if (HIF_SDIO == hif_info->hif_type)
    {
#ifndef CONFIG_USB_FLAG
        zt_sdioh_interrupt_disable(hif_info);
#endif
    }
    zt_hif_queue_disable(hif_info);

    zt_hif_bulk_disable(hif_info);

    /* term hif trx mode */
    LOG_D("<< term hif tx/rx queue >>");
    zt_data_queue_mngt_term(hif_info);

    /*proc term*/
    zt_proc_term(hif_info);

    /* power off */
    LOG_D("<< Power off >>");
    // power_off(hif_info);//TODO Waiting for new power down logic
}


void hif_node_register(hif_node_st **node, hif_enum type,
                       struct hif_node_ops *ops)
{
    hif_node_st  *hif_node = NULL;

    hif_node = zt_kzalloc(sizeof(hif_node_st));
    if (NULL == hif_node)
    {
        LOG_E("zt_kzalloc for hif_node failed");
        return;
    }

    hif_node->hif_type      = type;
    hif_node->node_id       = hm_new_id(NULL);
    hif_node->ops           = ops;
    hif_node->wdn_id_bitmap = 0;
    hif_node->cam_id_bitmap = 0;
	hif_node->error_cnt_cmd = 0;

    zt_os_api_lock_lock(hm_get_lock());
    zt_list_insert_tail(&hif_node->next, hm_get_list_head());

    if (HIF_SDIO == hif_node->hif_type)
    {
        hm_set_sdio_num(HM_ADD);
    }
    else if (HIF_USB == hif_node->hif_type)
    {
        hm_set_usb_num(HM_ADD);
    }

    hm_set_num(HM_ADD);

    zt_os_api_lock_unlock(hm_get_lock());

    *node = hif_node;

};

void hif_node_unregister(hif_node_st *pnode)
{
    zt_list_t *tmp   = NULL;
    zt_list_t *next  = NULL;
    hif_node_st *node       = NULL;
    zt_s32 ret                 = 0;

    LOG_I("[%s] start", __func__);
    zt_os_api_lock_lock(hm_get_lock());
    zt_list_for_each_safe(tmp, next, hm_get_list_head())
    {
        node = zt_list_entry(tmp, hif_node_st, next);
        if (NULL != node  && pnode == node)
        {
            zt_list_delete(&node->next);
            hm_set_num(HM_SUB);
            if (HIF_USB == node->hif_type)
            {
                hm_set_usb_num(HM_SUB);
            }
            else if (HIF_SDIO == node->hif_type)
            {
                hm_set_sdio_num(HM_SUB);
            }
            break;
        }
    }

    if (NULL != node)
    {
        ret = hm_del_id(node->node_id);
        if (ret)
        {
            LOG_E("hm_del_id [%d] failed", node->node_id);
        }
        zt_kfree(node);
        node = NULL;
    }

    zt_os_api_lock_unlock(hm_get_lock());
    LOG_I("[%s] end", __func__);
}

static void io_txdesc_chksum(zt_u8 *ptx_desc)
{
    zt_u16 *usPtr = (zt_u16 *) ptx_desc;
    zt_u32 index;
    zt_u16 checksum = 0;

    for (index = 0; index < 9; index++)
    {
        checksum ^= zt_le16_to_cpu(*(usPtr + index));
    }

    zt_set_bits_to_le_u32(ptx_desc + 16, 16, 16, checksum);
}

static zt_u16 io_firmware_chksum(zt_u8 *firmware, zt_u32 len)
{
    zt_u32 loop;
    zt_u16 *u16Ptr = (zt_u16 *) firmware;
    zt_u32 index;
    zt_u16 checksum = 0;

    loop = len / 2;
    for (index = 0; index < loop; index++)
    {
        checksum ^= zt_le16_to_cpu(*(u16Ptr + index));
    }

    return checksum;
}

zt_s32 zt_hif_bulk_enable(hif_node_st *hif_node)
{
    hif_node->fw_buffer =  zt_kzalloc(512);
    if (NULL == hif_node->fw_buffer)
    {
        LOG_E("no memmory for hif fw buffer");
        return ZT_RETURN_FAIL;
    }
    hif_node->cmd_snd_buffer =  zt_kzalloc(512);
    if (NULL == hif_node->cmd_snd_buffer)
    {
        LOG_E("no memmory for hif cmd buffer");
        return ZT_RETURN_FAIL;
    }

    hif_node->cmd_rcv_buffer =  zt_kzalloc(512);
    if (NULL == hif_node->cmd_rcv_buffer)
    {
        LOG_E("no memmory for hif cmd buffer");
        return ZT_RETURN_FAIL;
    }

    mutex_init(&hif_node->reg_mutex);
    init_completion(&hif_node->fw_completion);
    init_completion(&hif_node->cmd_completion);

    hif_node->reg_size = 0;
    hif_node->fw_size = 0;
    hif_node->cmd_size = 0;
    hif_node->bulk_enable = zt_true;
    return ZT_RETURN_OK;
}

zt_s32 zt_hif_bulk_disable(hif_node_st *hif_node)
{
    if (NULL != hif_node->fw_buffer)
    {
        zt_kfree(hif_node->fw_buffer);
    }
    if (NULL != hif_node->cmd_snd_buffer)
    {
        zt_kfree(hif_node->cmd_snd_buffer);
    }
    if (NULL != hif_node->cmd_rcv_buffer)
    {
        zt_kfree(hif_node->cmd_rcv_buffer);
    }
    hif_node->reg_size = 0;
    hif_node->fw_size = 0;
    hif_node->cmd_size = 0;

    return ZT_RETURN_OK;
}

void zt_hif_bulk_fw_init(hif_node_st *hif_node)
{
    hif_node->fw_completion.done = 0;
    hif_node->fw_size = 0;
}

zt_s32 zt_hif_bulk_fw_wait(hif_node_st *hif_node, zt_u32 timeout)
{
    return wait_for_completion_timeout(&hif_node->fw_completion,
                                       (timeout * ZT_HZ) / 1000);
}

void zt_hif_bulk_fw_post(hif_node_st *hif_node, zt_u8 *buff, zt_u16 len)
{
    if (len <= 512)
    {
        zt_memcpy(hif_node->fw_buffer, buff, len);
        hif_node->fw_size = len;
        complete(&hif_node->fw_completion);
    }
}

void zt_hif_bulk_cmd_init(hif_node_st *hif_node)
{
    hif_node->cmd_completion.done = 0 ;
    hif_node->cmd_size = 0;
}

zt_s32 zt_hif_bulk_cmd_wait(hif_node_st *hif_node, zt_u32 timeout)
{
    hif_node->cmd_completion_flag = 1;

    if (hif_node->hif_type == HIF_SDIO && hif_node->u.sdio.clk_pwr_save)
    {
        zt_timer_t timer;
        zt_timer_set(&timer, timeout);
        do
        {
            hif_io_read32(hif_node, SDIO_BASE | ZT_REG_TXCTL, NULL); /* dummy read */
        } while (!zt_timer_expired(&timer) && hif_node->cmd_completion_flag == 1);
        return !hif_node->cmd_completion_flag;
    }
    else
    {
        return wait_for_completion_timeout(&hif_node->cmd_completion,
                                           (timeout * ZT_HZ) / 1000);
    }
}

void zt_hif_bulk_cmd_post(hif_node_st *hif_node, zt_u8 *buff, zt_u16 len)
{
    if (len <= 512)
    {
        zt_memcpy(hif_node->cmd_rcv_buffer, buff, len);
        hif_node->cmd_size = len;
        hif_node->cmd_completion_flag = 0;
        if (!(hif_node->hif_type == HIF_SDIO && hif_node->u.sdio.clk_pwr_save))
        {
            complete(&hif_node->cmd_completion);
        }
    }
}

void zt_hif_bulk_cmd_post_exit(hif_node_st *hif_node)
{
    hif_node->cmd_completion_flag = 0;
    if (zt_true == hif_node->bulk_enable)
    {
        if (!(hif_node->hif_type == HIF_SDIO && hif_node->u.sdio.clk_pwr_save))
        {
            complete(&hif_node->cmd_completion);
        }
    }
}


zt_s32 hif_write_firmware(void *node, zt_u8 which, zt_u8 *firmware, zt_u32 len)
{
    zt_u8  u8Value;
    zt_u16 i;
    zt_u16 checksum;
    zt_u16 u16Value;
    zt_u32 align_len;
    zt_u32 buffer_len;
    zt_u32 back_len;
    zt_u32 send_once;
    zt_u32 send_len;
    zt_u32 send_size;
    zt_u32 remain_size;
    zt_u32 block_num;
    zt_u8 *alloc_buffer;
    zt_u8 *use_buffer;
    zt_u8 *ptx_desc;
    zt_u8 *prx_desc;
    hif_node_st *hif_node = (hif_node_st *)node;

    if (hif_node->dev_removed == zt_true)
    {
        return -1;
    }
    align_len = ((len + 3) / 4) * 4;

    /* alloc mem for xmit */
    buffer_len = TXDESC_OFFSET_NEW + TXDESC_PACK_LEN + FIRMWARE_BLOCK_SIZE;
    LOG_D("firmware download length is %d", len);
    LOG_D("firmware download buffer size is %d", buffer_len);
    alloc_buffer = zt_kzalloc(buffer_len + 4);
    if (alloc_buffer == NULL)
    {
        LOG_E("can't zt_kzalloc memmory for download firmware");
        return -1;
    }
    use_buffer = (zt_u8 *) ZT_N_BYTE_ALIGMENT((SIZE_PTR)(alloc_buffer), 4);

    block_num = align_len / FIRMWARE_BLOCK_SIZE;
    if (align_len % FIRMWARE_BLOCK_SIZE)
    {
        block_num += 1;
    }
    else
    {
        align_len += 4;
        block_num += 1;
    }
    remain_size = align_len;
    send_size = 0;

    LOG_I("fwdownload block number is %d", block_num);
    zt_hif_bulk_fw_init(hif_node);

    for (i = 0; i < block_num; i++)
    {
        zt_memset(use_buffer, 0, buffer_len);
        ptx_desc = use_buffer;
        /* set for fw xmit */
        zt_set_bits_to_le_u32(ptx_desc, 0, 2, TYPE_FW);
        /* set for first packet */
        if (i == 0)
        {
            zt_set_bits_to_le_u32(ptx_desc, 11, 1, 1);
        }
        /* set for last packet */
        if (i == (block_num - 1))
        {
            zt_set_bits_to_le_u32(ptx_desc, 10, 1, 1);
        }
        /* set for which firmware */
        zt_set_bits_to_le_u32(ptx_desc, 12, 1, which);
        /* set for reg HWSEQ_EN */
        zt_set_bits_to_le_u32(ptx_desc, 18, 1, 1);
        /* set for pkt_len */
        if (remain_size > FIRMWARE_BLOCK_SIZE)
        {
            send_once = FIRMWARE_BLOCK_SIZE;
        }
        else
        {
            send_once = remain_size;
        }

        zt_memcpy(ptx_desc + TXDESC_OFFSET_NEW, firmware + send_size, send_once);

        send_len = TXDESC_OFFSET_NEW + send_once;
        /* set for  firmware checksum */
        if (i == (block_num - 1))
        {
            checksum = io_firmware_chksum(firmware, align_len);
            LOG_I("cal checksum=%d", checksum);
            zt_set_bits_to_le_u32(ptx_desc + send_len, 0, 32, checksum);
            LOG_D("my checksum is 0x%04x, fw_len=%d", checksum, align_len);
            send_len += TXDESC_PACK_LEN;
            send_once += TXDESC_PACK_LEN;
        }
        zt_set_bits_to_le_u32(ptx_desc + 8, 0, 16, send_once);

        /* set for checksum */
        io_txdesc_chksum(ptx_desc);

        if (hif_io_write(hif_node, 2, CMD_QUEUE_INX, ptx_desc, send_len) < 0)
        {
            LOG_E("bulk download firmware error");
            zt_kfree(alloc_buffer);
            return -1;
        }

        send_size += send_once;
        remain_size -= send_once;

        zt_msleep(1);
    }

    if (zt_hif_bulk_fw_wait(hif_node, HIF_BULK_MSG_TIMEOUT) == 0)
    {
        LOG_E("bulk access fw read timeout");
        zt_kfree(alloc_buffer);
        return -1;
    }

    prx_desc = use_buffer;
    back_len = RXDESC_OFFSET_NEW + RXDESC_PACK_LEN;
    if (hif_node->fw_size != back_len)
    {
        LOG_E("bulk access fw read length error");
        zt_kfree(alloc_buffer);
        return -1;
    }

    zt_memcpy(prx_desc, hif_node->fw_buffer, hif_node->fw_size);

    u8Value = zt_le_u8_read(prx_desc);
    if ((u8Value & 0x03) != TYPE_FW)
    {
        LOG_E("bulk download firmware type error by read back");
        zt_kfree(alloc_buffer);
        return -1;
    }
    u16Value = zt_le_u16_read(prx_desc + 4);
    u16Value &= 0x3FFF;
    if (u16Value != RXDESC_PACK_LEN)
    {
        LOG_E("bulk download firmware length error, value: %d", u16Value);
        zt_kfree(alloc_buffer);
        return -1;
    }

    u8Value = zt_le_u8_read(prx_desc + 16);
    if (u8Value != 0x00)
    {
        LOG_E("bulk download firmware status error");
        u16Value = zt_le_u16_read(prx_desc + 18);
        LOG_D("Read checksum is 0x%04x", u16Value);
        if (u8Value == 0x01)
        {
            LOG_E("bulk download firmware txd checksum error");
        }
        else if (u8Value == 0x02)
        {
            LOG_E("bulk download firmware fw checksum error");
        }
        else if (u8Value == 0x03)
        {
            LOG_E("bulk download firmware fw & txd checksum error");
        }
        zt_kfree(alloc_buffer);
        return -1;
    }
    zt_kfree(alloc_buffer);

    LOG_I("bulk download firmware ok");

    return 0;
}


zt_s32 hif_write_cmd(void *node, zt_u32 cmd, zt_u32 *send_buf, zt_u32 send_len,
                     zt_u32 *recv_buf, zt_u32 recv_len)
{
    zt_s32 ret = 0;
    zt_u8  u8Value;
    zt_u8 *ptx_desc;
    zt_u8 *prx_desc;
    zt_u16 snd_pktLen = 0;
    zt_u16 rcv_pktLen = 0;
    hif_node_st *hif_node = (hif_node_st *)node;

    if (hif_node->dev_removed == zt_true)
    {
        return -1;
    }

    ptx_desc = hif_node->cmd_snd_buffer;
    zt_memset(ptx_desc, 0, TXDESC_OFFSET_NEW + TX_CMD_PARAM_LENGTH);

    zt_set_bits_to_le_u32(ptx_desc, 0, 2, TYPE_CMD);
    zt_set_bits_to_le_u32(ptx_desc, 2, 8, 0);
    zt_set_bits_to_le_u32(ptx_desc, 18, 1, 1);
    zt_set_bits_to_le_u32(ptx_desc + 8, 0, 16, TX_CMD_PARAM_LENGTH + send_len * 4);
    io_txdesc_chksum(ptx_desc);

    zt_set_bits_to_le_u32(ptx_desc + TXDESC_OFFSET_NEW, 0, 32, cmd);
    zt_set_bits_to_le_u32(ptx_desc + TXDESC_OFFSET_NEW + 4, 0, 32, send_len);
    zt_set_bits_to_le_u32(ptx_desc + TXDESC_OFFSET_NEW + 8, 0, 32, recv_len);

    if (send_len != 0)
    {
        zt_memcpy(ptx_desc + TXDESC_OFFSET_NEW + TX_CMD_PARAM_LENGTH, send_buf,
                  send_len * 4);
    }

    snd_pktLen = TXDESC_OFFSET_NEW + TX_CMD_PARAM_LENGTH + send_len * 4;

    zt_hif_bulk_cmd_init(hif_node);
    ret = zt_tx_queue_insert(hif_node, 1, ptx_desc, snd_pktLen, CMD_QUEUE_INX, NULL,
                             NULL, NULL);
    if (ret != 0)
    {
        LOG_E("bulk access cmd error by send");
        ret = -1;
        goto mcu_cmd_communicate_exit;
    }

    if (zt_hif_bulk_cmd_wait(hif_node, HIF_BULK_MSG_TIMEOUT) == 0)
    {
        LOG_E("bulk access cmd read timeout");
        hif_node->error_cnt_cmd++;
        if(hif_node->error_cnt_cmd >= HIF_HW_RECOVER_TH_CMD && (hif_node->nic_info[0])->is_init_commplete) {
            zt_wlan_mgmt_chip_reset(hif_node->nic_info[0], 0);
            hif_node->error_cnt_cmd = 0;
        }
        ret = -1;
        goto mcu_cmd_communicate_exit;
    } else {
        hif_node->error_cnt_cmd = 0;
    }

    prx_desc = hif_node->cmd_rcv_buffer;
    rcv_pktLen = RXDESC_OFFSET_NEW + recv_len * 4 + RX_CMD_PARAM_LENGTH;

    prx_desc = hif_node->cmd_rcv_buffer;
    u8Value = zt_le_u8_read(prx_desc);
    if ((u8Value & 0x03) != TYPE_CMD)
    {
        LOG_E("bulk access cmd read error");
        ret = -1;
        goto mcu_cmd_communicate_exit;
    }

    if (recv_len != 0)
    {
        zt_memcpy(recv_buf, prx_desc + RXDESC_OFFSET_NEW + RX_CMD_PARAM_LENGTH,
                  recv_len * 4);
    }

mcu_cmd_communicate_exit:

    return ret;
}

#ifdef CONFIG_ZT9101XV20_SUPPORT
struct device_info_ops ZT9101XV20_Info =
{
    .firmware_no = 0,
    .driver_flag = 0
};
#endif


#ifdef CONFIG_ZT9101XV30_SUPPORT
struct device_info_ops ZT9101XV30_Info =
{
    .firmware_no = 1,
    .driver_flag = 1
};
#endif


module_init(hif_init);
module_exit(hif_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ZTOP Wireless Lan Driver");
MODULE_AUTHOR("ZTOP Semiconductor Corp.");



