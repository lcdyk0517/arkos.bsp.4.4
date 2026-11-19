/*
 * rx_linux.h
 *
 * used for frame xmit for linux
 *
 * Author: renhaibo
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


#include <net/ieee80211_radiotap.h>
#include "ndev_linux.h"
#include "hif.h"
#include "rx.h"
#include "common.h"
#include "rx_linux.h"


/* A-MSDU  struc
| A-MSDU Frame 1 | A-MSDU Frame 2 | A-MSDU Frame 3 |...| A-MSDU Frame n|
Every A-MSDU frame structrue:
| DA | SA | Len |    MSDU    | Padding |
|  6 |  6 |  2  |   0~2304   |   0~3   |
*/
#define AMSDU_HDR_LEN               14
#define AMSDU_HDR_DA_OFFSET         0
#define AMSDU_HDR_SA_OFFSET         6
#define AMSDU_HDR_Len_OFFSET        12
#define AMSDU_MSDU_OFFSET           14

static void rx_data_reorder_process(rx_pkt_t *pkt, struct net_device *ndev);

zt_bool is_snap_pkt(struct sk_buff *pskb)
{
    return zt_80211_is_snap_hdr(pskb->data);
}

static inline zt_s32 create_skb(prx_pkt_t prx_pkt, zt_u8 *pbuf, zt_u32 pkt_len)
{
    struct sk_buff *skb = zt_alloc_skb(pkt_len + 14);
    if (!skb)
    {
        LOG_E("%s, alloc skb error", __FUNCTION__);
        return -1;
    }
    skb_put(skb, pkt_len);
    zt_memcpy(skb->data, pbuf, pkt_len);

    prx_pkt->pskb = skb;
    prx_pkt->pdata = skb->data;
    prx_pkt->ptail = skb_tail_pointer(skb);
    prx_pkt->len = skb->len;

    return 0;
}


static inline zt_u8 *pkt_pull(prx_pkt_t prx_pkt, zt_s32 size)
{
    struct sk_buff *pskb = (struct sk_buff *)prx_pkt->pskb;
    if (size > prx_pkt->len)
    {
        LOG_E("%s, size = %d, prx_pkt->len=%d", __func__, size, prx_pkt->len);
        return NULL;
    }
    if (pskb)
    {
        skb_pull(pskb, size);
        prx_pkt->pdata = pskb->data;
        prx_pkt->len  = pskb->len;
    }
    else
    {
        prx_pkt->pdata += size;
        prx_pkt->len -= size;
    }

    return prx_pkt->pdata;
}

static inline zt_u8 *pkt_trim(prx_pkt_t prx_pkt, zt_s32 size)
{
    struct sk_buff *skb = (struct sk_buff *)prx_pkt->pskb;

    if (skb->len < size)
    {
        LOG_D("skb->len < size");
        return NULL;
    }

    if (size == 0)
    {
        return (zt_u8 *)skb->data;
    }

    pskb_trim(skb, skb->len - size);

    prx_pkt->ptail = skb_tail_pointer(skb);
    prx_pkt->len = skb->len;

    return (zt_u8 *)prx_pkt->ptail;
}

static inline void free_skb_func(nic_info_st *nic_info, void *skb)
{
    ZT_UNUSED(nic_info);

    zt_free_skb(skb);
}

static inline void upload_skb(nic_info_st *nic_info, void *pskbv)
{
    zt_s32 ret;
    struct net_device *ndev = nic_info->ndev;
    struct sk_buff *pskb = pskbv;
    struct zt_ethhdr *pethhdr;

    if (pskb == NULL)
    {
        LOG_E("NULL Pointer");
        return ;
    }

    pethhdr = (struct zt_ethhdr *)pskb->data;

#ifdef CFG_ENABLE_MONITOR_MODE
    if (zt_local_cfg_get_work_mode(nic_info) == ZT_MONITOR_MODE)
    {
        pskb->dev = ndev;
        pskb->protocol = eth_type_trans(pskb, ndev);
        pskb->ip_summed = CHECKSUM_NONE;
        if (in_interrupt())
        {
            //LOG_I("[%s, %d]", __func__, __LINE__);
            ret = netif_rx(pskb);
        }
        else
        {
            //LOG_I("[%s, %d]", __func__, __LINE__);
            ret = netif_rx_ni(pskb);
        }

        if (ret == NET_RX_SUCCESS)
        {
            //LOG_D("netif rx success <%d>", pskb->len);
        }
        else
        {
            LOG_D("netif_rx failed, skb is dropped, ret:%d", ret);
        }
        return;
    }
#endif

    if ((zt_mlme_check_mode(nic_info, ZT_INFRA_MODE) == zt_true)
#ifdef CFG_ENABLE_ADHOC_MODE
            || (zt_mlme_check_mode(nic_info, ZT_ADHOC_MODE) == zt_true)
#endif
       )
    {
        if ((zt_memcmp(ndev->dev_addr, pethhdr->dest, ETH_ALEN) == 0) ||
                (IS_MCAST(pethhdr->dest)))
        {
            pskb->protocol = eth_type_trans(pskb, ndev);
            pskb->dev = ndev;
            pskb->ip_summed = CHECKSUM_NONE;
            if (in_interrupt())
            {
                //LOG_I("[%s, %d]", __func__, __LINE__);
                ret = netif_rx(pskb);
            }
            else
            {
                //LOG_I("[%s, %d]", __func__, __LINE__);
                ret = netif_rx_ni(pskb);
            }

            if (ret == NET_RX_SUCCESS)
            {
                //LOG_D("netif rx success <%d>", pskb->len);
            }
            else
            {
                LOG_D("netif_rx failed, skb is dropped, ret:%d", ret);
            }
        }
        else
        {
            zt_free_skb(pskb);
        }
    }
#ifdef CFG_ENABLE_AP_MODE
    else if ((zt_mlme_check_mode(nic_info, ZT_MASTER_MODE) == zt_true))
    {
        if (zt_memcmp(ndev->dev_addr, pethhdr->dest, ETH_ALEN) == 0)
        {
            pskb->protocol = eth_type_trans(pskb, ndev);
            pskb->dev = ndev;
            pskb->ip_summed = CHECKSUM_NONE;
            if (in_interrupt())
            {
                //LOG_I("[%s, %d]", __func__, __LINE__);
                ret = netif_rx(pskb);
            }
            else
            {
                //LOG_I("[%s, %d]", __func__, __LINE__);
                ret = netif_rx_ni(pskb);
            }

            if (ret == NET_RX_SUCCESS)
            {
                //LOG_D("netif rx success <%d>", pskb->len);
            }
            else
            {
                LOG_D("netif_rx failed, skb is dropped, ret:%d", ret);
            }
            return;
        }

        if (!IS_MCAST(pethhdr->dest))
        {
            // forward it
            ret = zt_tx_msdu(nic_info, pskb->data, pskb->len, pskb);
            if (ret < 0)
            {
                /* failed xmit, must release the resource */
                zt_free_skb(pskb);
            }
            else if (!ret)
            {
                tx_work_wake(ndev);
            }
        }
        else
        {
            struct sk_buff *psend_skb = skb_clone(pskb, GFP_ATOMIC);
            pskb->protocol = eth_type_trans(pskb, ndev);
            pskb->dev = ndev;
            pskb->ip_summed = CHECKSUM_NONE;
            if (in_interrupt())
            {
                //LOG_I("[%s, %d]", __func__, __LINE__);
                ret = netif_rx(pskb);
            }
            else
            {
                //LOG_I("[%s, %d]", __func__, __LINE__);
                ret = netif_rx_ni(pskb);
            }
            if (ret == NET_RX_SUCCESS)
            {
                //LOG_D("netif rx success <%d>", pskb->len);
            }
            else
            {
                LOG_D("netif_rx failed, skb is dropped, ret:%d", ret);
            }

            if (psend_skb != NULL)
            {
                // forward it
                if (0 != zt_tx_msdu(nic_info, psend_skb->data, psend_skb->len, psend_skb))
                {
                    /* failed xmit, must release the resource */
                    zt_free_skb(psend_skb);
                }
                else
                {
                    tx_work_wake(ndev);
                }
            }
        }
    }
#endif
    else
    {
        LOG_I("mlme mode is not allowed to recv frame");
        zt_free_skb(pskb);
    }
}


struct sk_buff *alloc_msdu(const zt_u8 *da, const zt_u8 *sa, zt_u8 *msdu,
                           zt_u16 msdu_len)
{
    zt_u8  *data_ptr;
    struct sk_buff *sub_skb;

    sub_skb = zt_alloc_skb(msdu_len + 14);
    if (sub_skb)
    {
        skb_reserve(sub_skb, 14);
        data_ptr = (zt_u8 *)skb_put(sub_skb, msdu_len);
        zt_memcpy(data_ptr, msdu, msdu_len);
    }
    else
    {
        return NULL;
    }

    if (is_snap_pkt(sub_skb))
    {
        /* remove RFC1042 or Bridge-Tunnel encapsulation and replace EtherType */
        skb_pull(sub_skb, ZT_80211_SNAP_HDR_SIZE);
        zt_memcpy(skb_push(sub_skb, ETH_ALEN), sa, ETH_ALEN);
        zt_memcpy(skb_push(sub_skb, ETH_ALEN), da, ETH_ALEN);
    }
    else
    {
        /* Leave Ethernet header part of hdr and full payload */
        zt_u16 len;

        len = htons(sub_skb->len);
        zt_memcpy(skb_push(sub_skb, 2), &len, 2);
        zt_memcpy(skb_push(sub_skb, ETH_ALEN), sa, ETH_ALEN);
        zt_memcpy(skb_push(sub_skb, ETH_ALEN), da, ETH_ALEN);
    }

    return sub_skb;
}


zt_s32 process_amsdu(struct net_device *ndev, prx_pkt_t ppkt)
{
    zt_s32 pktlen;
    zt_s32 ret = 0;
    zt_u16 subframeSize, padding_len;
    zt_u8 *pdata;
    zt_u8 *sa;
    zt_u8 *da;
    zt_u8 nr_subframes = 0;
    struct sk_buff_head amsdu_skb_list;
    struct sk_buff *pskb;
    zt_80211_ftype_t frame_type;

    LOG_W("%s, ", __func__);
    skb_queue_head_init(&amsdu_skb_list);

    frame_type = zt_80211_hdr_ftype_get(ppkt->pdata);
    if (frame_type != ZT_80211_FTYPE_DATA)
    {
        LOG_E("amsdu should be data frame ! check...");
        return -1;
    }

    pktlen = ppkt->len - ppkt->pkt_info.wlan_hdr_len - ppkt->pkt_info.iv_len;
    pdata = ppkt->pdata + ppkt->pkt_info.wlan_hdr_len + ppkt->pkt_info.iv_len;

    LOG_I("amsdu process TID:%d  wlan_hdr_len:%d  pkt_len:%d  iv_len:%d",
          ppkt->pkt_info.qos_pri, ppkt->pkt_info.wlan_hdr_len, ppkt->len,
          ppkt->pkt_info.iv_len);

    LOG_I("da addr="ZT_MAC_FMT, ZT_MAC_ARG(pdata));
    LOG_I("sa addr="ZT_MAC_FMT, ZT_MAC_ARG(pdata + ETH_ALEN));


    do
    {
        subframeSize = ZT_PCHAR_2_BE16(pdata + AMSDU_HDR_Len_OFFSET);
        if (subframeSize + AMSDU_HDR_LEN > pktlen)
        {
            LOG_E("amsdu process error  (msdu_size:%d  pktLen:%d)", subframeSize, pktlen);
            break;
        }

        da = pdata;
        sa = pdata + ETH_ALEN;

        pskb = alloc_msdu(da, sa, pdata + ETH_HLEN, subframeSize);
        if (!pskb)
        {
            LOG_E("alloc skb error");
            break;
        }

        nr_subframes++;
        skb_queue_tail(&amsdu_skb_list, pskb);
        LOG_I("[process_amsdu] add skb");

        pdata += ETH_HLEN;
        pktlen -= ETH_HLEN;

        if (nr_subframes > MAX_SUBFRAME_COUNT)
        {
            LOG_E("too many subframes!!");
            break;
        }

        pdata += subframeSize;
        pktlen -= subframeSize;
        if (pktlen != 0)
        {
            padding_len = 4 - ((subframeSize + ETH_HLEN) & (4 - 1));
            if (padding_len == 4)
            {
                padding_len = 0;
            }

            if (pktlen < padding_len)
            {
                LOG_E("ParseSubframe(): pktlen < padding_len !\n");
                break;
            }
            pdata += padding_len;
            pktlen -= padding_len;
        }
    } while (pktlen > AMSDU_HDR_LEN);

    /* process every msdu */
    while ((pskb = skb_dequeue(&amsdu_skb_list)) != NULL)
    {
        ndev_priv_st *ndev_priv = netdev_priv(ndev);
        upload_skb(ndev_priv->nic, pskb);
        LOG_I("upload_skb(): len = %d", pskb->len);
    }

    return ret;
}

zt_s32 wlan_to_eth(prx_pkt_t ppkt)
{
    prx_pkt_info_t prx_info = &ppkt->pkt_info;
    zt_u8 *pbuf = ppkt->pdata;
    zt_u8 rmv_len;
    zt_u8 snap_hdr_offset;
    zt_u16 data_len;
    zt_u16 eth_type;
    zt_bool bsnap;

    if (prx_info->bdecrypted)
    {
        pkt_trim(ppkt, prx_info->icv_len);
    }

    /* if this is a null packet, maybe lps should handle it */
    if (ppkt->len == prx_info->wlan_hdr_len)
    {
        LOG_D("recv a null packet! src_addr:"ZT_MAC_FMT,
              ZT_MAC_ARG(prx_info->src_addr));
        return -1;
    }

    snap_hdr_offset = prx_info->wlan_hdr_len + prx_info->iv_len;
    bsnap = zt_80211_is_snap_hdr(pbuf + snap_hdr_offset);
    rmv_len = snap_hdr_offset + (bsnap ? ZT_80211_SNAP_HDR_SIZE : 0);

    if (rmv_len > ppkt->len)
    {
        LOG_E("[wlan_to_eth] data_len error (pktlen:%d  rmv_len:%d)",
              ppkt->len, rmv_len);
        return -1;
    }

    data_len = ppkt->len - rmv_len;
    //LOG_W("rmv_len=%d, prx_info->wlan_hdr_len = %d", rmv_len, prx_info->wlan_hdr_len);
    zt_memcpy(&eth_type, pbuf + rmv_len, 2);
    eth_type = ntohs((zt_u16)eth_type);
    prx_info->eth_type = eth_type;

    pbuf = pkt_pull(ppkt, (rmv_len - sizeof(struct ethhdr) + (bsnap ? 2 : 0)));
    if (!pbuf)
    {
        LOG_D("pbuf == NULL");
        return -1;
    }

    zt_memcpy(pbuf, prx_info->dst_addr, ZT_80211_MAC_ADDR_LEN);
    zt_memcpy(pbuf + ZT_80211_MAC_ADDR_LEN, prx_info->src_addr,
              ZT_80211_MAC_ADDR_LEN);

    if (!bsnap)
    {
        data_len = htons(data_len);
        zt_memcpy(pbuf + 12, (zt_u8 *)&data_len, sizeof(data_len));
    }

    return 0;
}



static void rx_data_upload_process(rx_pkt_t *pkt, struct net_device *ndev)
{
    zt_s32 ret;
    if (pkt->pkt_info.amsdu)
    {
        /* amsdu process */
        ret = process_amsdu(ndev, pkt);
        if (pkt->pskb)
        {
            zt_free_skb(pkt->pskb);
        }
    }
    else
    {
        /* msdu process */
        ret = wlan_to_eth(pkt);
        if (ret == 0)
        {
            ndev_priv_st *ndev_priv = netdev_priv(ndev);
            upload_skb(ndev_priv->nic, pkt->pskb);
        }
        else
        {
            if (pkt->pskb)
            {
                zt_free_skb(pkt->pskb);
            }
        }
    }
}


static void rx_data_reorder_process(rx_pkt_t *pkt, struct net_device *ndev)
{
    wdn_net_info_st *pwdn_info = pkt->wdn_info;
    zt_s32 hdr_tansfer_ret = 0;
    zt_s32 prio    = 0;
    zt_s32 seq_num = 0;


    prio = pkt->pkt_info.qos_pri;
    seq_num = pkt->pkt_info.seq_num;

    if (NULL == pwdn_info || (prio > 15))
    {
        if (pkt->pskb)
        {
            zt_free_skb(pkt->pskb);
        }

        LOG_E("[%s]:pri error:%d , drop packet", __func__, prio);
        return;
    }

    hdr_tansfer_ret = wlan_to_eth(pkt);
    if (hdr_tansfer_ret < 0)
    {
        if (pkt->pskb)
        {
            zt_free_skb(pkt->pskb);
        }
        return;
    }

    if (0 == pkt->pkt_info.qos_flag)
    {
        ndev_priv_st *ndev_priv = netdev_priv(ndev);
        upload_skb(ndev_priv->nic, pkt->pskb);
        return;
    }

    if (NULL == pwdn_info->ba_ctl ||  zt_false == pwdn_info->ba_ctl[prio].enable)
    {
        ndev_priv_st *ndev_priv = netdev_priv(ndev);
        upload_skb(ndev_priv->nic, pkt->pskb);
        //rx_do_update_expect_seq(seq_num, &pwdn_info->ba_ctl[prio]);
        return;
    }

    if (0 == pwdn_info->ba_ctl[prio].wait_timeout)
    {
        pwdn_info->ba_ctl[prio].wait_timeout = 50;
    }

    if (NULL == pwdn_info->ba_ctl[prio].upload_func ||
            NULL == pwdn_info->ba_ctl[prio].free_skb)
    {
        pwdn_info->ba_ctl[prio].upload_func = upload_skb;
        pwdn_info->ba_ctl[prio].free_skb    = free_skb_func;
    }

    zt_rx_data_reorder_core(pkt);
}

void mpdu_process(struct net_device *ndev, zt_u8 *buf, zt_u16 buf_size)
{
    zt_s32 ret;
    rx_pkt_t pkt;
    ndev_priv_st *pndev_priv = netdev_priv(ndev);
    nic_info_st *nic_info = pndev_priv->nic;
    hw_info_st *hw_info = NULL;
    rx_info_t *rx_info = NULL;

    if (NULL == nic_info)
    {
        LOG_E("%s, NULL== nic_info", __func__);
        goto exit;
    }

    hw_info = nic_info->hw_info;

    if (zt_rx_data_type(buf) != ZT_PKT_TYPE_FRAME)
    {
        LOG_E("[%s]: error mpdu type, pkt_len is %d", __func__, buf_size);
        goto exit;
    }

    rx_info = nic_info->rx_info;

    rx_info->rx_total_pkts++;
    rx_info->rx_bytes += buf_size;

    zt_memset(&pkt, 0, sizeof(rx_pkt_t));

    /* agg pkt, new create new skb*/
    ret = create_skb(&pkt, buf, buf_size);
    if (ret)
    {
        goto exit;
    }

    pkt.p_nic_info = nic_info;
    pkt.prx_info = nic_info->rx_info;

    zt_rx_rxd_prase(pkt.pdata, &pkt);

    if (pkt.pkt_info.hif_hdr_len > pkt.len)
    {
        LOG_E("error mpdu (hif_hdr_len:%d  pkt_len:%d)", pkt.pkt_info.hif_hdr_len,
              pkt.len);
        goto exit;
    }

    /* process phystatus */
    if (pkt.pkt_info.phy_status)
    {
        zt_u8 agc_gain = pkt.pdata[RXD_SIZE];
        zt_u8 cck_sig_qual_or_ofdm_pwdb = pkt.pdata[RXD_SIZE + 2];
        zt_u8 cck_agc_rpt_or_ofdm_cfo  = pkt.pdata[RXD_SIZE + 3];
        zt_s8 rx_evm =  pkt.pdata[RXD_SIZE + 5];
        zt_rx_calc_str_and_qual(nic_info, agc_gain, cck_sig_qual_or_ofdm_pwdb,
                                cck_agc_rpt_or_ofdm_cfo, rx_evm, &pkt);
    }

    pkt_pull(&pkt, pkt.pkt_info.hif_hdr_len);

    /* process frame*/
    ret = zt_rx_common_process(&pkt);
    if (ret)
    {
        goto exit;
    }

#ifdef CFG_ENABLE_MONITOR_MODE
    if (zt_local_cfg_get_work_mode(nic_info) == ZT_MONITOR_MODE)
    {
        ndev_priv_st *ndev_priv = netdev_priv(ndev);
        upload_skb(ndev_priv->nic, pkt.pskb);
        return;
    }
#endif

    if (zt_80211_hdr_ftype_get(pkt.pdata) == ZT_80211_FTYPE_DATA)
    {
        if ((hw_info->ba_enable_rx == zt_true) && (pkt.pkt_info.qos_flag))
        {
            rx_data_reorder_process(&pkt, ndev);
        }
        else
        {
            rx_data_upload_process(&pkt, ndev);
        }

        return;
    }

exit:
    if (pkt.pskb)
    {
        zt_free_skb(pkt.pskb);
    }
}

