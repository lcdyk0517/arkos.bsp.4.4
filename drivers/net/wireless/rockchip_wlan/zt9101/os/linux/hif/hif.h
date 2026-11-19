/*
 * hif.h
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
#ifndef __HIF_H__
#define __HIF_H__
#include <linux/mutex.h>
#include <linux/completion.h>

#include "zt_list.h"
#include "sdio.h"
#include "usb.h"
#include "ndev_linux.h"
#include "hif_queue.h"

typedef enum hif_enum_
{
    HIF_USB     = 1,
    HIF_SDIO    = 2,
} hif_enum;

#define MAX_NIC 5

struct hif_node_;
typedef struct hif_node_ hif_node_st;

/*node operation*/
struct hif_node_ops
{
    zt_s32(*hif_write)(struct hif_node_ *node, zt_u8 flag, zt_u32 addr,
                       zt_s8 *data, zt_s32 datalen);
    zt_s32(*hif_read)(struct hif_node_ *node, zt_u8 flag, zt_u32 addr,
                      zt_s8 *data, zt_s32 datalen);
    zt_s32(*hif_show)(struct hif_node_ *node);
    zt_s32(*hif_init)(struct hif_node_ *node);
    zt_s32(*hif_exit)(struct hif_node_ *node);
    zt_s32(*hif_insert_netdev)(struct hif_node_ *node);
    zt_s32(*hif_remove_netdev)(struct hif_node_ *node);
    zt_s32(*hif_tx_queue_insert)(void *hif_info, zt_u8 agg_num, zt_s8 *buff,
                                 zt_u32 buff_len, zt_u32 addr,
                                 zt_s32(*tx_callback_func)(void *tx_info, void *param), void *tx_info,
                                 void *param);
    zt_s32(*hif_tx_queue_empty)(void *hif_info);
    zt_s32(*hif_alloc_buff)(data_queue_node_st *data_node);
    zt_s32(*hif_free_buff)(data_queue_node_st *data_node);
};

struct device_info_ops
{
    const uint8_t firmware_no;
    const uint8_t driver_flag;
};

struct hif_node_
{
    zt_u8 node_id;
    zt_list_t next;
    hif_enum hif_type;//can be HIF_USB, HIF_SDIO
    union
    {
        hif_sdio_st   sdio;
        hif_usb_mngt  usb;
    } u;

    /*common part */
    struct hif_node_ops *ops;
    struct device_info_ops *drv_ops;
    nic_info_st *nic_info[MAX_NIC];
    data_queue_mngt_st trx_pipe;
    zt_bool hif_tr_ctrl;
    zt_bool dev_removed;
    zt_u32 nic_number;
    /*proc debug system*/
    void *proc_info;

    /* usb or sdio rx handle info */
#define HIF_BULK_MSG_TIMEOUT    5000
    struct mutex      reg_mutex;
    struct completion fw_completion;
    struct completion cmd_completion;
    zt_u8 *fw_buffer;
    zt_u8 *cmd_snd_buffer;
    zt_u8 *cmd_rcv_buffer;
    zt_u16 reg_size;
    zt_u16 fw_size;
    zt_u16 cmd_size;
    zt_u8 cmd_completion_flag;
    zt_bool bulk_enable;

    zt_u32 wdn_id_bitmap;
    zt_u32 cam_id_bitmap;
    zt_os_api_lock_t mlme_hw_access_lock;
    zt_os_api_lock_t mcu_hw_access_lock;
	zt_u8 error_cnt_cmd;
    zt_u8 hw_ch;
    zt_u8 hw_bw;
    zt_u8 hw_offset;
};

struct hif_firmware_info
{
    zt_u8  fw_rom_type;
    zt_u32 fw0_size;
    zt_u32 fw1_size;
    zt_s8  *fw0;
    zt_s8  *fw1;
};

typedef struct hif_management_
{
    zt_u8 usb_num; //usb number in hif_usb_sdio
    zt_u8 sdio_num; //sdio number in hif_usb_sdio
    zt_u8 hif_num; //all usb and sido number node in hif_usb_sdio, so hif_num = usb_num+sdio_num

    zt_u64 id_bitmap;
    zt_u64 usb_id_bitmap;
    zt_u64 sdio_id_bitmap;

    zt_u32  cfg_size;
    zt_s8   *cfg_content;

    zt_u8 *fw_path;
    zt_u8 *fw1_path;
    zt_s8 *ifname, *if2name;

    const char *fw_full_path[2];

    zt_list_t hif_usb_sdio;
    zt_os_api_lock_t lock_mutex;

} hif_mngent_st;


/*hm: hif management*/

typedef enum
{
    HM_ADD = 1,//usb_num,sdio_num and hif_num do add operation
    HM_SUB = 2 //usb_num,sdio_num and hif_num do subtraction operation
} HM_OPERATION;

zt_bool hm_get_mod_removed(void);

zt_list_t *hm_get_list_head(void);
zt_u8 hm_new_id(zt_s32 *err);
zt_u8 hm_new_usb_id(zt_s32 *err);
zt_u8 hm_new_sdio_id(zt_s32 *err);
zt_u8 hm_del_id(zt_u8 id);
zt_u8 hm_del_usb_id(zt_u8 id);
zt_u8 hm_del_sdio_id(zt_u8 id);
zt_os_api_lock_t *hm_get_lock(void);

zt_u8 hm_set_num(HM_OPERATION op);
zt_u8 hm_set_usb_num(HM_OPERATION op);
zt_u8 hm_set_sdio_num(HM_OPERATION op);
hif_mngent_st *hif_mngent_get(void);

int hif_firmware_read_get(zt_u8 firmware_no,
                          struct hif_firmware_info *firmware_info);
void hif_firmware_read_free(struct hif_firmware_info *firmware_info);

void hif_node_register(hif_node_st **node, hif_enum type,
                       struct hif_node_ops *ops);
void hif_node_unregister(hif_node_st *node);

zt_s32 hif_exception_handle(void);
zt_s32 hif_chip_reset(hif_node_st *hif_info);
zt_s32  hif_dev_insert(hif_node_st *hif_info);
void hif_dev_removed(hif_node_st *hif_info);


zt_s32 hif_io_write(void *node,  zt_u8 flag, zt_u32 addr, zt_s8 *data,
                    zt_s32 datalen);
zt_s32 hif_io_read(void *node,  zt_u8 flag, zt_u32 addr, zt_s8 *data,
                   zt_s32 datalen);
zt_s32 hif_io_enable(void *node);
zt_s32 hif_io_disable(void *node);
zt_u8 hif_io_read8(void *node, zt_u32 addr, zt_s32 *err);
zt_ptr hif_io_read16(void *node, zt_u32 addr, zt_s32 *err);
zt_u32 hif_io_read32(void *node, zt_u32 addr, zt_s32 *err);
zt_s32 hif_io_write8(void *node, zt_u32 addr, zt_u8 value);
zt_s32 hif_io_write16(void *node, zt_u32 addr, zt_ptr value);
zt_s32 hif_io_write32(void *node, zt_u32 addr, zt_u32 value);

zt_s32 zt_hif_queue_enable(hif_node_st *hif_node);
zt_s32 zt_hif_queue_disable(hif_node_st *hif_node);

zt_s32 zt_hif_bulk_enable(hif_node_st *hif_node);
zt_s32 zt_hif_bulk_disable(hif_node_st *hif_node);

void zt_hif_bulk_fw_init(hif_node_st *hif_node);
zt_s32 zt_hif_bulk_fw_wait(hif_node_st *hif_node, zt_u32 timeout);
void zt_hif_bulk_fw_post(hif_node_st *hif_node, zt_u8 *buff, zt_u16 len);
void zt_hif_bulk_cmd_init(hif_node_st *hif_node);
void zt_hif_bulk_cmd_post(hif_node_st *hif_node, zt_u8 *buff, zt_u16 len);
void zt_hif_bulk_cmd_post_exit(hif_node_st *hif_node);

zt_s32 zt_hif_bulk_rxd_type(zt_u8 *prx_desc);

zt_s32 hif_write_firmware(void *node, zt_u8 which,  zt_u8 *firmware,
                          zt_u32 len);
zt_s32 hif_write_cmd(void *node, zt_u32 cmd, zt_u32 *send_buf, zt_u32 send_len,
                     zt_u32 *recv_buf, zt_u32 recv_len);

zt_s32 hif_tasklet_rx_handle(hif_node_st *hif_info);
zt_s32 hif_tasklet_tx_handle(hif_node_st *hif_info);


#ifdef CONFIG_ZT9101XV20_SUPPORT
extern struct device_info_ops ZT9101XV20_Info;
#endif


#ifdef CONFIG_ZT9101XV30_SUPPORT
extern struct device_info_ops ZT9101XV30_Info;
#endif

#endif
