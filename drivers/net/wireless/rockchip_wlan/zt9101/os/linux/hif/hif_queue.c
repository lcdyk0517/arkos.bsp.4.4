/*
 * hif_queue.c
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

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/usb.h>

#include "common.h"
#include "zt_que.h"
#include "hif_queue.h"
#include "hif.h"
#include "rx_linux.h"
#ifdef CONFIG_MP_MODE
#include "proc_trx.h"
#endif

#define TX_AGG_BUFF_SIZE (32*1024)

static zt_s32 zt_data_queue_full(zt_que_t *queue, zt_s32 queue_node_num)
{
    return (queue_node_num == zt_que_count(queue));
}

zt_s32 zt_data_queue_insert(zt_que_t *queue, data_queue_node_st *qnode)
{
    //LOG_I("[%s] num:%d",__func__,queue->num);
    zt_enque_tail(&qnode->node, queue);
    return queue->cnt;
}

data_queue_node_st *zt_data_queue_remove(zt_que_t *queue)
{
    data_queue_node_st *rb_node     = NULL;
    zt_list_t    *head              = NULL;

    head    = zt_deque_head(queue);
    if (NULL != head)
    {
        rb_node = zt_list_entry(head, data_queue_node_st, node);
    }

    return rb_node;
}


data_queue_node_st *zt_queue_node_malloc(zt_s32 cnt)
{
    data_queue_node_st *node = NULL;

    node = zt_kzalloc(cnt * sizeof(data_queue_node_st));
    if (NULL == node)
    {
        LOG_E("[%s] zt_kzalloc failed ,check!!!!", __func__);
        return NULL;
    }

    return node;
}

zt_s32 zt_hif_queue_alloc_skb_one(struct sk_buff_head *skb_head, zt_u8 hif_type)
{
    struct sk_buff *pskb    = NULL;
    SIZE_PTR tmpaddr        = 0;
    SIZE_PTR alignment      = 0;
    if (hif_type == HIF_USB)
    {
        pskb = zt_alloc_skb(ZT_MAX_RECV_BUFF_LEN_USB + HIF_QUEUE_ALLOC_SKB_ALIGN_SZ);
    }
    else
    {
        pskb = zt_alloc_skb(ZT_MAX_RECV_BUFF_LEN_SDIO + HIF_QUEUE_ALLOC_SKB_ALIGN_SZ);
    }
    if (pskb)
    {
        // if(skb_is_nonlinear(pskb)) {
        //     LOG_E("[%s]: alloc skb is no linear", __func__);
        // }
        tmpaddr = (SIZE_PTR) pskb->data;
        alignment = tmpaddr & (HIF_QUEUE_ALLOC_SKB_ALIGN_SZ - 1);
        skb_reserve(pskb, (HIF_QUEUE_ALLOC_SKB_ALIGN_SZ - alignment));
        skb_trim(pskb, 0);
        skb_queue_tail(skb_head, pskb);
    }
    else
    {
        return ZT_RETURN_FAIL;
    }

    return ZT_RETURN_OK;
}

void zt_hif_queue_alloc_skb(struct sk_buff_head *skb_head, zt_u8 hif_type)
{
    zt_s32 i                   = 0;
    zt_s32 ret                 = 0;
    for (i = 0; i < HIF_QUEUE_ALLOC_SKB_NUM; i++)
    {
        ret = zt_hif_queue_alloc_skb_one(skb_head, hif_type);
        if (ZT_RETURN_FAIL == ret)
        {
            LOG_E("[%s] zt_hif_queue_alloc_skb failed.[%d]", __func__, i);
            break;
        }
    }
}

static void zt_hif_queue_free_skb(struct sk_buff_head *skb_head)
{
    struct sk_buff *skb = NULL;

    while ((skb = skb_dequeue(skb_head)) != NULL)
    {
        zt_free_skb(skb);
    }
}

zt_s32 zt_tx_queue_empty(void *hif_info)
{
    hif_node_st *hif_node = (hif_node_st *)hif_info;
    data_queue_mngt_st *trxq     = (data_queue_mngt_st *)&hif_node->trx_pipe;
    zt_que_t      *free_tx_queue = &trxq->free_tx_queue;
    //zt_que_t      *tx_queue = &trxq->tx_queue;

    //LOG_I("[free_tx_queue] num:%d",free_tx_queue->num);

    if (zt_data_queue_full(free_tx_queue, ZT_TX_MAX_DATA_QUEUE_NODE_NUM) == 1)
    {
        return 1;
    }
    //LOG_I("[free_tx_queue] cnt:%d, tx_queue cnt:%d",free_tx_queue->cnt,tx_queue->cnt);
    return 0;
}
zt_bool zt_free_tx_need_stop(hif_node_st *hif_node)
{
    data_queue_mngt_st *trxq     = (data_queue_mngt_st *)&hif_node->trx_pipe;
    zt_que_t      *free_tx_queue = &trxq->free_tx_queue;

    return (free_tx_queue->cnt <= ZT_TX_FREE_QUEUE_STOP_DATA_NUM && trxq->is_free_tx_stopped == zt_false);
}

zt_bool zt_free_tx_need_wake(hif_node_st *hif_node)
{
    data_queue_mngt_st *trxq     = (data_queue_mngt_st *)&hif_node->trx_pipe;
    zt_que_t      *free_tx_queue = &trxq->free_tx_queue;

    return (free_tx_queue->cnt >= ZT_TX_FREE_QUEUE_WAKE_DATA_NUM && trxq->is_free_tx_stopped == zt_true);
}


zt_s32 zt_tx_queue_insert(void *hif_info, zt_u8 agg_num, zt_s8 *buff,
                          zt_u32 buff_len, zt_u32 addr, zt_s32(*tx_callback_func)(void *tx_info,
                                  void *param), void *tx_info, void *param)
{
    zt_s32 ret = 0;
    hif_node_st *hif_node = (hif_node_st *)hif_info;
    data_queue_node_st *qnode = NULL;
    data_queue_mngt_st *trxq     = (data_queue_mngt_st *)&hif_node->trx_pipe;
    zt_que_t      *free_tx_queue = &trxq->free_tx_queue;
    zt_que_t      *tx_queue = &trxq->tx_queue;
    struct xmit_buf *pxmitbuf   = NULL;
    zt_s32 tmp_cnt   = 0;
    zt_timer_t timer;
    zt_s32  i;

    zt_os_api_lock_lock(&trxq->queu_txop_lock);
    /*if tx_queue is full, drop and return*/
    if (zt_data_queue_full(tx_queue, ZT_TX_MAX_DATA_QUEUE_NODE_NUM))
    {
        LOG_I("[%s] tx_queue is full", __func__);
        zt_os_api_lock_unlock(&trxq->queu_txop_lock);
        return ZT_RETURN_FAIL;
    }

    if (hif_node->nic_number == 2)
    {
        /*check free tx queue resource*/
        if (zt_free_tx_need_stop(hif_node))
        {
            for (i = 0; i < hif_node->nic_number; i++)
            {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
                netif_tx_stop_all_queues(hif_node->nic_info[i]->ndev);
#else
                netif_stop_queue(hif_node->nic_info[i]->ndev);
#endif
            }
            trxq->is_free_tx_stopped = zt_true;
        }
    }

    /*get node from free_tx_queue*/
    qnode = zt_data_queue_remove(free_tx_queue);
    if (qnode == NULL)
    {
        LOG_E("get node from free_tx_queue error free_tx_queue_cnt[%d]", free_tx_queue->cnt);
        if (tx_callback_func)
        {
            tx_callback_func(tx_info, param);
        }

        zt_os_api_lock_unlock(&trxq->queu_txop_lock);
        return ZT_RETURN_FAIL;
    }

    qnode->buff             = buff;
    qnode->real_size        = buff_len;
    qnode->addr             = addr;
    qnode->tx_info          = tx_info;
    qnode->param            = param;
    qnode->tx_callback_func = tx_callback_func;
    qnode->agg_num          = agg_num;
    qnode->state            = TX_STATE_IDL;
    qnode->pg_num           = (buff_len + 20 + 127) / 128;
    if (NULL != param)
    {
        pxmitbuf = param;
        qnode->encrypt_algo     = pxmitbuf->encrypt_algo;
        qnode->qsel             = pxmitbuf->qsel;
        qnode->ether_type       = pxmitbuf->ether_type;
        qnode->icmp_pkt         = pxmitbuf->icmp_pkt;
    }
    //LOG_I("[%s] addr:%x",__func__, addr);

    zt_data_queue_insert(tx_queue, qnode);
    qnode->state            = TX_STATE_INSERT;
    zt_os_api_lock_unlock(&trxq->queu_txop_lock);
    if (HIF_SDIO == hif_node->hif_type)
    {

#if HIF_QUEUE_TX_WORKQUEUE_USE
        trxq->tx_wq.ops->workqueue_work(&trxq->tx_wq);
#else
        zt_tasklet_hi_sched(&hif_node->trx_pipe.send_task);
#endif
    }
    else
    {
        //if (zt_que_count(tx_queue) <=1)
        {
            zt_tasklet_hi_sched(&hif_node->trx_pipe.send_task);
        }
    }
    return ret;
}

data_queue_node_st *zt_tx_queue_remove(data_queue_mngt_st *trxq)
{
    data_queue_node_st *qnode   = NULL;
    zt_que_t      *tx_queue     = &trxq->tx_queue;

    /*get node from free_tx_queue*/
    zt_os_api_lock_lock(&trxq->queu_txop_lock);
    qnode = zt_data_queue_remove(tx_queue);
    if (NULL != qnode)
    {
        if (TX_STATE_INSERT != qnode->state)
        {
            LOG_W("[%s], state[%d] != TX_STATE_INSERT", __func__, qnode->state);
        }
        qnode->state = TX_STATE_IN_PIP;
    }
    zt_os_api_lock_unlock(&trxq->queu_txop_lock);
    return qnode;
}
#ifdef CONFIG_SOFT_TX_AGGREGATION
/*check the packet that can't be agg*/
zt_bool pkt_is_agg_disable(data_queue_node_st *qnode)
{
    zt_bool ret = zt_false;
    if (0x0806 == qnode->ether_type)
    {
        //LOG_I("[%s,%d] ether_type:ARP", __func__, __LINE__);
        ret = zt_true;
    }
    else if (0x86dd == qnode->ether_type)
    {
        //LOG_I("[%s,%d] ether_type:IPv6", __func__, __LINE__);
        ret = zt_true;
    }
    else if (0x888e == qnode->ether_type)
    {
        //LOG_I("[%s,%d] ether_type:802.1x", __func__, __LINE__);
        ret = zt_true;
    }
    else if (0x8864 == qnode->ether_type)
    {
        //LOG_I("[%s,%d] ether_type:PPPoE", __func__, __LINE__);
        ret = zt_true;
    }
    else if (0x0800 == qnode->ether_type && qnode->icmp_pkt)
    {
        //LOG_I("[%s,%d] ether_type:IP--ICMP", __func__, __LINE__);
        ret = zt_true;
    }

    return ret;
}

static void agg_update_send_core(hif_node_st *hif_info,
                                 data_queue_node_st *agg_qnode, zt_u8  agg_num, zt_u32 pkt_len)
{
    hif_sdio_st *sd = &hif_info->u.sdio;
    zt_u8 send_pg_num = ZT_RND4(agg_qnode->pg_num);

    zt_tx_agg_num_fill(agg_num, sd->tx_agg_buffer);
    agg_qnode->buff         = sd->tx_agg_buffer;
    agg_qnode->real_size = pkt_len;
    agg_qnode->agg_num  = agg_num;
    sd->free_tx_page -= send_pg_num;
    sd->tx_state = TX_STATE_SENDING;
    hif_info->ops->hif_write(hif_info, ZT_SDIO_TRX_QUEUE_FLAG, agg_qnode->addr,
                             (zt_s8 *)agg_qnode, agg_qnode->real_size);
    sd->tx_state = TX_STATE_COMPETE;
    zt_sdio_tx_flow_free_pg_ctl(hif_info, agg_qnode->hw_queue, send_pg_num);
    zt_sdio_tx_flow_agg_num_ctl(hif_info, agg_qnode->agg_num);
}
static void zt_tx_work_agg(struct work_struct *work)
{
    data_queue_mngt_st *dqm         = NULL;
    zt_que_t *data_queue            = NULL;
    data_queue_node_st *qnode       = NULL;
    hif_sdio_st *sd                 = NULL;
    data_queue_node_st agg_qnode;
    hif_node_st *hif_info           = NULL;
    zt_s32 pkt_len                  = 0;
    zt_s32 max_page_num             = 0;
    zt_s32 max_agg_num              = 0;
    zt_s32 agg_num                  = 0;
    zt_bool bret                    = 0;
    zt_bool agg_break               = zt_false;
    zt_u32 align_size               = 0;
    zt_workqueue_mgnt_st *wq_mgt    = NULL;
    wq_mgt = container_of(work, zt_workqueue_mgnt_st, work);
    dqm = container_of(wq_mgt, data_queue_mngt_st, tx_wq);
    if (NULL == dqm)
    {
        return;
    }

    hif_info = container_of(dqm, hif_node_st, trx_pipe);
    if (NULL == hif_info)
    {
        return;
    }

    data_queue = &dqm->tx_queue;
    if (NULL == data_queue)
    {
        return;
    }
    sd = &hif_info->u.sdio;

    zt_memset(&agg_qnode, 0, sizeof(agg_qnode));
    while (NULL != (qnode = zt_tx_queue_remove(dqm)))
    {
        if (hm_get_mod_removed() == zt_true || hif_info->dev_removed == zt_true)
        {
            return;
        }

        sd->tx_state = qnode->state = TX_STATE_FLOW_CTL;
        if (ZT_PKT_TYPE_FRAME != (qnode->buff[0] & 0x03))  //not need flow control
        {

            sd->tx_state = qnode->state = TX_STATE_SENDING;
            hif_info->ops->hif_write(hif_info, ZT_SDIO_TRX_QUEUE_FLAG, qnode->addr,
                                     (zt_s8 *)qnode, qnode->real_size);

        }
        else
        {

            if (zt_true == pkt_is_agg_disable(qnode))
            {
                agg_break = zt_true;
            }
            else
            {
                agg_break = zt_false;
            }
            //update max_page_num, max_agg_num
            if (0 == agg_num || agg_qnode.qsel != qnode->qsel)
            {
                zt_sdio_update_txbuf_size(hif_info, qnode, &max_page_num, &max_agg_num);
            }

            align_size = TXDESC_SIZE + ZT_RND_MAX((qnode->real_size - TXDESC_SIZE), 8);
            if ((0 != agg_num &&
                    ZT_RND4(agg_qnode.pg_num + qnode->pg_num) > max_page_num) ||
                    (0 != agg_num &&  agg_qnode.qsel != qnode->qsel)                ||
                    (0 != agg_num && agg_num > max_agg_num)                        ||
                    (0 != agg_num && zt_true == agg_break)
               )
            {

                agg_update_send_core(hif_info, &agg_qnode, agg_num, pkt_len);
                agg_num = 0;
                pkt_len = 0;

                //for current qnode
                zt_sdio_update_txbuf_size(hif_info, qnode, &max_page_num, &max_agg_num);

                zt_memcpy(&agg_qnode, qnode, sizeof(agg_qnode));

                agg_num++;
                zt_memcpy(sd->tx_agg_buffer + pkt_len, qnode->buff, qnode->real_size);
                pkt_len += align_size ;

            }
            else
            {

                agg_num++;
                if (1 == agg_num)
                {
                    zt_memcpy(&agg_qnode, qnode, sizeof(agg_qnode));
                }
                else
                {
                    agg_qnode.pg_num    += qnode->pg_num;
                }

                zt_memcpy(sd->tx_agg_buffer + pkt_len, qnode->buff, qnode->real_size);
                pkt_len += align_size ;

            }
        }

        if (qnode->tx_callback_func)
        {
            bret = qnode->tx_callback_func(qnode->tx_info, qnode->param);
            if (zt_true == bret)
            {

            }
            else
            {
                LOG_W("[%s,%d] tx_callback_func failed", __func__, __LINE__);
            }
        }

        qnode->state = TX_STATE_COMPETE;
        zt_data_queue_insert(&hif_info->trx_pipe.free_tx_queue, qnode);
    }

    if (pkt_len > 0 && agg_num > 0)
    {
        agg_update_send_core(hif_info, &agg_qnode, agg_num, pkt_len);
    }
}
#else
static void zt_tx_work(struct work_struct *work)
{
    data_queue_mngt_st *dqm     = NULL;
    zt_que_t *data_queue        = NULL;
    data_queue_node_st *qnode   = NULL;
    hif_node_st *hif_info       = NULL;

    zt_workqueue_mgnt_st *wq_mgt = NULL;
    wq_mgt = container_of(work, zt_workqueue_mgnt_st, work);
    dqm = container_of(wq_mgt, data_queue_mngt_st, tx_wq);

    if (NULL == dqm)
    {
        return;
    }
    data_queue = &dqm->tx_queue;
    if (NULL == data_queue)
    {
        return;
    }

    while (NULL != (qnode = zt_tx_queue_remove(dqm)))
    {

        hif_info = qnode->hif_node;

        if (HIF_USB == hif_info->hif_type)
        {
            hif_info->ops->hif_write(hif_info, ZT_USB_NET_PIP, qnode->addr, (zt_s8 *)qnode,
                                     qnode->real_size);
        }
        else
        {
            //LOG_I("[%s] addr:%x",__func__, qnode->addr);
            hif_info->ops->hif_write(hif_info, ZT_SDIO_TRX_QUEUE_FLAG, qnode->addr,
                                     (zt_s8 *)qnode, qnode->real_size);
        }
    }
}

#endif

zt_s32 hif_frame_dispath(hif_node_st *hif_info, struct sk_buff *skb)
{
    zt_u8 *pbuf;
    zt_s32 remain_len;
    zt_u16 hdr_len;
    zt_u16 once_len;
    zt_u16 pkt_len;
    zt_u16 usb_agg_index = 0;
    zt_bool  valid, notice;
    zt_s32 num;
    struct net_device *ndev = NULL;

    pbuf = skb->data;
    remain_len = skb->len;

    do
    {
        pkt_len = zt_rx_get_pkt_len_and_check_valid((hif_info->drv_ops->driver_flag >
                  0), pbuf, remain_len, &hdr_len, &valid,
                  &notice);
        if (valid != zt_true)
        {
            LOG_E("zt_rx_get_pkt_len_and_check_valid error! agg index:%d, tot_len:%d, remain_len:%d, pkt_len: %d",
                  usb_agg_index, skb->len, remain_len, pkt_len);
            //  {
            //     zt_s32 i;
            //     for(i = 0; i < skb->len;)
            //     {
            //         printk("0x%02X, ", skb->data[i]);
            //         i++;
            //         if((i % 16) == 0) {
            //            printk("\r\n");
            //         }
            //     }
            // }
            return 0;
        }
#ifdef CONFIG_MP_MODE
        ZT_UNUSED(num);
        ZT_UNUSED(ndev);
        zt_mp_proc_rx_common_process(hif_info->nic_info[0], pbuf, pkt_len);
#else
        num = 0;
        while (num < hif_info->nic_number)
        {
            if (NULL == hif_info->nic_info[num])
            {
                LOG_W("%s  nic_info[%d] NULL", __func__, num);
                return ZT_RETURN_FAIL;
            }

            ndev = hif_info->nic_info[num]->ndev;

            if (ndev == NULL)
            {
                LOG_W("%s  ndev[%d] NULL", __func__, num);
                return ZT_RETURN_FAIL;
            }

            if (!hif_info->nic_info[num]->is_up)
            {
                num++;
                continue;
            }
#ifdef CFG_ENABLE_MONITOR_MODE
            if (zt_local_cfg_get_work_mode(hif_info->nic_info[num]) == ZT_MONITOR_MODE)
            {
                mpdu_process(ndev, pbuf, pkt_len);
            }
            else
#endif
            {
                if (zt_memcmp(ndev->dev_addr, get_ra(&pbuf[hdr_len]), ETH_ALEN) == 0)
                {
                    mpdu_process(ndev, pbuf, pkt_len);
                    break;
                }
                else if (IS_MCAST(get_ra(&pbuf[hdr_len])))
                {
                    mpdu_process(ndev, pbuf, pkt_len);
                }
            }
            num++;
        }
#endif

        usb_agg_index++;
        once_len = ZT_RND8(pkt_len);
        pbuf += once_len;
        remain_len -= once_len;
    } while (remain_len > 0);

    return 0;
}

zt_s32 hif_tasklet_rx_handle(hif_node_st *hif_info)
{
    struct sk_buff *pskb  = NULL;
    data_queue_node_st *qnode       = NULL;
    zt_s32 ret = 0;

    if (NULL == hif_info)
    {
        LOG_E("[%s] hif_info is null", __func__);
        return 0;
    }

    while (1)
    {
        pskb = skb_dequeue(&hif_info->trx_pipe.rx_queue);
        if (NULL == pskb)
        {
            break;
        }

        if (hm_get_mod_removed() == zt_true || hif_info->dev_removed == zt_true)
        {
            skb_trim(pskb, 0);
            skb_queue_tail(&hif_info->trx_pipe.free_rx_queue_skb, pskb);
            continue;
        }

        ret = hif_frame_dispath(hif_info, pskb);
        if (ret)
        {
            //LOG_W("[%s] failed ret:%d",__func__,ret);
        }

        skb_trim(pskb, 0);
        skb_queue_tail(&hif_info->trx_pipe.free_rx_queue_skb, pskb);

        if (HIF_USB == hif_info->hif_type)
        {
            /* check urb node, if have free, used it */
            if (NULL != (qnode = zt_data_queue_remove(&hif_info->trx_pipe.free_rx_queue)))
            {
                hif_info->ops->hif_read(hif_info, ZT_USB_NET_PIP, READ_QUEUE_INX,
                                        (zt_u8 *)qnode, ZT_MAX_RECV_BUFF_LEN_USB);
            }
        }
    }

    return 0;
}



zt_s32 hif_tasklet_tx_handle(hif_node_st *hif_info)
{
    data_queue_mngt_st *dqm     = &hif_info->trx_pipe;
    data_queue_node_st *qnode   = NULL;

    while (NULL != (qnode = zt_tx_queue_remove(dqm)))
    {
        if (hm_get_mod_removed() == zt_true || hif_info->dev_removed == zt_true)
        {
            return 0;
        }

        hif_info = qnode->hif_node;
        if (HIF_USB == hif_info->hif_type)
        {
            hif_info->ops->hif_write(hif_info, ZT_USB_NET_PIP, qnode->addr, (zt_s8 *)qnode,
                                     qnode->real_size);
        }
        else
        {
            hif_info->ops->hif_write(hif_info, ZT_SDIO_TRX_QUEUE_FLAG, qnode->addr,
                                     (zt_s8 *)qnode, qnode->real_size);
        }
    }
    return 0;
}

zt_s32 zt_hif_queue_enable(hif_node_st *hif_node)
{
    data_queue_mngt_st *hqueue  = NULL;
    data_queue_node_st  *qnode  = NULL;

    LOG_D("[%s] begin", __func__);

    hqueue  = &hif_node->trx_pipe;

    /*rx queue*/
    if (HIF_USB == hif_node->hif_type)
    {
        while (NULL != (qnode = zt_data_queue_remove(
                                    &hif_node->trx_pipe.free_rx_queue)))
        {
            hif_node->ops->hif_read(hif_node, ZT_USB_NET_PIP, READ_QUEUE_INX,
                                    (zt_u8 *)qnode, ZT_MAX_RECV_BUFF_LEN_USB);
        }

        hif_node->hif_tr_ctrl = zt_true;
    }
    LOG_D("[%s] end", __func__);
    return ZT_RETURN_OK;
}


zt_s32 zt_hif_queue_disable(hif_node_st *hif_node)
{
    struct sk_buff *pskb    = NULL;

    hif_node->hif_tr_ctrl = zt_false;

    /*clean rx queue*/
    while (NULL != (pskb = skb_dequeue(&hif_node->trx_pipe.rx_queue)))
    {
        zt_free_skb(pskb);
        pskb = NULL;
    }

    return ZT_RETURN_OK;
}

zt_s32 zt_data_queue_mngt_init(void *hif_node)
{
    zt_s32 i                                           = 0;
    data_queue_node_st *recv_node                   = NULL;
    data_queue_node_st *send_node                   = NULL;
    hif_node_st *hif_info                           = (hif_node_st *)hif_node;
    data_queue_mngt_st *data_queue_mngt             = &hif_info->trx_pipe;
#ifdef CONFIG_SOFT_TX_AGGREGATION
    static zt_workqueue_func_param_st wq_tx_param   = {"zt_tx_workqueue", zt_tx_work_agg};
#else
    static zt_workqueue_func_param_st wq_tx_param   = {"zt_tx_workqueue", zt_tx_work};
#endif

    skb_queue_head_init(&data_queue_mngt->rx_queue);
    skb_queue_head_init(&data_queue_mngt->free_rx_queue_skb);
    zt_que_init(&data_queue_mngt->free_rx_queue, ZT_LOCK_TYPE_IRQ);

    zt_que_init(&data_queue_mngt->free_tx_queue, ZT_LOCK_TYPE_IRQ);
    zt_que_init(&data_queue_mngt->tx_queue, ZT_LOCK_TYPE_IRQ);
    zt_os_api_lock_init(&data_queue_mngt->queu_txop_lock, ZT_LOCK_TYPE_NONE);

    data_queue_mngt->alloc_cnt = 0;

#ifdef CONFIG_SOFT_TX_AGGREGATION
    if (HIF_SDIO == hif_info->hif_type)
    {
        hif_info->u.sdio.tx_agg_buffer = zt_kzalloc(TX_AGG_BUFF_SIZE);
        if (NULL == hif_info->u.sdio.tx_agg_buffer)
        {
            LOG_E("[%s] tx_agg_buffer failed", __func__);
            return 0;
        }
        hif_info->u.sdio.free_tx_page = 0;
    }
#endif

    zt_tasklet_init(&data_queue_mngt->recv_task,
                    (void *)hif_tasklet_rx_handle, (zt_ptr)hif_node);



    /*tx queue init*/
    zt_os_api_workqueue_register(&data_queue_mngt->tx_wq, &wq_tx_param);
    zt_tasklet_init(&data_queue_mngt->send_task,
                    (void *)hif_tasklet_tx_handle, (zt_ptr)hif_node);


    data_queue_mngt->alloc_cnt++;
    zt_hif_queue_alloc_skb(&data_queue_mngt->free_rx_queue_skb, hif_info->hif_type);

    /*rx queue init */
    hif_info->trx_pipe.all_rx_queue = zt_queue_node_malloc(
                                          ZT_RX_MAX_DATA_QUEUE_NODE_NUM);
    for (i = 0; i < ZT_RX_MAX_DATA_QUEUE_NODE_NUM; i++)
    {
        recv_node = hif_info->trx_pipe.all_rx_queue + i;

        //LOG_I("[%s] [%d] recv_node:%p",__func__,i,recv_node);
        recv_node->hif_node = hif_node;
        recv_node->real_size = 0;
        recv_node->node_id = i;
        if (NULL != hif_info->ops->hif_alloc_buff &&
                0 != hif_info->ops->hif_alloc_buff(recv_node))
        {
            if (HIF_USB == hif_info->hif_type)
            {
                LOG_E("[%s] hif_alloc_buff failed", __func__);
                zt_free_skb((struct sk_buff *)recv_node->buff);
                zt_kfree(recv_node);
            }
            continue;
        }

        zt_enque_tail(&recv_node->node, &data_queue_mngt->free_rx_queue);
        recv_node = NULL;
    }

    hif_info->trx_pipe.all_tx_queue = zt_queue_node_malloc(
                                          ZT_TX_MAX_DATA_QUEUE_NODE_NUM);
    for (i = 0; i < ZT_TX_MAX_DATA_QUEUE_NODE_NUM; i++)
    {

        send_node = hif_info->trx_pipe.all_tx_queue + i;
        //LOG_I("[%s] [%d] send_node:%p",__func__,i,send_node);
        send_node->hif_node = hif_info;
        send_node->real_size    = 0;
        send_node->node_id      = i;
        if (NULL != hif_info->ops->hif_alloc_buff &&
                0 != hif_info->ops->hif_alloc_buff(send_node))
        {
            if (HIF_USB == hif_info->hif_type)
            {
                LOG_E("[%s] hif_alloc_buff failed", __func__);
                zt_free_skb((struct sk_buff *)send_node->buff);
            }
            continue;
        }

        zt_enque_tail(&send_node->node, &data_queue_mngt->free_tx_queue);
        send_node = NULL;
    }


    data_queue_mngt->is_init = zt_true;
    data_queue_mngt->is_free_tx_stopped = zt_false;

    return ZT_RETURN_OK;
}


zt_s32 zt_data_queue_mngt_term(void *hif_node)
{
    zt_list_t *pos = NULL;
    zt_list_t *next = NULL;
    data_queue_node_st *data_node = NULL;
    data_queue_node_st *tx_node = NULL;
    hif_node_st  *hif_info      = (hif_node_st *)hif_node;
    data_queue_mngt_st *trxq    = &hif_info->trx_pipe;

    if (trxq)
    {
        tasklet_kill(&trxq->recv_task);
    }
#if HIF_QUEUE_TX_WORKQUEUE_USE
    if (trxq->tx_wq.ops)
    {
        if (trxq->tx_wq.ops->workqueue_term)
        {
            trxq->tx_wq.ops->workqueue_term(&trxq->tx_wq);
        }
    }
    else
    {
        return ZT_RETURN_OK;
    }
#else
    if (trxq)
    {
        tasklet_kill(&trxq->send_task);
    }
#endif

    if (HIF_SDIO == hif_info->hif_type)
    {
        if (hif_info->u.sdio.tx_agg_buffer)
        {
            zt_kfree(hif_info->u.sdio.tx_agg_buffer);
            hif_info->u.sdio.tx_agg_buffer = NULL;
        }

    }
    //LOG_I("[%s,%d]",__func__,__LINE__);
    /*rx queue freee */
    // zt_os_api_lock_lock(&trxq->free_rx_queue.lock); // no need lock, lock will cause usb_kill_urb run bug, by renhaibo
    zt_list_for_each_safe(pos, next, &trxq->free_rx_queue.head)
    {
        data_node = zt_list_entry(pos, data_queue_node_st, node);
        if (data_node)
        {
            hif_info  = (hif_node_st *)data_node->hif_node;
            if (HIF_USB == hif_info->hif_type)
            {
                hif_info->ops->hif_free_buff(data_node);
            }

            zt_list_delete(&data_node->node);
            data_node = NULL;
            trxq->free_rx_queue.cnt--;

        }

    }
    //zt_os_api_lock_unlock(&trxq->free_rx_queue.lock);
    zt_que_deinit(&trxq->free_rx_queue);

    if (trxq->all_rx_queue)
    {
        zt_kfree(trxq->all_rx_queue);
        trxq->all_rx_queue = NULL;
    }

    //LOG_I("[%s,%d]",__func__,__LINE__);
    /*tx queue free*/
    //zt_os_api_lock_lock(&trxq->free_tx_queue.lock);   // no need lock, lock will cause usb_kill_urb run bug, by renhaibo
    zt_list_for_each_safe(pos, next, &trxq->free_tx_queue.head)
    {
        tx_node = zt_list_entry(pos, data_queue_node_st, node);
        if (tx_node)
        {
            hif_info  = (hif_node_st *)tx_node->hif_node;
            if (HIF_USB == hif_info->hif_type)
            {
                hif_info->ops->hif_free_buff(tx_node);
            }
            zt_list_delete(&tx_node->node);
            tx_node = NULL;
            trxq->free_tx_queue.cnt--;
        }
    }
    //zt_os_api_lock_unlock(&trxq->free_tx_queue.lock);
    zt_que_deinit(&trxq->free_tx_queue);
    zt_que_deinit(&trxq->tx_queue);
    zt_os_api_lock_term(&trxq->queu_txop_lock);
    if (trxq->all_tx_queue)
    {
        zt_kfree(trxq->all_tx_queue);
        trxq->all_tx_queue = NULL;
    }

    zt_hif_queue_free_skb(&hif_info->trx_pipe.free_rx_queue_skb);

    return ZT_RETURN_OK;
}

