/*
 * proc.h
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
#ifndef __PROC_TRX_H__
#define __PROC_TRX_H__

#ifdef CONFIG_MP_MODE

enum ZT_NEW_MGN_RATE
{
    WL_MGN_1M = 0x00,
    WL_MGN_2M = 0x01,
    WL_MGN_5_5M = 0x02,
    WL_MGN_11M = 0x3,
    WL_MGN_6M = 0x80,
    WL_MGN_9M = 0x81,
    WL_MGN_12M = 0x82,
    WL_MGN_18M = 0x83,
    WL_MGN_24M = 0x84,
    WL_MGN_36M = 0x85,
    WL_MGN_48M = 0x86,
    WL_MGN_54M = 0x87,
    WL_MGN_MCS0 = 0x100,
    WL_MGN_MCS1 = 0x101,
    WL_MGN_MCS2 = 0x102,
    WL_MGN_MCS3 = 0x103,
    WL_MGN_MCS4 = 0x104,
    WL_MGN_MCS5 = 0x105,
    WL_MGN_MCS6 = 0x106,
    WL_MGN_MCS7 = 0x107,
};

enum RATEID_IDX_MP
{
    RATEID_IDX_BGN_40M_1SS = 1,
    RATEID_IDX_BGN_20M_1SS_BN = 3,
};

typedef struct _zt_mp_tx
{
    zt_u8 stop;
    zt_u32 count, sended;
    zt_u8 payload;
    struct xmit_frame attrib;
    zt_u8 desc[TXDESC_SIZE];
    zt_u8 *pallocated_buf;
    zt_u8 *buf;
    zt_u32 buf_size, write_size;
    void *PktTxThread;
} zt_proc_mp_tx;

zt_s32 zt_mp_proc_test_rx(nic_info_st *pnic_info, char *data, size_t data_len);
zt_s32 zt_mp_proc_test_tx(nic_info_st *pnic_info, char *data, size_t data_len);
zt_s32 zt_mp_proc_stats(nic_info_st *pnic_info, char *data, size_t data_len);
zt_s32 zt_mp_proc_rate_to_rateidx(zt_u32 rate);
zt_s32 zt_mp_proc_rx_common_process(nic_info_st *pnic_info, zt_u8 *pktBuf,
                                    zt_u32 pktLen);
#endif
#endif
