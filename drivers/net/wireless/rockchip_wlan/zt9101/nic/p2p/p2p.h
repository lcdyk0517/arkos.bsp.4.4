/*
 * p2p.h
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
#ifndef __ZT_P2P_H__
#define __ZT_P2P_H__

#include "p2p_proto_mgt.h"
#include "p2p_frame_mgt.h"
#include "p2p_wfd.h"
#include "p2p_wowlan.h"

#define to_str(x) #x

#define WPSOUI                      0x0050f204
/*	Value of WPS Version Attribute */
#define WPS_VERSION_1               0x10
/*	WPS attribute ID */
#define WPS_ATTR_VER1               0x104A
#define WPS_ATTR_SIMPLE_CONF_STATE  0x1044
#define WPS_ATTR_RESP_TYPE          0x103B
#define WPS_ATTR_UUID_E             0x1047
#define WPS_ATTR_MANUFACTURER       0x1021
#define WPS_ATTR_MODEL_NAME         0x1023
#define WPS_ATTR_MODEL_NUMBER       0x1024
#define WPS_ATTR_SERIAL_NUMBER      0x1042
#define WPS_ATTR_PRIMARY_DEV_TYPE   0x1054
#define WPS_ATTR_SEC_DEV_TYPE_LIST  0x1055
#define WPS_ATTR_DEVICE_NAME        0x1011
#define WPS_ATTR_CONF_METHOD        0x1008
#define WPS_ATTR_RF_BANDS           0x103C
#define WPS_ATTR_DEVICE_PWID        0x1012
#define WPS_ATTR_REQUEST_TYPE       0x103A
#define WPS_ATTR_ASSOCIATION_STATE  0x1002
#define WPS_ATTR_CONFIG_ERROR       0x1009
#define WPS_ATTR_VENDOR_EXT         0x1049
#define WPS_ATTR_SELECTED_REGISTRAR 0x1041

#define WPS_CONFIG_METHOD_DISPLAY   0x0008
#define WPS_CONFIG_METHOD_PBC       0x0080
#define WPS_CONFIG_METHOD_KEYPAD    0x0100

/*	Value of Category ID of WPS Primary Device Type Attribute */
#define WPS_PDT_CID_DISPLAYS        0x0007
#define WPS_PDT_CID_MULIT_MEDIA     0x0008
#define WPS_PDT_CID_TELEPHONE       0x000A
#define WPS_PDT_CID_RTK_WIDI        WPS_PDT_CID_MULIT_MEDIA

/*	Value of Sub Category ID of WPS Primary Device Type Attribute */
#define WPS_PDT_SCID_MEDIA_SERVER   0x0005
#define WPS_PDT_SCID_RTK_DMP        WPS_PDT_SCID_MEDIA_SERVER

/*	Value of WPS Response Type Attribute */
#define WPS_RESPONSE_TYPE_INFO_ONLY 0x00
#define WPS_RESPONSE_TYPE_8021X     0x01
#define WPS_RESPONSE_TYPE_REGISTRAR 0x02
#define WPS_RESPONSE_TYPE_AP        0x03

/*	Value of WPS WiFi Simple Configuration State Attribute */
#define WPS_WSC_STATE_NOT_CONFIG    0x01
#define WPS_WSC_STATE_CONFIG        0x02

#define P2POUI                          0x506F9A09
#define P2P_ATTR_STATUS                 0x00
#define P2P_ATTR_MINOR_REASON_CODE      0x01
#define P2P_ATTR_CAPABILITY             0x02
#define P2P_ATTR_DEVICE_ID              0x03
#define P2P_ATTR_GO_INTENT              0x04
#define P2P_ATTR_CONF_TIMEOUT           0x05
#define P2P_ATTR_LISTEN_CH              0x06
#define P2P_ATTR_GROUP_BSSID            0x07
#define P2P_ATTR_EX_LISTEN_TIMING       0x08
#define P2P_ATTR_INTENTED_IF_ADDR       0x09
#define P2P_ATTR_MANAGEABILITY          0x0A
#define P2P_ATTR_CH_LIST                0x0B
#define P2P_ATTR_NOA                    0x0C
#define P2P_ATTR_DEVICE_INFO            0x0D
#define P2P_ATTR_GROUP_INFO             0x0E
#define P2P_ATTR_GROUP_ID               0x0F
#define P2P_ATTR_INTERFACE              0x10
#define P2P_ATTR_OPERATING_CH           0x11
#define P2P_ATTR_INVITATION_FLAGS       0x12

#define P2P_STATUS_SUCCESS                      0x00
#define P2P_STATUS_FAIL_INFO_UNAVAILABLE        0x01
#define P2P_STATUS_FAIL_INCOMPATIBLE_PARAM      0x02
#define P2P_STATUS_FAIL_LIMIT_REACHED           0x03
#define P2P_STATUS_FAIL_INVALID_PARAM           0x04
#define P2P_STATUS_FAIL_REQUEST_UNABLE          0x05
#define P2P_STATUS_FAIL_PREVOUS_PROTO_ERR       0x06
#define P2P_STATUS_FAIL_NO_COMMON_CH            0x07
#define P2P_STATUS_FAIL_UNKNOWN_P2PGROUP        0x08
#define P2P_STATUS_FAIL_BOTH_GOINTENT_15        0x09
#define P2P_STATUS_FAIL_INCOMPATIBLE_PROVSION   0x0A
#define P2P_STATUS_FAIL_USER_REJECT             0x0B

/*	Value of Inviation Flags Attribute */
#define	P2P_INVITATION_FLAGS_PERSISTENT         ZT_BIT(0)

#define	DMP_P2P_DEVCAP_SUPPORT	(P2P_DEVCAP_SERVICE_DISCOVERY | \
                 P2P_DEVCAP_INVITATION_PROC)

#define	DMP_P2P_GRPCAP_SUPPORT	(P2P_GRPCAP_INTRABSS)

/*	Value of Device Capability Bitmap */
#define	P2P_DEVCAP_SERVICE_DISCOVERY        ZT_BIT(0)
#define	P2P_DEVCAP_CLIENT_DISCOVERABILITY   ZT_BIT(1)
#define	P2P_DEVCAP_CONCURRENT_OPERATION     ZT_BIT(2)
#define	P2P_DEVCAP_INFRA_MANAGED            ZT_BIT(3)
#define	P2P_DEVCAP_DEVICE_LIMIT             ZT_BIT(4)
#define	P2P_DEVCAP_INVITATION_PROC          ZT_BIT(5)

/*	Value of Group Capability Bitmap */
#define	P2P_GRPCAP_GO                       ZT_BIT(0)
#define	P2P_GRPCAP_PERSISTENT_GROUP         ZT_BIT(1)
#define	P2P_GRPCAP_GROUP_LIMIT              ZT_BIT(2)
#define	P2P_GRPCAP_INTRABSS                 ZT_BIT(3)
#define	P2P_GRPCAP_CROSS_CONN               ZT_BIT(4)
#define	P2P_GRPCAP_PERSISTENT_RECONN        ZT_BIT(5)
#define	P2P_GRPCAP_GROUP_FORMATION          ZT_BIT(6)

#define P2P_GO_NEGO_REQ         0
#define P2P_GO_NEGO_RESP        1
#define P2P_GO_NEGO_CONF        2
#define P2P_INVIT_REQ           3
#define P2P_INVIT_RESP          4
#define P2P_DEVDISC_REQ         5
#define P2P_DEVDISC_RESP        6
#define P2P_PROVISION_DISC_REQ  7
#define P2P_PROVISION_DISC_RESP  8

#define P2P_NOTICE_OF_ABSENCE   0
#define P2P_PRESENCE_REQUEST    1
#define P2P_PRESENCE_RESPONSE   2
#define P2P_GO_DISC_REQUEST     3

#define P2P_WILDCARD_SSID_LEN   7
#define P2P_SSID_LEN           (32)

#define WPS_CM_PUSH_BUTTON      0x0080

typedef enum P2P_ROLE
{
    P2P_ROLE_DISABLE = 0,
    P2P_ROLE_DEVICE = 1,
    P2P_ROLE_CLIENT = 2,
    P2P_ROLE_GO = 3
} P2P_ROLE;


typedef enum P2P_STATE
{
    P2P_STATE_NONE = 0,
    P2P_STATE_IDLE = 1,
    P2P_STATE_LISTEN = 2,
    P2P_STATE_SCAN = 3,
    P2P_STATE_FIND_PHASE_LISTEN = 4,
    P2P_STATE_FIND_PHASE_SEARCH = 5,
    P2P_STATE_TX_PROVISION_DIS_REQ = 6,
    P2P_STATE_RX_PROVISION_DIS_RSP = 7,
    P2P_STATE_RX_PROVISION_DIS_REQ = 8,
    P2P_STATE_GONEGO_ING = 9,
    P2P_STATE_GONEGO_OK = 10,
    P2P_STATE_GONEGO_FAIL = 11,
    P2P_STATE_RECV_INVITE_REQ_MATCH = 12,
    P2P_STATE_PROVISIONING_ING = 13,
    P2P_STATE_PROVISIONING_DONE = 14,
    P2P_STATE_TX_INVITE_REQ = 15,
    P2P_STATE_RX_INVITE_RESP_OK = 16,
    P2P_STATE_RECV_INVITE_REQ_DISMATCH = 17,
    P2P_STATE_RECV_INVITE_REQ_GO = 18,
    P2P_STATE_RECV_INVITE_REQ_JOIN = 19,
    P2P_STATE_RX_INVITE_RESP_FAIL = 20,
    P2P_STATE_RX_INFOR_NOREADY = 21,
    P2P_STATE_TX_INFOR_NOREADY = 22,
    P2P_STATE_EAPOL_DONE = 23,
} P2P_STATE;

typedef zt_s32(*sys_priv_callback)(void *nic_info, void *param,
                                   zt_u32 param_len);
typedef struct
{
    zt_bool init_flag;
    sys_priv_callback remain_on_channel_expired;
    sys_priv_callback rx_mgmt;
    sys_priv_callback ready_on_channel;
} p2p_sys_cb_st;

typedef struct
{
    zt_timer_t remain_on_ch_timer;
} p2p_timer_st;

#define P2P_IE_BUF_LEN (1024)
typedef struct p2p_info_st_
{
    void *nic_info;
    p2p_timer_st p2p_timers;

    enum P2P_ROLE role;
    enum P2P_STATE pre_p2p_state;
    enum P2P_STATE p2p_state;
    zt_u8 p2p_wildcard_ssid[P2P_WILDCARD_SSID_LEN + 1];
    zt_u8 p2p_device_ssid[ZT_80211_MAX_SSID_LEN + 1];
    zt_u8 p2p_device_ssid_len;
    zt_u8 p2p_scan_ssid[P2P_SSID_LEN];
    zt_u8 p2p_support_rate[8];
    zt_u8 p2p_group_ssid[ZT_80211_MAX_SSID_LEN];
    zt_u8 p2p_group_ssid_len;
    zt_u8 device_addr[ZT_80211_MAC_ADDR_LEN];
    zt_u8 interface_addr[ZT_80211_MAC_ADDR_LEN];
    zt_u8 p2p_uuid[16];
	zt_u8 ext_channel_num;
    zt_u8 social_channel[13];
    zt_u8 listen_channel;
    zt_u8 peer_listen_channel;
    zt_u8 link_channel;
    zt_u8 action;
    zt_u8 intent;
    zt_u16 ext_listen_interval;
    zt_u16 supported_wps_cm;
    zt_u16 report_mgmt;
    zt_u8  report_ch;
    zt_u8 provdisc_req_issued;

    zt_u8 scan_times;

    zt_widev_nego_info_t nego_info;
    zt_widev_invit_info_t invit_info;

    zt_bool is_ro_ch;
    zt_u8 ro_ch_again;
    zt_u32 last_ro_ch_time;
    zt_u8 remain_ch;
    zt_u32 ro_ch_duration;
    zt_u32 go_negoing;
    zt_u8 scan_deny;
    zt_os_api_timer_t nego_timer;
    zt_u8 nego_timer_flag;
    zt_os_api_timer_t remain_ch_timer;

    /*wfd*/
    zt_u8 session_available;
    zt_u8 stack_wfd_mode;
    wfd_info_st wfd_info;

    //p2p_wd_info_st wdinfo;

    zt_u8 *p2p_ie[ZT_P2P_IE_MAX];
    zt_u32 p2p_ie_len[ZT_P2P_IE_MAX];

    zt_u8 p2p_enabled;
    zt_u8 mgnt_tx_rate;

    zt_bool full_ch_in_p2p_handshake; //copy from registrypriv.full_ch_in_p2p_handshake

    /*call back function*/
    p2p_sys_cb_st scb;
} p2p_info_st;

zt_s32 zt_p2p_init(nic_info_st *nic_info);
zt_s32 zt_p2p_term(nic_info_st *nic_info);

zt_s32 zt_p2p_enable(nic_info_st *nic_info, P2P_ROLE role);
zt_s32 zt_p2p_disable(nic_info_st *nic_info);

zt_s32 zt_p2p_suspend(nic_info_st *nic_info);
zt_s32 zt_p2p_resume(nic_info_st *nic_info);

zt_bool zt_p2p_check_buddy_linkstate(nic_info_st *nic_info);
zt_u8 zt_p2p_get_buddy_channel(nic_info_st *pnic_info);

void zt_p2p_set_role(p2p_info_st *p2p_info, enum P2P_ROLE role);
P2P_ROLE p2p_get_role(p2p_info_st *p2p_info);
void zt_p2p_set_state(p2p_info_st *p2p_info, enum P2P_STATE state);
void zt_p2p_set_pre_state(p2p_info_st *p2p_info, enum P2P_STATE state);
zt_s32 zt_p2p_dump_attrs(zt_u8 *p2p_ie, zt_u32 p2p_ielen);

zt_s8 *zt_p2p_state_to_str(P2P_STATE state);
zt_s8 *zt_p2p_role_to_str(P2P_ROLE role);

zt_bool zt_p2p_is_valid(nic_info_st *nic_info);
zt_s32 zt_p2p_scan_entry(nic_info_st *nic_info, zt_u8 social_channel,
                         zt_u8 *ies, zt_s32 ieslen);
zt_s32 zt_p2p_scan_rsp_entry(nic_info_st *pnic_info, zt_80211_mgmt_t *pmgmt,
                             zt_u16 mgmt_len);
zt_s32 zt_p2p_connect_entry(nic_info_st *pnic_info, zt_u8 *ie, zt_u32 ie_len);
zt_s32 zt_p2p_parse_ie(nic_info_st *nic_info, zt_u8 *buf, zt_s32 len,
                       zt_s32 type);
zt_s32 zt_p2p_reset(nic_info_st *pnic_info);

#endif
