/*
 * lps.h
 *
 * used for low power saving
 *
 * Author: houchuang
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
#ifndef __LPS_H__
#define __LPS_H__

#ifdef CONFIG_LPS
/***************************************************************
    Define
***************************************************************/
#define LPS_DELAY_TIME             1*ZT_HZ

#define TIMESTAMPE                 0
#define BCN_INTERVAL               1
#define CAPABILITY                 2

#define BCNQ_PAGE_NUM        0x08
#define BCNQ1_PAGE_NUM       0x08

#ifdef CONFIG_WOWLAN
#define WOWLAN_PAGE_NUM      0x07
#else
#define WOWLAN_PAGE_NUM      0x00
#endif

#define TX_TOTAL_PAGE_NUMBER      (0xFF - BCNQ_PAGE_NUM - BCNQ1_PAGE_NUM - WOWLAN_PAGE_NUM)
#define TX_PAGE_BOUNDARY_9086X          (TX_TOTAL_PAGE_NUMBER + 1)

#define PageNum(_Len, _Size)            (zt_u32)(((_Len)/(_Size)) + ((_Len)&((_Size) - 1) ? 1:0))

/***************************************************************
    Typedef
***************************************************************/
enum LPS_CTRL_TYPE
{
    LPS_CTRL_SCAN = 0,
    LPS_CTRL_JOINBSS = 1,
    LPS_CTRL_CONNECT = 2,
    LPS_CTRL_DISCONNECT = 3,
    LPS_CTRL_SPECIAL_PACKET = 4,
    LPS_CTRL_LEAVE = 5,
    LPS_CTRL_TRAFFIC_BUSY = 6,
    LPS_CTRL_TX_TRAFFIC_LEAVE = 7,
    LPS_CTRL_RX_TRAFFIC_LEAVE = 8,
    LPS_CTRL_ENTER = 9,
    LPS_CTRL_LEAVE_CFG80211_PWRMGMT = 10,
    LPS_CTRL_NO_LINKED = 11,
    LPS_CTRL_MAX = 12
};

enum Power_Mgnt
{
    PWR_MODE_ACTIVE = 0,
    PWR_MODE_MIN,
    PWR_MODE_MAX,
    PWR_MODE_DTIM,
    PWR_MODE_VOIP,
    PWR_MODE_UAPSD_WMM,
    PWR_MODE_UAPSD,
    PWR_MODE_IBSS,
    PWR_MODE_WWLAN,
    PWR_Radio_Off,
    PWR_Card_Disable,
    PWR_MODE_NUM,
};

typedef struct _lps_rsvdpage
{
    zt_u8 lps_probe_rsp;
    zt_u8 lps_poll;
    zt_u8 lps_null_data;
    zt_u8 lps_qos_data;
    zt_u8 lps_bt_qos;
    zt_u8 lps_offload_bcn;
} lps_rsvdpage, *lps_prsvdpage;

typedef struct
{
    zt_u8 lps_ctrl_type;
} mlme_lps_t;
#endif

// For nic_info->pwr_info
typedef struct pwr_info
{
    zt_bool bInSuspend;
    zt_u8 dtim;
#ifdef CONFIG_WOWLAN

#endif
#ifdef CONFIG_LPS
    zt_os_api_sema_t lock;
    zt_os_api_sema_t check_32k_lock;
    zt_u32 lps_enter_cnts;
    zt_u32 lps_exit_cnts;
    zt_u8 lps_idle_cnts;
    zt_u8 pwr_mgnt;
    zt_u8 pwr_current_mode;
    volatile zt_u8 rpwm;
    zt_bool b_fw_current_in_ps_mode;
    zt_u8 smart_lps;
    zt_u64 delay_lps_last_timestamp;
    zt_u32 lps_deny;
    zt_bool b_power_saving;
    zt_bool b_mailbox_sync;
    zt_timer_t lps_timer;
#endif
} pwr_info_st;

/***************************************************************
    Function Declare
***************************************************************/
#ifdef CONFIG_LPS
void zt_lps_ctrl_wk_hdl(nic_info_st *pnic_info, zt_u8 lps_ctrl_type);
zt_u32 zt_lps_wakeup(nic_info_st *pnic_info, zt_u8 lps_ctrl_type,
                     zt_bool enqueue);
zt_u32 zt_lps_sleep(nic_info_st *pnic_info, zt_u8 lps_ctrl_type,
                    zt_bool enqueue);
zt_pt_ret_t zt_lps_sleep_mlme_monitor(zt_pt_t *pt, nic_info_st *pnic_info);
void zt_lps_ctrl_state_hdl(nic_info_st *pnic_info, zt_u8 lps_ctrl_type);
#endif
zt_s32 zt_lps_init(nic_info_st *pnic_info);
zt_s32 zt_lps_term(nic_info_st *pnic_info);

#endif


