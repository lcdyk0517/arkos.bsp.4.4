/*
 * hif_queue.h
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
#ifndef __HIF_QUEUE_H__
#define __HIF_QUEUE_H__

#include <linux/list.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>

/*Both network cards are streaming simultaneously, and because Urb sends data asynchronously, it is possible for the free_tx_queue count to run out*/
#define ZT_TX_ADD_DOUBLE_NIC_DATA_NUM   (10)
#define ZT_TX_FREE_QUEUE_STOP_DATA_NUM   (4)
#define ZT_TX_FREE_QUEUE_WAKE_DATA_NUM   (8)

#ifdef CONFIG_STA_AND_AP_MODE
#define ZT_TX_MAX_DATA_QUEUE_NODE_NUM   \
    (XMIT_DATA_BUFFER_CNT + XMIT_MGMT_BUFFER_CNT + XMIT_CMD_BUFFER_CNT + ZT_TX_ADD_DOUBLE_NIC_DATA_NUM)
#else
#define ZT_TX_MAX_DATA_QUEUE_NODE_NUM   \
    (XMIT_DATA_BUFFER_CNT + XMIT_MGMT_BUFFER_CNT + XMIT_CMD_BUFFER_CNT)
#endif
#define ZT_RX_MAX_DATA_QUEUE_NODE_NUM   (14)

#define ZT_MAX_RECV_BUFF_LEN_USB        (1024 * 4)
#define ZT_MAX_RECV_BUFF_LEN_SDIO       (1024 * 8 + 512)

#define HIF_QUEUE_TX_WORKQUEUE_USE      (1)

#define HIF_QUEUE_ALLOC_SKB_ALIGN_SZ    (8)
#define HIF_QUEUE_ALLOC_SKB_NUM         (16)
#define HIF_MAX_ALLOC_CNT               (4)

typedef enum DATA_QUEUE_NODE_STATUS_
{
    TX_STATE_IDL   = 0,
    TX_STATE_INSERT = 1,
    TX_STATE_IN_PIP  = 2,
    TX_STATE_FLOW_CTL = 3,
    TX_STATE_FLOW_CTL_SECOND = 4,
    TX_STATE_SENDING = 5,
    TX_STATE_COMPETE = 6,
} hif_queue_node_state_enum;
typedef struct
{
    zt_list_t node;
    void *hif_node;//point to hif_node_st
    zt_u8 *buff; //point to sk_buff for rx
    zt_u32 buff_size;
    zt_u32 real_size;
    zt_u32 addr;
    zt_u8  agg_num;
    zt_u8  pg_num;
    zt_u8 encrypt_algo;
    zt_u8 qsel;
    zt_u16 ether_type;
    zt_u8 icmp_pkt;
    zt_u8 hw_queue;
    zt_u32 fifo_addr;
    void *hif_dev;
    void *tx_info;
    void *param;
    zt_s32(*tx_callback_func)(void *tx_info, void *param);
    zt_s32 node_id;
    hif_queue_node_state_enum state;
} data_queue_node_st;


typedef struct trx_queue_st_
{
    struct sk_buff_head rx_queue;
    struct sk_buff_head free_rx_queue_skb;
    zt_u8  alloc_cnt;
    zt_que_t free_rx_queue;
    data_queue_node_st *all_rx_queue;

    zt_que_t free_tx_queue;
    zt_que_t tx_queue;//
    data_queue_node_st *all_tx_queue;

    zt_os_api_lock_t queu_txop_lock; //for tx queue operation lock.

    zt_u64 rx_queue_cnt;
    zt_u64 tx_queue_cnt;

    /*hif tx handle*/
    zt_workqueue_mgnt_st tx_wq;
    struct tasklet_struct send_task;

    /*hif rx handle*/
    zt_workqueue_mgnt_st rx_wq;
    struct tasklet_struct recv_task;
    zt_bool is_init;
    zt_bool is_free_tx_stopped;
} data_queue_mngt_st;

zt_s32 zt_data_queue_mngt_init(void *hif_node);
zt_s32 zt_data_queue_mngt_term(void *hif_node);

data_queue_node_st *zt_tx_queue_remove(data_queue_mngt_st *trxq);
zt_s32 zt_tx_queue_insert(void *hif_info, zt_u8 agg_num, zt_s8 *buff,
                          zt_u32 buff_len, zt_u32 addr,
                          zt_s32(*tx_callback_func)(void *tx_info, void *param), void *tx_info,
                          void *param);

zt_s32 zt_tx_queue_empty(void *hif_info);

zt_s32 zt_data_queue_insert(zt_que_t *queue, data_queue_node_st *qnode);
data_queue_node_st *zt_data_queue_remove(zt_que_t *queue);


void zt_hif_queue_alloc_skb(struct sk_buff_head *skb_head, zt_u8 hif_type);

#endif
