/*
 * proc.c
 *
 * used for print logs
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

#ifdef CONFIG_MP_MODE
#include "common.h"
#include "proc.h"
#include "ndev_linux.h"
#include "hif.h"
#include "proc_trx.h"

#define N_BYTE_ALIGMENT(value, aligment) ((aligment == 1) ? (value) : (((value + aligment - 1) / aligment) * aligment))

#define RCR_APPFCS              ZT_BIT(31)
#define RCR_APP_MIC             ZT_BIT(30)
#define RCR_APP_ICV             ZT_BIT(29)
#define RCR_APP_PHYST_RXFF      ZT_BIT(28)
#define RCR_HTC_LOC_CTRL        ZT_BIT(14)
#define RCR_AMF                 ZT_BIT(13)
#define RCR_ADF                 ZT_BIT(11)
#define RCR_ACF                 ZT_BIT(12)
#define RCR_ACRC32              ZT_BIT(8)
#define RCR_CBSSID_BCN          ZT_BIT(7)
#define RCR_CBSSID_DATA         ZT_BIT(6)
#define RCR_APWRMGT             ZT_BIT(5)
#define RCR_AB                  ZT_BIT(3)
#define RCR_AM                  ZT_BIT(2)
#define RCR_APM                 ZT_BIT(1)
#define RCR_AAP                 ZT_BIT(0)

zt_s32 zt_mp_proc_rate_to_rateidx(zt_u32 rate)
{
    zt_s32 ret_rate = MGN_1M;

    switch (rate)
    {
        case WL_MGN_1M:
            ret_rate = DESC_RATE1M;
            break;
        case WL_MGN_2M:
            ret_rate = DESC_RATE2M;
            break;
        case WL_MGN_5_5M:
            ret_rate = DESC_RATE5_5M;
            break;
        case WL_MGN_6M:
            ret_rate = DESC_RATE6M;
            break;
        case WL_MGN_11M:
            ret_rate = DESC_RATE11M;
            break;
        case WL_MGN_9M:
            ret_rate = DESC_RATE9M;
            break;
        case WL_MGN_12M:
            ret_rate = DESC_RATE12M;
            break;
        case WL_MGN_18M:
            ret_rate = DESC_RATE18M;
            break;
        case WL_MGN_24M:
            ret_rate = DESC_RATE24M;
            break;
        case WL_MGN_36M:
            ret_rate = DESC_RATE36M;
            break;
        case WL_MGN_48M:
            ret_rate = DESC_RATE48M;
            break;
        case WL_MGN_54M:
            ret_rate = DESC_RATE54M;
            break;
        case WL_MGN_MCS0:
            ret_rate = DESC_RATEMCS0;
            break;
        case WL_MGN_MCS1:
            ret_rate = DESC_RATEMCS1;
            break;
        case WL_MGN_MCS2:
            ret_rate = DESC_RATEMCS2;
            break;
        case WL_MGN_MCS3:
            ret_rate = DESC_RATEMCS3;
            break;
        case WL_MGN_MCS4:
            ret_rate = DESC_RATEMCS4;
            break;
        case WL_MGN_MCS5:
            ret_rate = DESC_RATEMCS5;
            break;
        case WL_MGN_MCS6:
            ret_rate = DESC_RATEMCS6;
            break;
        case WL_MGN_MCS7:
            ret_rate = DESC_RATEMCS7;
            break;

    }
    return ret_rate;

}

static void mp_proc_test_fill_tx_desc(nic_info_st *pnic_info)
{
    zt_mp_info_st *pmp_priv = pnic_info->mp_info;
    struct xmit_frame *pattrib = &(pmp_priv->tx.attrib);
    zt_u8 *ptxdesc = pmp_priv->tx.desc;

    /*set mac id*/
    zt_set_bits_to_le_u32(ptxdesc + 16, 0, 5, 0);

    /* set TX RATE */
    zt_set_bits_to_le_u32(ptxdesc + 8, 18, 7, pmp_priv->rateidx);
    LOG_D("pmp_priv->rateidx:0x%x", pmp_priv->rateidx);
    /* set USE_RATE */
    zt_set_bits_to_le_u32(ptxdesc + 8, 16, 1, 1);
    /* set SEQ */
    zt_set_bits_to_le_u32(ptxdesc, 19, 12, pattrib->seqnum);
    if (pmp_priv->bandwidth == CHANNEL_WIDTH_40)
    {
        /* set DBW */
        zt_set_bits_to_le_u32(ptxdesc + 16, 12, 1, CHANNEL_WIDTH_40);
        /* set RATE ID, mgmt frame use 802.11 B, the number is 0 */
        zt_set_bits_to_le_u32(ptxdesc + 16, 6, 3, RATEID_IDX_BGN_20M_1SS_BN);
    }
    else
    {
        /* set DBW */
        zt_set_bits_to_le_u32(ptxdesc + 16, 12, 1, CHANNEL_WIDTH_20);
        /* set RATE ID, mgmt frame use 802.11 B, the number is 0 */
        zt_set_bits_to_le_u32(ptxdesc + 16, 6, 3, RATEID_IDX_BGN_40M_1SS);
    }

    if (pmp_priv->preamble)
    {
        if (pmp_priv->rateidx <= DESC_RATE54M)
        {
            zt_set_bits_to_le_u32(ptxdesc + 8, 17, 1, 1);
        }
    }
    zt_set_bits_to_le_u32(ptxdesc + 12, 19, 1, 1);

    /* set QOS QUEUE  */
    zt_set_bits_to_le_u32(ptxdesc + 12, 6, 5, pattrib->seqnum);
}



static void mp_proc_tx_desc_fill(struct xmit_frame *pxmitframe, zt_u8 *pbuf,
                                 zt_bool bSendAck)
{
    nic_info_st *nic_info = pxmitframe->nic_info;
    zt_mp_info_st *mp_info = nic_info->mp_info;

    zt_memcpy(pbuf, mp_info->tx.desc, TXDESC_SIZE);

    /* set for data type */
    zt_set_bits_to_le_u32(pbuf, 0, 2, TYPE_DATA);

    /* set PKT_LEN */
    zt_set_bits_to_le_u32(pbuf + 8, 0, 16, pxmitframe->last_txcmdsz);

    /* set HWSEQ_EN */
    zt_set_bits_to_le_u32(pbuf, 18, 1, 1);
    /*set bmc*/
    zt_set_bits_to_le_u32(pbuf + 12, 14, 1, 1);
}

static zt_bool mp_proc_tx_desc_update(struct xmit_frame *pxmitframe,
                                      zt_u8 *pbuf)
{
    mp_proc_tx_desc_fill(pxmitframe, pbuf, zt_false);
    zt_txdesc_chksum(pbuf);

    return zt_false;
}

struct completion mp_completion;
static zt_bool mp_proc_tx_send_complete_cb(nic_info_st *nic_info,
        struct xmit_buf *pxmitbuf)
{
    tx_info_st *tx_info = nic_info->tx_info;

    //LOG_D("mp_tx_send_complete_cb pxmitbuf:%p",pxmitbuf);
    zt_xmit_buf_delete(tx_info, pxmitbuf);
    complete(&mp_completion);

    return zt_true;
}


static zt_bool mp_proc_tx_sending_queue(nic_info_st *nic_info,
                                        struct xmit_frame *pxmitframe, zt_bool ack)
{
    zt_u8 *mem_addr;
    zt_u32 ff_hwaddr;
    zt_bool bRet = zt_true;
    zt_s32 ret;
    zt_bool inner_ret = zt_true;
    zt_bool blast = zt_false;
    zt_s32 t, sz, w_sz, pull = 0;
    struct xmit_buf *pxmitbuf = pxmitframe->pxmitbuf;
    hw_info_st *hw_info = nic_info->hw_info;
    zt_u32  txlen = 0;
    //zt_s32 i=0;

    mem_addr = pxmitframe->buf_addr;

    for (t = 0; t < pxmitframe->nr_frags; t++)
    {
        if (inner_ret != zt_true && ret == zt_true)
        {
            ret = zt_false;
        }
        if (t != (pxmitframe->nr_frags - 1))
        {
            LOG_D("pattrib->nr_frags=%d\n", pxmitframe->nr_frags);
            sz = hw_info->frag_thresh;
            sz = sz - 4 - 0; /* 4: wlan head filed????????? */
        }
        else
        {
            /* no frag */
            blast = zt_true;
            sz = pxmitframe->last_txcmdsz;
        }

        pull = mp_proc_tx_desc_update(pxmitframe, mem_addr);
        if (pull)
        {
            mem_addr += PACKET_OFFSET_SZ; /* pull txdesc head */
            pxmitframe->buf_addr = mem_addr;
            w_sz = sz + TXDESC_SIZE;
        }
        else
        {
            w_sz = sz + TXDESC_SIZE + PACKET_OFFSET_SZ;
        }

        //for(i=0;i<60;i++)
        {
            //LOG_D("0x%x",*(mem_addr+i));

        }
        //LOG_E("txd end!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
        ff_hwaddr = zt_quary_addr(pxmitframe->qsel);

        txlen = TXDESC_SIZE + pxmitframe->last_txcmdsz;
        if (blast)
        {
            ret = zt_io_write_data(nic_info, 1, mem_addr, w_sz,
                                   ff_hwaddr, (void *)mp_proc_tx_send_complete_cb, nic_info, pxmitbuf);
        }
        else
        {
            ret = zt_io_write_data(nic_info, 1, mem_addr, w_sz,
                                   ff_hwaddr, NULL, nic_info, pxmitbuf);
        }

        if (ZT_RETURN_FAIL == ret)
        {
            bRet = zt_false;
            break;
        }

        zt_tx_stats_cnt(nic_info, pxmitframe, sz);

        mem_addr += w_sz;
        mem_addr = (zt_u8 *) ZT_RND4(((SIZE_PTR)(mem_addr)));
    }

    return bRet;
}


static zt_s32 mp_proc_xmit_packet_thread(void *nic_info)
{
    nic_info_st *pnic_info = (nic_info_st *)nic_info;
    struct xmit_frame *xmitframe;
    struct xmit_buf *xmitBuf;
    zt_list_t list;
    zt_mp_info_st *pmp_priv;
    tx_info_st *pxmitpriv;
    zt_s32 ret = zt_false;
    zt_bool bufAlocFailed = zt_false;
    pmp_priv = pnic_info->mp_info;
    pxmitpriv = (tx_info_st *)pnic_info->tx_info;

    LOG_I("[%s]: mp->tx.count: %d, mp->tx.stop:%d", __func__, pmp_priv->tx.count,
          pmp_priv->tx.stop);
    while (1)
    {
        if ((pnic_info->is_driver_stopped == zt_true) ||
                (pnic_info->is_surprise_removed == zt_true))
        {
            pmp_priv->tx.stop = 1;
        }

        if ((pmp_priv->tx.count != 0) && (pmp_priv->tx.sended >= pmp_priv->tx.count))
        {
            pmp_priv->tx.stop = 1;
        }

        if (pmp_priv->tx.stop == 1)
        {
            break;
        }

        xmitBuf = zt_xmit_buf_new(pxmitpriv);
        if (xmitBuf != NULL)
        {
            xmitframe = zt_xmit_frame_new(pxmitpriv);
            if (xmitframe != NULL)
            {
                bufAlocFailed = zt_false;
            }
            else
            {
                zt_xmit_buf_delete(pxmitpriv, xmitBuf);
                xmitBuf = NULL;

                bufAlocFailed = zt_true;
            }
        }
        else
        {
            bufAlocFailed = zt_true;
        }

        if (bufAlocFailed == zt_true)
        {
            wait_for_completion_timeout(&mp_completion, (5 * ZT_HZ) / 1000);
            continue;
        }
        list = xmitframe->list;
        zt_memcpy((xmitframe), &(pmp_priv->tx.attrib), sizeof(struct xmit_frame));
        xmitframe->list = list;
        xmitframe->frame_tag = MP_FRAMETAG;
        xmitframe->pxmitbuf = xmitBuf;
        xmitframe->buf_addr = xmitBuf->pbuf;
        xmitBuf->priv_data = xmitframe;

        zt_memcpy((zt_u8 *)(xmitBuf->pbuf + TXDESC_OFFSET), pmp_priv->tx.buf,
                  pmp_priv->tx.write_size);
        xmitBuf->pkt_len = pmp_priv->tx.write_size;
        init_completion(&mp_completion);
        //zt_memcpy(&(xmitframe->attrib), &(pmp_priv->tx.attrib), sizeof(struct pkt_attrib));
        ret = mp_proc_tx_sending_queue(pnic_info, xmitframe, zt_false);
        if (zt_false == ret)
        {
            LOG_I("mp_tx_sending_queue...failed");
            zt_xmit_buf_delete(pxmitpriv, xmitBuf);
        }
        else
        {
            pmp_priv->tx.sended++;
            pmp_priv->tx_pktcount++;
            // LOG_I("mp_tx_sending_queue...OK, count is %d", pmp_priv->tx.sended);
        }
        zt_xmit_frame_delete(pxmitpriv, xmitframe);

        //        zt_msleep(1);
    }

    LOG_I("[%s]: mp tx test ok, send count is %d", __func__, pmp_priv->tx.sended);
    zt_kfree(pmp_priv->tx.pallocated_buf);
    pmp_priv->tx.pallocated_buf = NULL;
    complete_and_exit(NULL, 0);
}


static zt_u32 mp_proc_random32(void)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6,5,0))
    return get_random_u32();
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3,8,0))
    return prandom_u32();
#elif (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,18))
    zt_u32 random_int;
    get_random_bytes(&random_int, 4);
    return random_int;
#else
    return random32();
#endif
}


zt_u8 proc_mac_test[6] = {0xc8, 0x2e, 0x47, 0xbc, 0xbb, 0xdf};

static void mp_proc_tx_packet(nic_info_st *pnic_info)
{
    zt_u8 *ptr, *pkt_start, *pkt_end;
    zt_u32 pkt_size, i;
    struct wl_ieee80211_hdr *hdr;
    zt_u8 payload;
    struct xmit_frame *pattrib;
    zt_mp_info_st *pmp_priv;
    hw_info_st *hw_info = (hw_info_st *) pnic_info->hw_info;
    pmp_priv = pnic_info->mp_info;

    if (pmp_priv->tx.stop)
    {
        return;
    }
    pmp_priv->tx.sended = 0;
    pmp_priv->tx.stop = 0;
    pmp_priv->tx_pktcount = 0;
#if 0
    if (pmp_priv->bandwidth == CHANNEL_WIDTH_40)
    {
        LOG_D("SET RF 0X18");
        data = 0x400 | pmp_priv->channel;
        LOG_D("------------data :%x ", data);
        Func_Chip_Bb_Rfserial_Write_Process(pnic_info, 0, 0x18, data);
    }
#endif
    pattrib = &pmp_priv->tx.attrib;
    //zt_memcpy(pattrib->src, hw_info->macAddr, ZT_80211_MAC_ADDR_LEN);
    //zt_memcpy(pattrib->ta, pattrib->src, ZT_80211_MAC_ADDR_LEN);
    //zt_memcpy(pattrib->bssid, proc_mac_test, ZT_80211_MAC_ADDR_LEN);

    //LOG_I("[mp_tx_packet]:pattrib->ra:"ZT_MAC_FMT, ZT_MAC_ARG(pattrib->ra));
#if 0
    pattrib->pwdn = zt_wdn_find_info(pnic_info, pattrib->ra);
    if (pattrib->pwdn == NULL)
    {
        MP_WARN("%s wdn not find", __func__);
    }
#else
    pattrib->pwdn = NULL;
#endif

    pattrib->last_txcmdsz = pattrib->hdrlen + pattrib->pktlen;

    pkt_size = pattrib->last_txcmdsz;

    if (pmp_priv->tx.pallocated_buf)
    {
        zt_kfree(pmp_priv->tx.pallocated_buf);
    }
    pmp_priv->tx.write_size = pkt_size;
    pmp_priv->tx.buf_size = pkt_size + XMITBUF_ALIGN_SZ;
    pmp_priv->tx.pallocated_buf = zt_kzalloc(pmp_priv->tx.buf_size);
    if (pmp_priv->tx.pallocated_buf == NULL)
    {
        LOG_W("%s: malloc(%d) fail!!\n", __func__, pmp_priv->tx.buf_size);
        return;
    }
    pmp_priv->tx.buf =
        (zt_u8 *) N_BYTE_ALIGMENT((SIZE_PTR)(pmp_priv->tx.pallocated_buf),
                                  XMITBUF_ALIGN_SZ);
    ptr = pmp_priv->tx.buf;

    zt_memset(pmp_priv->tx.desc, 0, TXDESC_SIZE);
    pkt_start = ptr;
    pkt_end = pkt_start + pkt_size;

    mp_proc_test_fill_tx_desc(pnic_info);

    hdr = (struct wl_ieee80211_hdr *)pkt_start;
    SetFrameSubType(&hdr->frame_ctl, WIFI_DATA);
    //SetToDs(&hdr->frame_ctl);
    zt_memset(hdr->addr1, 0xFF, ZT_80211_MAC_ADDR_LEN);
    zt_memcpy(hdr->addr2, hw_info->macAddr, ZT_80211_MAC_ADDR_LEN);
    zt_memcpy(hdr->addr3, proc_mac_test, ZT_80211_MAC_ADDR_LEN);

    ptr = pkt_start + pattrib->hdrlen;

    switch (pmp_priv->tx.payload)
    {
        case 0:
            payload = 0x00;
            break;
        case 1:
            payload = 0x5a;
            break;
        case 2:
            payload = 0xa5;
            break;
        case 3:
            payload = 0xff;
            break;
        default:
            payload = 0x00;
            break;
    }
    pmp_priv->TXradomBuffer = zt_kzalloc(4096);
    if (pmp_priv->TXradomBuffer == NULL)
    {
        LOG_W("mp create random buffer fail!\n");
        return;
    }

    for (i = 0; i < 4096; i++)
    {
        pmp_priv->TXradomBuffer[i] = mp_proc_random32() % 0xFF;
    }

    zt_memcpy(ptr, pmp_priv->TXradomBuffer, pkt_end - ptr);
    zt_kfree(pmp_priv->TXradomBuffer);

    pmp_priv->tx.PktTxThread = kthread_run(mp_proc_xmit_packet_thread, pnic_info,
                                           "ZT_MP_TX_THREAD");
    if (IS_ERR(pmp_priv->tx.PktTxThread))
    {
        LOG_W("Create PktTx Thread Fail !!!!!\n");
    }

    return;
}

static zt_s32 mp_proc_tx_test_stop_process(nic_info_st *pnic_info)
{
    zt_u32 inbuff;
    zt_s32 ret;

    inbuff = 0x26;
    ret = mcu_cmd_communicate(pnic_info, UMSG_OPS_MSG_WRITE_DIG, &inbuff, 1, NULL,
                              0);
    if (ret == ZT_RETURN_FAIL)
    {
        LOG_W("set reg fail");
        return -1;
    }

    return ZT_RETURN_OK;
}


static zt_s32 mp_proc_tx_test_start_process(nic_info_st *pnic_info)
{
    zt_u32 inbuff;
    zt_s32 ret;

    inbuff = 0x7f;
    ret = mcu_cmd_communicate(pnic_info, UMSG_OPS_MSG_WRITE_DIG, &inbuff, 1, NULL,
                              0);
    if (ret == ZT_RETURN_FAIL)
    {
        LOG_W("set reg fail");
        return -1;
    }

    return ZT_RETURN_OK;
}


static void mp_proc_set_rx_filter(nic_info_st *pnic_info, zt_u8 bStartRx,
                                  zt_u8 bssidFT, zt_u8 bAB)
{
    zt_u32 inbuff[3] = { 0 };
    zt_u32 ReceiveConfig;
    zt_u32 ret = -1;

    if (bStartRx)
    {
        ReceiveConfig = RCR_APM | RCR_AM | RCR_AB | RCR_APP_ICV | RCR_AMF |
                        RCR_HTC_LOC_CTRL | RCR_APP_MIC | RCR_APP_PHYST_RXFF;
        if (bssidFT == 1)
        {
            ReceiveConfig |= RCR_CBSSID_DATA;
        }

        ReceiveConfig |= RCR_ACRC32;

        inbuff[0] = 1;
        inbuff[1] = ReceiveConfig;
    }
    else
    {
        inbuff[0] = 0;
        inbuff[1] = 0;
    }
    if (bAB)
    {
        inbuff[2] = zt_true;
    }
    else
    {
        inbuff[2] = zt_false;
    }
    ret = mcu_cmd_communicate(pnic_info, UMSG_OPS_MP_SET_PRX, inbuff, 3, NULL, 0);


    if (ret == ZT_RETURN_FAIL)
    {
        LOG_W("set reg fail");
        return ;
    }

}


static void mp_proc_set_lck(nic_info_st *pnic_info)
{
    mcu_msg_body_st msg_rw_val;
    zt_u32 ret = -1;
    zt_u32 timeout = 2000, timecount = 0;

    ret = mcu_cmd_communicate(pnic_info, UMSG_OPS_HAL_READVAR_MSG, NULL, 0,
                              (zt_u32 *) & msg_rw_val, sizeof(msg_rw_val) / 4);

    if (ret == ZT_RETURN_FAIL)
    {
        LOG_W("set reg fail");
        return ;
    }

    if (!(msg_rw_val.ability & ZT_BIT(26)))
    {
        return;
    }

    while (msg_rw_val.bScanInProcess && timecount < timeout)
    {
        zt_msleep(50);
        timecount += 50;
        if (NIC_USB == pnic_info->nic_type)
        {
            ret = mcu_cmd_communicate(pnic_info, UMSG_OPS_HAL_READVAR_MSG, NULL, 0,
                                      (zt_u32 *) & msg_rw_val, sizeof(msg_rw_val) / 4);
        }
        else
        {
            //          ret = mcu_cmd_communicate(pnic_info, WLAN_OPS_DXX0_READ_VERSION, &efuse_code, 0, version, 1);
        }
        if (ret == ZT_RETURN_FAIL)
        {
            LOG_W("set reg fail");
            return ;
        }
    }

    ret = mcu_cmd_communicate(pnic_info, UMSG_OPS_HAL_CALI_LLC, NULL, 0, NULL, 0);

    if (ret == ZT_RETURN_FAIL)
    {
        LOG_W("set reg fail");
        return ;
    }
}

static void mp_proc_sdio_set_rx_filter(nic_info_st *pnic_info, zt_u8 bStartRx,
                                       zt_u8 bAB)
{
    zt_mp_info_st *mp_info = pnic_info->mp_info;
    zt_u32 ret;
    zt_u32 outbuf;
    zt_u32 inbuf[4] = {0};
    zt_u32 ReceiveConfig;

    if (bStartRx == 1)
    {

        ReceiveConfig = RCR_APM | RCR_AM | RCR_AB | RCR_APP_ICV | RCR_AMF |
                        RCR_HTC_LOC_CTRL | RCR_APP_MIC | RCR_APP_PHYST_RXFF;
    }
    else
    {
        ReceiveConfig = 0;
    }
    inbuf[0] = bStartRx;
    inbuf[1] = mp_info->bSetRxBssid;
    inbuf[2] = ReceiveConfig;
    inbuf[3] = bAB;
    ret = mcu_cmd_communicate(pnic_info, UMSG_OPS_MP_SET_PRX, inbuf, 4, &outbuf, 1);
    if (ret == ZT_RETURN_FAIL)
    {
        LOG_W("set reg fail");
        return ;
    }
    mp_info->sdio_ReceiveConfig = outbuf;

}

static zt_s32 mp_proc_trx_test_pretx_proc(nic_info_st *pnic_info,
        zt_u8 bStartTest,
        zt_s8 *extra)
{
    zt_mp_info_st *pmp_info = pnic_info->mp_info;
    zt_u32 inbuff;
    zt_u32 inbuff1[2] = {0};
    zt_u32 ret;
    zt_u32 loopback;
    if ((pnic_info->is_surprise_removed) || (pnic_info->is_driver_stopped))
    {
        return -1;
    }

    switch (pmp_info->mode)
    {
        case MP_PACKET_TX:
        {
            LOG_I("[%s]: trx mode is MP_PACKET_TX", __func__);
            if (bStartTest == 0)
            {
                pmp_info->tx.stop = 1;

                mp_proc_tx_test_stop_process(pnic_info);
            }
            else if (pmp_info->tx.stop == 1)
            {

                pmp_info->tx.stop = 0;
                zt_mcu_set_user_info(pnic_info, zt_true);
                mp_proc_tx_test_start_process(pnic_info);
                mp_proc_tx_packet(pnic_info);

            }
            else
            {
                return ZT_RETURN_FAIL;
            }

            return 0;
        }
        break;

        case MP_SINGLE_TONE_TX:
        {
            LOG_I("[%s]: trx mode is MP_SINGLE_TONE_TX", __func__);

            if (pmp_info->rateidx <= DESC_RATE11M)
            {
                printk("%s", "single rate error");
                break;
            }
            if (bStartTest == 1)
            {
                LOG_D("Start continuous DA=ffffffffffff len=%d\n infinite=yes.",
                      pmp_info->tx.attrib.pktlen);
                inbuff = 1;
                ret = mcu_cmd_communicate(pnic_info, UMSG_OPS_MP_SET_SINGLETONETX, &inbuff, 1,
                                          NULL, 0);
                pmp_info->tx.stop = 0;
                mp_proc_tx_test_start_process(pnic_info);
                mp_proc_tx_packet(pnic_info);
            }
            else
            {
                inbuff = 0;
                ret = mcu_cmd_communicate(pnic_info, UMSG_OPS_MP_SET_SINGLETONETX, &inbuff, 1,
                                          NULL, 0);
                mp_proc_tx_test_stop_process(pnic_info);
                pmp_info->tx.stop = 1;
            }
        }
        break;

        case MP_SINGLE_CARRIER_TX:
        {
            LOG_I("[%s]: trx mode is MP_SINGLE_CARRIER_TX", __func__);
            if (bStartTest == 1)
            {
                LOG_D("Start continuous DA=ffffffffffff len=%d\n infinite=yes.",
                      pmp_info->tx.attrib.pktlen);
                inbuff = 1;
                ret = mcu_cmd_communicate(pnic_info, UMSG_OPS_MP_SET_SINGLECARRTX, &inbuff, 1,
                                          NULL, 0);
                mp_proc_tx_test_start_process(pnic_info);
                pmp_info->tx.stop = 0;
                mp_proc_tx_packet(pnic_info);
            }
            else
            {
                inbuff = 0;
                ret = mcu_cmd_communicate(pnic_info, UMSG_OPS_MP_SET_SINGLECARRTX, &inbuff, 1,
                                          NULL, 0);
                mp_proc_tx_test_stop_process(pnic_info);
                pmp_info->tx.stop = 1;
            }
        }
        break;

        case MP_CONTINUOUS_TX:
        {
            LOG_I("[%s]: trx mode is MP_CONTINUOUS_TX", __func__);
            if (bStartTest == 1)
            {
                LOG_D("Start continuous DA=ffffffffffff len=%d\n infinite=yes.",
                      pmp_info->tx.attrib.pktlen);

                if (pmp_info->rateidx <= DESC_RATE11M)
                {
                    inbuff1[0] = 1;
                    inbuff1[1] = pmp_info->rateidx;
                    ret = mcu_cmd_communicate(pnic_info, UMSG_OPS_MP_SET_CCKCTX, inbuff1, 2, NULL,
                                              0);
                }
                else if (pmp_info->rateidx >= DESC_RATE6M)
                {
                    inbuff = 1;
                    ret = mcu_cmd_communicate(pnic_info, UMSG_OPS_MP_SET_OFDMCTX, &inbuff, 1, NULL,
                                              0);
                }
            }
            else
            {
                if (pmp_info->rateidx <= DESC_RATE11M)
                {
                    inbuff1[0] = 0;
                    inbuff1[1] = pmp_info->rateidx;
                    ret = mcu_cmd_communicate(pnic_info, UMSG_OPS_MP_SET_CCKCTX, inbuff1, 2, NULL,
                                              0);
                }
                else if (pmp_info->rateidx >= DESC_RATE6M)
                {
                    inbuff = 0;
                    ret = mcu_cmd_communicate(pnic_info, UMSG_OPS_MP_SET_OFDMCTX, &inbuff, 1, NULL,
                                              0);
                }
            }
        }
        break;

        case MP_CARRIER_SUPPRISSION_TX:
        {
            LOG_I("[%s]: trx mode is MP_CARRIER_SUPPRISSION_TX", __func__);
            if (bStartTest == 1)
            {
                if (pmp_info->rateidx <= DESC_RATE11M)
                {
                    LOG_D("Start continuous DA=ffffffffffff len=%d\n infinite=yes.",
                          pmp_info->tx.attrib.pktlen);
                }
                else
                {
                    LOG_D("Specify carrier suppression but not CCK rate");
                    break;
                }

                inbuff1[0] = 1;
                inbuff1[1] = pmp_info->rateidx;
                ret = mcu_cmd_communicate(pnic_info, UMSG_OPS_MP_SET_CARRSUPPTX, inbuff1, 2,
                                          NULL, 0);
                mp_proc_tx_test_start_process(pnic_info);
                pmp_info->tx.stop = 0;
                mp_proc_tx_packet(pnic_info);
            }
            else
            {
                inbuff1[0] = 0;
                inbuff1[1] = pmp_info->rateidx;
                ret = mcu_cmd_communicate(pnic_info, UMSG_OPS_MP_SET_CARRSUPPTX, inbuff1, 2,
                                          NULL, 0);
                mp_proc_tx_test_stop_process(pnic_info);
                pmp_info->tx.stop = 1;
            }
        }
        break;

        case MP_TX_LCK:
        {
            LOG_I("[%s]: trx mode is MP_TX_LCK", __func__);
            mp_proc_set_lck(pnic_info);
        }
        break;

        default:
            printk("Error! Continuous-Tx is not on-going.");
            return ZT_RETURN_FAIL;
    }

    return 0;
}

zt_s32 zt_mp_proc_test_tx(nic_info_st *pnic_info, char *data, size_t data_len)
{
    zt_mp_info_st *pmp_info = pnic_info->mp_info;
    struct xmit_frame *pattrib = &pmp_info->tx.attrib;
    //zt_u8 input[wrqu->length];

    zt_u32 pkTx = 1;
    zt_u32 countPkTx = 1, cotuTx = 1, CarrSprTx = 1, scTx = 1, sgleTx = 1, stop = 1;
    zt_u32 bStartTest = 1;
    zt_u32 count = 0, pktinterval = 0, len = 0;
    zt_u8 status;
    zt_u32 lck = 1;

    char *pch;
    zt_u8 *input = NULL;

    input = zt_kzalloc(data_len);
    if ((pnic_info->is_surprise_removed) || (pnic_info->is_driver_stopped))
    {
        return -1;
    }

    zt_memcpy(input, data, data_len);

    LOG_D("set %s", input);

    pch = input;
    countPkTx = zt_strncmp(pch, "count=", 5);
    cotuTx = zt_strncmp(pch, "background", 10);
    CarrSprTx = zt_strncmp(pch, "carr", 4);
    scTx = zt_strncmp(pch, "sc", 2);
    sgleTx = zt_strncmp(pch, "single", 6);
    pkTx = zt_strncmp(pch, "frame", 5);
    lck = zt_strncmp(pch, "lck", 3);
    stop = zt_strncmp(pch, "stop", 4);

    if (sscanf(pch, "mac_loopback,count=%d", &count) > 0)
    {
        LOG_D("count=%d\n", count);
    }
    if (sscanf(pch, "phy_loopback,count=%d", &count) > 0)
    {
        LOG_D("count=%d\n", count);
    }
    if (sscanf(pch, "frame,count=%d", &count) > 0)
    {
        LOG_D("count=%d\n", count);
    }
    if (sscanf(pch, "pktinterval=%d", &pktinterval) > 0)
    {
        LOG_D("pktinterval=%d\n", pktinterval);
    }
    if (sscanf(pch, "frame,len=%d", &len) > 0)
    {
        LOG_D("len=%d\n", len);
    }
    if (sscanf(pch, "frame,len=%d,count=%d", &len, &count) > 0)
    {
        LOG_D("len=%d,count=%d\n", len, count);
    }

    if (zt_memcmp(pch, "destmac=", 8))
    {
        //        mp_tx_destaddr(pnic_info, &pch[8]);
        //        printk( "Set dest mac OK !\n");
    }

    if (pktinterval != 0)
    {
        LOG_D("Pkt Interval = %d", pktinterval);
        pmp_info->pktInterval = pktinterval;

    }

    if (len != 0)
    {
        LOG_D("Pkt len = %d", len);
        pattrib->pktlen = len;
    }
    else
    {
        len = 200;
        LOG_D("Pkt len = %d", len);
        pattrib->pktlen = len;
    }

    pmp_info->tx.count = count;

    if (pkTx == 0 || countPkTx == 0)
    {
        LOG_I("[%s]: mp trx mode: MP_PACKET_TX", __func__);
        pmp_info->mode = MP_PACKET_TX;
    }

    if (sgleTx == 0)
    {
        LOG_I("[%s]: mp trx mode: MP_SINGLE_TONE_TX", __func__);
        pmp_info->mode = MP_SINGLE_TONE_TX;
    }

    if (scTx == 0)
    {
        LOG_I("[%s]: mp trx mode: MP_SINGLE_CARRIER_TX", __func__);
        pmp_info->mode = MP_SINGLE_CARRIER_TX;
    }

    if (cotuTx == 0)
    {
        LOG_I("[%s]: mp trx mode: MP_CONTINUOUS_TX", __func__);
        pmp_info->mode = MP_CONTINUOUS_TX;
    }

    if (CarrSprTx == 0)
    {
        LOG_I("[%s]: mp trx mode: MP_CARRIER_SUPPRISSION_TX", __func__);
        pmp_info->mode = MP_CARRIER_SUPPRISSION_TX;
    }

    if (lck == 0)
    {
        LOG_I("[%s]: mp trx mode: MP_TX_LCK", __func__);
        pmp_info->mode = MP_TX_LCK;
    }

    if (stop == 0)
    {
        bStartTest = 0;
        LOG_I("[%s]: mp trx stop tx", __func__);
        LOG_D("Stop Tx");
    }
    else
    {
        bStartTest = 1;
        LOG_I("[%s]: mp trx start tx", __func__);
        LOG_D("Start Tx");
    }

    status = mp_proc_trx_test_pretx_proc(pnic_info, bStartTest, NULL);

    LOG_D("Tx %s", status == 0 ? "ok" : "fail");

    //wrqu->length = zt_strlen(extra);
    zt_kfree(input);
    return status;
}

static void mp_proc_dump_mac_rx_counters(nic_info_st *pnic_info,
        struct dbg_rx_counter *rx_counter)
{
    zt_u32 outbuff[6];
    zt_u32 ret;
    if (!rx_counter)
    {
        LOG_W("rx_counter NULL");
        return;
    }

    ret = mcu_cmd_communicate(pnic_info, UMSG_OPS_MP_MACRXCOUNT, NULL, 0, outbuff,
                              6);
    if (ret == ZT_RETURN_FAIL)
    {
        LOG_W("set reg fail");
        return ;
    }
    rx_counter->rx_pkt_ok = outbuff[0];
    rx_counter->rx_pkt_crc_error = outbuff[1];
    rx_counter->rx_cck_fa = outbuff[2];
    rx_counter->rx_ofdm_fa = outbuff[3];
    rx_counter->rx_ht_fa = outbuff[4];
    rx_counter->rx_pkt_drop = outbuff[5];
}


static void mp_proc_dump_phy_rx_counters(nic_info_st *pnic_info,
        struct dbg_rx_counter *rx_counter)
{
    zt_u32 outbuff[4];
    zt_u32 ret;
    if (!rx_counter)
    {
        LOG_W("rx_counter NULL");
        return;
    }

    ret = mcu_cmd_communicate(pnic_info, UMSG_OPS_MP_PHYRXCOUNT, NULL, 0, outbuff,
                              4);
    if (ret == ZT_RETURN_FAIL)
    {
        LOG_W("set reg fail");
        return ;
    }

    rx_counter->rx_pkt_ok = outbuff[0];
    rx_counter->rx_pkt_crc_error = outbuff[1];
    rx_counter->rx_ofdm_fa = outbuff[2];
    rx_counter->rx_cck_fa = outbuff[3];
}

static zt_u8 mp_proc_trx_key_of_char2num_func(zt_u8 ch)
{
    if ((ch >= '0') && (ch <= '9'))
    {
        return ch - '0';
    }
    else if ((ch >= 'a') && (ch <= 'f'))
    {
        return ch - 'a' + 10;
    }
    else if ((ch >= 'A') && (ch <= 'F'))
    {
        return ch - 'A' + 10;
    }
    else
    {
        return 0xff;
    }
}

static zt_u8 mp_proc_trx_key_of_2char2num_func(zt_u8 hch, zt_u8 lch)
{
    return ((mp_proc_trx_key_of_char2num_func(hch) << 4) |
            mp_proc_trx_key_of_char2num_func(
                lch));
}

zt_s32 zt_mp_proc_rx_common_process(nic_info_st *pnic_info, zt_u8 *pktBuf,
                                    zt_u32 pktLen)
{
    zt_s32 ret = 0;
    zt_mp_info_st *pmp_priv = pnic_info->mp_info;
    rx_pkt_t pkt;
    zt_u8 *data;
    static zt_s32 rx_cnt = 0;
    if (pmp_priv == NULL)
    {
        return -1;
    }

    if (pmp_priv->rx_start == 0)
    {
        return -1;
    }

    pkt.p_nic_info = pnic_info;
    zt_rx_rxd_prase(pktBuf, &pkt);

    //LOG_D("[RXD] pkt_len:%d",pkt.pkt_info.pkt_len);
    if (!(++rx_cnt % 500))
    {
        LOG_D("total rx_cnt=%d", rx_cnt);
    }

    data = pktBuf + pkt.pkt_info.hif_hdr_len;

    if (pmp_priv->mode == MP_MAC_LOOPBACK)
    {
        if (zt_memcmp(GetAddr3Ptr(data), proc_mac_test, 6) == 0)
        {
            pmp_priv->rx_pktcount++;
        }
        //LOG_D("bssid:"ZT_MAC_FMT,ZT_MAC_ARG(get_bssid(data)));
    }
    else
    {
        pmp_priv->rx_pktcount++;
    }
    /* process phystatus */
    if (pkt.pkt_info.phy_status)
    {
        zt_u8 lna_index, vga_index;
        zt_u8 cck_agc_rpt_or_ofdm_cfo  = pktBuf[RXD_SIZE + 3];
        lna_index   = ((cck_agc_rpt_or_ofdm_cfo & 0xE0) >> 5);
        vga_index   = cck_agc_rpt_or_ofdm_cfo & 0x1F;
        //        LOG_I("lna_index,   vga_index:%d,%d", lna_index, vga_index);
    }

    return ret;
}

zt_s32 zt_mp_proc_test_rx(nic_info_st *pnic_info, char *data, size_t data_len)
{
    zt_mp_info_st *pmp_priv = pnic_info->mp_info;
    zt_s32 bStartRx_c50_fw = 0, bStartRx_c50_user = 0, bStopRx = 0, bQueryRx = 0,
           bQueryPhy = 0, bQueryMac = 0,
           bSetBssid = 0;
    zt_s32 bmac_filter = 0, bmon = 0, bSmpCfg = 0;
    char *pch, *token, *tmp[2] = { 0x00, 0x00 };
    zt_u32 i = 0, jj = 0, kk = 0, cnts = 0, ret;
    struct dbg_rx_counter rx_counter;
    zt_u32 txok, txfail, rxok, rxfail, rxfilterout;
    char *ptr = NULL;
    zt_proc_mp_tx *pmptx;
    zt_u8 *input = NULL;

    input = zt_kzalloc(data_len);
    zt_memcpy(input, data, data_len);

    LOG_D("%s: %s\n", __func__, input);
    if (pmp_priv == NULL)
    {
        LOG_W("mp_info not init");
        return ZT_RETURN_FAIL;
    }
    pmptx = &pmp_priv->tx;

    ptr = input;
    bStartRx_c50_fw = (zt_strncmp(ptr, "start1", 6) == 0) ? 1 : 0;
    bStartRx_c50_user = (zt_strncmp(ptr, "start0", 6) == 0) ? 1 : 0;
    bStopRx = (zt_strncmp(ptr, "stop", 4) == 0) ? 1 : 0;
    bQueryRx = (zt_strncmp(ptr, "query", 5) == 0) ? 1 : 0;
    bQueryPhy = (zt_strncmp(ptr, "phy", 3) == 0) ? 1 : 0;
    bQueryMac = (zt_strncmp(ptr, "mac", 3) == 0) ? 1 : 0;
    bSetBssid = (zt_strncmp(ptr, "setbssid=", 8) == 0) ? 1 : 0;
    bmac_filter = (zt_strncmp(ptr, "accept_mac", 10) == 0) ? 1 : 0;
    bmon = (zt_strncmp(ptr, "mon=", 4) == 0) ? 1 : 0;
    bSmpCfg = (zt_strncmp(ptr, "smpcfg=", 7) == 0) ? 1 : 0;

    if (bSetBssid == 1)
    {
        pch = ptr;
        while ((token = strsep(&pch, "=")) != NULL)
        {
            if (i > 1)
            {
                break;
            }
            tmp[i] = token;
            i++;
        }
        if ((tmp[0] != NULL) && (tmp[1] != NULL))
        {
            cnts = zt_strlen(tmp[1]) / 2;
            if (cnts < 1)
            {
                return ZT_RETURN_FAIL;
            }
            LOG_D("%s: cnts=%d\n", __func__, cnts);
            LOG_D("%s: data=%s\n", __func__, tmp[1]);
            for (jj = 0, kk = 0; jj < cnts; jj++, kk += 2)
            {
                pmp_priv->network_macaddr[jj] = mp_proc_trx_key_of_2char2num_func(tmp[1][kk],
                                                tmp[1][kk + 1]);
                LOG_D("network_macaddr[%d]=%x\n", jj, pmp_priv->network_macaddr[jj]);
            }
        }
        else
        {
            return ZT_RETURN_FAIL;
        }

        pmp_priv->bSetRxBssid = zt_true;
    }

    if (bmac_filter)
    {
        pmp_priv->bmac_filter = bmac_filter;
        pch = input;
        while ((token = strsep(&pch, "=")) != NULL)
        {
            if (i > 1)
            {
                break;
            }
            tmp[i] = token;
            i++;
        }
        if ((tmp[0] != NULL) && (tmp[1] != NULL))
        {
            cnts = zt_strlen(tmp[1]) / 2;
            if (cnts < 1)
            {
                return ZT_RETURN_FAIL;
            }
            LOG_D("%s: cnts=%d\n", __func__, cnts);
            LOG_D("%s: data=%s\n", __func__, tmp[1]);
            for (jj = 0, kk = 0; jj < cnts; jj++, kk += 2)
            {
                pmp_priv->mac_filter[jj] = mp_proc_trx_key_of_2char2num_func(tmp[1][kk],
                                           tmp[1][kk + 1]);
                LOG_D("%s mac_filter[%d]=%x\n", __func__, jj, pmp_priv->mac_filter[jj]);
            }
        }
        else
        {
            return ZT_RETURN_FAIL;
        }

    }

    if (bStartRx_c50_fw)
    {
        LOG_D("start ok");


        mp_proc_set_rx_filter(pnic_info, bStartRx_c50_fw, 0, zt_true);

        LOG_D("Received packet OK:%d CRC error:%d ,Filter out:%d",
              pmp_priv->rx_pktcount,
              pmp_priv->rx_crcerrpktcount,
              pmp_priv->rx_pktcount_filter_out);

        pmp_priv->rx_start = 1;

    }
    else if (bStartRx_c50_user)
    {
        LOG_D("start ok");

        mp_proc_set_rx_filter(pnic_info, bStartRx_c50_user, 0, zt_false);

        LOG_D("Received packet OK:%d CRC error:%d ,Filter out:%d",
              pmp_priv->rx_pktcount,
              pmp_priv->rx_crcerrpktcount,
              pmp_priv->rx_pktcount_filter_out);

        pmp_priv->rx_start = 1;

    }
    else if (bStopRx)
    {

        pmp_priv->rx_start = 0;
        mp_proc_set_rx_filter(pnic_info, 0, 0, zt_false);
        pmp_priv->bmac_filter = zt_false;
        LOG_D("Received packet OK:%d CRC error:%d ,Filter out:%d",
              pmp_priv->rx_pktcount,
              pmp_priv->rx_crcerrpktcount,
              pmp_priv->rx_pktcount_filter_out);

    }
    else if (bQueryRx)
    {
        txok = pmptx->sended;
        txfail = 0;
        rxok = pmp_priv->rx_pktcount;
        rxfail = pmp_priv->rx_crcerrpktcount;
        rxfilterout = pmp_priv->rx_pktcount_filter_out;


        LOG_D(
            "Tx OK:%d, Tx Fail:%d, Rx OK:%d, CRC error:%d ,Rx Filter out:%d\n",
            txok, txfail, rxok, rxfail, rxfilterout);


    }
    else if (bQueryPhy)
    {
        zt_memset(&rx_counter, 0, sizeof(struct dbg_rx_counter));
        mp_proc_dump_phy_rx_counters(pnic_info, &rx_counter);

        LOG_D("%s: OFDM_FA =%d\n", __func__, rx_counter.rx_ofdm_fa);
        LOG_D("%s: CCK_FA =%d\n", __func__, rx_counter.rx_cck_fa);
        LOG_D("Phy Received packet OK:%d CRC error:%d FA Counter: %d",
              rx_counter.rx_pkt_ok, rx_counter.rx_pkt_crc_error,
              rx_counter.rx_cck_fa + rx_counter.rx_ofdm_fa);

    }
    else if (bQueryMac)
    {
        zt_memset(&rx_counter, 0, sizeof(struct dbg_rx_counter));
        mp_proc_dump_mac_rx_counters(pnic_info, &rx_counter);
        LOG_D("Mac Received packet OK: %d , CRC error: %d , Drop Packets: %d\n",
              rx_counter.rx_pkt_ok, rx_counter.rx_pkt_crc_error,
              rx_counter.rx_pkt_drop);

    }

    if (bmon == 1)
    {
        ret = sscanf(input, "mon=%d", &bmon);
        pmp_priv->rx_bindicatePkt = zt_true;
        LOG_D("Indicating Receive Packet to network start\n");

    }

    if (bSmpCfg == 1)
    {
        ret = sscanf(input, "smpcfg=%d", &bSmpCfg);

        if (bSmpCfg == 1)
        {
            pmp_priv->bWLSmbCfg = zt_true;
            LOG_D("Indicate By Simple Config Format\n");
            if (NIC_USB == pnic_info->nic_type)
            {
                mp_proc_set_rx_filter(pnic_info, zt_true, 0, zt_true);
            }
            else
            {
                mp_proc_sdio_set_rx_filter(pnic_info, zt_true, zt_true);
            }
        }
        else
        {
            pmp_priv->bWLSmbCfg = zt_false;
            LOG_D("Indicate By Normal Format\n");
            if (NIC_USB == pnic_info->nic_type)
            {
                mp_proc_set_rx_filter(pnic_info, zt_true, 0, zt_false);
            }
            else
            {
                mp_proc_sdio_set_rx_filter(pnic_info, zt_true, zt_false);
            }
        }
    }

    zt_kfree(input);

    return 0;
}

zt_s32 zt_mp_proc_stats(nic_info_st *pnic_info, char *data, size_t data_len)
{
    zt_s32 ret = 0;
    bool bReset = 0, bQuery = 0;
    zt_mp_info_st *pmp_priv;
    //zt_u8 input[wrqu->length];
    zt_u8 *input = NULL;
    zt_u32 txok, txfail, rxok, rxfail, rxfilterout;
    zt_u32 flag = 0x03;
    char *ptr = NULL;
    zt_proc_mp_tx *pmptx;
    pmp_priv = pnic_info->mp_info;
    pmptx = &pmp_priv->tx;

    input = zt_kzalloc(data_len);

    zt_memcpy(input, data, data_len);
    ptr = input;

    bReset = (zt_strncmp(ptr, "reset", 5) == 0) ? 1 : 0;
    bQuery = (zt_strncmp(ptr, "query", 5) == 0) ? 1 : 0;

    if (bReset == 1)
    {
        pmp_priv->tx.sended = 0;
        pmp_priv->tx_pktcount = 0;
        pmp_priv->rx_pktcount = 0;
        pmp_priv->rx_pktcount_filter_out = 0;
        pmp_priv->rx_crcerrpktcount = 0;

        if (NIC_USB == pnic_info->nic_type)
        {
            ret = mcu_cmd_communicate(pnic_info, UMSG_OPS_MP_RESETCOUNT, &flag, 1, NULL, 0);
        }
        else
        {
            /* reset phy count */
            ret = mcu_cmd_communicate(pnic_info, UMSG_OPS_MP_RESET_PHY_RX_COUNTERS, NULL, 0,
                                      NULL, 0);
            /* reset mac count */
            ret = mcu_cmd_communicate(pnic_info, UMSG_OPS_MP_RESET_MAC_RX_COUNTERS, NULL, 0,
                                      NULL, 0);
        }
        if (ret == ZT_RETURN_FAIL)
        {
            LOG_W("set reg fail");
            return -1;
        }
        printk("stats reset %s", ret == 0 ? "ok" : "fail");
    }
    else if (bQuery == 1)
    {
        txok = pmptx->sended;
        txfail = 0;
        rxok = pmp_priv->rx_pktcount;
        rxfail = pmp_priv->rx_crcerrpktcount;
        rxfilterout = pmp_priv->rx_pktcount_filter_out;

        printk(
            "Tx OK:%d, Tx Fail:%d, Rx OK:%d, CRC error:%d ,Rx Filter out:%d\n",
            txok, txfail, rxok, rxfail, rxfilterout);
    }
    else
    {
        ret = ZT_RETURN_FAIL;
    }
    zt_kfree(input);
    return ret;
}
#endif

