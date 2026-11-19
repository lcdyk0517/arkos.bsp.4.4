/*
 * proc.h
 *
 * used for print debugging information
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
#ifndef __PROC_H__
#define __PROC_H__

#include "ndev_linux.h"
#ifdef CONFIG_MP_MODE
#include "proc_trx.h"
#endif

#define zt_register_proc_interface(_name, _show, _write) \
    { .name = _name, .show = _show, .write = _write}
#define zt_print_seq seq_printf

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 24))
#define zt_proc_net proc_net
#else
extern struct net init_net;
#define zt_proc_net init_net.proc_net
#endif

struct zt_proc_handle
{
    zt_s8 *name;
    zt_s32(*show)(struct seq_file *, void *);
    ssize_t (*write)(struct file *file, const char __user *buffer, size_t count,
                     loff_t *pos, void *data);
};
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 17, 0))
#define PDE_DATA(inode) pde_data(inode)
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(3, 10, 0))
#define PDE_DATA(inode) PDE((inode))->data
#define proc_get_parent_data(inode) PDE((inode))->parent->data
#endif

typedef struct
{
    void *hif_info;
    struct proc_dir_entry *proc_root;
    zt_s8 proc_name[32];
    zt_bool mp_proc_test_enable;
} zt_proc_st;

#ifdef CONFIG_MP_MODE
typedef enum _MP_MODE_
{
    MP_OFF,
    MP_ON,
    MP_ERR,
    MP_CONTINUOUS_TX,
    MP_SINGLE_CARRIER_TX,
    MP_CARRIER_SUPPRISSION_TX,
    MP_SINGLE_TONE_TX,
    MP_PACKET_TX,
    MP_PACKET_RX,
    MP_TX_LCK,
    MP_MAC_LOOPBACK,
    MP_PHY_LOOPBACK,
} ZT_MP_MODE;

typedef struct _zt_mp_info_st
{
    zt_u32 mode;
    zt_u32 prev_fw_state;

    zt_u8 TID;
    zt_u32 tx_pktcount;
    zt_u32 pktInterval;
    zt_u32 pktLength;

    zt_u8  rx_start;
    zt_u32 rx_bssidpktcount;
    zt_u32 rx_pktcount;
    zt_u32 rx_pktcount_filter_out;
    zt_u32 rx_crcerrpktcount;
    zt_u32 rx_pktloss;

    zt_u8 channel;
    zt_u8 bandwidth;
    zt_u8 prime_channel_offset;
    zt_u8 txpoweridx;
    zt_u32 preamble;
    zt_u32 CrystalCap;

    zt_u16 antenna_tx;
    zt_u16 antenna_rx;

    zt_u8 check_mp_pkt;
    zt_u32 rateidx;

    zt_u8 bSetTxPower;
    zt_u8 mp_dm;
    zt_u8 mac_filter[ZT_80211_MAC_ADDR_LEN];
    zt_u8 bmac_filter;
    zt_u8 *pallocated_mp_xmitframe_buf;
    zt_u8 *pmp_xmtframe_buf;
    zt_u32 free_mp_xmitframe_cnt;
    zt_bool bStartContTx;
    zt_bool bCarrierSuppression;
    zt_bool bSingleTone;
    zt_bool bSetRxBssid;
    zt_bool rx_bindicatePkt;
    zt_bool bWLSmbCfg;
    zt_u8 *TXradomBuffer;

    zt_bool sta_connect_stats;
    zt_bool ap_connect_stats;

    zt_proc_mp_tx tx;

    zt_80211_addr_t         network_macaddr;

    zt_u8         efuse_data_map[ZT_EEPROM_MAX_SIZE];

    void *pnic_info;
    void *PktRxThread;

    zt_u32 sdio_ReceiveConfig;

} zt_mp_info_st;

struct dbg_rx_counter
{
    zt_u32 rx_pkt_ok;
    zt_u32 rx_pkt_crc_error;
    zt_u32 rx_pkt_drop;
    zt_u32 rx_ofdm_fa;
    zt_u32 rx_cck_fa;
    zt_u32 rx_ht_fa;
};
#endif

zt_s32 zt_proc_init(void *nic_info);
void zt_proc_term(void *nic_info);

#endif

