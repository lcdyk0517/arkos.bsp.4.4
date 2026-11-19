/*
 * tx.c
 *
 * used for data frame xmit
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

#include "common.h"

static zt_bool xmit_buf_resource_new(struct xmit_buf *pxmit_buf,
                                     nic_info_st *nic_info, zt_u32 alloc_sz)
{
    if (alloc_sz > 0)
    {
        pxmit_buf->pallocated_buf = zt_kzalloc(alloc_sz + XMITBUF_ALIGN_SZ);
        if (pxmit_buf->pallocated_buf == NULL)
        {
            return zt_false;
        }

        pxmit_buf->ptail = pxmit_buf->pbuf =
                               (zt_u8 *) ZT_N_BYTE_ALIGMENT((SIZE_PTR)(pxmit_buf->pallocated_buf),
                                       XMITBUF_ALIGN_SZ);
    }

    return zt_true;
}

typedef struct
{
    zt_u8 version: 4;
    zt_u8 header_len: 4;
    zt_u8 tos;
    zt_u16 total_len;
    zt_u16 ident;
    zt_u16 flags: 3;
    zt_u16 seg_offset: 13;
    zt_u8  ttl;
    zt_u8  proto;
    zt_u16 checksum;
    zt_u8  src_ip[4];
    zt_u8  dest_ip[4];
} zt_packed ip_header;

static void do_set_qos(struct xmit_frame *pxmitframe, ip_header *ip_hdr)
{
    zt_s32 user_priority = 0;

    if (pxmitframe->ether_type == 0x0800)
    {
        user_priority = ip_hdr->tos >> 5;
    }

    pxmitframe->priority = user_priority;
    pxmitframe->hdrlen = WLAN_HDR_A3_QOS_LEN;
}

static zt_bool xmit_frame_sec_init(nic_info_st *nic_info, wdn_net_info_st *pwdn,
                                   struct xmit_frame *pxmitframe)
{
    sec_info_st *sec_info = nic_info->sec_info;

    zt_memset(pxmitframe->dot11tkiptxmickey.skey, 0, 16);
    zt_memset(pxmitframe->dot118021x_UncstKey.skey, 0, 16);

    /* if network is 8021X type, befor EAPOL 4 handshark complete, only EAPOL
    packet can get througth */
    if (sec_info->dot11AuthAlgrthm == dot11AuthAlgrthm_8021X &&
            pwdn->ieee8021x_blocked == zt_true)
    {
        LOG_I("pwdn->ieee8021x_blocked == zt_true");

        pxmitframe->encrypt_algo = _NO_PRIVACY_;

        if (pxmitframe->ether_type != 0x888e)
        {
            LOG_I("pxmitframe->ether_type(%.4x) != 0x888e", pxmitframe->ether_type);
            return zt_false;
        }
    }
    else
    {
        GET_ENCRY_ALGO(sec_info, pwdn, pxmitframe->encrypt_algo, pxmitframe->bmcast);

        switch (sec_info->dot11AuthAlgrthm)
        {
            case dot11AuthAlgrthm_Open:
            case dot11AuthAlgrthm_Shared:
            case dot11AuthAlgrthm_Auto:
                pxmitframe->key_idx = (zt_u8) sec_info->dot11PrivacyKeyIndex;
                break;
            case dot11AuthAlgrthm_8021X:
                if (pxmitframe->bmcast)
                {
                    pxmitframe->key_idx = (zt_u8) sec_info->dot118021XGrpKeyid;
                }
                else
                {
                    pxmitframe->key_idx = 0;
                }
                break;
            default:
                pxmitframe->key_idx = 0;
                break;
        }

        if (((pxmitframe->encrypt_algo == _WEP40_) ||
                (pxmitframe->encrypt_algo == _WEP104_))
                && (pxmitframe->ether_type == 0x888e))
        {
            pxmitframe->encrypt_algo = _NO_PRIVACY_;
        }
    }

    switch (pxmitframe->encrypt_algo)
    {
        case _WEP40_:
        case _WEP104_:
            pxmitframe->iv_len = 4;
            pxmitframe->icv_len = 4;
            WEP_IV(pxmitframe->iv, pwdn->dot11txpn, pxmitframe->key_idx);
            break;
        case _TKIP_:
            pxmitframe->iv_len = 8;
            pxmitframe->icv_len = 4;

            if (sec_info->busetkipkey == zt_false)
            {
                return zt_false;
            }

            if (pxmitframe->bmcast)
            {
                TKIP_IV(pxmitframe->iv, pwdn->dot11txpn, pxmitframe->key_idx);
            }
            else
            {
                TKIP_IV(pxmitframe->iv, pwdn->dot11txpn, 0);
            }

            zt_memcpy(pxmitframe->dot11tkiptxmickey.skey, pwdn->dot11tkiptxmickey.skey, 16);
            break;
        case _AES_:
            pxmitframe->iv_len = 8;
            pxmitframe->icv_len = 8;

            if (pxmitframe->bmcast)
            {
                AES_IV(pxmitframe->iv, pwdn->dot11txpn, pxmitframe->key_idx);
            }
            else
            {
                AES_IV(pxmitframe->iv, pwdn->dot11txpn, 0);
            }
            break;
        default:
            pxmitframe->iv_len = 0;
            pxmitframe->icv_len = 0;
            break;
    }

    if (pxmitframe->encrypt_algo != _NO_PRIVACY_ &&
            pxmitframe->encrypt_algo != _WEP40_ &&
            pxmitframe->encrypt_algo != _WEP104_)
    {
        zt_memcpy(pxmitframe->dot118021x_UncstKey.skey,
                  pwdn->dot118021x_UncstKey.skey, 16);
    }

    pxmitframe->bswenc = pxmitframe->encrypt_algo == _AES_ ? zt_false : zt_true;

    return zt_true;
}

static void xmit_frame_vcs_init(nic_info_st *nic_info,
                                struct xmit_frame *pxmitframe)
{
    zt_u32 sz;
    hw_info_st *hw_info = nic_info->hw_info;
    wdn_net_info_st *pwdn = pxmitframe->pwdn;

#ifdef CFG_ENABLE_AP_MODE
    if (zt_local_cfg_get_work_mode(nic_info) == ZT_MASTER_MODE)
    {
        pxmitframe->vcs_mode = NONE_VCS;
        return;
    }
#endif

    if (pxmitframe->nr_frags != 1)
    {
        sz = hw_info->frag_thresh;
    }
    else
    {
        sz = pxmitframe->last_txcmdsz;
    }

    if (pwdn->network_type < WIRELESS_11_24N)
    {
        if (sz > hw_info->rts_thresh)
        {
            pxmitframe->vcs_mode = RTS_CTS;
        }
        else
        {
            if (pwdn->rtsen)
            {
                pxmitframe->vcs_mode = RTS_CTS;
            }
            else if (pwdn->cts2self)
            {
                pxmitframe->vcs_mode = CTS_TO_SELF;
            }
            else
            {
                pxmitframe->vcs_mode = NONE_VCS;
            }
        }
    }
    else
    {
        while (zt_true)
        {
            if (pwdn->rtsen || pwdn->cts2self)
            {
                if (pwdn->rtsen)
                {
                    pxmitframe->vcs_mode = RTS_CTS;
                }
                else if (pwdn->cts2self)
                {
                    pxmitframe->vcs_mode = CTS_TO_SELF;
                }

                break;
            }

            if (pxmitframe->ht_en)
            {
                zt_u8 HTOpMode = pwdn->HT_protection;
                if ((pwdn->bw_mode && (HTOpMode == 2 || HTOpMode == 3))
                        || (!pwdn->bw_mode && HTOpMode == 3))
                {
                    pxmitframe->vcs_mode = RTS_CTS;
                    break;
                }
            }

            if (sz > hw_info->rts_thresh)
            {
                pxmitframe->vcs_mode = RTS_CTS;
                break;
            }

            if (pxmitframe->ampdu_en == zt_true)
            {
                pxmitframe->vcs_mode = RTS_CTS;
                break;
            }

            pxmitframe->vcs_mode = NONE_VCS;
            break;
        }
    }

    if (hw_info->vcs_en == 1)
    {
        pxmitframe->vcs_mode = hw_info->vcs_type;
    }
}

zt_bool zt_xmit_frame_init(nic_info_st *nic_info, struct xmit_frame *pxmitframe,
                           zt_u8 *msdu_buf, zt_s32 msdu_len)
{
    zt_u8 *ra_addr;
    struct zt_ethhdr *pethhdr;
    ip_header iphdr;
    wdn_net_info_st *pwdn = NULL;
    zt_u8 bc_addr[ZT_80211_MAC_ADDR_LEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

    hw_info_st *hw_info = nic_info->hw_info;
    zt_s32 pkt_offset = 0;

    pethhdr = (struct zt_ethhdr *)msdu_buf;
#ifdef CFG_ENABLE_ADHOC_MODE
    if (zt_mlme_check_mode(nic_info, ZT_ADHOC_MODE))
    {
        ra_addr = pethhdr->dest;
    }
    else
#endif
        if (zt_mlme_check_mode(nic_info, ZT_INFRA_MODE))
        {
            ra_addr = zt_wlan_get_cur_bssid(nic_info);
        }
#ifdef CFG_ENABLE_AP_MODE
        else if (zt_mlme_check_mode(nic_info, ZT_MASTER_MODE))
        {
            ra_addr = pethhdr->dest;
        }
#endif
        else
        {
            LOG_E("[%s]: mlme mode error, can't xmit data", __func__);
            return zt_false;
        }

    pxmitframe->bmcast = IS_MCAST(ra_addr);
    pxmitframe->ether_type = zt_be_u16_to_host_u16(&pethhdr->type);
    pkt_offset += ZT_ETH_HLEN;

    if (pxmitframe->bmcast)
    {
        pwdn = zt_wdn_find_info(nic_info, bc_addr);
        if (pwdn == NULL)
        {
            LOG_I("get wdn_info fail");
            return zt_false;
        }
    }
    else
    {
        pwdn = zt_wdn_find_info(nic_info, ra_addr);
        if (pwdn == NULL)
        {
            LOG_D("[frame_attrib_init] => get sta_unit fail, ra:" ZT_MAC_FMT,
                  ZT_MAC_ARG(ra_addr));
            return zt_false;
        }
    }

    pxmitframe->pktlen = msdu_len - ZT_ETH_HLEN;
    pxmitframe->dhcp_pkt = 0;

    iphdr.tos = 0;

    switch (pxmitframe->ether_type)
    {
        case ZT_ETH_P_IP:
            iphdr.header_len = ZT_GET_IPV4_IHL(msdu_buf + pkt_offset);
            iphdr.proto = ZT_GET_IPV4_PROTOCOL(msdu_buf + pkt_offset);
            iphdr.tos = ZT_GET_IPV4_TOS(msdu_buf + pkt_offset);

            pkt_offset += iphdr.header_len * 4;
            switch (iphdr.proto)
            {
                case 0x01:
                {
                    /* ICMP */
#ifdef TX_DEBUG
                    LOG_D("= ICMP Packet =");
#endif
                }
                break;
                case 0x02:
                {
                    /* IGMP */
#ifdef TX_DEBUG
                    LOG_D("= IGMP Packet =");
#endif
                }
                break;
                case 0x11:
                    /* UDP */
                {
                    zt_u8 udp[8];

                    zt_memcpy(udp, msdu_buf + pkt_offset, 8);
                    pkt_offset += 8;

#ifdef TX_DEBUG
                    LOG_D("= UDP Packet =");
#endif
                    if ((ZT_GET_UDP_SRC(udp) == 68 && ZT_GET_UDP_DST(udp) == 67)
                            || (ZT_GET_UDP_SRC(udp) == 67 && ZT_GET_UDP_DST(udp) == 68))
                    {
                        if (pxmitframe->pktlen > 282)
                        {
                            pxmitframe->dhcp_pkt = 1;

                            //#ifdef TX_DEBUG
                            LOG_D("<DHCP> Send");
                            //#endif
                        }
                    }
                }
                break;
                case 0x06:
                {
                    /* TCP */

                    //zt_memcpy(tcp, msdu_buf+pkt_offset, 20);
                    pkt_offset += 20;

#ifdef TX_DEBUG
                    LOG_D("= TCP Packet =");
#endif
                }
                break;
                default:
                    break;
            }
            break;
        // case 0x888e:
        //     #ifdef TX_DEBUG
        //     LOG_D("= EAPOL packet =");
        //     #endif
        //     break;
        // case ZT_ETH_P_ARP:
        //     #ifdef TX_DEBUG
        //     LOG_D("= ARP Packet =");
        //     #endif
        //     break;
        // case ZT_ETH_P_IPV6:
        //     #ifdef TX_DEBUG
        //     LOG_D("= IPv6 Packet =");
        //     #endif
        //     break;
        default:
            break;
    }

#ifdef CONFIG_LPS
    // if (pxmitframe->icmp_pkt == 1)
    // {
    //     zt_lps_wakeup(nic_info, LPS_CTRL_SPECIAL_PACKET, zt_true);
    // }
    // else if (pxmitframe->dhcp_pkt == 1)
    // {
    //     //DBG_COUNTER(nic_info->tx_logs.core_tx_upd_attrib_active);
    //     zt_lps_wakeup(nic_info, LPS_CTRL_SPECIAL_PACKET, zt_true);
    // }
    // if (atomic_read(&pwr_info->lps_spc_flag) == 0 && pwr_info->b_fw_current_in_ps_mode == zt_true)
    // {
    //     zt_lps_wakeup(nic_info, LPS_CTRL_SPECIAL_PACKET, zt_true);
    //     atomic_set(&pwr_info->lps_spc_flag, 1);
    // }
#endif

    if (xmit_frame_sec_init(nic_info, pwdn, pxmitframe) == zt_false)
    {
        return zt_false;
    }

    pxmitframe->pwdn = pwdn;
    pxmitframe->priority = 0;
    pxmitframe->pkt_hdrlen = ZT_ETH_HLEN;
    pxmitframe->hdrlen = WLAN_HDR_A3_LEN;

    if (pwdn->qos_option)
    {
        do_set_qos(pxmitframe, &iphdr);

        if (pwdn->acm_mask != 0)
        {
            pxmitframe->priority = zt_chk_qos(pwdn->acm_mask, pxmitframe->priority, 1);
        }
    }

#ifdef TX_DEBUG
    LOG_D("priority:%d", pxmitframe->priority);
#endif

    pxmitframe->qsel = pxmitframe->priority;

    if (hw_info->dot80211n_support)
    {
        pxmitframe->ampdu_en = zt_false;
        pxmitframe->ht_en = pwdn->ht_enable;
    }

    return zt_true;
}


zt_bool frame_txp_addmic(nic_info_st *nic_info, struct xmit_frame *pxmitframe)
{
    zt_u8 hw_hdr_offset = 0;
    zt_u8 priority[4] = { 0x0, 0x0, 0x0, 0x0 };
    zt_u8 null_key[16];
    zt_u8 *pframe, *payload, mic[8];
    zt_s32 curfragnum, length;
    struct mic_data micdata;
    hw_info_st *hw_info = nic_info->hw_info;
    sec_info_st *sec_info = nic_info->sec_info;

    /* make none(all zone) key */
    zt_memset(null_key, 0x0, sizeof(null_key));

#ifdef CONFIG_SOFT_TX_AGGREGATION
    hw_hdr_offset = TXDESC_SIZE + (pxmitframe->pkt_offset * PACKET_OFFSET_SZ);
#else
    hw_hdr_offset = TXDESC_OFFSET;
#endif

    if (pxmitframe->encrypt_algo == _TKIP_)
    {
        pframe = pxmitframe->buf_addr + hw_hdr_offset; /* point to msdu filed */
        /* calculate use tx mic key */
        if (pxmitframe->bmcast)
        {
            if (zt_memcmp(
                        sec_info->dot118021XGrptxmickey[sec_info->dot118021XGrpKeyid].skey,
                        null_key, 16) == 0)
            {
                return zt_false;
            }
            zt_sec_mic_set_key(&micdata,
                               sec_info->dot118021XGrptxmickey[sec_info->dot118021XGrpKeyid].skey);
        }
        else
        {
            if (!zt_memcmp(&pxmitframe->dot11tkiptxmickey.skey[0], null_key,
                           sizeof(null_key)))
            {
                return zt_false;
            }
            zt_sec_mic_set_key(&micdata, &pxmitframe->dot11tkiptxmickey.skey[0]);
        }
        /* calculate use DA & SA */
        if (pframe[1] & 1) /* ToDS == 1 */
        {
            zt_sec_mic_append(&micdata, &pframe[16], 6); /* addr3 for DA */
            if (pframe[1] & 2) /* From Ds == 1 */
            {
                zt_sec_mic_append(&micdata, &pframe[24], 6);    /* addr4 for SA */
            }
            else
            {
                zt_sec_mic_append(&micdata, &pframe[10], 6);    /* addr2 for SA */
            }
        }
        else /* ToDS == 0 */
        {
            zt_sec_mic_append(&micdata, &pframe[4], 6); /* addr1 for DA */
            if (pframe[1] & 2) /* From Ds == 1 */
            {
                zt_sec_mic_append(&micdata, &pframe[16], 6);    /* addr3 for SA */
            }
            else
            {
                zt_sec_mic_append(&micdata, &pframe[10], 6);    /* addr2 for SA */
            }
        }
        /* calculate use priority value */
        if (pxmitframe->pwdn->qos_option)
        {
            priority[0] = (zt_u8)pxmitframe->priority;
        }
        zt_sec_mic_append(&micdata, &priority[0], 4);
        /* calculate use msdu(all fragments) */
        payload = pframe;
        for (curfragnum = 0; curfragnum < pxmitframe->nr_frags; curfragnum++)
        {
            payload = (zt_u8 *)ZT_RND4((SIZE_PTR)payload);
            payload = &payload[pxmitframe->hdrlen + pxmitframe->iv_len];
            if ((curfragnum + 1) == pxmitframe->nr_frags)
            {
                length = pxmitframe->last_txcmdsz - pxmitframe->hdrlen -
                         pxmitframe->iv_len - pxmitframe->icv_len;
                zt_sec_mic_append(&micdata, payload, length);
                payload += length;
            }
            else
            {
                length = hw_info->frag_thresh - pxmitframe->hdrlen -
                         pxmitframe->iv_len - pxmitframe->icv_len;
                zt_sec_mic_append(&micdata, payload, length);
                payload += length + pxmitframe->icv_len;
                /* auther: luozhi
                   date: 2020-9-10
                   todo: fix bug
                   point to next fragment(payload) should skip tx descript head */
                payload += hw_hdr_offset;
            }
        }

        /* fill mic field */
        zt_sec_get_mic(&micdata, &(mic[0]));
        zt_memcpy(payload, &(mic[0]), sizeof(mic));
        pxmitframe->last_txcmdsz += sizeof(mic);

    }

    return zt_true;
}

static zt_bool tx_mac_hdr_build(nic_info_st *nic_info,
                                struct xmit_frame *pxmitframe, zt_u8 *msdu_buf, zt_u8 *hdr)
{
    zt_bool qos_option = zt_false;
    zt_u16 *qc;
    struct zt_ethhdr *pethhdr;
    zt_80211_data_t *pwlanhdr = (zt_80211_data_t *)hdr;
    zt_u16 *fctrl = &pwlanhdr->frame_control;
    hw_info_st *hw_info = nic_info->hw_info;
    wdn_net_info_st *pwdn;
    zt_u8 bc_addr[ZT_80211_MAC_ADDR_LEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

    ZT_ASSERT(pxmitframe != NULL);

    pethhdr = (struct zt_ethhdr *)msdu_buf;
    zt_memset(hdr, 0, WLANHDR_OFFSET);
    SetFrameSubType(fctrl, WIFI_DATA_TYPE);
    if ((zt_mlme_check_mode(nic_info, ZT_INFRA_MODE) == zt_true))
    {
        SetToDs(fctrl);
        zt_memcpy(pwlanhdr->addr3, pethhdr->dest, ZT_80211_MAC_ADDR_LEN);
        zt_memcpy(pwlanhdr->addr2, pethhdr->src, ZT_80211_MAC_ADDR_LEN);
        zt_memcpy(pwlanhdr->addr1, zt_wlan_get_cur_bssid(nic_info),
                  ZT_80211_MAC_ADDR_LEN);
    }
#ifdef CFG_ENABLE_AP_MODE
    else if ((zt_mlme_check_mode(nic_info, ZT_MASTER_MODE) == zt_true))
    {
        SetFrDs(fctrl);
        zt_memcpy(pwlanhdr->addr3, pethhdr->src, ZT_80211_MAC_ADDR_LEN);
        zt_memcpy(pwlanhdr->addr2, zt_wlan_get_cur_bssid(nic_info),
                  ZT_80211_MAC_ADDR_LEN);
        zt_memcpy(pwlanhdr->addr1, pethhdr->dest, ZT_80211_MAC_ADDR_LEN);
    }
#endif
#ifdef CFG_ENABLE_ADHOC_MODE
    else if (zt_mlme_check_mode(nic_info, ZT_ADHOC_MODE) == zt_true)
    {
        zt_memcpy(pwlanhdr->addr3, zt_wlan_get_cur_bssid(nic_info),
                  ZT_80211_MAC_ADDR_LEN);
        zt_memcpy(pwlanhdr->addr2, pethhdr->src, ZT_80211_MAC_ADDR_LEN);
        zt_memcpy(pwlanhdr->addr1, pethhdr->dest, ZT_80211_MAC_ADDR_LEN);
    }
#endif
    else
    {
        LOG_I("mlme mode is not allowed to xmit frame");
        return zt_false;
    }

    if (IS_MCAST(pwlanhdr->addr1))
    {
        pwdn = zt_wdn_find_info(nic_info, bc_addr);
    }
    else
    {
        pwdn = zt_wdn_find_info(nic_info, pwlanhdr->addr1);
    }
    if (pwdn == NULL)
    {
        LOG_I("%s, pwdn==NULL\n", __func__);
        return zt_false;
    }
    if (pxmitframe->pwdn != pwdn)
    {
        LOG_I("[%s]:pxmitframe->pwdn(%p) != pwdn(%p)\n", __func__,
              pxmitframe->pwdn, pwdn);
        return zt_false;
    }

    if (pwdn->qos_option)
    {
        SetFrameSubType(fctrl, WIFI_QOS_DATA_TYPE);
        qos_option = zt_true;
    }

    if (pxmitframe->encrypt_algo)
    {
        SetPrivacy(fctrl);
    }

    if (qos_option)
    {
        qc = (zt_u16 *)(hdr + pxmitframe->hdrlen - 2);

        if (pxmitframe->priority)
        {
            SetPriority(qc, pxmitframe->priority);
        }

        SetEOSP(qc, 0);

        SetAckpolicy(qc, 0);
        pwdn->wdn_xmitpriv.txseq_tid[pxmitframe->priority]++;
        pwdn->wdn_xmitpriv.txseq_tid[pxmitframe->priority] &= 0xFFF;
        pxmitframe->seqnum = pwdn->wdn_xmitpriv.txseq_tid[pxmitframe->priority];
    } else {
        pwdn->wdn_xmitpriv.txseq_tid[TID_NUM]++;
        pwdn->wdn_xmitpriv.txseq_tid[TID_NUM] &= 0xFFF;
        pxmitframe->seqnum = pwdn->wdn_xmitpriv.txseq_tid[TID_NUM];
    }

    SetSeqNum(hdr, pxmitframe->seqnum);

    if (hw_info->dot80211n_support)
    {
        if (pwdn->ht_enable && pwdn->htpriv.mcu_ht.ampdu_enable)
        {
            if (pwdn->htpriv.mcu_ht.agg_enable_bitmap & ZT_BIT(pxmitframe->priority))
            {
                pxmitframe->ampdu_en = zt_true;
            }
        }

        if (pxmitframe->ampdu_en == zt_true)
        {
            zt_u16 tx_seq;

            tx_seq = pwdn->ba_starting_seqctrl[pxmitframe->priority & 0x0f];

            if (SN_LESS(pxmitframe->seqnum, tx_seq))
            {
                pxmitframe->ampdu_en = zt_false;
            }
            else if (SN_EQUAL(pxmitframe->seqnum, tx_seq))
            {
                pwdn->ba_starting_seqctrl[pxmitframe->priority & 0x0f] = (tx_seq + 1) & 0xfff;
                pxmitframe->ampdu_en = zt_true;
            }
            else
            {
                pwdn->ba_starting_seqctrl[pxmitframe->priority & 0x0f] =
                    (pxmitframe->seqnum + 1) & 0xfff;
                pxmitframe->ampdu_en = zt_true;
            }

        }
    }

    return zt_true;
}

static zt_s32 tx_set_snap(zt_u16 h_proto, zt_u8 *data)
{
    struct zt_80211_snap_header *snap = (struct zt_80211_snap_header *)data;

    snap->dsap = 0xaa;
    snap->ssap = 0xaa;
    snap->ctrl = 0x03;

    snap->oui[0] = 0x00;
    snap->oui[1] = 0x00;
    if (h_proto == 0x8137 || h_proto == 0x80f3)
    {
        snap->oui[2] = 0xf8;
    }
    else
    {
        snap->oui[2] = 0x00;
    }

    *(zt_u16 *)(data + sizeof(zt_80211_snap_header_t)) = htons(h_proto);

    return sizeof(zt_80211_snap_header_t) + sizeof(zt_u16);
}

void zt_tx_stats_cnt(nic_info_st *nic_info, struct xmit_frame *pxmitframe,
                     zt_s32 sz)
{
    wdn_net_info_st *pwdn = NULL;
    struct wdninfo_stats *pwdns = NULL;
    mlme_info_t *mlme_info = nic_info->mlme_info;
    tx_info_st *tx_info = nic_info->tx_info;

    if ((pxmitframe->frame_tag & 0x0f) == DATA_FRAMETAG)
    {
        mlme_info->link_info.num_tx_ok_in_period += 1;
        mlme_info->link_info.num_tx_ok_in_period_with_tid[pxmitframe->qsel] += 1;

        tx_info->tx_pkts += 1;
        tx_info->tx_bytes += sz;

        pwdn = pxmitframe->pwdn;
        if (pwdn)
        {
            pwdns = &pwdn->wdn_stats;

            pwdns->tx_pkts += 1;

            pwdns->tx_bytes += sz;
        }

    }
}

static zt_u8 sectype_to_hwdesc_get(struct xmit_frame *pxmitframe)
{
    zt_u8 sectype = 0;

    if ((pxmitframe->encrypt_algo > 0) && !pxmitframe->bswenc)
    {
        switch (pxmitframe->encrypt_algo)
        {
            case _WEP40_:
            case _WEP104_:
                sectype = 1;
                break;
            case _TKIP_:
            case _TKIP_WTMIC_:
                sectype = 2;
                break;
            case _AES_:
                sectype = 3;
                break;

            case _NO_PRIVACY_:
                sectype = 0;
                break;
            default:
                sectype = 4;
                break;
        }
    }

    return sectype;
}

static zt_u8 txdesc_scmapping_get(nic_info_st *nic_info,
                                  struct xmit_frame *pxmitframe)
{
    zt_u8 txdesc_scseting = 0;
    wdn_net_info_st *pwdn = pxmitframe->pwdn;

    if (zt_wlan_get_cur_bw(nic_info) == CHANNEL_WIDTH_40)
    {
        if (pwdn->bw_mode == CHANNEL_WIDTH_40)
        {
            txdesc_scseting = HT_DATA_SC_DONOT_CARE;
        }
        else if (pwdn->bw_mode == CHANNEL_WIDTH_20)
        {
            if (pwdn->channle_offset == HAL_PRIME_CHNL_OFFSET_UPPER)
            {
                txdesc_scseting = HT_DATA_SC_20_UPPER_OF_40MHZ;
            }
            else if (pwdn->channle_offset == HAL_PRIME_CHNL_OFFSET_LOWER)
            {
                txdesc_scseting = HT_DATA_SC_20_LOWER_OF_40MHZ;
            }
            else
            {
                txdesc_scseting = HT_DATA_SC_DONOT_CARE;
            }
        }
    }
    else
    {
        txdesc_scseting = HT_DATA_SC_DONOT_CARE;
    }

    return txdesc_scseting;
}

static zt_u8 txdesc_bwmapping_get(nic_info_st *nic_info,
                                  struct xmit_frame *pxmitframe)
{
    zt_u8 desc_bw_setting = 0;

    if (zt_wlan_get_cur_bw(nic_info) == CHANNEL_WIDTH_40)
    {
        if (pxmitframe->pwdn->bw_mode == CHANNEL_WIDTH_40)
        {
            desc_bw_setting = 1;
        }
        else
        {
            desc_bw_setting = 0;
        }
    }
    else
    {
        desc_bw_setting = 0;
    }

    return desc_bw_setting;
}

static void txdesc_vcs_fill(nic_info_st *nic_info,
                            struct xmit_frame *pxmitframe, zt_u8 *ptxdesc)
{
    wdn_net_info_st *pwdn = pxmitframe->pwdn;

    zt_set_bits_to_le_u32(ptxdesc + 8, 27, 2, 0);

    switch (pxmitframe->vcs_mode)
    {
        case RTS_CTS:
            zt_set_bits_to_le_u32(ptxdesc + 8, 27, 2, 1);
            break;
        case CTS_TO_SELF:
            zt_set_bits_to_le_u32(ptxdesc + 8, 27, 2, 2);
            break;
        case NONE_VCS:
        default:
            break;
    }

    if (pxmitframe->vcs_mode)
    {

        if (pwdn->short_preamble == zt_true)
        {
            zt_set_bits_to_le_u32(ptxdesc + 8, 29, 1, 1);
        }

        if (pxmitframe->ht_en)
        {
            zt_set_bits_to_le_u32(ptxdesc + 8, 30, 2, txdesc_scmapping_get(nic_info,
                                  pxmitframe));
        }
    }
    else
    {

    }
}

static void txdesc_phy_fill(nic_info_st *nic_info,
                            struct xmit_frame *pxmitframe,
                            zt_u8 *ptxdesc)
{

    if (pxmitframe->ht_en)
    {
        zt_set_bits_to_le_u32(ptxdesc + 16, 12, 1, txdesc_bwmapping_get(nic_info,
                              pxmitframe));
        zt_set_bits_to_le_u32(ptxdesc + 16, 13, 2, txdesc_scmapping_get(nic_info,
                              pxmitframe));
    }
}

static void txdesc_fill(struct xmit_frame *pxmitframe, zt_u8 *pbuf,
                        zt_bool bSendAck)
{
    nic_info_st *nic_info = pxmitframe->nic_info;
    wdn_net_info_st *pwdn = pxmitframe->pwdn;
    hw_info_st *hw_info = nic_info->hw_info;

    if (pxmitframe->frame_tag != DATA_FRAMETAG)
    {
        return;
    }

    zt_set_bits_to_le_u32(pbuf, 0, 2, TYPE_DATA);
    zt_set_bits_to_le_u32(pbuf + 16, 0, 5, pwdn->wdn_id);

    zt_set_bits_to_le_u32(pbuf + 16, 5, 1, 1);
    zt_set_bits_to_le_u32(pbuf + 16, 9, 3, 0);

    zt_set_bits_to_le_u32(pbuf + 12, 6, 5, pxmitframe->qsel);
    zt_set_bits_to_le_u32(pbuf, 19, 12, pxmitframe->seqnum);
    zt_set_bits_to_le_u32(pbuf + 12, 3, 3, sectype_to_hwdesc_get(pxmitframe));

    txdesc_vcs_fill(nic_info, pxmitframe, pbuf);

    if ((pxmitframe->ether_type != ZT_ETH_P_EAPOL) &&
            (pxmitframe->ether_type != ZT_ETH_P_ARP) &&
            (pxmitframe->ether_type != ZT_ETH_P_WAPI) &&
            (pxmitframe->dhcp_pkt != 1) &&
            (hw_info->use_fixRate != zt_true))
    {
        if (pxmitframe->ampdu_en == zt_true)
        {
            zt_set_bits_to_le_u32(pbuf + 12, 0, 1, 1);
        }
        else
        {
            zt_set_bits_to_le_u32(pbuf + 12, 19, 1, 1);
        }

        txdesc_phy_fill(nic_info, pxmitframe, pbuf);
        //determine that 11n has no supported rate plus 11bg has only one supported rate
        if (pwdn->ht_cap.supp_mcs_set[0] == 0x0 &&
            ((pwdn->datarate_len + pwdn->ext_datarate_len) == 1))
        {
            zt_u8 dot11_rate_table[] = { ZT_80211_CCK_RATE_1MB, ZT_80211_CCK_RATE_2MB,
                                 ZT_80211_CCK_RATE_5MB, ZT_80211_CCK_RATE_11MB,
                                 ZT_80211_OFDM_RATE_6MB, ZT_80211_OFDM_RATE_9MB,
                                 ZT_80211_OFDM_RATE_12MB, ZT_80211_OFDM_RATE_18MB,
                                 ZT_80211_OFDM_RATE_24MB, ZT_80211_OFDM_RATE_36MB,
                                 ZT_80211_OFDM_RATE_48MB, ZT_80211_OFDM_RATE_54MB, 0};
            zt_s32 i;

            zt_set_bits_to_le_u32(pbuf + 8, 16, 1, 1);
            i = 0;
            if (pwdn->datarate_len)
            {
                while (dot11_rate_table[i] != 0)
                {
                    if ((pwdn->datarate[0] & 0x7f) == dot11_rate_table[i])
                    {
                        zt_set_bits_to_le_u32(pbuf + 8, 18, 7, i);
                        break;
                    }
                    i++;
                }
            }
            else
            {
                while (dot11_rate_table[i] != 0)
                {
                    if ((pwdn->ext_datarate[0] & 0x7f) == dot11_rate_table[i])
                    {
                        zt_set_bits_to_le_u32(pbuf + 8, 18, 7, i);
                        break;
                    }
                    i++;
                }
            }
        }
        //determine that 11bg has no supported rate plus 11n has only one supported rate
        else if ((pwdn->ht_cap.supp_mcs_set[0] & (pwdn->ht_cap.supp_mcs_set[0] - 1)) == 0 &&
                ((pwdn->datarate_len + pwdn->ext_datarate_len) == 0))
        {
            int rate, num;

            rate = pwdn->ht_cap.supp_mcs_set[0];
            num = 0;
            while (rate > 0)
            {
                rate = rate >> 1;
                num++;
            }
            zt_set_bits_to_le_u32(pbuf + 8, 16, 1, 1);
            zt_set_bits_to_le_u32(pbuf + 8, 18, 7, num + DESC_RATE54M);
        }
        else
        {
            zt_set_bits_to_le_u32(pbuf + 8, 16, 1, 0);
        }
    }
    else
    {
        zt_set_bits_to_le_u32(pbuf + 12, 19, 1, 1);
        zt_set_bits_to_le_u32(pbuf + 8, 16, 1, 1);

        //txdesc_phy_fill(nic_info, pattrib, pbuf);

        if (pwdn->short_preamble == zt_true)
        {
            zt_set_bits_to_le_u32(pbuf + 8, 17, 1, 1);
        }

        zt_set_bits_to_le_u32(pbuf + 8, 16, 1, 1);

        if (hw_info->use_fixRate == zt_true)
        {
            zt_set_bits_to_le_u32(pbuf + 8, 18, 7, hw_info->fix_tx_rate);
        }
        else
        {
            zt_set_bits_to_le_u32(pbuf + 8, 18, 7, DESC_RATE1M);
        }
    }

#ifdef CONFIG_SOFT_TX_AGGREGATION
    zt_set_bits_to_le_u32(pbuf + 12, 24, 8, pxmitframe->agg_num);
#endif

    if (bSendAck == zt_true)
    {
        zt_set_bits_to_le_u32(pbuf + 12, 21, 1, 1);
        zt_set_bits_to_le_u32(pbuf + 4, 16, 12,
                              pwdn->wdn_xmitpriv.txseq_tid[pxmitframe->qsel]);
    }

    zt_set_bits_to_le_u32(pbuf + 8, 0, 16, pxmitframe->last_txcmdsz);

    if (pxmitframe->bmcast)
    {
        zt_set_bits_to_le_u32(pbuf + 12, 14, 1, 1);
    }

    if (!pwdn->qos_option)
    {
        zt_set_bits_to_le_u32(pbuf, 18, 1, 1);
    }
}


void zt_txdesc_chksum(zt_u8 *ptx_desc)
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

static void txdesc_update(struct xmit_frame *pxmitframe, zt_u8 *pbuf)
{
    nic_info_st *nic_info = pxmitframe->nic_info;
    hw_info_st *hw_info = nic_info->hw_info;

    txdesc_fill(pxmitframe, pbuf, (zt_bool)(hw_info->tx_data_rpt));

    zt_txdesc_chksum(pbuf);
}

zt_bool zt_tx_txdesc_init(struct xmit_frame *pxmitframe, zt_u8 *pmem, zt_s32 sz,
                          zt_bool bagg_pkt, zt_u8 dum)
{
    zt_bool ret = zt_false;

    struct tx_desc *ptxdesc = (struct tx_desc *)pmem;

    if ((PACKET_OFFSET_SZ != 0)
            && (zt_false == bagg_pkt))
    {
        ptxdesc = (struct tx_desc *)(pmem + PACKET_OFFSET_SZ);
        ret = zt_true;
        pxmitframe->pkt_offset--;
    }
    if (dum)
    {
        zt_memset(ptxdesc, 0, TXDESC_OFFSET);
        txdesc_update(pxmitframe, (zt_u8 *)ptxdesc);
    }
    return ret;

}

zt_u32 zt_quary_addr(zt_u8 qsel)
{
    zt_u32 addr = 0;

    switch (qsel)
    {
        case 0:
        case 3:
            addr = BE_QUEUE_INX;
            break;
        case 1:
        case 2:
            addr = BK_QUEUE_INX;
            break;
        case 4:
        case 5:
            addr = VI_QUEUE_INX;
            break;
        case 6:
        case 7:
            addr = VO_QUEUE_INX;
            break;
        case QSLT_BEACON:
            addr = BCN_QUEUE_INX;
            break;
        case QSLT_HIGH:
            addr = HIGH_QUEUE_INX;
            break;
        case QSLT_MGNT:
        default:
            addr = MGT_QUEUE_INX;
            break;

    }
    return addr;
}


zt_u8 zt_ra_sGI_get(wdn_net_info_st *pwdn, zt_u8 pad)
{
    zt_u8 sgi = zt_false, sgi_20m = zt_false, sgi_40m = zt_false;

    if (pad)
    {
        sgi_20m = pwdn->htpriv.mcu_ht.sgi_20m;
        sgi_40m = pwdn->htpriv.mcu_ht.sgi_40m;
    }

    if (pwdn->bw_mode == CHANNEL_WIDTH_40)
    {
        sgi = sgi_40m;
    }
    else
    {
        sgi = sgi_20m;
    }

    return sgi;
}

zt_u8 zt_chk_qos(zt_u8 acm_mask, zt_u8 priority, zt_u8 pad)
{
    zt_u8 change_priority = priority;

    if (pad)
    {
        switch (priority)
        {
            case 0:
            case 3:
                if (acm_mask & ZT_BIT(1))
                {
                    change_priority = 1;
                }
                break;
            case 1:
            case 2:
                break;
            case 4:
            case 5:
                if (acm_mask & ZT_BIT(2))
                {
                    change_priority = 0;
                }
                break;
            case 6:
            case 7:
                if (acm_mask & ZT_BIT(3))
                {
                    change_priority = 5;
                }
                break;
            default:
                LOG_E("[%s]: invalid pattrib->priority: %d!!!", __func__, priority);
                break;
        }
    }

    return change_priority;
}

zt_inline zt_bool zt_need_stop_queue(nic_info_st *nic_info)
{
    tx_info_st *tx_info = nic_info->tx_info;

    if (tx_info->free_xmitframe_cnt <= 1)
    {
        tx_info->xmitFrameCtl = 1;
        return zt_true;
    }

    return zt_false;
}


zt_inline zt_bool zt_need_wake_queue(nic_info_st *nic_info)
{
    tx_info_st *tx_info = nic_info->tx_info;

    if (tx_info->xmitFrameCtl == 1)
    {
        if (tx_info->free_xmitframe_cnt > (NR_XMITFRAME - 1))
        {
            tx_info->xmitFrameCtl = 0;
            return zt_true;
        }
    }

    return zt_false;
}

struct xmit_buf *zt_xmit_buf_new(tx_info_st *tx_info)
{
    zt_list_t *plist, *phead;
    struct xmit_buf *pxmitbuf = NULL;
    zt_que_t *pfree_xmitbuf_queue = &tx_info->xmit_buf_queue;

    zt_os_api_lock_lock(&pfree_xmitbuf_queue->lock);

    if (zt_list_is_empty(zt_que_list_head(pfree_xmitbuf_queue)) == zt_true)
    {
        pxmitbuf = NULL;
    }
    else
    {
        phead = zt_que_list_head(pfree_xmitbuf_queue);
        plist = zt_list_next(phead);
        pxmitbuf = ZT_CONTAINER_OF(plist, struct xmit_buf, list);
        zt_list_delete(&(pxmitbuf->list));
    }

    if (pxmitbuf != NULL)
    {
        tx_info->free_xmitbuf_cnt--;
        pxmitbuf->priv_data = NULL;
        pxmitbuf->pkt_len = 0;
        pxmitbuf->agg_num = 0;
        pxmitbuf->send_flag = 0;
        pxmitbuf->ptail = pxmitbuf->pbuf = (zt_u8 *) ZT_N_BYTE_ALIGMENT((SIZE_PTR)(
                                               pxmitbuf->pallocated_buf), XMITBUF_ALIGN_SZ);
    }

    zt_os_api_lock_unlock(&pfree_xmitbuf_queue->lock);

    return pxmitbuf;
}

#ifdef CONFIG_LPS
static struct xmit_buf *__wnew_cmd_txbuf(tx_info_st *tx_info,
        enum cmdbuf_type buf_type)
{
    struct xmit_buf *pxmitbuf = NULL;

    pxmitbuf = &tx_info->pcmd_xmitbuf[buf_type];
    if (pxmitbuf != NULL)
    {
        pxmitbuf->priv_data = NULL;
    }
    else
    {
        LOG_I("%s fail, no xmitbuf available", __func__);
    }

    return pxmitbuf;
}

struct xmit_frame *zt_xmit_cmdframe_new(tx_info_st *tx_info,
                                        enum cmdbuf_type buf_type,
                                        zt_u8 tag)
{
    struct xmit_frame *pcmdframe;
    struct xmit_buf *pxmitbuf;

    LOG_I(" func: %s", __func__);
    if ((pcmdframe = zt_xmit_frame_new(tx_info)) == NULL)
    {
        LOG_I("%s, alloc xmitframe fail", __FUNCTION__);
        return NULL;
    }

    if ((pxmitbuf = __wnew_cmd_txbuf(tx_info, buf_type)) == NULL)
    {
        LOG_I("%s, alloc xmitbuf fail", __FUNCTION__);
        zt_xmit_frame_delete(tx_info, pcmdframe);
        return NULL;
    }

    if (tag)
    {
        pcmdframe->frame_tag = MGNT_FRAMETAG;

        pcmdframe->pxmitbuf = pxmitbuf;

        if (pxmitbuf->pbuf == NULL)
        {
            LOG_I(" pxmitbuf->pbuf == NULL");
        }

        pcmdframe->buf_addr = pxmitbuf->pbuf;

        pxmitbuf->priv_data = pcmdframe;
    }
    return pcmdframe;

}
#endif
zt_bool zt_xmit_buf_delete(tx_info_st *tx_info, struct xmit_buf *pxmitbuf)
{
    zt_que_t *pfree_xmitbuf_queue = &tx_info->xmit_buf_queue;

    if ((pxmitbuf == NULL) || (tx_info == NULL))
    {
        return zt_false;
    }

    zt_os_api_lock_lock(&pfree_xmitbuf_queue->lock);
    zt_list_delete(&pxmitbuf->list);
    zt_list_insert_tail(&(pxmitbuf->list),
                        zt_que_list_head(pfree_xmitbuf_queue));
    tx_info->free_xmitbuf_cnt++;
    zt_os_api_lock_unlock(&pfree_xmitbuf_queue->lock);
    return zt_true;
}

struct xmit_buf *zt_xmit_extbuf_new(tx_info_st *tx_info)
{
    zt_list_t *plist, *phead;
    struct xmit_buf *pxmitbuf = NULL;
    zt_que_t *pfree_xmitbuf_queue = &tx_info->xmit_extbuf_queue;

    zt_os_api_lock_lock(&pfree_xmitbuf_queue->lock);

    if (zt_list_is_empty(zt_que_list_head(pfree_xmitbuf_queue))
#ifdef CFG_ENABLE_AP_MODE
            || (tx_info->free_xmit_extbuf_cnt == 1 && !tx_info->is_bcn_pkt)
#endif
       )
    {
        zt_os_api_lock_unlock(&pfree_xmitbuf_queue->lock);
        return NULL;
    }

    phead = zt_que_list_head(pfree_xmitbuf_queue);
    plist = zt_list_next(phead);
    pxmitbuf = ZT_CONTAINER_OF(plist, struct xmit_buf, list);
    zt_list_delete(&(pxmitbuf->list));

    tx_info->free_xmit_extbuf_cnt--;
    pxmitbuf->priv_data = NULL;
    pxmitbuf->pkt_len = 0;
    pxmitbuf->agg_num = 0;
    pxmitbuf->send_flag = 0;

    zt_os_api_lock_unlock(&pfree_xmitbuf_queue->lock);
    return pxmitbuf;
}

zt_bool zt_xmit_extbuf_delete(tx_info_st *tx_info, struct xmit_buf *pxmitbuf)
{
    zt_que_t *pfree_xmitbuf_queue = &tx_info->xmit_extbuf_queue;

    if ((pxmitbuf == NULL) || (tx_info == NULL))
    {
        LOG_I("[%s]: tx_info or xmit_buf is NULL", __func__);
        return zt_false;
    }

    zt_os_api_lock_lock(&pfree_xmitbuf_queue->lock);
    zt_list_delete(&pxmitbuf->list);
    zt_list_insert_tail(&(pxmitbuf->list),
                        zt_que_list_head(pfree_xmitbuf_queue));
    tx_info->free_xmit_extbuf_cnt++;
    zt_os_api_lock_unlock(&pfree_xmitbuf_queue->lock);

#ifdef CFG_ENABLE_AP_MODE
    tx_info->is_bcn_pkt = zt_false;
#endif

    return zt_true;
}

struct xmit_frame *zt_xmit_frame_new(tx_info_st *tx_info)
{

    zt_list_t *plist, *phead;
    struct xmit_frame *pxframe = NULL;
    zt_que_t *pfree_xmit_queue = &tx_info->xmit_frame_queue;

    zt_os_api_lock_lock(&pfree_xmit_queue->lock);

    if (zt_list_is_empty(zt_que_list_head(pfree_xmit_queue)) == zt_true)
    {
        pxframe = NULL;
    }
    else
    {
        phead = zt_que_list_head(pfree_xmit_queue);

        plist = zt_list_next(phead);

        pxframe = ZT_CONTAINER_OF(plist, struct xmit_frame, list);

        zt_list_delete(&(pxframe->list));
        tx_info->free_xmitframe_cnt--;
    }

    if (pxframe != NULL)
    {
        pxframe->buf_addr = NULL;
        pxframe->pxmitbuf = NULL;

        pxframe->frame_tag = DATA_FRAMETAG;

        pxframe->pkt = NULL;
        pxframe->pkt_offset = (PACKET_OFFSET_SZ / 8);

#ifdef CONFIG_SOFT_TX_AGGREGATION
        pxframe->agg_num = 1;
#endif
    }

    zt_os_api_lock_unlock(&pfree_xmit_queue->lock);

    return pxframe;
}

zt_bool zt_xmit_frame_delete(tx_info_st *tx_info, struct xmit_frame *pxmitframe)
{
    zt_que_t *free_queue = NULL;
    zt_que_t *queue = NULL;

    if (pxmitframe == NULL)
    {
        LOG_E("[%s]:pxmitframe==NULL!!!", __func__);
        return zt_false;
    }

    queue = &tx_info->xmit_frame_queue;
    zt_os_api_lock_lock(&tx_info->pending_lock);
    zt_list_delete(&pxmitframe->list);
    tx_info->pending_frame_cnt--;
    zt_os_api_lock_unlock(&tx_info->pending_lock);
    free_queue = &tx_info->xmit_frame_queue;
    zt_os_api_lock_lock(&free_queue->lock);
    zt_list_insert_tail(&pxmitframe->list, zt_que_list_head(free_queue));
    tx_info->free_xmitframe_cnt++;
    zt_os_api_lock_unlock(&free_queue->lock);

    return zt_true;
}

zt_bool zt_xmit_frame_enqueue(tx_info_st *tx_info,
                              struct xmit_frame *pxmitframe)
{
    zt_que_t *queue = NULL;

    if (pxmitframe == NULL)
    {
        LOG_E("[%s]:pxmitframe==NULL!!!", __func__);
        return zt_false;
    }

    queue = &tx_info->xmit_frame_queue;

    zt_os_api_lock_lock(&queue->lock);
    zt_list_insert_tail(&pxmitframe->list, zt_que_list_head(queue));
    tx_info->free_xmitframe_cnt++;
    zt_os_api_lock_unlock(&queue->lock);

    return zt_true;
}

void zt_tx_data_enqueue_tail(tx_info_st *tx_info, struct xmit_frame *pxmitframe)
{
    zt_list_t *phead;

    zt_os_api_lock_lock(&tx_info->pending_lock);
    phead = zt_que_list_head(&tx_info->pending_frame_queue);
    zt_list_insert_tail(&pxmitframe->list, phead);
    tx_info->pending_frame_cnt++;
    zt_os_api_lock_unlock(&tx_info->pending_lock);
}

void zt_tx_data_enqueue_head(tx_info_st *tx_info, struct xmit_frame *pxmitframe)
{
    zt_list_t *phead;

    zt_os_api_lock_lock(&tx_info->pending_lock);
    phead = zt_que_list_head(&tx_info->pending_frame_queue);
    zt_list_insert_head(&pxmitframe->list, phead);
    tx_info->pending_frame_cnt++;
    zt_os_api_lock_unlock(&tx_info->pending_lock);
}


struct xmit_frame *zt_tx_data_getqueue(tx_info_st *tx_info)
{
    zt_list_t *plist, *phead;
    struct xmit_frame *pxframe;

    if (zt_que_is_empty(&tx_info->pending_frame_queue) == zt_true)
    {
        return NULL;
    }

    zt_os_api_lock_lock(&tx_info->pending_lock);
    phead = zt_que_list_head(&tx_info->pending_frame_queue);
    plist = zt_list_next(phead);
    pxframe = ZT_CONTAINER_OF(plist, struct xmit_frame, list);
    zt_os_api_lock_unlock(&tx_info->pending_lock);

    return pxframe;
}

struct xmit_frame *zt_tx_data_dequeue(tx_info_st *tx_info)
{
    zt_list_t *plist, *phead;
    struct xmit_frame *pxframe;

    if (zt_que_is_empty(&tx_info->pending_frame_queue) == zt_true)
    {
        return NULL;
    }

    zt_os_api_lock_lock(&tx_info->pending_lock);
    phead = zt_que_list_head(&tx_info->pending_frame_queue);
    plist = zt_list_next(phead);
    pxframe = ZT_CONTAINER_OF(plist, struct xmit_frame, list);
    tx_info->pending_frame_cnt--;
    zt_list_delete(&pxframe->list);
    zt_os_api_lock_unlock(&tx_info->pending_lock);

    return pxframe;
}


void zt_tx_agg_enqueue_head(tx_info_st *tx_info, struct xmit_frame *pxmitframe)
{
    zt_list_t  *phead;

    zt_os_api_lock_lock(&tx_info->agg_frame_queue.lock);
    phead = zt_que_list_head(&tx_info->agg_frame_queue);
    zt_list_insert_head(&pxmitframe->list, phead);
    tx_info->agg_frame_queue.cnt++;
    zt_os_api_lock_unlock(&tx_info->agg_frame_queue.lock);
}

struct xmit_frame *zt_tx_agg_dequeue(tx_info_st *tx_info)
{
    zt_list_t *plist, *phead;
    struct xmit_frame *pxframe;

    if (zt_que_is_empty(&tx_info->agg_frame_queue) == zt_true)
    {
        return NULL;
    }

    zt_os_api_lock_lock(&tx_info->agg_frame_queue.lock);
    phead = zt_que_list_head(&tx_info->agg_frame_queue);
    plist = zt_list_next(phead);
    pxframe = ZT_CONTAINER_OF(plist, struct xmit_frame, list);
    tx_info->agg_frame_queue.cnt--;
    zt_list_delete(&pxframe->list);
    zt_os_api_lock_unlock(&tx_info->agg_frame_queue.lock);

    return pxframe;
}

void zt_tx_frame_queue_clear(nic_info_st *nic_info)
{
    tx_info_st *tx_info = nic_info->tx_info;
    struct xmit_frame *pxframe;

    LOG_D("[%s]: clean tx frame queue", __func__);
    while (1)
    {
        pxframe = zt_tx_data_dequeue(tx_info);
        if (NULL == pxframe)
        {
            break;
        }
        zt_free_skb(pxframe->pkt);
        pxframe->pkt = NULL;
    }
}

zt_s32 zt_nic_null_xmit(nic_info_st *pnic_info,
                        wdn_net_info_st *pwdn_info, zt_bool ps_en, zt_u32 to_ms)
{
    struct xmit_buf *pxmit_buf;
    tx_info_st *ptx_info = (void *)pnic_info->tx_info;
    zt_80211_data_t *pframe;
    zt_u16 frame_len;
    zt_s32 ret;

    LOG_D("[%s]: ""ps=%d", __func__, ps_en);

    /* alloc xmit_buf */
    pxmit_buf = zt_xmit_extbuf_new(ptx_info);
    if (pxmit_buf == NULL)
    {
        LOG_W("[%s]: ""xmit_buf alloc fail", __func__);
        return -1;
    }
    frame_len = sizeof(zt_80211_data_t);
    zt_memset(pxmit_buf->pbuf, 0, TXDESC_OFFSET + frame_len);

    /* set frame control field */
    pframe = (void *)&pxmit_buf->pbuf[TXDESC_OFFSET];
    pframe->frame_control = 0x0;
    zt_80211_hdr_type_set(pframe, ZT_80211_FRM_NULLFUNC);
#ifdef CFG_ENABLE_AP_MODE
    if (zt_local_cfg_get_work_mode(pnic_info) == ZT_MASTER_MODE)
    {
        zt_80211_hdr_ds_set(pframe, ZT_80211_FCTL_FROMDS);
    }
    else
#else
    zt_80211_hdr_ds_set(pframe, ZT_80211_FCTL_TODS);
#endif
        zt_80211_hdr_pm_set(pframe, ps_en);

    /* set mac address */
    zt_memcpy(pframe->addr1, pwdn_info->mac, ZT_ARRAY_SIZE(pframe->addr1));
    zt_memcpy(pframe->addr2, nic_to_local_addr(pnic_info),
              ZT_ARRAY_SIZE(pframe->addr2));
    zt_memcpy(pframe->addr3, pwdn_info->bssid, ZT_ARRAY_SIZE(pframe->addr3));

    pxmit_buf->pkt_len = frame_len;
    ret = zt_nic_mgmt_frame_xmit(pnic_info, pwdn_info,
                                          pxmit_buf, pxmit_buf->pkt_len);
    if (ret < 0)
    {
        LOG_W("[%s]: ""ret=%d", __func__, ret);
        return ret;
    }

    return 0;
}

zt_s32 zt_nic_beacon_xmit(nic_info_st *nic_info, struct xmit_buf *pxmitbuf,
                          zt_u16 len)
{
    zt_u8 *pbuf;
    zt_u8 *pwlanhdr;

    tx_info_st *tx_info = nic_info->tx_info;

    if (pxmitbuf == NULL)
    {
        LOG_E("[%s]: xmitbuf is NULL", __func__);
        return -1;
    }

    if (ZT_CANNOT_RUN(nic_info))
    {
        zt_xmit_extbuf_delete(tx_info, pxmitbuf);
        return -1;
    }

    /* add txd */
    pbuf = pxmitbuf->pbuf;
    pwlanhdr = pbuf + TXDESC_OFFSET_NEW;
    zt_memset(pbuf, 0, TXDESC_OFFSET_NEW);

    zt_set_bits_to_le_u32(pbuf, 0, 2, TYPE_DATA);
    zt_set_bits_to_le_u32(pbuf, 18, 1, 1);
    zt_set_bits_to_le_u32(pbuf + 8, 0, 16, len);
    zt_set_bits_to_le_u32(pbuf + 8, 16, 1, 1);
    zt_set_bits_to_le_u32(pbuf + 8, 18, 7, DESC_RATE1M);
    zt_set_bits_to_le_u32(pbuf + 12, 6, 5, 0);
    zt_set_bits_to_le_u32(pbuf + 12, 11, 3, 1);
    zt_set_bits_to_le_u32(pbuf + 12, 18, 1, nic_info->virNic);
    zt_set_bits_to_le_u32(pbuf + 12, 15, 1, 1);
    zt_set_bits_to_le_u32(pbuf + 12, 16, 2, 0);
    zt_set_bits_to_le_u32(pbuf + 16, 5, 1, 0);
    zt_set_bits_to_le_u32(pbuf + 16, 6, 3, 0);

    zt_set_bits_to_le_u32(pbuf + 16, 12, 1, CHANNEL_WIDTH_20);
    zt_set_bits_to_le_u32(pbuf + 16, 13, 2, HT_DATA_SC_DONOT_CARE);

    zt_set_bits_to_le_u32(pbuf + 12, 14, 1, 1);

    zt_txdesc_chksum(pbuf);

    len += TXDESC_OFFSET_NEW;

    zt_io_write_data(nic_info,
                     1,
                     (zt_s8 *)pbuf, len,
                     zt_quary_addr(QSLT_BEACON),
                     (zt_s32(*)(void *, void *))zt_xmit_extbuf_delete, tx_info, pxmitbuf);

    return 0;
}

static zt_s32 nic_mgmt_frame_xmit(nic_info_st *nic_info, wdn_net_info_st *wdn,
                                  struct xmit_buf *pxmitbuf, zt_u16 len,
                                  zt_bool wait_ack)
{
    zt_u8 *pbuf;
    zt_u8 *pwlanhdr;
    zt_s32 ret;

    tx_info_st *tx_info = nic_info->tx_info;
    hw_info_st *hw_info = nic_info->hw_info;

    if (pxmitbuf == NULL)
    {
        LOG_E("[%s]: xmitbuf is NULL", __func__);
        return -1;
    }

    if (ZT_CANNOT_RUN(nic_info))
    {
        zt_xmit_extbuf_delete(tx_info, pxmitbuf);
        return -1;
    }

    /* add txd */
    pbuf = pxmitbuf->pbuf;
    pwlanhdr = pbuf + TXDESC_OFFSET_NEW;
    zt_memset(pbuf, 0, TXDESC_OFFSET_NEW);

    zt_set_bits_to_le_u32(pbuf, 0, 2, TYPE_DATA);
    zt_set_bits_to_le_u32(pbuf, 18, 1, 1);
    zt_set_bits_to_le_u32(pbuf + 8, 0, 16, len);
    zt_set_bits_to_le_u32(pbuf + 8, 16, 1, 1);
    if (hw_info->use_fixRate)
    {
        zt_set_bits_to_le_u32(pbuf + 8, 18, 7, hw_info->fix_tx_rate);
    }
    else
    {
        zt_set_bits_to_le_u32(pbuf + 8, 18, 7,
                              zt_p2p_is_valid(nic_info) ? DESC_RATE6M : DESC_RATE1M);
    }
    zt_set_bits_to_le_u32(pbuf + 12, 6, 5, 0); //be queue
    if (!nic_info->virNic || zt_80211_hdr_type_get((zt_80211_mgmt_t *)pwlanhdr) !=
            ZT_80211_FRM_PROBE_RESP)
    {
        zt_set_bits_to_le_u32(pbuf + 12, 11, 3, 2);
    }
    zt_set_bits_to_le_u32(pbuf + 12, 18, 1, nic_info->virNic);
    zt_set_bits_to_le_u32(pbuf + 12, 15, 1, 1);
    zt_set_bits_to_le_u32(pbuf + 12, 16, 2, 0);
    zt_set_bits_to_le_u32(pbuf + 16, 5, 1, 0);
    zt_set_bits_to_le_u32(pbuf + 16, 6, 3, zt_p2p_is_valid(nic_info) ? 1 : 0);
#ifdef CFG_ENABLE_AP_MODE
    if (zt_local_cfg_get_work_mode(nic_info) == ZT_MASTER_MODE)
    {
        if (wdn)
        {
            zt_set_bits_to_le_u32(pbuf + 16, 0, 5, wdn->wdn_id);
        }
    }
#endif

    zt_set_bits_to_le_u32(pbuf + 16, 12, 1, CHANNEL_WIDTH_20);
    zt_set_bits_to_le_u32(pbuf + 16, 13, 2, HT_DATA_SC_DONOT_CARE);

    zt_set_bits_to_le_u32(pbuf + 12, 14, 1,
                          IS_MCAST(GetAddr1Ptr(pwlanhdr)) ? 1 : 0);

    if (wait_ack == zt_true)
    {
        zt_set_bits_to_le_u32(pbuf + 12, 21, 1, 1);
    }

    zt_txdesc_chksum(pbuf);

    len += TXDESC_OFFSET_NEW;

    ret = zt_io_write_data(nic_info,
                           1,
                           (zt_s8 *)pbuf, len,
                           zt_quary_addr(pxmitbuf->qsel = QSLT_MGNT),
                           (zt_s32(*)(void *, void *))zt_xmit_extbuf_delete,
                           tx_info,
                           pxmitbuf);
    tx_info->tx_mgnt_pkts += 1;

    return ret;
}

zt_s32 zt_nic_mgmt_frame_xmit(nic_info_st *nic_info, wdn_net_info_st *wdn,
                              struct xmit_buf *pxmitbuf, zt_u16 len)
{
    return nic_mgmt_frame_xmit(nic_info, wdn, pxmitbuf, len, zt_false);
}

zt_s32 zt_nic_mgmt_frame_xmit_with_ack(nic_info_st *nic_info,
                                       wdn_net_info_st *wdn,
                                       struct xmit_buf *pxmitbuf, zt_u16 len,
                                       zt_u32 to_ms)
{
    union
    {
        zt_u32 w;
        struct
        {
            zt_u32 seq : 16;
            zt_u32 cnt : 16;
        };
    } rpt, last_rpt;
    zt_timer_t timer;
    zt_u8 i;
    zt_s32 ret;

    zt_timer_set(&timer, to_ms);
    do
    {
        last_rpt.w = zt_mcu_get_ack_rpt(nic_info);
        LOG_D("[%s]: seq=%d cnt=%d>>>>>", __func__, last_rpt.seq, last_rpt.cnt);
        if ((ret = nic_mgmt_frame_xmit(nic_info, wdn, pxmitbuf, len, zt_true)))
        {
            goto exit;
        }
        for (i = 0; i < 10; i++)
        {
            rpt.w = zt_mcu_get_ack_rpt(nic_info);
            if (rpt.cnt > last_rpt.cnt)
            {
                LOG_D("[%s]: seq=%d cnt=%d<<<<<", __func__, rpt.seq, rpt.cnt);
                goto exit;
            }
            zt_msleep(1);
        }
        LOG_D("[%s]: seq=%d cnt=%d<<<<<", __func__, rpt.seq, rpt.cnt);
    } while (!zt_timer_expired(&timer));
    ret = -1;

exit:
    return ret;
}

zt_u32 zt_get_wlan_pkt_size(struct xmit_frame *pxmit_frame)
{
    zt_u32 len = 0;

    len = pxmit_frame->hdrlen + pxmit_frame->iv_len;
    len += ZT_80211_SNAP_HDR_SIZE + sizeof(zt_u16);
    len += pxmit_frame->pktlen;
    if (pxmit_frame->encrypt_algo == _TKIP_)
    {
        len += 8;
        len += ((zt_false) ? pxmit_frame->icv_len : 0);
    }

    return len;
}



zt_s32 zt_tx_info_init(nic_info_st *nic_info)
{
    zt_bool res;
    zt_s32 i;
    tx_info_st *tx_info;
    struct xmit_frame *pxmit_frame;
    struct xmit_buf *pxmit_buf;


    LOG_I("tx_info init");
    tx_info = zt_kzalloc(sizeof(tx_info_st));
    if (tx_info == NULL)
    {
        LOG_E("[%s] malloc tx_info failed", __func__);
        nic_info->tx_info = NULL;
        return -1;
    }

    zt_os_api_lock_init(&tx_info->lock, ZT_LOCK_TYPE_BH);
    /* xmit_frame buffer init */
    zt_que_init(&tx_info->xmit_frame_queue, ZT_LOCK_TYPE_IRQ);
#if TX_AGG_QUEUE_ENABLE
    zt_que_init(&tx_info->agg_frame_queue, ZT_LOCK_TYPE_IRQ);
#endif
    tx_info->pallocated_frame_buf =
        zt_vmalloc(NR_XMITFRAME * sizeof(struct xmit_frame) + 4);

    if (tx_info->pallocated_frame_buf == NULL)
    {
        tx_info->pxmit_frame_buf = NULL;
        LOG_E("[zt_tx_info_init] alloc xmit_frame buf fail!");
        return -2;
    }
    zt_memset(tx_info->pallocated_frame_buf, 0,
              NR_XMITFRAME * sizeof(struct xmit_frame) + 4);

    tx_info->pxmit_frame_buf =
        (zt_u8 *) ZT_N_BYTE_ALIGMENT((SIZE_PTR)(tx_info->pallocated_frame_buf), 4);

    pxmit_frame = (struct xmit_frame *)tx_info->pxmit_frame_buf;
    for (i = 0; i < NR_XMITFRAME; i++)
    {
        zt_list_init(&(pxmit_frame->list));

        pxmit_frame->frame_tag = NULL_FRAMETAG;
        pxmit_frame->nic_info = nic_info;

        pxmit_frame->pxmitbuf = NULL;
        pxmit_frame->buf_addr = NULL;
        pxmit_frame->pkt = NULL;
        pxmit_frame->frame_id = (zt_u16)(i + 1);
        zt_list_insert_tail(&(pxmit_frame->list), &(tx_info->xmit_frame_queue.head));
        pxmit_frame++;
    }
    tx_info->free_xmitframe_cnt = NR_XMITFRAME;

    /* xmit_buf buffer init */
    zt_que_init(&tx_info->xmit_buf_queue, ZT_LOCK_TYPE_IRQ);
    tx_info->pallocated_xmitbuf =
        zt_vmalloc(XMIT_DATA_BUFFER_CNT * sizeof(struct xmit_buf) + 4);

    if (tx_info->pallocated_xmitbuf == NULL)
    {
        LOG_E("[%s] alloc xmit_buf buf fail!", __func__);
        return -3;
    }
    zt_memset(tx_info->pallocated_xmitbuf, 0,
              XMIT_DATA_BUFFER_CNT * sizeof(struct xmit_buf) + 4);

    tx_info->pxmitbuf =
        (zt_u8 *) ZT_N_BYTE_ALIGMENT((SIZE_PTR)(tx_info->pallocated_xmitbuf), 4);

    pxmit_buf = (struct xmit_buf *)tx_info->pxmitbuf;

    for (i = 0; i < XMIT_DATA_BUFFER_CNT; i++)
    {
        zt_list_init(&pxmit_buf->list);

        pxmit_buf->priv_data = NULL;
        pxmit_buf->nic_info = nic_info;
        pxmit_buf->buffer_id      = (zt_u8)i;
        if ((res = xmit_buf_resource_new(pxmit_buf, nic_info,
                                         MAX_XMITBUF_SZ)) == zt_false)
        {
            zt_msleep(10);
            res = xmit_buf_resource_new(pxmit_buf, nic_info, MAX_XMITBUF_SZ);
            if (res == zt_false)
            {
                LOG_E("[%s] no memory for xmit_buf frame buf!", __func__);
                return -4;
            }
        }

        pxmit_buf->flags = XMIT_VO_QUEUE;

        zt_list_insert_tail(&pxmit_buf->list, &(tx_info->xmit_buf_queue.head));

        pxmit_buf++;
    }

    tx_info->free_xmitbuf_cnt = XMIT_DATA_BUFFER_CNT;

    /* mgmt frame xmit_buf buffer init */
    zt_que_init(&tx_info->xmit_extbuf_queue, ZT_LOCK_TYPE_IRQ);
    tx_info->pallocated_xmit_extbuf =
        zt_vmalloc(XMIT_MGMT_BUFFER_CNT * sizeof(struct xmit_buf) + 4);

    if (tx_info->pallocated_xmit_extbuf == NULL)
    {
        LOG_E("[%s] alloc xmit_buf buf fail!", __func__);
        return -3;
    }
    zt_memset(tx_info->pallocated_xmit_extbuf, 0,
              XMIT_MGMT_BUFFER_CNT * sizeof(struct xmit_buf) + 4);

    tx_info->pxmit_extbuf =
        (zt_u8 *) ZT_N_BYTE_ALIGMENT((SIZE_PTR)(tx_info->pallocated_xmit_extbuf), 4);

    pxmit_buf = (struct xmit_buf *)tx_info->pxmit_extbuf;

    for (i = 0; i < XMIT_MGMT_BUFFER_CNT; i++)
    {
        zt_list_init(&pxmit_buf->list);

        pxmit_buf->priv_data = NULL;
        pxmit_buf->nic_info = nic_info;
        pxmit_buf->buffer_id = XMIT_DATA_BUFFER_CNT + (zt_u8)i;

        if ((res = xmit_buf_resource_new(pxmit_buf, nic_info,
                                         MAX_XMIT_EXTBUF_SZ)) == zt_false)
        {
            LOG_E("[%s] no memory for xmit_extbuf frame buf!", __func__);
            return -4;
        }

        zt_list_insert_tail(&pxmit_buf->list, &(tx_info->xmit_extbuf_queue.head));

        pxmit_buf++;
    }

    tx_info->free_xmit_extbuf_cnt = XMIT_MGMT_BUFFER_CNT;

#ifdef CONFIG_LPS
    for (i = 0; i < CMDBUF_MAX; i++)
    {
        pxmit_buf = &tx_info->pcmd_xmitbuf[i];
        if (pxmit_buf)
        {
            zt_list_init(&pxmit_buf->list);

            pxmit_buf->priv_data = NULL;
            pxmit_buf->nic_info = nic_info;

            if ((res = xmit_buf_resource_new(pxmit_buf, nic_info,
                                             MAX_CMDBUF_SZ)) == zt_false)
            {
                return -5;
            }
            pxmit_buf->alloc_sz = MAX_CMDBUF_SZ + XMITBUF_ALIGN_SZ;
        }
    }
#endif
    /* pending frame queue init */
    zt_que_init(&tx_info->pending_frame_queue, ZT_LOCK_TYPE_IRQ);
    zt_os_api_lock_init(&tx_info->pending_lock, ZT_LOCK_TYPE_IRQ);
    tx_info->pending_frame_cnt = 0;
#ifdef CFG_ENABLE_AP_MODE
    tx_info->pause = zt_false;
#endif

    tx_info->nic_info = nic_info;
    nic_info->tx_info = tx_info;

    return 0;
}

zt_s32 zt_tx_info_term(nic_info_st *nic_info)
{
    tx_info_st *tx_info = nic_info->tx_info;
    struct xmit_buf *pxmitbuf;
    struct xmit_frame *pxmitframe;

    LOG_D("[%s] start", __func__);

    if (tx_info)
    {
        zt_s32 i;
        pxmitbuf = (struct xmit_buf *)tx_info->pxmitbuf;
        pxmitframe = (struct xmit_frame *)tx_info->pxmit_frame_buf;

        pxmitbuf = (struct xmit_buf *)tx_info->pxmitbuf;
        for (i = 0; i < XMIT_DATA_BUFFER_CNT; i++)
        {
            if (pxmitbuf->pallocated_buf)
            {
                zt_kfree(pxmitbuf->pallocated_buf);
            }
            pxmitbuf++;
        }

        pxmitbuf = (struct xmit_buf *)tx_info->pxmit_extbuf;
        for (i = 0; i < XMIT_MGMT_BUFFER_CNT; i++)
        {
            if (pxmitbuf->pallocated_buf)
            {
                zt_kfree(pxmitbuf->pallocated_buf);
            }
            pxmitbuf++;
        }

#ifdef CONFIG_LPS
        pxmitbuf = (struct xmit_buf *)tx_info->pcmd_xmitbuf;
        for (i = 0; i < CMDBUF_MAX; i++)
        {
            if (pxmitbuf->pallocated_buf)
            {
                zt_kfree(pxmitbuf->pallocated_buf);
            }
            pxmitbuf++;
        }
#endif

        if (tx_info->pallocated_frame_buf)
        {
            zt_vfree(tx_info->pallocated_frame_buf);
        }

        if (tx_info->pallocated_xmitbuf)
        {
            zt_vfree(tx_info->pallocated_xmitbuf);
        }

        if (tx_info->pallocated_xmit_extbuf)
        {
            zt_vfree(tx_info->pallocated_xmit_extbuf);
        }

        zt_os_api_lock_term(&tx_info->pending_lock);
        zt_os_api_lock_term(&tx_info->lock);
        zt_kfree(tx_info);
        nic_info->tx_info = NULL;
    }

    LOG_D("[%s] end", __func__);


    return 0;
}


zt_u8 zt_mrate_to_hwrate(zt_u8 rate)
{
    zt_u8 ret = DESC_RATE1M;

    switch (rate)
    {
        case MGN_1M:
            ret = DESC_RATE1M;
            break;
        case MGN_2M:
            ret = DESC_RATE2M;
            break;
        case MGN_5_5M:
            ret = DESC_RATE5_5M;
            break;
        case MGN_11M:
            ret = DESC_RATE11M;
            break;
        case MGN_6M:
            ret = DESC_RATE6M;
            break;
        case MGN_9M:
            ret = DESC_RATE9M;
            break;
        case MGN_12M:
            ret = DESC_RATE12M;
            break;
        case MGN_18M:
            ret = DESC_RATE18M;
            break;
        case MGN_24M:
            ret = DESC_RATE24M;
            break;
        case MGN_36M:
            ret = DESC_RATE36M;
            break;
        case MGN_48M:
            ret = DESC_RATE48M;
            break;
        case MGN_54M:
            ret = DESC_RATE54M;
            break;

        case MGN_MCS0:
            ret = DESC_RATEMCS0;
            break;
        case MGN_MCS1:
            ret = DESC_RATEMCS1;
            break;
        case MGN_MCS2:
            ret = DESC_RATEMCS2;
            break;
        case MGN_MCS3:
            ret = DESC_RATEMCS3;
            break;
        case MGN_MCS4:
            ret = DESC_RATEMCS4;
            break;
        case MGN_MCS5:
            ret = DESC_RATEMCS5;
            break;
        case MGN_MCS6:
            ret = DESC_RATEMCS6;
            break;
        case MGN_MCS7:
            ret = DESC_RATEMCS7;
            break;
        default:
            break;
    }

    return ret;
}

zt_u8 zt_hwrate_to_mrate(zt_u8 rate)
{
    zt_u8 ret_rate = MGN_1M;

    switch (rate)
    {

        case DESC_RATE1M:
            ret_rate = MGN_1M;
            break;
        case DESC_RATE2M:
            ret_rate = MGN_2M;
            break;
        case DESC_RATE5_5M:
            ret_rate = MGN_5_5M;
            break;
        case DESC_RATE11M:
            ret_rate = MGN_11M;
            break;
        case DESC_RATE6M:
            ret_rate = MGN_6M;
            break;
        case DESC_RATE9M:
            ret_rate = MGN_9M;
            break;
        case DESC_RATE12M:
            ret_rate = MGN_12M;
            break;
        case DESC_RATE18M:
            ret_rate = MGN_18M;
            break;
        case DESC_RATE24M:
            ret_rate = MGN_24M;
            break;
        case DESC_RATE36M:
            ret_rate = MGN_36M;
            break;
        case DESC_RATE48M:
            ret_rate = MGN_48M;
            break;
        case DESC_RATE54M:
            ret_rate = MGN_54M;
            break;
        case DESC_RATEMCS0:
            ret_rate = MGN_MCS0;
            break;
        case DESC_RATEMCS1:
            ret_rate = MGN_MCS1;
            break;
        case DESC_RATEMCS2:
            ret_rate = MGN_MCS2;
            break;
        case DESC_RATEMCS3:
            ret_rate = MGN_MCS3;
            break;
        case DESC_RATEMCS4:
            ret_rate = MGN_MCS4;
            break;
        case DESC_RATEMCS5:
            ret_rate = MGN_MCS5;
            break;
        case DESC_RATEMCS6:
            ret_rate = MGN_MCS6;
            break;
        case DESC_RATEMCS7:
            ret_rate = MGN_MCS7;
            break;

        default:
            LOG_E("[%s]: Non supported Rate [%x]!!!", __func__, rate);
            break;
    }

    return ret_rate;
}


zt_bool zt_tx_msdu_to_mpdu(nic_info_st *nic_info, struct xmit_frame *pxmitframe,
                           zt_u8 *msdu_buf, zt_s32 msdu_len)
{
    SIZE_PTR addr;
    zt_u8 hw_hdr_offset;
    zt_u8 *pbuf_start;
    zt_u8 *pframe, *mem_start;
    zt_s32 frg_inx, frg_len, mpdu_len, llc_sz, mem_sz;
    hw_info_st *hw_info = nic_info->hw_info;
    zt_s32 msduOffset = 0;
    zt_s32 msduRemainLen = 0;

    ZT_ASSERT(pxmitframe != NULL);

    if (pxmitframe->buf_addr == NULL)
    {
        LOG_E("[%s]: xmit_buf->buf_addr==NULL", __func__);
        return zt_false;
    }

    pbuf_start = pxmitframe->buf_addr;

#ifdef CONFIG_SOFT_TX_AGGREGATION
    hw_hdr_offset = TXDESC_SIZE + (pxmitframe->pkt_offset * PACKET_OFFSET_SZ);
#else
    hw_hdr_offset = TXDESC_OFFSET;
#endif

    mem_start = pbuf_start + hw_hdr_offset; /* point to wlan head(skip TXD filed) */

    /* fill wlan head filed */
    if (tx_mac_hdr_build(nic_info, pxmitframe, msdu_buf, mem_start) == zt_false)
    {
        LOG_I("[%s]: do_wlanhdr_build fail; drop pkt", __func__);
        return zt_false;
    }

    pxmitframe->pwlanhdr = (zt_80211_data_t *)mem_start;
    msduOffset += ZT_ETH_HLEN;

    frg_inx = 0;
    frg_len = hw_info->frag_thresh;

#ifdef CFG_ENABLE_AP_MODE
    /* todo: pop data in psm_data_que, and mark the more-data flag in mac
       frame head, if all data has been removed from psm_data_que, clear the
       bit in the TIM this will tell the station the last data will send,
       can enter pms mode again. */
    if (zt_local_cfg_get_work_mode(nic_info) == ZT_MASTER_MODE &&
            !pxmitframe->bmcast && !pxmitframe->pwdn->psm)
    {
        wdn_net_info_st *pwdn = pxmitframe->pwdn;
        zt_que_list_t *node;

        zt_os_api_lock_lock(&pwdn->psm_lock);
        node = zt_deque_head(&pwdn->psm_data_que);
        zt_os_api_lock_unlock(&pwdn->psm_lock);
        if (node != NULL)
        {
            struct xmit_frame *pxmitframe =
                ZT_CONTAINER_OF(node, struct xmit_frame, list);
            tx_info_st *ptx_info = nic_info->tx_info;
            zt_tx_data_enqueue_tail(ptx_info, pxmitframe);
            zt_80211_hdr_moredata_set(mem_start, 1);
        }
        else
        {
            zt_wlan_mgmt_info_t *pwlan_info = nic_info->wlan_mgmt_info;
            zt_wlan_network_t *pcur_network = &pwlan_info->cur_network;
            if (zt_bmp_is_set(pcur_network->tim_bitmap, pwdn->aid))
            {
                int ret;
                zt_bmp_clear(&pcur_network->tim_bitmap, pwdn->aid);
                ret = zt_ap_update_beacon(nic_info, ZT_80211_MGMT_EID_TIM,
                                          NULL, zt_true);
                LOG_D("[%s]: clear pwdn->aid=%d", __func__, pwdn->aid);
                if (ret)
                {
                    zt_bmp_set(&pcur_network->tim_bitmap, pwdn->aid);
                    LOG_D("[%s]: rollback bmp clear %d", __func__, pwdn->aid);
                }
            }
        }
    }
#endif

    while (1)
    {
        llc_sz = 0;

        mpdu_len = frg_len;

        pframe = mem_start;

        SetMFrag(mem_start);

        pframe += pxmitframe->hdrlen; /* point to (iv+llc+msdu) filed */
        mpdu_len -= pxmitframe->hdrlen;

        /* fill iv filed */
        if (pxmitframe->iv_len)
        {
            zt_memcpy(pframe, pxmitframe->iv, pxmitframe->iv_len);
            pframe += pxmitframe->iv_len;
            mpdu_len -= pxmitframe->iv_len;
        }

        /* fill llc head filed if first fragment */
        if (frg_inx == 0)
        {
            llc_sz = tx_set_snap(pxmitframe->ether_type, pframe);
            pframe += llc_sz;
            mpdu_len -= llc_sz;
        }

        /* fill fragment msdu filed */
        if (pxmitframe->icv_len && pxmitframe->bswenc)
        {
            mpdu_len -= pxmitframe->icv_len;
        }

        if (pxmitframe->bmcast)
        {
            /* don't do fragment to broadcat/multicast packets */
            zt_memcpy(pframe, msdu_buf + msduOffset, msdu_len - msduOffset);
            mem_sz = msdu_len - msduOffset;
        }
        else
        {
            msduRemainLen = msdu_len - msduOffset;
            if (msduRemainLen > mpdu_len)
            {
                zt_memcpy(pframe, msdu_buf + pxmitframe->pkt_hdrlen, mpdu_len);
                msduOffset += mpdu_len;
                mem_sz = mpdu_len;
            }
            else
            {
                zt_memcpy(pframe, msdu_buf + pxmitframe->pkt_hdrlen, msduRemainLen);
                msduOffset += msduRemainLen;
                mem_sz = msduRemainLen;
            }
        }
        pframe += mem_sz;

        /* skip icv field */
        if (pxmitframe->icv_len && pxmitframe->bswenc)
        {
            pframe += pxmitframe->icv_len;
        }

        frg_inx++;

        if (pxmitframe->bmcast || (msduOffset == msdu_len))
        {
            pxmitframe->nr_frags = (zt_u8)frg_inx;
            pxmitframe->last_txcmdsz =
                pxmitframe->hdrlen + pxmitframe->iv_len + llc_sz +
                mem_sz + (pxmitframe->bswenc ? pxmitframe->icv_len : 0);
            ClearMFrag(mem_start);
            break;
        }
        else
        {
            LOG_D("[%s]: There're still something in packet!", __func__);
        }

        addr = (SIZE_PTR)(pframe);
        mem_start = (zt_u8 *)ZT_RND4(addr) +
                    hw_hdr_offset; /* adjust next point, should jump tx descrption head */
        /* copy wlan head, use the same as first fragment head */
        zt_memcpy(mem_start, pbuf_start + hw_hdr_offset, pxmitframe->hdrlen);
    }

    if (frame_txp_addmic(nic_info, pxmitframe) == zt_false)
    {
        LOG_I("[%s]: frame_txp_addmic return false", __func__);
        return zt_false;
    }

    if (pxmitframe->bmcast == zt_false)
    {
        xmit_frame_vcs_init(nic_info, pxmitframe);
    }
    else
    {
        pxmitframe->vcs_mode = NONE_VCS;
    }

    return zt_true;
}



zt_s32 zt_tx_msdu(nic_info_st *nic_info, zt_u8 *msdu_buf, zt_s32 msdu_len,
                  void *pkt)
{
    zt_s32 res;
    tx_info_st *ptx_info = nic_info->tx_info;
    struct xmit_frame *pxmitframe = NULL;
    pxmitframe = zt_xmit_frame_new(ptx_info);
    if (pxmitframe == NULL)
    {
        LOG_D("[%s]: no more pxmitframe", __func__);
        return -1;
    }
    res = zt_xmit_frame_init(nic_info, pxmitframe, msdu_buf, msdu_len);
    if (res == zt_false)
    {
        LOG_D("[%s]: xmit frame info init fail", __func__);
        zt_xmit_frame_enqueue(ptx_info, pxmitframe);
        return -2;
    }

    pxmitframe->pkt = pkt;

#ifdef CFG_ENABLE_AP_MODE
    /* todo: checkout if should wakeup the sleep station. for transmit
       broadcast data will set BIT0 of TIM filed, other should according
       to corresponding AID whitch the data been send to target station.
       for send unicast data, whitch will store into psm_data_que, it's
       not send immediately, but will send after the station has been wakeup */
    if (zt_local_cfg_get_work_mode(nic_info) == ZT_MASTER_MODE)
    {
        zt_wlan_mgmt_info_t *pwlan_info = nic_info->wlan_mgmt_info;
        zt_wlan_network_t *pcur_network = &pwlan_info->cur_network;
        wdn_net_info_st *pwdn = pxmitframe->pwdn;
        int ret;

        if (pxmitframe->bmcast)
        {
            /* set broadcast in TIM filed */
            if (!zt_bmp_is_set(pcur_network->tim_bitmap, 0))
            {
                zt_bmp_set(&pcur_network->tim_bitmap, 0);
                ret = zt_ap_update_beacon(nic_info, ZT_80211_MGMT_EID_TIM,
                                          NULL, zt_true);
                LOG_D("[%s]: set TIM for broadcast", __func__);
                if (ret)
                {
                    zt_bmp_set(&pcur_network->tim_bitmap, pwdn->aid);
                    LOG_D("[%s]: rollback TIM for broadcast set", __func__);
                }
            }
        }
        else
        {
            zt_os_api_lock_lock(&pwdn->psm_lock);
            if (pwdn->psm || !zt_que_is_empty(&pwdn->psm_data_que))
            {
                zt_enque_tail(&pxmitframe->list, &pwdn->psm_data_que);
                zt_os_api_lock_unlock(&pwdn->psm_lock);

                /* update beacon TIM filed */
                if (!zt_bmp_is_set(pcur_network->tim_bitmap, pwdn->aid))
                {
                    zt_bmp_set(&pcur_network->tim_bitmap, pwdn->aid);
                    ret = zt_ap_update_beacon(nic_info, ZT_80211_MGMT_EID_TIM,
                                              NULL, zt_true);
                    LOG_D("[%s]: set TIM for bmp %d", __func__, pwdn->aid);
                    if (ret)
                    {
                        zt_bmp_set(&pcur_network->tim_bitmap, pwdn->aid);
                        LOG_D("[%s]: rollback TIM for bmp set %d", __func__, pwdn->aid);
                    }
                }
                return 1;

            }
            zt_os_api_lock_unlock(&pwdn->psm_lock);
        }
    }
#endif

    zt_tx_data_enqueue_tail(ptx_info, pxmitframe);

    return 0;
}


zt_bool zt_tx_data_check(nic_info_st *nic_info)
{
    tx_info_st *ptx_info = nic_info->tx_info;
    hw_info_st *phw_info = nic_info->hw_info;
    mlme_info_t *mlme_info = nic_info->mlme_info;
    local_info_st *plocal = (local_info_st *)nic_info->local_info;

    if (nic_info->is_up == 0)
    {
        goto tx_drop;
    }

    if (phw_info->mp_mode)
    {
        LOG_I("mp mode will drop the tx frame");
        goto tx_drop;
    }

    if (plocal->work_mode == ZT_INFRA_MODE)
    {
        if (mlme_info->connect == zt_false)
        {
            goto tx_drop;
        }
    }
#ifdef CFG_ENABLE_AP_MODE
    else if (plocal->work_mode == ZT_MASTER_MODE)
    {
        if (zt_ap_status_get(nic_info) != ZT_AP_STATE_ESTABLISHED)
        {
            goto tx_drop;
        }
    }
#endif

    return zt_true;

tx_drop:
    ptx_info->tx_drop++;
    // LOG_W("[%s,%d] tx_drop",__func__,__LINE__);

    return zt_false;
}


#ifdef CONFIG_SOFT_TX_AGGREGATION

void zt_tx_agg_num_fill(zt_u16 agg_num, zt_u8 *pbuf)
{
    zt_set_bits_to_le_u32(pbuf + 12, 24, 8, agg_num);
    // recalc txd checksum
    zt_txdesc_chksum(pbuf);
}

zt_u32 zt_nic_get_tx_max_len(nic_info_st *nic_info,
                             struct xmit_frame *pxmitframe)
{
    return MAX_XMITBUF_SZ;
}

zt_s32 zt_nic_tx_qsel_check(zt_u8 pre_qsel, zt_u8 next_qsel)
{
    zt_s32 chk_rst = ZT_RETURN_OK;
    if (((pre_qsel == QSLT_HIGH) || ((next_qsel == QSLT_HIGH)))
            && (pre_qsel != next_qsel))
    {
        chk_rst = ZT_RETURN_FAIL;
    }
    return chk_rst;
}

zt_s32 check_agg_condition(nic_info_st *nic_info, struct xmit_buf *pxmitbuf)
{
    return 0;
}
#endif

void zt_tx_xmit_stop(nic_info_st *nic_info)
{
    tx_info_st *ptx_info = nic_info->tx_info;

    zt_os_api_lock_lock(&ptx_info->lock);
    ptx_info->xmit_stop_flag++;
    zt_os_api_lock_unlock(&ptx_info->lock);
}

void zt_tx_xmit_start(nic_info_st *nic_info)
{
    tx_info_st *ptx_info = nic_info->tx_info;

    zt_os_api_lock_lock(&ptx_info->lock);
    if (ptx_info->xmit_stop_flag > 0)
    {
        ptx_info->xmit_stop_flag--;
    }
    zt_os_api_lock_unlock(&ptx_info->lock);
    zt_io_tx_xmit_wake(nic_info);
}

void zt_tx_xmit_pending_queue_clear(nic_info_st *nic_info)
{
    tx_info_st *tx_info = nic_info->tx_info;
    struct xmit_frame *pxmitframe;

    zt_tx_xmit_stop(nic_info);
    while (zt_que_is_empty(&tx_info->pending_frame_queue) == zt_false)
    {
        pxmitframe = zt_tx_data_getqueue(tx_info);
        zt_xmit_frame_delete(tx_info, pxmitframe);
        if (pxmitframe->pkt)
        {
            zt_free_skb(pxmitframe->pkt);
            pxmitframe->pkt = NULL;
        }
    }
    zt_tx_xmit_start(nic_info);
}

zt_s32 zt_tx_xmit_hif_queue_empty(nic_info_st *nic_info)
{
    if (zt_io_write_data_queue_check(nic_info) == zt_false)
    {
        LOG_D("tx queue no empty");
        return 0;
    }

    if (!ZT_CANNOT_RUN(nic_info))
    {
        zt_u8 is_empty;
        if (zt_mcu_check_tx_buff(nic_info, &is_empty) != ZT_RETURN_OK || !is_empty)
        {
            LOG_D("hw tx fifo no empty");
            return 0;
        }
    }

    return 1;
}

zt_s32 zt_tx_suspend(nic_info_st *nic_info)
{
    zt_timer_t timer;

    zt_timer_set(&timer, 5000);
    while (zt_io_write_data_queue_check(nic_info) == zt_false)
    {
        zt_msleep(10);
        if (zt_timer_expired(&timer))
        {
            LOG_E("data queue error");
            return -1;
        }
    }

    return 0;
}

zt_s32 zt_tx_resume(nic_info_st *nic_info)
{
    return 0;
}

