/*
 * hw_info.h
 *
 * used for Hardware information
 *
 * Author: songqiang
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
#ifndef __ZT_HW_INFO_H__
#define __ZT_HW_INFO_H__

#define RXDESC_SIZE             24
#define MAX_CHANNEL_NUM         14
#define ZT_CCK_RATES_NUM        4
#define ZT_XMIT_AMPDU_DENSITY   2

typedef enum _REGULATION_TXPWR_LMT
{
    TXPWR_LMT_FCC = 0,
    TXPWR_LMT_MKK = 1,
    TXPWR_LMT_ETSI = 2,
    TXPWR_LMT_WW = 3,

    TXPWR_LMT_MAX_REGULATION_NUM = 4
} REGULATION_TXPWR_LMT;


typedef enum _CHANNEL_WIDTH
{
    CHANNEL_WIDTH_20 = 0,
    CHANNEL_WIDTH_40 = 1,
    CHANNEL_WIDTH_MAX = 2,
} CHANNEL_WIDTH;

typedef enum _HT_DATA_SC
{
    HT_DATA_SC_DONOT_CARE = 0,
    HT_DATA_SC_20_UPPER_OF_40MHZ = 1,
    HT_DATA_SC_20_LOWER_OF_40MHZ = 2,
} HT_DATA_SC;


typedef enum HAL_PRIME_CH_OFFSET_
{
    HAL_PRIME_CHNL_OFFSET_DONT_CARE = 0,
    HAL_PRIME_CHNL_OFFSET_LOWER     = 1,
    HAL_PRIME_CHNL_OFFSET_UPPER     = 2,
} HAL_PRIME_CH_OFFSET;


typedef enum _HT_CAP_AMPDU_FACTOR
{
    MAX_AMPDU_FACTOR_8K = 0,
    MAX_AMPDU_FACTOR_16K = 1,
    MAX_AMPDU_FACTOR_32K = 2,
    MAX_AMPDU_FACTOR_64K = 3,
} HT_CAP_AMPDU_FACTOR;

typedef enum _WP_CHANNEL_DOMAIN
{
    ZT_CHPLAN_FCC = 0x00,
    ZT_CHPLAN_IC = 0x01,
    ZT_CHPLAN_ETSI = 0x02,
    ZT_CHPLAN_SPAIN = 0x03,
    ZT_CHPLAN_FRANCE = 0x04,
    ZT_CHPLAN_MKK = 0x05,
    ZT_CHPLAN_MKK1 = 0x06,
    ZT_CHPLAN_ISRAEL = 0x07,
    ZT_CHPLAN_TELEC = 0x08,
    ZT_CHPLAN_GLOBAL_DOAMIN = 0x09,
    ZT_CHPLAN_WORLD_WIDE_13 = 0x0A,
    ZT_CHPLAN_TAIWAN = 0x0B,
    ZT_CHPLAN_CHINA = 0x0C,
    ZT_CHPLAN_SINGAPORE_INDIA_MEXICO = 0x0D,
    ZT_CHPLAN_KOREA = 0x0E,
    ZT_CHPLAN_TURKEY = 0x0F,
    ZT_CHPLAN_JAPAN = 0x10,
    ZT_CHPLAN_FCC_NO_DFS = 0x11,
    ZT_CHPLAN_JAPAN_NO_DFS = 0x12,
    ZT_CHPLAN_WORLD_WIDE_5G = 0x13,
    ZT_CHPLAN_TAIWAN_NO_DFS = 0x14,

    ZT_CHPLAN_WORLD_NULL = 0x20,
    ZT_CHPLAN_ETSI1_NULL = 0x21,
    ZT_CHPLAN_FCC1_NULL = 0x22,
    ZT_CHPLAN_MKK1_NULL = 0x23,
    ZT_CHPLAN_ETSI2_NULL = 0x24,
    ZT_CHPLAN_FCC1_FCC1 = 0x25,
    ZT_CHPLAN_WORLD_ETSI1 = 0x26,
    ZT_CHPLAN_MKK1_MKK1 = 0x27,
    ZT_CHPLAN_WORLD_KCC1 = 0x28,
    ZT_CHPLAN_WORLD_FCC2 = 0x29,
    ZT_CHPLAN_FCC2_NULL = 0x2A,
    ZT_CHPLAN_WORLD_FCC3 = 0x30,
    ZT_CHPLAN_WORLD_FCC4 = 0x31,
    ZT_CHPLAN_WORLD_FCC5 = 0x32,
    ZT_CHPLAN_WORLD_FCC6 = 0x33,
    ZT_CHPLAN_FCC1_FCC7 = 0x34,
    ZT_CHPLAN_WORLD_ETSI2 = 0x35,
    ZT_CHPLAN_WORLD_ETSI3 = 0x36,
    ZT_CHPLAN_MKK1_MKK2 = 0x37,
    ZT_CHPLAN_MKK1_MKK3 = 0x38,
    ZT_CHPLAN_FCC1_NCC1 = 0x39,
    ZT_CHPLAN_FCC1_NCC2 = 0x40,
    ZT_CHPLAN_GLOBAL_NULL = 0x41,
    ZT_CHPLAN_ETSI1_ETSI4 = 0x42,
    ZT_CHPLAN_FCC1_FCC2 = 0x43,
    ZT_CHPLAN_FCC1_NCC3 = 0x44,
    ZT_CHPLAN_WORLD_ETSI5 = 0x45,
    ZT_CHPLAN_FCC1_FCC8 = 0x46,
    ZT_CHPLAN_WORLD_ETSI6 = 0x47,
    ZT_CHPLAN_WORLD_ETSI7 = 0x48,
    ZT_CHPLAN_WORLD_ETSI8 = 0x49,
    ZT_CHPLAN_WORLD_ETSI9 = 0x50,
    ZT_CHPLAN_WORLD_ETSI10 = 0x51,
    ZT_CHPLAN_WORLD_ETSI11 = 0x52,
    ZT_CHPLAN_FCC1_NCC4 = 0x53,
    ZT_CHPLAN_WORLD_ETSI12 = 0x54,
    ZT_CHPLAN_FCC1_FCC9 = 0x55,
    ZT_CHPLAN_WORLD_ETSI13 = 0x56,
    ZT_CHPLAN_FCC1_FCC10 = 0x57,
    ZT_CHPLAN_MKK2_MKK4 = 0x58,
    ZT_CHPLAN_WORLD_ETSI14 = 0x59,
    ZT_CHPLAN_FCC1_FCC5 = 0x60,

    ZT_CHPLAN_MAX,
    ZT_CHPLAN_WK_WLAN_DEFINE = 0x7F,
} WP_CHANNEL_DOMAIN, *WTL_CHANNEL_DOMAIN;

typedef enum _WP_CHANNEL_DOMAIN_2G
{
    ZT_RD_2G_NULL = 0,
    ZT_RD_2G_WORLD = 1,
    ZT_RD_2G_ETSI1 = 2,
    ZT_RD_2G_FCC1 = 3,
    ZT_RD_2G_MKK1 = 4,
    ZT_RD_2G_ETSI2 = 5,
    ZT_RD_2G_GLOBAL = 6,
    ZT_RD_2G_MKK2 = 7,
    ZT_RD_2G_FCC2 = 8,

    ZT_RD_2G_MAX,
} WP_CHANNEL_DOMAIN_2G, *WTL_CHANNEL_DOMAIN_2G;

typedef struct
{
    zt_u8 channel[14];
    zt_u8 len;
} channel_plan_2g_t;

typedef struct
{
    zt_u8 index_2g;
    zt_u8 regd;
} channel_plan_map_t;

/* hw_info_st */
typedef struct
{
    zt_u32 qual; /* dbm */
    zt_u32 level; /* dbm */
    zt_u32 noise; /* dbm */
    zt_u32 updated;
} rf_quality_st;
typedef struct
{
    zt_u8 num; /* The channel number. */
    zt_s32 freq; /* channel frequence */
} channel_info_st;


typedef struct hw_register_st_
{
    zt_u32 rf_reg_chnl_val; //it could not used ,because it is zero in USB
    zt_u32 cam_invalid;
    zt_u8  channel;
} hw_register_st;

typedef struct wireless_info_st_
{
    /* iw_get_range */
    zt_u32              throughput; /* the maximum benchmarked TCP/IP throughput */
    rf_quality_st       max_qual; /* Quality of the link */
    rf_quality_st
    avg_qual; /* the average/typical values of the quality indicator */
    zt_u8               num_bitrates;
    zt_s32              min_frag; /* Max frag threshold */
    zt_s32              max_frag; /* Max frag threshold */
    zt_u16              pm_capa;
    channel_info_st    *pchannel_tab;
    zt_u16              num_channels; /* Number of channels [0; num - 1] */
    zt_u32              enc_capa; /* the security capability to network manager */
    zt_u8               scan_capa; /* Scan capabilities */
    zt_s32              bitrate[ZT_MAX_BITRATES];

} wireless_info_st;


/*important data stored in efuse*/
typedef struct efuse_data_st_
{
    zt_u16 id;
    zt_u16 vid;//vendor id
    zt_u16 pid;//product id
} efuse_data_st;

typedef enum
{
    SCAN_TYPE_PASSIVE,
    SCAN_TYPE_ACTIVE,

    SCAN_TYPE_MAX,
} scan_type_e;

typedef struct
{
    zt_u8 channel_num;
    scan_type_e scan_type;
} zt_channel_info_t;


typedef struct hardware_info_struct_
{
    zt_u8 mp_mode;   // unknown, but need use

    zt_u8 chip_version;
    zt_u8 macAddr[ZT_80211_MAC_ADDR_LEN];

    zt_bool       bautoload_flag;
    zt_bool       efuse_sel;
    zt_u8         efuse_data_map[ZT_EEPROM_MAX_SIZE];
    efuse_data_st efuse;
    zt_u8         efuse_read_flag;

    wireless_info_st *wireless_info;
    hw_register_st hw_reg;

    zt_u16 rts_thresh;
    zt_u16 frag_thresh;
    zt_u8  vcs_en;
    zt_u8  vcs_type;    //virtual carrier sense
    zt_u8  use_fixRate;
    zt_u8  fix_tx_rate;
    zt_u8  tx_data_rpt;
    zt_u8  ba_enable_tx;
    zt_u8  ba_enable_rx;
    zt_u8  dot80211n_support;
    zt_u8  cbw40_support;
    zt_u8  sm_ps_support; /* SM Power Save. 0(static mode) 1(dynamic) 3(disabled or not supported) */
    zt_u8  sm_ps_mode; /* SM Power Save */
    zt_u8  rf_type;
    zt_u8  wdn_sleep_support;
    zt_u8  ldpc_support;
    zt_u8  tx_stbc_support;
    zt_u8  rx_stbc_support;
    zt_u8  rx_stbc_num;
    zt_u32 rx_packet_offset;
    zt_u32 max_recvbuf_sz;
    zt_u8  max_rx_ampdu_factor;
    zt_u8  best_ampdu_density;
    zt_s32 UndecoratedSmoothedPWDB;
    zt_u8  channel_plan;
    zt_channel_info_t channel_set[MAX_CHANNEL_NUM];
    zt_u8  max_chan_nums;
    zt_u8  Regulation2_4G;

    zt_u8  datarate[ZT_RATES_NUM];
    zt_u8  default_supported_mcs_set[ZT_MCS_NUM];

    zt_u8  ars_policy;
} hw_info_st;


zt_s32 zt_hw_info_init(nic_info_st *nic_info);
zt_s32 zt_hw_info_get_default_cfg(nic_info_st *nic_info);
zt_s32 zt_hw_info_set_default_cfg(nic_info_st *nic_info);
zt_s32 zt_hw_info_term(nic_info_st *nic_info);
zt_s32 zt_hw_info_set_channel_bw(nic_info_st *nic_info, zt_u8 channel,
                                  CHANNEL_WIDTH cw, HAL_PRIME_CH_OFFSET offset);
zt_s32 zt_hw_info_get_channel_bw(nic_info_st *nic_info,
                                  zt_u8 *channel, CHANNEL_WIDTH *cw,
                                  HAL_PRIME_CH_OFFSET *offset);
zt_s32 zt_hw_info_get_channel_bw_ext(nic_info_st *nic_info, zt_u8 *ch, CHANNEL_WIDTH *bw,
                            HAL_PRIME_CH_OFFSET *offset);
zt_u8 do_query_center_ch(zt_u8 chnl_bw, zt_u8 channel, zt_u8 chnl_offset);
zt_s32 channel_init(nic_info_st *pnic_info);

#endif
