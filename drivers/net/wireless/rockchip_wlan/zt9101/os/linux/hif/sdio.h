/*
 * sdio.h
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
#ifndef __SDIO_H__
#define __SDIO_H__

#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>


#define MIN_RXD_SIZE      16

#define SDIO_BASE                   0x10250000

#define ZT_REG_HIMR                 0x9004
#define ZT_REG_HISR                 0x9008
#define ZT_REG_SZ_RX_REQ            0x9010
#define ZT_REG_HCTL                 0x903A
#define ZT_REG_PUB_FREEPG           0x901C
#define ZT_REG_HIG_FREEPG           0x9020
#define ZT_REG_MID_FREEPG           0x9024
#define ZT_REG_LOW_FREEPG           0x9028
#define ZT_REG_EXT_FREEPG           0x902C

#define ZT_REG_AC_OQT_FREEPG        0x9030
#define ZT_REG_TXCTL                0x9000

#define ZT_REG_QUE_PRI_SEL          0x906C


#define SDIO_RD                     1
#define SDIO_WD                     0

#define TX_RESERVED_PG_NUM         0

enum ZT_SDIO_OPERATION_FLAG
{
    ZT_SDIO_NORMAL_FLAG     = 0,
    ZT_SDIO_TRX_QUEUE_FLAG  = 1,
};

struct hif_sdio_management_;
typedef struct hif_sdio_management_
{
    zt_u8 sdio_id;
    struct sdio_func *func;
    zt_u32 sdio_himr;
    zt_u32 sdio_hisr;
    zt_u8 sdio_hisr_en;
    zt_u8 SdioTxOQTFreeSpace;
    zt_u8 tx_no_low_queue;
    zt_s32 tx_fifo_ppg_num;
    zt_s32 tx_fifo_lpg_num;
    zt_s32 tx_fifo_epg_num;
    zt_s32 tx_fifo_mpg_num;
    zt_s32 tx_fifo_hpg_num;
    struct work_struct irq_work;
    struct workqueue_struct *irq_wq;
    zt_u64 irq_cnt;
    zt_s32 int_flag;

    zt_u8 *tx_agg_buffer;
    zt_s32 free_tx_page;
    zt_s32 tx_state;

    void *current_irq;
    zt_u8 clk_pwr_save;/* support host sdio power saveing mode (if no data exchange
                          througth sdio port,the sdio clock will stable Hi-Z)*/
} hif_sdio_st;

zt_s32 sdio_init(void);
zt_s32 sdio_exit(void);




zt_s32 zt_sdioh_interrupt_disable(void *hif_info);
zt_s32 zt_sdioh_interrupt_enable(void *hif_info);
zt_s32 zt_sdioh_config(void *hif_info);

#if defined(CONFIG_SDIO_FLAG) || defined(CONFIG_BOTH_FLAG)
zt_s32 zt_sdio_update_txbuf_size(void *hif_info, void *qnode,
                                 zt_s32 *max_page_num, zt_s32 *max_agg_num);
zt_s32 zt_sdio_tx_flow_free_pg_ctl(void *hif_info, zt_u32 hw_queue,
                                   zt_u8 pg_num);
zt_s32 zt_sdio_tx_flow_agg_num_ctl(void *hif_info, zt_u8 agg_num);
#else
zt_inline static zt_s32 zt_sdio_update_txbuf_size(void *hif_info, void *qnode,
        zt_s32 *max_page_num, zt_s32 *max_agg_num)
{
    return 0;
}
zt_inline static zt_s32 zt_sdio_tx_flow_free_pg_ctl(void *hif_info,
        zt_u32 hw_queue, zt_u8 pg_num)
{
    return 0;
}
zt_inline static zt_s32 zt_sdio_tx_flow_agg_num_ctl(void *hif_info,
        zt_u8 agg_num)
{
    return 0;
}
#endif

#endif
