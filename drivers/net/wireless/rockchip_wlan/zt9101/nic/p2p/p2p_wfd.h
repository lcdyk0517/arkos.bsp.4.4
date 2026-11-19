/*
 * p2p_wfd.h
 *
 * used for wfd
 *
 * Author: kanglin
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
#ifndef __P2P_WFD_H__
#define __P2P_WFD_H__

#define MAX_WFD_IE_LEN (128)
#define WFD_ATTR_DEVICE_INFO        0x00
#define WFD_ATTR_ASSOC_BSSID        0x01
#define WFD_ATTR_COUPLED_SINK_INFO  0x06
#define WFD_ATTR_LOCAL_IP_ADDR      0x08
#define WFD_ATTR_SESSION_INFO       0x09
#define WFD_ATTR_ALTER_MAC          0x0a

#define WFD_DEVINFO_SOURCE          0x0000
#define WFD_DEVINFO_PSINK           0x0001
#define WFD_DEVINFO_DUAL            0x0003

#define WFD_DEVINFO_SESSION_AVAIL   0x0010
#define WFD_DEVINFO_WSD             0x0040
#define WFD_DEVINFO_PC_TDLS         0x0080
#define WFD_DEVINFO_HDCP_SUPPORT    0x0100
#define WFD_DEVINFO_TDLS_PRE_GREP   0x1000

typedef enum
{
    ZT_WFD_IE_BEACON        = 0,
    ZT_WFD_IE_PROBE_REQ     = 1,
    ZT_WFD_IE_PROBE_RSP     = 2,
    ZT_WFD_IE_GO_PROBE_RSP  = 3,
    ZT_WFD_IE_ASSOC_REQ     = 4,
    ZT_WFD_IE_ASSOC_RSP     = 5,
    ZT_WFD_IE_MAX,
} ZT_WFD_IE_E;


enum SCAN_RESULT_TYPE
{
    SCAN_RESULT_P2P_ONLY = 0,
    SCAN_RESULT_ALL = 1,
    SCAN_RESULT_WFD_TYPE = 2
};

typedef struct wifi_display_info_
{
    zt_u16 wfd_enable;
    zt_u16 init_rtsp_ctrlport;
    zt_u16 rtsp_ctrlport;
    zt_u16 peer_rtsp_ctrlport;

    zt_u8 peer_session_avail;
    zt_u8 ip_address[4];
    zt_u8 peer_ip_address[4];
    zt_u8 wfd_pc;

    zt_u8 wfd_device_type;
    enum SCAN_RESULT_TYPE scan_result_type;
    zt_u8 op_wfd_mode;
    zt_u8 stack_wfd_mode;

    zt_u8 *wfd_ie[ZT_WFD_IE_MAX];
    zt_u32 wfd_ie_len[ZT_WFD_IE_MAX];
} wfd_info_st;

void zt_p2p_wfd_enable(nic_info_st *pnic_info, zt_bool on);
void zt_p2p_wfd_set_ctrl_port(nic_info_st *pnic_info, zt_u16 port);
zt_s32 zt_p2p_wfd_init(nic_info_st *pnic_info, zt_u8 flag);
zt_u8 *zt_p2p_wfd_get_ie(zt_u8 flag, zt_u8 *in_ie, zt_s32 in_len, zt_u8 *wfd_ie,
                         zt_u32 *wfd_ielen);
zt_u32 zt_p2p_wfd_append_probe_req_ie(nic_info_st *pnic_info, zt_u8 *pbuf,
                                      zt_u8 flag);
zt_u32 zt_p2p_wfd_append_probe_resp_ie(nic_info_st *pnic_info, zt_u8 *pbuf,
                                       zt_u8 flag);
zt_u32 zt_p2p_wfd_append_assoc_req_ie(nic_info_st *pnic_info, zt_u8 *pbuf,
                                      zt_u8 flag);
zt_u32 zt_p2p_wfd_append_assoc_resp_ie(nic_info_st *pnic_info, zt_u8 *pbuf,
                                       zt_u8 flag);
zt_s32 zt_p2p_wfd_update_ie(nic_info_st *pnic_info, ZT_WFD_IE_E ie_type,
                            zt_u8 *ie, zt_u32 ie_len, zt_u8 tag);
zt_bool zt_p2p_wfd_is_valid(nic_info_st *pnic_info);

#endif
