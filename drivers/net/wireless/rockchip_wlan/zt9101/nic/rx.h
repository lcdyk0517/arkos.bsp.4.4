/*
 * rx.h
 *
 * used for rx frame handle
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
#ifndef __RX_H__
#define __RX_H__

#define RX_REORDER_ENABLE (1)

#define ETHERNET_HEADER_SIZE                14  /* A-MSDU header ï¼? DA(6)+SA(6)+Length(2) */
#define LLC_HEADER_SIZE                     6

#define HIF_HDR_LEN 56

typedef enum
{
    STA_TO_STA = 0,
    STA_TO_DS  = 1,
    DS_TO_STA  = 2,
    MESH_TO_MESH = 3,
} To_From_Ds_t;

#ifndef MAX_RXBUF_SZ
#define MAX_RXBUF_SZ (32768)
#endif

#ifndef NR_RECVBUFF
#define NR_RECVBUFF 8
#endif

#ifndef RXD_SIZE
#define RXD_SIZE    24
#endif

#define MAX_PKT_NUM     20

enum phydm_ctrl_info_rate
{
    PHY_DM_RATE1M          = 0x00,
    PHY_DM_RATE2M          = 0x01,
    PHY_DM_RATE5_5M        = 0x02,
    PHY_DM_RATE11M         = 0x03,
    /* OFDM Rates, TxHT = 0 */
    PHY_DM_RATE6M          = 0x04,
    PHY_DM_RATE9M          = 0x05,
    PHY_DM_RATE12M         = 0x06,
    PHY_DM_RATE18M         = 0x07,
    PHY_DM_RATE24M         = 0x08,
    PHY_DM_RATE36M         = 0x09,
    PHY_DM_RATE48M         = 0x0A,
    PHY_DM_RATE54M         = 0x0B,
    /* MCS Rates, TxHT = 1 */
    PHY_DM_RATEMCS0            = 0x0C,
    PHY_DM_RATEMCS1            = 0x0D,
    PHY_DM_RATEMCS2            = 0x0E,
    PHY_DM_RATEMCS3            = 0x0F,
    PHY_DM_RATEMCS4            = 0x10,
    PHY_DM_RATEMCS5            = 0x11,
    PHY_DM_RATEMCS6            = 0x12,
    PHY_DM_RATEMCS7            = 0x13,
};

/* version is always 0 */
#define PKTHDR_RADIOTAP_VERSION 0

/* see the radiotap website for the descriptions */
enum zt_radiotap_presence
{
    ZT_RADIOTAP_TSFT = 0,
    ZT_RADIOTAP_FLAGS = 1,
    ZT_RADIOTAP_RATE = 2,
    ZT_RADIOTAP_CHANNEL = 3,
    ZT_RADIOTAP_FHSS = 4,
    ZT_RADIOTAP_DBM_ANTSIGNAL = 5,
    ZT_RADIOTAP_DBM_ANTNOISE = 6,
    ZT_RADIOTAP_LOCK_QUALITY = 7,
    ZT_RADIOTAP_TX_ATTENUATION = 8,
    ZT_RADIOTAP_DB_TX_ATTENUATION = 9,
    ZT_RADIOTAP_DBM_TX_POWER = 10,
    ZT_RADIOTAP_ANTENNA = 11,
    ZT_RADIOTAP_DB_ANTSIGNAL = 12,
    ZT_RADIOTAP_DB_ANTNOISE = 13,
    ZT_RADIOTAP_RX_FLAGS = 14,
    ZT_RADIOTAP_TX_FLAGS = 15,
    ZT_RADIOTAP_RTS_RETRIES = 16,
    ZT_RADIOTAP_DATA_RETRIES = 17,
    /* 18 is XChannel, but it's not defined yet */
    ZT_RADIOTAP_MCS = 19,
    ZT_RADIOTAP_AMPDU_STATUS = 20,
    ZT_RADIOTAP_VHT = 21,
    ZT_RADIOTAP_TIMESTAMP = 22,

    /* valid in every it_present bitmap, even vendor namespaces */
    ZT_RADIOTAP_RADIOTAP_NAMESPACE = 29,
    ZT_RADIOTAP_VENDOR_NAMESPACE = 30,
    ZT_RADIOTAP_EXT = 31
};

/* for IEEE80211_RADIOTAP_FLAGS */
enum zt_radiotap_flags
{
    ZT_RADIOTAP_CFP = 0x01,
    ZT_RADIOTAP_SHORTPRE = 0x02,
    ZT_RADIOTAP_WEP = 0x04,
    ZT_RADIOTAP_FRAG = 0x08,
    ZT_RADIOTAP_FCS = 0x10,
    ZT_RADIOTAP_DATAPAD = 0x20,
    ZT_RADIOTAP_BADFCS = 0x40,
};

/* for IEEE80211_RADIOTAP_CHANNEL */
enum zt_radiotap_channel_flags
{
    ZT_CHAN_CCK = 0x0020,
    ZT_CHAN_OFDM = 0x0040,
    ZT_CHAN_2GHZ = 0x0080,
    ZT_CHAN_5GHZ = 0x0100,
    ZT_CHAN_DYN = 0x0400,
    ZT_CHAN_HALF = 0x4000,
    ZT_CHAN_QUARTER = 0x8000,
};

typedef enum PKT_TYPE
{
    ZT_PKT_TYPE_CMD         = 0x00,
    ZT_PKT_TYPE_FW          = 0x01,
    ZT_PKT_TYPE_FRAME       = 0x03,
    ZT_PKT_TYPE_MAX
} PKT_TYPE_T;

typedef enum MAC_FRAME_TYPE
{
    _MAC_FRAME_TYPE_MGMT_ = 0,
    _MAC_FRAME_TYPE_CTRL_ = 1,
    _MAC_FRAME_TYPE_DATA_ = 2,
} MAC_FRAME_TYPE_T;

struct rxd_detail_new
{
    /* DW0 */
    zt_u32 data_type     : 2;       /* bit0~bit1 */
    zt_u32 cmd_index     : 8;       /* bit2~bit9 */
    zt_u32 rvd0_0        : 4;       /* bit10~bit13 */
    zt_u32 drvinfo_size  : 4;       /* bit14~bit17 */
    zt_u32 phy_status    : 1;       /* bit18 */
    zt_u32 crc32         : 1;       /* bit19 */
    zt_u32 rvd0_1        : 12;      /* bit20~bit31 */
    /* DW1 */
    zt_u32 pkt_len       : 14;      /* bit0~bit13 */
    zt_u32 rvd1_0        : 9;       /* bit14~bit22 */
    zt_u32 swdec         : 1;       /* bit23 */
    zt_u32 encrypt_algo  : 3;       /* bit24~bit26 */
    zt_u32 qos           : 1;       /* bit27 */
    zt_u32 tid           : 4;       /* bit28~bit31 */
    /* DW2 */
    zt_u32 rx_rate       : 7;      /* bit0~bit6 */
    zt_u32 rvd2_0        : 8;      /* bit7~bit14 */
    zt_u32 notice        : 1;      /* bit15 */
    zt_u32 rpt_sel       : 1;      /* bit16 */
    zt_u32 amsdu         : 1;      /* bit17 */
    zt_u32 more_data     : 1;      /* bit18 */
    zt_u32 more_frag     : 1;      /* bit19 */
    zt_u32 frag          : 4;      /* bit20~bit23 */
    zt_u32 usb_agg_pktnum : 8;     /* bit24~bit31 */
    /* DW3 */
    zt_u32 mac_id        : 5;      /* bit0~bit4 */
    zt_u32 rsvd3_0       : 27;     /* bit5~bit31 */
    /* DW4 */
    zt_u32 seq           : 12;     /* bit0~bit11 */
    zt_u32 rsvd4_0       : 20;     /* bit12~bit31 */
};

#define RX_DESC_GET_FIELD(_rxdescfield,_mask,_offset) \
    (( _rxdescfield >> _offset ) & _mask)

/*
the stutus of every pkt
*/
typedef struct rx_pkt_info
{
    zt_u8 pkt_type;
    zt_u8 cmd_index;
    zt_u8 crc_check;
    zt_u16 seq_num;
    zt_u16 pkt_len;
    zt_u8 hif_hdr_len;     /* rxd , drvinfo,shift_sz total 56byts*/
    zt_u8 wlan_hdr_len;    /* 802.11 frame header length  */
    zt_u8 sw_decrypt;
    /* software decrypt */
    zt_u8 qos_flag;        /* qos frame flag */
    zt_u8 qos_pri;         /* priority */
    zt_u16 rx_rate;
    zt_u8 amsdu;
    zt_u8 more_data;
    zt_u8 more_frag;
    zt_u8 frag_num;
    zt_u8 frame_type;
    zt_u8 bdecrypted;
    zt_u8 encrypt_algo;  /* 0:open mode ; others:encrypt */
    zt_u8 iv_len;
    zt_u8 icv_len;
    zt_u8 crc_err;
    zt_u8 icv_err;
    zt_u16 eth_type;
    zt_u8 phy_status;
    zt_u8 usb_agg_pktnum;
    zt_u8 dst_addr[ZT_80211_MAC_ADDR_LEN];
    zt_u8 src_addr[ZT_80211_MAC_ADDR_LEN];
    zt_u8 tx_addr[ZT_80211_MAC_ADDR_LEN];
    zt_u8 rx_addr[ZT_80211_MAC_ADDR_LEN];
    zt_u8 bssid[ZT_80211_MAC_ADDR_LEN];

    zt_u8 ack_policy;
    zt_u8 tcpchk_valid;
    zt_u8 ip_chkrpt;
    zt_u8 tcp_chkrpt;
    zt_u8 key_index;
    zt_u8 bw;
    zt_u8 stbc;
    zt_u8 ldpc;
    zt_u32 tsfl;
    zt_u8 sgi;
} rx_pkt_info_t, * prx_pkt_info_t;

/* there 2bytes  */
typedef struct normal_mac_hdr
{
    zt_u16 protocol_ver : 2;
    zt_u16 type         : 2;
    zt_u16 subtype      : 4;
    zt_u16 to_ds        : 1;
    zt_u16 from_ds      : 1;
    zt_u16 mfrag        : 1;
    zt_u16 retry        : 1;
    zt_u16 pwr_mng      : 1;
    zt_u16 mdata        : 1;
    zt_u16 bprotected   : 1;
    zt_u16 order    : 1;
    zt_u16 duration;
    zt_u8  addr1[6];
    zt_u8  addr2[6];
    zt_u8  addr3[6];
    zt_u16  seq_ctrl;
    zt_u8  addr4[6];
    zt_u16 qos_ctrl;
} normal_mac_hdr_t, * pnormal_mac_hdr_t;

typedef struct rx_statu
{
    zt_u8 signal_strength;
    zt_u8 signal_qual;
} rx_status_t;

typedef struct
{
    zt_list_t node;
    zt_u32 addr;
    zt_u64 index;
    zt_u8 sorce_mode;//0:hif,1:rx
} skb_mgt_info_st;

#define RX_REORDER_NAME_LEN    (32)

typedef struct
{
    zt_u64 pkt_num_last;
    zt_timer_t to;
    zt_u8 count;
} rx_watch_t;

typedef struct rx_info
{
    zt_u8 *prx_pkt_buf_alloc;
    que_t free_rx_pkt_list; /* list for rx_pkt that note in use*/
    que_t recv_rx_pkt_list; /* list for rx pkt which is used */
    que_t rx_mgmt_frame_defrag_list; /* list for fragmentation frame which is not defragment yet */
    void *p_nic_info;
    rx_status_t rx_sta;
    que_t disc_defrag_q;

    zt_u64 rx_bytes;
    zt_u64 rx_total_pkts;
    zt_u64 rx_probersp_pkts;
    zt_u64 rx_pkts;
    zt_u64 rx_drop;
    zt_u64 rx_mgnt_pkt;
    zt_u64 rx_data_pkt;
    zt_u64 rx_crcerr_pkt;
    zt_u32 m0_rxbuf[3];

    rx_watch_t total_pkts_watch, probrsp_pkts_watch;

    recv_ba_ctrl_st  ba_ctl[TID_NUM + 1];
} rx_info_t, * prx_info_t;


typedef struct phy_status
{
    zt_u8 signal_strength;
    zt_u8 signal_qual;
    zt_s8 rssi;
} phy_status_st;
/*
a rx_pkt structure describe the packet attribute
*/
typedef struct rx_pkt
{
    que_entry_t entry;
    void *p_hif_node;     /* point to struct hif_node structure */
    void *p_nic_info;     /* point to struct nic_info structure */
    prx_info_t prx_info;
    struct rx_pkt_info pkt_info;
    phy_status_st phy_status;
    zt_u8  rxd_raw_buf[RXDESC_SIZE];
    void *pskb;
    zt_u32 len;
    zt_u8 *phead;
    zt_u8 *pdata;
    zt_u8 *ptail;
    zt_u8 *pend;
    wdn_net_info_st *wdn_info;
} rx_pkt_t, * prx_pkt_t;

typedef struct
{
    zt_list_t list;
    zt_u16 seq_num;
    void *pskb;
} rx_reorder_queue_st;


/**
 * struct rx_radiotap_header - base radiotap header
 */
struct rx_radiotap_header
{
    zt_u8 it_version;
    zt_u8 it_pad;
    zt_u16 it_len;
    zt_u32 it_present;
} zt_packed;

typedef enum
{
    REORDER_DROP = -1,
    REORDER_SEND = 0,
    REORDER_ENQUE = 1,
    REORDER_ENQUE_SEND = 2,
    REORDER_DEQUE_SEND = 3,
} REORDER_STATE;

zt_s32 zt_rx_init(nic_info_st *nic_info);
zt_s32 zt_rx_term(nic_info_st *nic_info);
zt_s32 zt_rx_common_process(prx_pkt_t ppkt);
zt_s32 zt_rx_data_len_check(nic_info_st *pnic_info, zt_u8 *pbuf,
                            zt_u16 skb_len);
zt_s32 zt_rx_cmd_check(zt_u8 *pbuf, zt_u16 skb_len);

zt_u8 calc_rx_rate(zt_u8 rx_rate);

zt_u16 zt_rx_get_pkt_len_and_check_valid(zt_u8 is_check, zt_u8 *buf,
        zt_u16 remain,
        zt_u16 *hdr_len,
        zt_bool *valid, zt_bool *notice);
PKT_TYPE_T zt_rx_data_type(zt_u8 *pbuf);
void       zt_rx_rxd_prase(zt_u8 *pbuf, struct rx_pkt *prx_pkt);
zt_s32 zt_rx_action_ba_ctl_init(nic_info_st *nic_info);

rx_reorder_queue_st *rx_free_reorder_dequeue(recv_ba_ctrl_st *ba_ctl);
zt_s32 rx_free_reorder_enqueue(recv_ba_ctrl_st *ba_ctl,
                               rx_reorder_queue_st *node);
zt_s32 rx_free_reorder_empty(recv_ba_ctrl_st *ba_ctl);
void rx_do_update_expect_seq(zt_u16 seq_num, recv_ba_ctrl_st   *ba_order);
zt_s32 rx_pending_reorder_is_empty(recv_ba_ctrl_st   *ba_order);
zt_s32 rx_pending_reorder_enqueue(zt_u16 current_seq, void *pskb,
                                  recv_ba_ctrl_st   *ba_order);
rx_reorder_queue_st *rx_pending_reorder_dequeue(recv_ba_ctrl_st   *ba_order);
rx_reorder_queue_st *rx_pending_reorder_getqueue(recv_ba_ctrl_st   *ba_order);
zt_s32 rx_do_chk_expect_seq(zt_u16 seq_num, recv_ba_ctrl_st   *ba_order);
zt_s32 zt_rx_action_ba_ctl_deinit(nic_info_st *nic_info);
zt_s32 zt_rx_ba_reinit(nic_info_st *nic_io, zt_u8 tid);

zt_s32 rx_pending_reorder_get_cnt(recv_ba_ctrl_st   *ba_order);
void rx_reorder_timeout_handle(zt_os_api_timer_t *timer);

void zt_rx_ba_all_reinit(nic_info_st *nic_info);
zt_s32 rx_reorder_upload(recv_ba_ctrl_st   *ba_order);
void zt_rx_data_reorder_core(rx_pkt_t *pkt);

zt_s32 rx_check_data_frame_valid(prx_pkt_t prx_pkt);
zt_s32 rx_check_mngt_frame_valid(prx_pkt_t prx_pkt);

zt_s32 zt_rx_calc_str_and_qual(nic_info_st *nic_info, zt_u8 agc_gain,
                               zt_u8 sig_qual_or_pwdb, zt_u8 agc_rpt_or_cfosho,
                               zt_s8 rx_evm, void *prx_pkt);

zt_s32 zt_rx_suspend(nic_info_st *pnic_info);
zt_s32 zt_rx_resume(nic_info_st *pnic_info);

#ifdef CFG_ENABLE_AP_MODE
void ap_rx_watch(nic_info_st *nic_info, zt_bool is_connected);
#endif
void rx_watch(nic_info_st *nic_info);

#endif

