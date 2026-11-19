/*
 * zt_os_api_mix.h
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
#ifndef __ZT_OS_API_MIX_H__
#define __ZT_OS_API_MIX_H__

#define zt_le16_to_cpu            le16_to_cpu
#define zt_cpu_to_le16            cpu_to_le16
#define zt_be16_to_cpu            be16_to_cpu
#define zt_cpu_to_be16            cpu_to_be16

#define zt_le32_to_cpu            le32_to_cpu
#define zt_cpu_to_le32            cpu_to_le32
#define zt_be32_to_cpu            be32_to_cpu
#define zt_cpu_to_be32            cpu_to_be32

#define zt_le64_to_cpu            le64_to_cpu
#define zt_cpu_to_le64            cpu_to_le64
#define zt_be64_to_cpu            be64_to_cpu
#define zt_cpu_to_be64            cpu_to_be64

void zt_os_api_ind_scan_done(void *arg, zt_bool arg1, zt_u8 arg2);
void zt_os_api_ind_connect(void *arg, zt_u8 arg1);
void zt_os_api_ind_disconnect(void *arg, zt_u8 arg1);
#ifdef CFG_ENABLE_ADHOC_MODE
void zt_os_api_cfg80211_unlink_ibss(void *arg);
#endif
#ifdef CFG_ENABLE_AP_MODE
void zt_os_api_ap_ind_assoc(void *arg, void *arg1, void *arg2, zt_u8 arg3);
void zt_os_api_ap_ind_disassoc(void *arg, void *arg1, zt_u8 arg2);
#endif
void zt_os_api_enable_all_data_queue(void *arg);
void zt_os_api_disable_all_data_queue(void *arg);
zt_u32 zt_os_api_rand32(void);

zt_s32 zt_os_api_get_cpu_id(void);
#endif

