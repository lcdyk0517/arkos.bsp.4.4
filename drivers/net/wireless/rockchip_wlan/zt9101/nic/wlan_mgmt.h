/*
 * wlan_mgmt.h
 *
 * This file contains all the prototypes for the wlan_mgmt.c file
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
#ifndef __WLAN_MGMT_H__
#define __WLAN_MGMT_H__

/* macro */
#define WLAN_HDR_A3_QOS_LEN             26
#define WLAN_HDR_A3_LEN                 24

/*
 * pscan_que_node   point to type of zt_wlan_mgmt_scan_que_node_t
 * pnic_info        point to type of nic_info_st
 * rst              point to type of zt_wlan_mgmt_scan_que_for_rst_e,
 *                  indicate result.
 */
#define zt_wlan_mgmt_param_chk(para, type)\
    do\
    {\
        type tmp;\
        (void)(&tmp == &para);\
    }\
    while (0)

#define _pscan_que(pnic_info)\
    (&((zt_wlan_mgmt_info_t *)(pnic_info)->wlan_mgmt_info)->scan_que)

#define zt_wlan_mgmt_scan_que_for_begin(pnic_info, pscan_que_node)\
    do\
    {\
        zt_que_list_t *_pos = NULL, *_phead = NULL; pscan_que_node = NULL;\
        zt_wlan_mgmt_param_chk(pnic_info, nic_info_st *);\
        zt_wlan_mgmt_param_chk(pscan_que_node, zt_wlan_mgmt_scan_que_node_t *);\
        if ((pnic_info) && !ZT_CANNOT_RUN(pnic_info) &&\
                !zt_wlan_mgmt_scan_que_read_try(_pscan_que(pnic_info)))\
        {\
            nic_info_st *_pnic_info = pnic_info;\
            _phead = zt_que_list_head(&_pscan_que(pnic_info)->ready);\
            zt_list_for_each(_pos, _phead)\
            {\
                /* get scan queue node point */\
                (pscan_que_node) =\
                                  (void *)zt_list_entry(_pos, zt_wlan_mgmt_scan_que_node_t, list);\
                /* { here include user code ... } */
#define zt_wlan_mgmt_scan_que_for_end(rst)\
    }\
    zt_wlan_mgmt_param_chk(rst, zt_wlan_mgmt_scan_que_for_rst_e);\
    zt_wlan_mgmt_scan_que_read_post(_pscan_que(_pnic_info));\
    }\
    rst = (_phead == NULL) ? ZT_WLAN_MGMT_SCAN_QUE_FOR_RST_FAIL :\
          (_pos == _phead) ? ZT_WLAN_MGMT_SCAN_QUE_FOR_RST_END :\
          ZT_WLAN_MGMT_SCAN_QUE_FOR_RST_BREAK;\
    }\
    while (0)

/* type define */
typedef struct
{
    zt_u8 length;
    zt_80211_mgmt_ssid_t data;
} zt_wlan_ssid_t;
typedef zt_s8 zt_wlan_rssi_t;
typedef zt_u8 zt_wlan_signal_strength_t;
typedef zt_u8 zt_wlan_signal_qual_t;

typedef enum
{
    ZT_WLAN_BSS_NAME_IEEE80211_A,
    ZT_WLAN_BSS_NAME_IEEE80211_B,
    ZT_WLAN_BSS_NAME_IEEE80211_G,
    ZT_WLAN_BSS_NAME_IEEE80211_BG,
    ZT_WLAN_BSS_NAME_IEEE80211_AN,
    ZT_WLAN_BSS_NAME_IEEE80211_BN,
    ZT_WLAN_BSS_NAME_IEEE80211_GN,
    ZT_WLAN_BSS_NAME_IEEE80211_BGN,
} zt_wlan_bss_name_e;
typedef enum
{
    ZT_WLAN_OPR_MODE_ADHOC  = 1,
    ZT_WLAN_OPR_MODE_MASTER = 3,
    ZT_WLAN_OPR_MODE_MESH   = 7,
} zt_wlan_operation_mode_e;
typedef struct
{
    zt_que_list_t list;
    zt_timer_t ttl; /* use to indcate this node time to live */

    zt_wlan_signal_strength_t signal_strength, signal_strength_scale;
    zt_wlan_signal_qual_t signal_qual;
    zt_80211_bssid_t bssid;
    zt_wlan_ssid_t ssid;
    zt_80211_hidden_ssid_e ssid_type;
    zt_u8 channel;
    zt_u8 spot_rate[16];
    zt_wlan_operation_mode_e opr_mode;
    zt_bool cap_privacy;
    zt_wlan_bss_name_e name;
    zt_bool ht_cap_en;
    zt_bool bw_40mhz;
    zt_bool short_gi;
    zt_u16 mcs;
    zt_u8 wpa_ie[64];
    zt_u8 wps_ie[64];
    zt_u8 rsn_ie[64];
    zt_u32 wpa_multicast_cipher;
    zt_u32 wpa_unicast_cipher;
    zt_u32 rsn_group_cipher;
    zt_u32 rsn_pairwise_cipher;
    zt_80211_frame_e frame_type;
    zt_u16 ie_len;
    zt_u8 ies[ZT_80211_IES_SIZE_MAX];
} zt_wlan_mgmt_scan_que_node_t;

typedef struct
{
    zt_que_t free, ready;
    zt_os_api_lock_t lock;
    zt_u8 read_cnt;
    zt_os_api_sema_t sema;
} zt_wlan_mgmt_scan_que_t;

typedef struct
{
    zt_u8 ACI;
    zt_u8 ECW;
    zt_u16 TXOP_limit;
} zt_wmm_ac_st;

typedef struct
{
    zt_u16 OUI;
    zt_u8  OUI_pandding;
    zt_u8  type_sub;
    zt_u8  version;
    zt_u8  qos_info;
    zt_u8 reserved;
    zt_wmm_ac_st ac[4];
} zt_wmm_para_st;

typedef struct
{
    zt_u8 primary_channel;
    zt_u8 infos[5];
    zt_u8 MCS_rate[16];
} zt_wlan_ht_op_info_st;

typedef struct
{    
    zt_u32 tid;
    zt_u32 start_seq;
    zt_u16 param;
    zt_u16 timeout;
    zt_u8 policy;
    zt_u8 addr[ZT_80211_MAC_ADDR_LEN];
    zt_u16 status;
    zt_u8 size;
    zt_u8 dialog;
} zt_add_ba_parm_st;

typedef struct
{
    zt_wlan_rssi_t          rssi;
    zt_80211_addr_t         mac_addr;
    zt_wlan_ssid_t          ssid;
    zt_80211_bssid_t        bssid;
    zt_u8                   channel;
    CHANNEL_WIDTH           bw;
    zt_u64                  timestamp;
    zt_u16                  bcn_interval;
    zt_80211_mgmt_capab_t   cap_info;
    zt_bool                 cap_privacy;
    zt_s32                  aid;
    zt_u32                  ies_length;
    zt_u8                   rate_len;
    zt_u8                   rate[16];
    zt_u8                   short_slot;
    zt_u8                   ies[ZT_80211_IES_SIZE_MAX];
    zt_u32                  join_res;
    zt_bool                 ht_enable;
    zt_u32                  wpa_multicast_cipher;
    zt_u32                  wpa_unicast_cipher;
    zt_u32                  rsn_group_cipher;
    zt_u32                  rsn_pairwise_cipher;
    struct
    {
        zt_u8 ie[ZT_OFFSETOF(zt_80211_mgmt_t, assoc_req.listen_interval) +
                                              ZT_80211_IES_SIZE_MAX];
        zt_u32 ie_len;
    } assoc_req;
    struct
    {
        zt_u8 ie[ZT_80211_IES_SIZE_MAX];
        zt_u32 ie_len;
    } assoc_resp;
    zt_u8                   bss_change_cnt;

#ifdef CFG_ENABLE_AP_MODE
    void                   *ap_tid;
    zt_s8                   ap_name[30];
    zt_80211_wmm_param_ie_t pwmm;
    zt_80211_mgmt_ht_cap_t  pht_cap;
    zt_80211_mgmt_ht_operation_t pht_oper;
    zt_u8                   cur_wireless_mode;
    zt_u8                   channle_offset;
    zt_que_t                ap_msg_free[ZT_AP_MSG_TAG_MAX];
    zt_ap_status            ap_state;
    zt_80211_hidden_ssid_e  hidden_ssid_mode;
    zt_wlan_ssid_t          hidden_ssid;
    zt_bool                 freeze_pend;
    zt_os_api_lock_t        wdn_new_lock, wdn_del_lock;
    zt_u8                   sta_cnt;
    zt_u8                   tim_bitmap;
#endif
} zt_wlan_network_t, zt_wlan_mgmt_cur_network_t;


#define ZT_WLAN_NAME_SIZE_MAX   16
typedef struct
{
    void *tid;
    zt_s8 name[ZT_WLAN_NAME_SIZE_MAX];
    zt_wlan_mgmt_cur_network_t cur_network;
    zt_wlan_mgmt_scan_que_t scan_que;
    zt_msg_que_t msg_que;
} zt_wlan_mgmt_info_t;

enum
{
    /* priority level 0 */
    ZT_WLAN_MGMT_TAG_UNINSTALL          = ZT_MSG_TAG_SET(0, 0, 0),
    ZT_WLAN_MGMT_TAG_CHIPRESET,

    /* priority level 1 */
    ZT_WLAN_MGMT_TAG_SCAN_QUE_FLUSH     = ZT_MSG_TAG_SET(0, 1, 0),

    /* priority level 2 */
    ZT_WLAN_MGMT_TAG_BEACON_FRAME       = ZT_MSG_TAG_SET(0, 2, 0),
    ZT_WLAN_MGMT_TAG_PROBERSP_FRAME,
    ZT_WLAN_MGMT_TAG_PROBEREQ_P2P,
    ZT_WLAN_MGMT_TAG_ACTION,

    /* priority level 3 */
    ZT_WLAN_MGMT_TAG_SCAN_QUE_REFRESH   = ZT_MSG_TAG_SET(0, 3, 0),
};

typedef enum
{
    ZT_WLAN_MGMT_SCAN_QUE_FOR_RST_END,
    ZT_WLAN_MGMT_SCAN_QUE_FOR_RST_BREAK,
    ZT_WLAN_MGMT_SCAN_QUE_FOR_RST_FAIL = -1,
} zt_wlan_mgmt_scan_que_for_rst_e;

/* function declaration */

zt_s32 zt_wlan_mgmt_rx_frame(void *ptr);
zt_s32 zt_wlan_mgmt_send_msg(nic_info_st *pnic_info, zt_msg_tag_t tag);
zt_inline static zt_s32 zt_wlan_mgmt_scan_que_flush(nic_info_st *pnic_info)
{
    return zt_wlan_mgmt_send_msg(pnic_info, ZT_WLAN_MGMT_TAG_SCAN_QUE_FLUSH);
}
zt_s32 zt_wlan_mgmt_scan_que_refresh(nic_info_st *pnic_info,
                                     zt_u8 *pch, zt_u8 ch_num);
zt_s32 zt_wlan_mgmt_chip_reset(nic_info_st *pnic_info, zt_u8 type);
zt_s32 zt_wlan_mgmt_init(nic_info_st *nic_info);
zt_s32 zt_wlan_mgmt_term(nic_info_st *nic_info);
zt_s32 zt_wlan_mgmt_scan_que_read_try(zt_wlan_mgmt_scan_que_t *pscan_que);
zt_s32 zt_wlan_mgmt_scan_que_read_post(zt_wlan_mgmt_scan_que_t *pscan_que);
zt_inline static
zt_bool zt_wlan_is_same_ssid(zt_wlan_ssid_t *pssid1, zt_wlan_ssid_t *pssid2)
{
    return (zt_bool)(pssid1->length == pssid2->length &&
                     !zt_memcmp(pssid1->data, pssid2->data, pssid1->length));
}
void zt_wlan_set_cur_ssid(nic_info_st *pnic_info, zt_wlan_ssid_t *pssid);
zt_wlan_ssid_t *zt_wlan_get_cur_ssid(nic_info_st *pnic_info);
void zt_wlan_set_cur_bssid(nic_info_st *pnic_info,  zt_u8 *bssid);
zt_u8 *zt_wlan_get_cur_bssid(nic_info_st *pnic_info);
void zt_wlan_set_cur_channel(nic_info_st *pnic_info, zt_u8 channel);
zt_u8 zt_wlan_get_cur_channel(nic_info_st *pnic_info);
void zt_wlan_set_cur_bw(nic_info_st *pnic_info, CHANNEL_WIDTH bw);
CHANNEL_WIDTH zt_wlan_get_cur_bw(nic_info_st *pnic_info);
zt_s32 zt_wlan_get_max_rate(nic_info_st *pnic_info, zt_u8 *mac,
                            zt_u16 *max_rate);
zt_s32 zt_wlan_get_signal_and_qual(nic_info_st *pnic_info, zt_u8 *qual,
                                   zt_u8 *level);


enum WIFI_FRAME_TYPE
{
    WIFI_MGT_TYPE = (0),
    WIFI_CTRL_TYPE = (ZT_BIT(2)),
    WIFI_DATA_TYPE = (ZT_BIT(3)),
    WIFI_QOS_DATA_TYPE = (ZT_BIT(7) | ZT_BIT(3)),
};

enum WIFI_FRAME_SUBTYPE
{

    WIFI_ASSOCREQ = (0 | WIFI_MGT_TYPE),
    WIFI_ASSOCRSP = (ZT_BIT(4) | WIFI_MGT_TYPE),
    WIFI_REASSOCREQ = (ZT_BIT(5) | WIFI_MGT_TYPE),
    WIFI_REASSOCRSP = (ZT_BIT(5) | ZT_BIT(4) | WIFI_MGT_TYPE),
    WIFI_PROBEREQ = (ZT_BIT(6) | WIFI_MGT_TYPE),
    WIFI_PROBERSP = (ZT_BIT(6) | ZT_BIT(4) | WIFI_MGT_TYPE),
    WIFI_BEACON = (ZT_BIT(7) | WIFI_MGT_TYPE),
    WIFI_ATIM = (ZT_BIT(7) | ZT_BIT(4) | WIFI_MGT_TYPE),
    WIFI_DISASSOC = (ZT_BIT(7) | ZT_BIT(5) | WIFI_MGT_TYPE),
    WIFI_AUTH = (ZT_BIT(7) | ZT_BIT(5) | ZT_BIT(4) | WIFI_MGT_TYPE),
    WIFI_DEAUTH = (ZT_BIT(7) | ZT_BIT(6) | WIFI_MGT_TYPE),
    WIFI_ACTION = (ZT_BIT(7) | ZT_BIT(6) | ZT_BIT(4) | WIFI_MGT_TYPE),
    WIFI_ACTION_NOACK = (ZT_BIT(7) | ZT_BIT(6) | ZT_BIT(5) | WIFI_MGT_TYPE),

    WIFI_NDPA = (ZT_BIT(6) | ZT_BIT(4) | WIFI_CTRL_TYPE),
    WIFI_PSPOLL = (ZT_BIT(7) | ZT_BIT(5) | WIFI_CTRL_TYPE),
    WIFI_RTS = (ZT_BIT(7) | ZT_BIT(5) | ZT_BIT(4) | WIFI_CTRL_TYPE),
    WIFI_CTS = (ZT_BIT(7) | ZT_BIT(6) | WIFI_CTRL_TYPE),
    WIFI_ACK = (ZT_BIT(7) | ZT_BIT(6) | ZT_BIT(4) | WIFI_CTRL_TYPE),
    WIFI_CFEND = (ZT_BIT(7) | ZT_BIT(6) | ZT_BIT(5) | WIFI_CTRL_TYPE),
    WIFI_CFEND_CFACK = (ZT_BIT(7) | ZT_BIT(6) | ZT_BIT(5) | ZT_BIT(4) | WIFI_CTRL_TYPE),

    WIFI_DATA = (0 | WIFI_DATA_TYPE),
    WIFI_DATA_CFACK = (ZT_BIT(4) | WIFI_DATA_TYPE),
    WIFI_DATA_CFPOLL = (ZT_BIT(5) | WIFI_DATA_TYPE),
    WIFI_DATA_CFACKPOLL = (ZT_BIT(5) | ZT_BIT(4) | WIFI_DATA_TYPE),
    WIFI_DATA_NULL = (ZT_BIT(6) | WIFI_DATA_TYPE),
    WIFI_CF_ACK = (ZT_BIT(6) | ZT_BIT(4) | WIFI_DATA_TYPE),
    WIFI_CF_POLL = (ZT_BIT(6) | ZT_BIT(5) | WIFI_DATA_TYPE),
    WIFI_CF_ACKPOLL = (ZT_BIT(6) | ZT_BIT(5) | ZT_BIT(4) | WIFI_DATA_TYPE),
    WIFI_QOS_DATA_NULL = (ZT_BIT(6) | WIFI_QOS_DATA_TYPE),
};

#define ZT_NUM_PRE_AUTH_KEY         16
#define ZT_NUM_PMKID_CACHE          ZT_NUM_PRE_AUTH_KEY

typedef enum
{
    dot11AuthAlgrthm_Open = 0,
    dot11AuthAlgrthm_Shared,
    dot11AuthAlgrthm_8021X,
    dot11AuthAlgrthm_Auto,
    dot11AuthAlgrthm_WAPI,
    dot11AuthAlgrthm_MaxNum
} auth_algo_e;

#define _TO_DS_     ZT_BIT(8)
#define _FROM_DS_   ZT_BIT(9)
#define _MORE_FRAG_ ZT_BIT(10)
#define _RETRY_     ZT_BIT(11)
#define _PWRMGT_    ZT_BIT(12)
#define _MORE_DATA_ ZT_BIT(13)
#define _PRIVACY_   ZT_BIT(14)
#define _ORDER_     ZT_BIT(15)


#define SetToDs(pbuf)   \
    do  {   \
        *(zt_u16 *)(pbuf) |= zt_le16_to_cpu(_TO_DS_); \
    } while(0)

#define GetToDs(pbuf)   (((*(zt_u16 *)(pbuf)) & zt_le16_to_cpu(_TO_DS_)) != 0)


#define SetFrDs(pbuf)   \
    do  {   \
        *(zt_u16 *)(pbuf) |= zt_le16_to_cpu(_FROM_DS_); \
    } while(0)

#define GetFrDs(pbuf)   (((*(zt_u16 *)(pbuf)) & zt_le16_to_cpu(_FROM_DS_)) != 0)



#define SetMFrag(pbuf)  \
    do  {   \
        *(zt_u16 *)(pbuf) |= zt_le16_to_cpu(_MORE_FRAG_); \
    } while(0)


#define ClearMFrag(pbuf)    \
    do  {   \
        *(zt_u16 *)(pbuf) &= (~ zt_le16_to_cpu(_MORE_FRAG_)); \
    } while(0)




#define SetPwrMgt(pbuf) \
    do  {   \
        *(zt_u16 *)(pbuf) |= zt_le16_to_cpu(_PWRMGT_); \
    } while(0)






#define SetPrivacy(pbuf)    \
    do  {   \
        *(zt_u16 *)(pbuf) |= zt_le16_to_cpu(_PRIVACY_); \
    } while(0)

#define GetPrivacy(pbuf)    (((*(zt_u16 *)(pbuf)) & zt_le16_to_cpu(_PRIVACY_)) != 0)



#define GetFrameType(pbuf)  (zt_le16_to_cpu(*(zt_u16 *)(pbuf)) & (ZT_BIT(3) | ZT_BIT(2)))

#define SetFrameType(pbuf,type) \
    do {    \
        *(zt_u16 *)(pbuf) &= zt_le16_to_cpu(~(ZT_BIT(3) | ZT_BIT(2))); \
        *(zt_u16 *)(pbuf) |= zt_le16_to_cpu(type); \
    } while(0)

#define GetFrameSubType(pbuf)   (zt_le16_to_cpu(*(zt_u16 *)(pbuf)) & (ZT_BIT(7) | ZT_BIT(6) | ZT_BIT(5) | ZT_BIT(4) | ZT_BIT(3) | ZT_BIT(2)))

#define SetFrameSubType(pbuf,type) \
    do {    \
        *(zt_u16 *)(pbuf) &= zt_le16_to_cpu(~(ZT_BIT(7) | ZT_BIT(6) | ZT_BIT(5) | ZT_BIT(4) | ZT_BIT(3) | ZT_BIT(2))); \
        *(zt_u16 *)(pbuf) |= zt_le16_to_cpu(type); \
    } while(0)

#define GetSequence(pbuf)   (zt_le16_to_cpu(*(zt_u16 *)((SIZE_PTR)(pbuf) + 22)) >> 4)




#define SetSeqNum(pbuf, num) \
    do {    \
        *(zt_u16 *)((SIZE_PTR)(pbuf) + 22) = \
                                             ((*(zt_u16 *)((SIZE_PTR)(pbuf) + 22)) & zt_le16_to_cpu((zt_u16)~0xfff0)) | \
                                             zt_le16_to_cpu((zt_u16)(0xfff0 & (num << 4))); \
    } while(0)

#define SetDuration(pbuf, dur) \
    do {    \
        *(zt_u16 *)((SIZE_PTR)(pbuf) + 2) = zt_le16_to_cpu(0xffff & (dur)); \
    } while(0)

#define SetPriority(pbuf, tid)  \
    do  {   \
        *(zt_u16 *)(pbuf) |= zt_le16_to_cpu(tid & 0xf); \
    } while(0)


#define SetEOSP(pbuf, eosp) \
    do  {   \
        *(zt_u16 *)(pbuf) |= zt_le16_to_cpu( (eosp & 1) << 4); \
    } while(0)

#define SetAckpolicy(pbuf, ack) \
    do  {   \
        *(zt_u16 *)(pbuf) |= zt_le16_to_cpu( (ack & 3) << 5); \
    } while(0)






#define GetAddr1Ptr(pbuf)   ((zt_u8 *)((SIZE_PTR)(pbuf) + 4))

#define GetAddr2Ptr(pbuf)   ((zt_u8 *)((SIZE_PTR)(pbuf) + 10))

#define GetAddr3Ptr(pbuf)   ((zt_u8 *)((SIZE_PTR)(pbuf) + 16))

#define GetAddr4Ptr(pbuf)   ((zt_u8 *)((SIZE_PTR)(pbuf) + 24))

#define MacAddr_isBcst(addr) \
    ( \
      ( (addr[0] == 0xff) && (addr[1] == 0xff) && \
        (addr[2] == 0xff) && (addr[3] == 0xff) && \
        (addr[4] == 0xff) && (addr[5] == 0xff) )  ? zt_true : zt_false \
    )

zt_inline static zt_bool IS_MCAST(zt_u8 *da)
{
    if ((*da) & 0x01)
    {
        return zt_true;
    }
    else
    {
        return zt_false;
    }
}

zt_inline static zt_u8 *get_ra(zt_u8 *pframe)
{
    zt_u8 *ra;
    ra = GetAddr1Ptr(pframe);
    return ra;
}

zt_inline static zt_u8 *get_ta(zt_u8 *pframe)
{
    zt_u8 *ta;
    ta = GetAddr2Ptr(pframe);
    return ta;
}

zt_inline static zt_u8 *get_da(zt_u8 *pframe)
{
    zt_u8 *da;
    zt_u32 to_fr_ds = (GetToDs(pframe) << 1) | GetFrDs(pframe);

    switch (to_fr_ds)
    {
        case 0x00:
            da = GetAddr1Ptr(pframe);
            break;
        case 0x01:
            da = GetAddr1Ptr(pframe);
            break;
        case 0x02:
            da = GetAddr3Ptr(pframe);
            break;
        default:
            da = GetAddr3Ptr(pframe);
            break;
    }

    return da;
}






typedef struct wl_ndis_802_11_wep
{
    zt_u32 Length;
    zt_u32 KeyIndex;
    zt_u32 KeyLength;
    zt_u8  KeyMaterial[16];
} wl_ndis_802_11_wep_st;

typedef struct wl_ndis_auth_mode
{
    zt_u32 Length;
    zt_80211_bssid_t Bssid;
    zt_u32 Flags;
} wl_ndis_auth_mode_st;
/*
 * Length is the 4 bytes multiples of the sume of
 *  [ETH_ALEN] + 2 + sizeof (struct ndis_802_11_ssid) + sizeof (zt_u32)
 *  + sizeof (NDIS_802_11_RSSI) + sizeof (enum NDIS_802_11_NETWORK_TYPE)
 *  + sizeof (struct ndis_802_11_config)
 *  + NDIS_802_11_LENGTH_RATES_EX + IELength
 *
 * Except the IELength, all other fields are fixed length.
 * Therefore, we can define a macro to represent the partial sum. */

typedef enum
{
    zt_ndis802_11AuthModeOpen,
    zt_ndis802_11AuthModeShared,
    zt_ndis802_11AuthModeAutoSwitch,
    zt_ndis802_11AuthModeWPA,
    zt_ndis802_11AuthModeWPAPSK,
    zt_ndis802_11AuthModeWPANone,
    zt_ndis802_11AuthModeWAPI,
    zt_ndis802_11AuthModeMax   /*  Not a real mode, upper bound */
} zt_ndis_802_11_auth_mode_e;

typedef enum
{
    zt_ndis802_11WEPEnabled,
    zt_ndis802_11Encryption1Enabled = zt_ndis802_11WEPEnabled,
    zt_ndis802_11WEPDisabled,
    zt_ndis802_11EncryptionDisabled = zt_ndis802_11WEPDisabled,
    zt_ndis802_11WEPKeyAbsent,
    zt_ndis802_11Encryption1KeyAbsent = zt_ndis802_11WEPKeyAbsent,
    zt_ndis802_11WEPNotSupported,
    zt_ndis802_11EncryptionNotSupported = zt_ndis802_11WEPNotSupported,
    zt_ndis802_11Encryption2Enabled,
    zt_ndis802_11Encryption2KeyAbsent,
    zt_ndis802_11Encryption3Enabled,
    zt_ndis802_11Encryption3KeyAbsent,
    zt_ndis802_11_EncrypteionWAPI
} ndis_802_11_wep_status_e;

typedef struct wlan_ieee80211_ssid_st_
{
    zt_u32 length;
    zt_u8 ssid[32];
} wlan_ieee80211_ssid_st;







#endif

