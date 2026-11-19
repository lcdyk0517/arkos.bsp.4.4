/*
 * ie.h
 *
 * This file contains all the prototypes for the ie.c file
 *
 * Author: luozhi
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
#ifndef __IE_H__
#define __IE_H__
#define MCS_RATE_1R (0x000000ff)


zt_s32 zt_ie_cap_info_update(nic_info_st *nic_info, wdn_net_info_st *wdn_info,
                             zt_u16 cap_info);
zt_s32 zt_ie_ssid_update(nic_info_st *nic_info, wdn_net_info_st *wdn_info,
                         zt_u8 *pie_data, zt_u8 len);
zt_s32 zt_ie_supported_rates_update(nic_info_st *nic_info,
                                    wdn_net_info_st *wdn_info, zt_u8 *pie_data, zt_u8 len);
zt_s32 zt_ie_extend_supported_rates_update(nic_info_st *nic_info,
        wdn_net_info_st *wdn_info, zt_u8 *pie_data, zt_u8 len);
zt_s32 zt_ie_wmm_update(nic_info_st *nic_info, wdn_net_info_st *wdn_info,
                        zt_u8 *pie_data, zt_u8 len);
zt_s32 zt_ie_wpa_update(nic_info_st *nic_info, wdn_net_info_st *wdn_info,
                        zt_u8 *pie_data, zt_u8 len);
zt_s32 zt_ie_ht_capability_update(nic_info_st *nic_info,
                                  wdn_net_info_st *wdn_info,
                                  zt_u8 *pie_data, zt_u8 len);
zt_s32 zt_ie_ht_operation_info_update(nic_info_st *nic_info,
                                      wdn_net_info_st *wdn_info, zt_u8 *pie_data, zt_u8 len);
zt_s32 zt_ie_erp_update(nic_info_st *nic_info, wdn_net_info_st *wdn_info,
                        zt_u8 *pie_data, zt_u8 len);
zt_s32 zt_ie_rsn_update(nic_info_st *nic_info, wdn_net_info_st *wdn_info,
                        zt_u8 *pie_data, zt_u8 len);
zt_s32 only_cckrates(zt_u8 *rate, zt_s32 ratelen);
zt_s32 have_cckrates(zt_u8 *rate, zt_s32 ratelen);

zt_u8 *set_ie(zt_u8 *pbuf, zt_u8 index, zt_u8 len, zt_u8 *source,
              zt_u32 *frlen);
zt_u8 *set_fixed_ie(zt_u8 *pbuf, zt_u32 len, zt_u8 *source, zt_u16 *frlen);


#endif

