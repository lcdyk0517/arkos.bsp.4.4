/*
 * p2p_frame_mgt.h
 *
 * used for p2p
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
#ifndef __P2P_FRAME_MGT_H__
#define __P2P_FRAME_MGT_H__

#define MAX_IE_SZ   768
#define PROBE_REQUEST_IE_SSID       0
#define PROBE_REQUEST_IE_RATE       1


typedef enum
{
    ZT_P2P_IE_BEACON        = 0,
    ZT_P2P_IE_PROBE_REQ     = 1,
    ZT_P2P_IE_PROBE_RSP     = 2,
    ZT_P2P_IE_ASSOC_REQ     = 3,
    ZT_P2P_IE_ASSOC_RSP     = 4,
    ZT_P2P_IE_MAX,
} ZT_P2P_IE_E;

typedef struct zt_widev_invit_info
{
    zt_u8 state;
    zt_u8 peer_mac[ZT_80211_MAC_ADDR_LEN];
    zt_u8 active;
    zt_u8 token;
    zt_u8 flags;
    zt_u8 status;
    zt_u8 req_op_ch;
    zt_u8 rsp_op_ch;
} zt_widev_invit_info_t;

#define zt_widev_invit_info_init(invit_info) \
    do { \
        (invit_info)->state = 0xff; \
        zt_memset((invit_info)->peer_mac, 0, ZT_80211_MAC_ADDR_LEN); \
        (invit_info)->active = 0xff; \
        (invit_info)->token = 0; \
        (invit_info)->flags = 0x00; \
        (invit_info)->status = 0xff; \
        (invit_info)->req_op_ch = 0; \
        (invit_info)->rsp_op_ch = 0; \
    } while (0)

typedef struct zt_widev_nego_info
{
    zt_u8 state;
    zt_u8 peer_mac[ZT_80211_MAC_ADDR_LEN];
    zt_u8 active;
    zt_u8 token;
    zt_u8 status;
    zt_u8 req_intent;
    zt_u8 req_op_ch;
    zt_u8 req_listen_ch;
    zt_u8 rsp_intent;
    zt_u8 rsp_op_ch;
    zt_u8 conf_op_ch;
} zt_widev_nego_info_t;

#define zt_widev_nego_info_init(nego_info) \
    do { \
        (nego_info)->state = 0xff; \
        zt_memset((nego_info)->peer_mac, 0, ZT_80211_MAC_ADDR_LEN); \
        (nego_info)->active = 0xff; \
        (nego_info)->token = 0; \
        (nego_info)->status = 0xff; \
        (nego_info)->req_intent = 0xff; \
        (nego_info)->req_op_ch = 0; \
        (nego_info)->req_listen_ch = 0; \
        (nego_info)->rsp_intent = 0xff; \
        (nego_info)->rsp_op_ch = 0; \
        (nego_info)->conf_op_ch = 0; \
    } while (0)


typedef struct p2p_frame_check_param_st_
{
    zt_u8 *buf;
    zt_u32 len;

    zt_u8 *frame_body;
    zt_u32 frame_body_len;
    zt_u8 *p2p_ie;
    zt_u32 p2p_ielen;
    zt_u8 dialogToken;
    zt_bool is_tx;
} p2p_frame_check_param_st;

zt_u8 *zt_p2p_get_ie(zt_u8 *in_ie, zt_s32 in_len, zt_u8 *p2p_ie,
                     zt_u32 *p2p_ielen);
zt_s32 zt_p2p_send_probereq(nic_info_st *nic_info, zt_u8 *da);
zt_u8 *zt_p2p_get_attr_content(zt_u8 *p2p_ie, zt_u32 p2p_ielen,
                               zt_u8 target_attr_id, zt_u8 *buf_content, zt_u32 *len_content);
zt_s32 zt_p2p_recv_probereq(nic_info_st *pnic_info, zt_80211_mgmt_t *pframe,
                            zt_u16 frame_len);
zt_s32 zt_p2p_check_frames(nic_info_st *nic_info, const zt_u8 *buf, zt_u32 len,
                           zt_bool tx, zt_u8 flag);
zt_s32 zt_p2p_recv_public_action(nic_info_st *pnic_info, zt_u8 *pframe,
                                 zt_u16 frame_len);
zt_s32 zt_p2p_fill_assoc_rsp(nic_info_st *pnic_info, zt_u8 *pframe,
                             zt_u16 *pkt_len, ZT_P2P_IE_E pie_type);
zt_u8 *zt_p2p_fill_assoc_req(nic_info_st *pnic_info, zt_u8 *pframe,
                             zt_u32 *pkt_len, ZT_P2P_IE_E pie_type);
zt_s32 zt_p2p_parse_p2pie(nic_info_st *pnic_info, void *p2p, zt_u16 len,
                          ZT_P2P_IE_E ie_type);
zt_s32 zt_p2p_proc_assoc_req(nic_info_st *pnic_info, zt_u8 *p2p_ie,
                             zt_u32 p2p_ielen, wdn_net_info_st *pwdn_info, zt_u8 flag);
zt_u8 *zt_p2p_ie_to_str(ZT_P2P_IE_E ie_type);
zt_s32 zt_p2p_rx_action_precess(nic_info_st *pnic_info, zt_u8 *frame,
                                zt_u32 len);
zt_s32 zt_p2p_tx_action_process(nic_info_st *pnic_info, zt_u8 *frame,
                                zt_u32 len,
                                zt_u8 ch, zt_u8 wait_ack);

#endif
