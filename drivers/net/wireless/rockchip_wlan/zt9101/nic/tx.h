/*
 * tx.h
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
#ifndef __TX_H__
#define __TX_H__

#ifndef MAX_XMITBUF_SZ
#ifdef CONFIG_SOFT_TX_AGGREGATION
#define MAX_XMITBUF_SZ      (8*1024)
#else
#define MAX_XMITBUF_SZ      (2048)
#endif
#endif


#define TX_AGG_QUEUE_ENABLE (0)

#ifndef XMIT_DATA_BUFFER_CNT
#define XMIT_DATA_BUFFER_CNT (8)
#endif

#ifndef XMIT_MGMT_BUFFER_CNT
#define XMIT_MGMT_BUFFER_CNT (8)
#endif

#ifndef XMIT_CMD_BUFFER_CNT
#define XMIT_CMD_BUFFER_CNT (1)
#endif

#ifndef NR_XMITFRAME
#define NR_XMITFRAME        256
#endif

#ifdef USB_XMITBUF_ALIGN_SZ
#define XMITBUF_ALIGN_SZ    (USB_XMITBUF_ALIGN_SZ)
#else
#define XMITBUF_ALIGN_SZ    512
#endif

#define MAX_XMIT_EXTBUF_SZ  (1536)

#define MAX_CMDBUF_SZ       (5120)

#define XMIT_VO_QUEUE       (0)
#define XMIT_VI_QUEUE       (1)
#define XMIT_BE_QUEUE       (2)
#define XMIT_BK_QUEUE       (3)

#define VO_QUEUE_INX        0
#define VI_QUEUE_INX        1
#define BE_QUEUE_INX        2
#define BK_QUEUE_INX        3
#define BCN_QUEUE_INX       4
#define MGT_QUEUE_INX       5
#define HIGH_QUEUE_INX      6
#define TXCMD_QUEUE_INX     7
#define CMD_QUEUE_INX       8
#define READ_QUEUE_INX      9

#define QSLT_BEACON         0x8
#define QSLT_HIGH           0x9
#define QSLT_MGNT           0xA

#define AC0_IDX 0
#define AC1_IDX 1
#define AC2_IDX 2
#define AC3_IDX 3
#define AC4_IDX 4
#define AC5_IDX 5
#define ACX_IDX AC2_IDX

/* CCK Rates, TxHT = 0 */
#define DESC_RATE1M                 0x00
#define DESC_RATE2M                 0x01
#define DESC_RATE5_5M               0x02
#define DESC_RATE11M                0x03

/* OFDM Rates, TxHT = 0 */
#define DESC_RATE6M                 0x04
#define DESC_RATE9M                 0x05
#define DESC_RATE12M                0x06
#define DESC_RATE18M                0x07
#define DESC_RATE24M                0x08
#define DESC_RATE36M                0x09
#define DESC_RATE48M                0x0A
#define DESC_RATE54M                0x0B

/* MCS Rates, TxHT = 1 */
#define DESC_RATEMCS0               0x0C
#define DESC_RATEMCS1               0x0D
#define DESC_RATEMCS2               0x0E
#define DESC_RATEMCS3               0x0F
#define DESC_RATEMCS4               0x10
#define DESC_RATEMCS5               0x11
#define DESC_RATEMCS6               0x12
#define DESC_RATEMCS7               0x13
#define DESC_RATEMCS8               0x14
#define DESC_RATEMCS9               0x15
#define DESC_RATEMCS10              0x16
#define DESC_RATEMCS11              0x17
#define DESC_RATEMCS12              0x18
#define DESC_RATEMCS13              0x19
#define DESC_RATEMCS14              0x1A
#define DESC_RATEMCS15              0x1B
#define DESC_RATEMCS16              0x1C
#define DESC_RATEMCS17              0x1D
#define DESC_RATEMCS18              0x1E
#define DESC_RATEMCS19              0x1F
#define DESC_RATEMCS20              0x20
#define DESC_RATEMCS21              0x21
#define DESC_RATEMCS22              0x22
#define DESC_RATEMCS23              0x23
#define DESC_RATEMCS24              0x24
#define DESC_RATEMCS25              0x25
#define DESC_RATEMCS26              0x26
#define DESC_RATEMCS27              0x27
#define DESC_RATEMCS28              0x28
#define DESC_RATEMCS29              0x29
#define DESC_RATEMCS30              0x2A
#define DESC_RATEMCS31              0x2B


#define HW_QUEUE_ENTRY      8

#define SN_LESS(a, b)   (((a-b)&0x800)!=0)
#define SN_EQUAL(a, b)  (a == b)


#define WEP_IV(iv, dot11txpn, keyidx)\
    do{\
        iv[0] = dot11txpn._byte_.TSC0;\
        iv[1] = dot11txpn._byte_.TSC1;\
        iv[2] = dot11txpn._byte_.TSC2;\
        iv[3] = ((keyidx & 0x3)<<6);\
        dot11txpn.val = (dot11txpn.val == 0xffffff) ? 0: (dot11txpn.val+1);\
    }while(0)

#define TKIP_IV(iv, dot11txpn, keyidx)\
    do{\
        iv[0] = dot11txpn._byte_.TSC1;\
        iv[1] = (dot11txpn._byte_.TSC1 | 0x20) & 0x7f;\
        iv[2] = dot11txpn._byte_.TSC0;\
        iv[3] = ZT_BIT(5) | ((keyidx & 0x3)<<6);\
        iv[4] = dot11txpn._byte_.TSC2;\
        iv[5] = dot11txpn._byte_.TSC3;\
        iv[6] = dot11txpn._byte_.TSC4;\
        iv[7] = dot11txpn._byte_.TSC5;\
        dot11txpn.val = dot11txpn.val == 0xffffffffffffULL ? 0: (dot11txpn.val+1);\
    }while(0)

#define AES_IV(iv, dot11txpn, keyidx)\
    do{\
        iv[0] = dot11txpn._byte_.TSC0;\
        iv[1] = dot11txpn._byte_.TSC1;\
        iv[2] = 0;\
        iv[3] = ZT_BIT(5) | ((keyidx & 0x3)<<6);\
        iv[4] = dot11txpn._byte_.TSC2;\
        iv[5] = dot11txpn._byte_.TSC3;\
        iv[6] = dot11txpn._byte_.TSC4;\
        iv[7] = dot11txpn._byte_.TSC5;\
        dot11txpn.val = dot11txpn.val == 0xffffffffffffULL ? 0: (dot11txpn.val+1);\
    }while(0)

#ifndef TXDESC_OFFSET_NEW
#define TXDESC_OFFSET_NEW      20
#endif
#define TXDESC_SIZE            TXDESC_OFFSET_NEW
#define PACKET_OFFSET_SZ       0
#define TXDESC_OFFSET          TXDESC_OFFSET_NEW

struct tx_desc
{
    zt_u32 txdw0;
    zt_u32 txdw1;
    zt_u32 txdw2;
    zt_u32 txdw3;
    zt_u32 txdw4;
    zt_u32 txdw5;
    zt_u32 txdw6;
    zt_u32 txdw7;

    zt_u32 txdw8;
    zt_u32 txdw9;
};

union txdesc
{
    struct tx_desc txdesc;
    zt_u32 value[TXDESC_SIZE >> 2];
};

#define WLANHDR_OFFSET      64

#define NULL_FRAMETAG       (0x0)
#define DATA_FRAMETAG       0x01
#define L2_FRAMETAG         0x02
#define MGNT_FRAMETAG       0x03
#define AMSDU_FRAMETAG      0x04
#ifdef CONFIG_MP_MODE
#define MP_FRAMETAG         0x07
#endif

struct xmit_buf
{
    zt_list_t list;
    struct nic_info *nic_info;
    zt_u8 *pallocated_buf;
    zt_u8 *pbuf;
    zt_u8 *ptail;
    zt_u8 agg_num;
    void *priv_data;
    zt_u16 flags;
    zt_u32 alloc_sz;
    zt_u32 ff_hwaddr;
    zt_u8 wlan_hdr_len;
    zt_u8 iv_len;
    zt_u8 icv_len;
    zt_u8 privacy;
    zt_u8 encrypt_algo;
    zt_u16 pkt_len;
    zt_u8 send_flag;
    zt_u8  buffer_id;
    zt_u8 qsel;
    zt_u16 ether_type;
    zt_u8 icmp_pkt;
    zt_u16 con_len; //condition len
};

struct xmit_frame
{
    zt_list_t list;

    zt_u16 ether_type;
    zt_bool bmcast;
    zt_u8 dhcp_pkt;
    zt_u8 icmp_pkt;
    zt_u16 seqnum;
    zt_u16 pkt_hdrlen;
    zt_u16 hdrlen;
    zt_u32 pktlen;
    zt_u32 last_txcmdsz;
    zt_u8 nr_frags;
    zt_u8 encrypt_algo;
    zt_u8 bswenc;
    zt_u8 iv_len;
    zt_u8 icv_len;
    zt_u8 iv[18];
    zt_u8 icv[16];
    zt_u8 priority;
    zt_u8 qsel;
    zt_u8 ampdu_en;
    zt_u8 vcs_mode;
    zt_u8 key_idx;
    zt_u8 ht_en;

    union Keytype dot11tkiptxmickey;
    union Keytype dot118021x_UncstKey;

    zt_80211_data_t *pwlanhdr;
    wdn_net_info_st *pwdn;

    void *pkt;

    zt_s32 frame_tag;

    struct nic_info *nic_info;

    zt_u8 *buf_addr;

    struct xmit_buf *pxmitbuf;

    zt_u8 agg_num;
    zt_s8 pkt_offset;
    zt_u16 frame_id;
};

enum cmdbuf_type
{
    CMDBUF_BEACON = 0x00,
    CMDBUF_RSVD,
    CMDBUF_MAX
};

typedef struct tx_info
{
    zt_os_api_lock_t lock;
    zt_u64 tx_bytes;
    zt_u64 tx_pkts;
    zt_u64 tx_drop;
    zt_u64 tx_mgnt_pkts;
#ifdef CFG_ENABLE_AP_MODE
    zt_bool is_bcn_pkt;
    zt_bool pause;
#endif

    zt_u8 xmitFrameCtl;

    // used to stop or restart send to hif queue
    zt_u16 xmit_stop_flag;

    /* define allocated xmit_frame memory */
    zt_u8 *pallocated_frame_buf;
    zt_u8 *pxmit_frame_buf;
    zt_s32 free_xmitframe_cnt;
    zt_que_t xmit_frame_queue;
    zt_que_t agg_frame_queue;

    /* define allocated xmit_buf memory */
    zt_u8 *pallocated_xmitbuf;
    zt_u8 *pxmitbuf;
    zt_u32 free_xmitbuf_cnt;
    zt_que_t xmit_buf_queue;

    /* define mgmt frame allocated xmit_buf memory */
    zt_u8 *pallocated_xmit_extbuf;
    zt_u8 *pxmit_extbuf;
    zt_u32 free_xmit_extbuf_cnt;
    zt_que_t xmit_extbuf_queue;

    /* pending queue */
    zt_os_api_lock_t pending_lock;
    zt_u32 pending_frame_cnt;
    zt_que_t pending_frame_queue;

    nic_info_st *nic_info;

#ifdef CONFIG_LPS
    struct xmit_buf pcmd_xmitbuf[CMDBUF_MAX]; // LPS need
#endif
} tx_info_st;

extern zt_u8 zt_ra_sGI_get(wdn_net_info_st *pwdn, zt_u8 pad);
extern zt_u8 zt_chk_qos(zt_u8 acm_mask, zt_u8 priority, zt_u8 pad);
extern zt_bool zt_need_stop_queue(nic_info_st *nic_info);
extern zt_bool zt_need_wake_queue(nic_info_st *nic_info);
extern zt_s32 zt_tx_info_init(nic_info_st *nic_info);
extern zt_s32 zt_tx_info_term(nic_info_st *nic_info);
extern struct xmit_buf *zt_xmit_buf_new(tx_info_st *tx_info);
extern zt_bool zt_xmit_buf_delete(tx_info_st *tx_info,
                                  struct xmit_buf *pxmitbuf);
extern struct xmit_buf *zt_xmit_extbuf_new(tx_info_st *tx_info);
extern zt_bool zt_xmit_extbuf_delete(tx_info_st *tx_info,
                                     struct xmit_buf *pxmitbuf);
extern struct xmit_frame *zt_xmit_frame_new(tx_info_st *tx_info);
extern zt_bool zt_xmit_frame_delete(tx_info_st *tx_info,
                                    struct xmit_frame *pxmitframe);
extern zt_bool zt_tx_data_check(nic_info_st *nic_info);
extern zt_s32 zt_tx_msdu(nic_info_st *nic_info, zt_u8 *msdu_buf,
                         zt_s32 msdu_len,
                         void *pkt);
zt_s32 zt_nic_null_xmit(nic_info_st *pnic_info,
                        wdn_net_info_st *pwdn_info, zt_bool ps_en, zt_u32 to_ms);
extern zt_s32 zt_nic_beacon_xmit(nic_info_st *nic_info,
                                 struct xmit_buf *pxmitbuf,
                                 zt_u16 len);
extern zt_s32 zt_nic_mgmt_frame_xmit(nic_info_st *nic_info,
                                     wdn_net_info_st *wdn,
                                     struct xmit_buf *pxmitbuf, zt_u16 len);
zt_s32 zt_nic_mgmt_frame_xmit_with_ack(nic_info_st *nic_info,
                                       wdn_net_info_st *wdn,
                                       struct xmit_buf *pxmitbuf, zt_u16 len,
                                       zt_u32 to_ms);
extern zt_s32 zt_tx_pending_frame_xmit(nic_info_st *nic_info);

extern struct xmit_frame *zt_tx_data_getqueue(tx_info_st *tx_info);
extern struct xmit_frame *zt_tx_data_dequeue(tx_info_st *tx_info);
extern void zt_tx_data_enqueue_tail(tx_info_st *tx_info,
                                    struct xmit_frame *pxmitframe);
extern void zt_tx_data_enqueue_head(tx_info_st *tx_info,
                                    struct xmit_frame *pxmitframe);
extern zt_bool zt_xmit_frame_enqueue(tx_info_st *tx_info,
                                     struct xmit_frame *pxmitframe);

extern void zt_tx_agg_enqueue_head(tx_info_st *tx_info,
                                   struct xmit_frame *pxmitframe);
extern struct xmit_frame *zt_tx_agg_dequeue(tx_info_st *tx_info);
extern void zt_tx_frame_queue_clear(nic_info_st *nic_info);

extern zt_u32 zt_get_wlan_pkt_size(struct xmit_frame *pxmitframe);

#ifdef CONFIG_LPS
extern struct xmit_frame *zt_xmit_cmdframe_new(tx_info_st *tx_info,
        enum cmdbuf_type buf_type, zt_u8 tag);
#endif

#ifdef CONFIG_SOFT_TX_AGGREGATION
extern void zt_tx_agg_num_fill(zt_u16 agg_num, zt_u8 *pbuf);
extern zt_u32 zt_nic_get_tx_max_len(nic_info_st *nic_info,
                                    struct xmit_frame *pxmitframe);
extern zt_s32 zt_nic_tx_qsel_check(zt_u8 pre_qsel, zt_u8 next_qsel);
#endif
void zt_txdesc_chksum(zt_u8 *ptx_desc);
zt_u32 zt_quary_addr(zt_u8 qsel);
zt_u8 zt_mrate_to_hwrate(zt_u8 rate);
zt_u8 zt_hwrate_to_mrate(zt_u8 rate);

zt_bool zt_tx_txdesc_init(struct xmit_frame *pxmitframe, zt_u8 *pmem, zt_s32 sz,
                          zt_bool bagg_pkt, zt_u8 dum);

zt_bool zt_xmit_frame_init(nic_info_st *nic_info, struct xmit_frame *pxmitframe,
                           zt_u8 *msdu_buf, zt_s32 msdu_len);

zt_bool zt_tx_msdu_to_mpdu(nic_info_st *nic_info, struct xmit_frame *pxmitframe,
                           zt_u8 *msdu_buf, zt_s32 msdu_len);
void zt_tx_stats_cnt(nic_info_st *nic_info, struct xmit_frame *pxmitframe,
                     zt_s32 sz);
void zt_tx_xmit_stop(nic_info_st *nic_info);
void zt_tx_xmit_start(nic_info_st *nic_info);
void zt_tx_xmit_pending_queue_clear(nic_info_st *nic_info);
zt_s32 zt_tx_xmit_hif_queue_empty(nic_info_st *nic_info);
zt_s32 zt_tx_suspend(nic_info_st *nic_info);
zt_s32 zt_tx_resume(nic_info_st *nic_info);

#define ZT_PAGE_NUM(_Len, _Size) (zt_u32)(((_Len)/(_Size)) + ((_Len)&((_Size) - 1) ? 1:0))

#endif
