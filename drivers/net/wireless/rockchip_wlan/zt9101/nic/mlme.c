/*
 * mlme.c
 *
 * used for impliment MLME(MAC sublayer management entity) logic
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

#include "common.h"

/* macro */
#define MLME_DBG(fmt, ...)      LOG_D("[%s:%d][%d]"fmt, __func__, __LINE__, pnic_info->ndev_id, ##__VA_ARGS__)
#define MLME_ARRAY(data, len)   zt_log_array(data, len)
#define MLME_INFO(fmt, ...)     LOG_I("[%s:%d][%d]"fmt, __func__, __LINE__, pnic_info->ndev_id, ##__VA_ARGS__)
#define MLME_WARN(fmt, ...)     LOG_W("[%s:%d][%d]"fmt, __func__, __LINE__, pnic_info->ndev_id, ##__VA_ARGS__)
#define MLME_ERROR(fmt, ...)    LOG_E("[%s:%d][%d]"fmt, __func__, __LINE__, pnic_info->ndev_id, ##__VA_ARGS__)

/* type define */
typedef struct
{
    zt_mlme_framework_e framework;
    scan_type_e type;
    zt_wlan_ssid_t ssids[ZT_SCAN_REQ_SSID_NUM];
    zt_u8 ssid_num;
    zt_u8 chs[ZT_SCAN_REQ_CHANNEL_NUM];
    zt_u8 ch_num;
} mlme_scan_t;
typedef zt_u8 mlme_scan_rsp_t[ZT_80211_MGMT_PROBERSP_SIZE_MAX];

typedef struct
{
    zt_bool indicate_en;
    zt_mlme_framework_e framework;
    zt_80211_bssid_t bssid;
    zt_wlan_ssid_t ssid;
} mlme_conn_t;

typedef zt_mlme_conn_res_t mlme_deauth_t, mlme_deassoc_t, mlme_conn_abort_t;

#ifdef CFG_ENABLE_ADHOC_MODE
typedef struct
{
    zt_mlme_framework_e framework;
    zt_wlan_ssid_t ssid;
    zt_u8 ch;
} mlme_conn_ibss_t;
#endif

/* function declaration */
static zt_s32 mlme_msg_send(nic_info_st *pnic_info,
                            zt_msg_tag_t tag, void *value, zt_u8 len);
static zt_s32 mlme_set_state(nic_info_st *pnic_info, mlme_state_e state);
zt_s32 mlme_set_connect(nic_info_st *pnic_info, zt_bool bconnect);

zt_inline static
zt_s32 hw_cfg(nic_info_st *pnic_info, wdn_net_info_st *wdn_info)
{
    zt_s32 ret = 0;
    zt_u16 basic_dr_cfg = 0;

    /* hardware configure */
    if (wdn_info->short_preamble)
    {
        ret |= zt_mcu_set_preamble(pnic_info, PREAMBLE_SHORT);
    }
    else
    {
        ret |= zt_mcu_set_preamble(pnic_info, PREAMBLE_LONG);
    }

    if (wdn_info->short_slot)
    {
        ret |= zt_mcu_set_slot_time(pnic_info, SHORT_SLOT_TIME);
    }
    else
    {
        ret |= zt_mcu_set_slot_time(pnic_info, NON_SHORT_SLOT_TIME);
    }

    if (wdn_info->wmm_enable)
    {
        ret |= zt_mcu_set_wmm_para_enable(pnic_info, wdn_info);
    }
    else
    {
        ret |= zt_mcu_set_wmm_para_disable(pnic_info, wdn_info);
    }
    ret |= zt_mcu_set_sifs(pnic_info);

    ret |= zt_mcu_set_max_ampdu_len(pnic_info,
                                    wdn_info->htpriv.mcu_ht.rx_ampdu_maxlen);
    ret |= zt_mcu_set_config_xmit(pnic_info, ZT_XMIT_AMPDU_DENSITY,
                                  wdn_info->htpriv.mcu_ht.rx_ampdu_min_spacing);

    get_bratecfg_by_support_dates(wdn_info->datarate, wdn_info->datarate_len,
                                  &basic_dr_cfg);
    get_bratecfg_by_support_dates(wdn_info->ext_datarate,
                                  wdn_info->ext_datarate_len, &basic_dr_cfg);
    ret |= zt_mcu_set_basic_rate(pnic_info,  basic_dr_cfg);

    ret |= zt_hw_info_set_channel_bw(pnic_info,
                                      wdn_info->channel,
                                      wdn_info->bw_mode,
                                      wdn_info->channle_offset);
    return ret;
}

zt_inline static zt_s32 build_wdn(nic_info_st *pnic_info)
{
    mlme_info_t *pmlme_info = pnic_info->mlme_info;
    wdn_net_info_st *pwdn_info;

    MLME_DBG();

    pmlme_info->pwdn_info = pwdn_info =
                                zt_wdn_add(pnic_info, zt_wlan_get_cur_bssid(pnic_info));
    if (pwdn_info == NULL)
    {
        MLME_WARN("new wdn fail");
        return -1;
    }

    if (zt_wdn_info_sta_update(pnic_info, pwdn_info))
    {
        MLME_WARN("wdn update fail");
        return -2;
    }

    if (hw_cfg(pnic_info, pwdn_info))
    {
        MLME_WARN("hw config fail");
        return -3;
    }

    return 0;
}

static
zt_pt_ret_t core_scan_thrd(zt_pt_t *pt, nic_info_st *pnic_info,
                           mlme_scan_t *preq, zt_s32 *prsn)
{
    zt_pt_t *pt_sub = &pt[1];
    mlme_info_t *pmlme_info = pnic_info->mlme_info;
    zt_msg_que_t *pmsg_que = &pmlme_info->msg_que;
    zt_msg_t *pmsg;
    zt_s32 rst;

    PT_BEGIN(pt);

    MLME_DBG();

    if (preq == NULL)
    {
        MLME_WARN("invalid scan request");
        *prsn = -1;
        PT_EXIT(pt);
    }

    if (preq->type == SCAN_TYPE_PASSIVE)
    {
        zt_wlan_mgmt_scan_que_flush(pnic_info);
    }

    /* start scan */
    rst = zt_scan_start(pnic_info, preq->type,
                        NULL,
                        preq->ssids, preq->ssid_num,
                        preq->chs, preq->ch_num);
    if (rst)
    {
        MLME_WARN("start fail, error code: %d", rst);
        *prsn = -2;
        PT_EXIT(pt);
    }

    /* scan process */
    MLME_INFO("scan...");
    PT_INIT(pt_sub);
    while (PT_SCHEDULE(zt_scan_thrd(pt_sub, pnic_info, prsn)))
    {
        if (!zt_msg_get(pmsg_que, &pmsg) && pmsg->tag == ZT_MLME_TAG_SCAN_ABORT)
        {
            zt_msg_del(pmsg_que, pmsg);
            MLME_DBG("abort scanning...");
            zt_scan_stop(pnic_info);
            PT_WAIT_THREAD(pt, zt_scan_thrd(pt_sub, pnic_info, prsn));
            break;
        }
        PT_YIELD(pt);
    }

    if (pnic_info->is_up)
    {
        /* notify system scan result */
        MLME_DBG("report scan result");
        zt_os_api_ind_scan_done(pnic_info, *prsn == ZT_SCAN_TAG_ABORT,
                                preq->framework);
    }

    *prsn = 0;
    PT_END(pt);
}

zt_inline static
zt_s32 set_cur_network(nic_info_st *pnic_info,
                       zt_80211_mgmt_t *pmgmt, zt_u16 mgmt_len)
{
    zt_wlan_mgmt_info_t *pwlan_info = pnic_info->wlan_mgmt_info;
    zt_wlan_network_t *pcur_network = &pwlan_info->cur_network;
    zt_u16 var_len;
    zt_u8 bss_ch;

    /* get bss channel number */
    {
        zt_u8 *pies = &pmgmt->probe_resp.variable[0];
        zt_u16 ies_len = mgmt_len - ZT_OFFSETOF(struct beacon_ie, variable);
        zt_80211_mgmt_ie_t *pie;
        if (zt_80211_mgmt_ies_search(pies, ies_len,
                                     ZT_80211_MGMT_EID_DS_PARAMS, &pie))
        {
            MLME_WARN("no DS element field");
            return -1;
        }
        {
            zt_80211_mgmt_dsss_parameter_t *pds = (void *)pie->data;
            bss_ch = pds->current_channel;
        }
    }

    pcur_network->bss_change_cnt = 0;
    /* set channel */
    MLME_INFO("channel: %d", bss_ch);
    zt_wlan_set_cur_channel(pnic_info, bss_ch);
    /* retrive address */
    zt_memcpy(pcur_network->mac_addr, pmgmt->sa, sizeof(zt_80211_addr_t));
    zt_wlan_set_cur_bssid(pnic_info, pmgmt->bssid);
    /* retrive ssid */
    {
        zt_u8 *pies = &pmgmt->probe_resp.variable[0];
        zt_u16 ies_len = mgmt_len - ZT_OFFSETOF(struct beacon_ie, variable);
        zt_80211_mgmt_ie_t *pie;
        zt_s32 rst = zt_80211_mgmt_ies_search(pies, ies_len,
                                              ZT_80211_MGMT_EID_SSID, &pie);
        if (rst)
        {
            MLME_ERROR("ies search fail, error code: %d", rst);
            return -2;
        }
        {
            zt_wlan_ssid_t ssid = {0};
            ssid.length = pie->len;
            zt_memcpy(ssid.data, pie->data, pie->len);
            zt_wlan_set_cur_ssid(pnic_info, &ssid);
        }
    }
    /* retrive no elements field */
    pcur_network->timestamp = zt_le64_to_cpu(pmgmt->probe_resp.timestamp);
    pcur_network->bcn_interval = pmgmt->probe_resp.intv;
    pcur_network->cap_info = zt_le16_to_cpu(pmgmt->probe_resp.capab);
    pcur_network->cap_privacy =
        (zt_bool)!!(pcur_network->cap_info & ZT_80211_MGMT_CAPAB_PRIVACY);
    /* copy ies */
    var_len = mgmt_len - ZT_OFFSETOF(zt_80211_mgmt_t, probe_resp.variable);
    if (var_len > sizeof(pcur_network->ies))
    {
        MLME_WARN("mangnet frame body size beyond limit");
        return -2;
    }
    pcur_network->ies_length = var_len;
    zt_memcpy(&pcur_network->ies[0], &pmgmt->probe_resp.variable[0], var_len);
    /* retrive wpa cipher type, clear value first */
    pcur_network->wpa_multicast_cipher = 0;
    pcur_network->wpa_unicast_cipher = 0;
    pcur_network->rsn_group_cipher = 0;
    pcur_network->rsn_pairwise_cipher = 0;
    zt_80211_mgmt_wpa_survey(pcur_network->ies, pcur_network->ies_length,
                             NULL, NULL,
                             &pcur_network->wpa_multicast_cipher,
                             &pcur_network->wpa_unicast_cipher);
    /* retrive rsn cipher type */
    zt_80211_mgmt_rsn_survey(pcur_network->ies, pcur_network->ies_length,
                             NULL, NULL,
                             &pcur_network->rsn_group_cipher,
                             &pcur_network->rsn_pairwise_cipher);

    return 0;
}

static
zt_pt_ret_t core_conn_scan_thrd(zt_pt_t *pt, nic_info_st *pnic_info,
                                mlme_conn_t *preq, zt_s32 *prsn)
{
    zt_pt_t *pt_sub = &pt[1];
    mlme_info_t *pmlme_info = pnic_info->mlme_info;
    zt_msg_que_t *pmsg_que = &pmlme_info->msg_que;
    zt_msg_t *pmsg = NULL;
    zt_s32 rst;
    zt_80211_mgmt_t *pmgmt;
    zt_u16 mgmt_len;

    PT_BEGIN(pt);

    if (preq == NULL)
    {
        MLME_WARN("invalid scan request");
        *prsn = -1;
        PT_EXIT(pt);
    }

    /* start scan */
    pmlme_info->try_cnt = 3;
retry :
    rst = zt_scan_start(pnic_info, SCAN_TYPE_ACTIVE,
                        preq->bssid,
                        &preq->ssid, preq->ssid.length ? 1 : 0,
                        NULL, 0);
    if (rst)
    {
        MLME_WARN("start fail, error code: %d", rst);
        *prsn = -2;
        PT_EXIT(pt);
    }

    /* scan process */
    MLME_INFO("wait probe respone...");
    PT_INIT(pt_sub);
    do
    {
        if (!PT_SCHEDULE(zt_scan_thrd(pt_sub, pnic_info, &rst)))
        {
            if (rst == ZT_SCAN_TAG_DONE && --pmlme_info->try_cnt)
            {
                goto retry;
            }
            MLME_WARN("scan fail, reason code: %d", rst);
            *prsn = -3;
            PT_EXIT(pt);
        }

        if (!zt_msg_pop(pmsg_que, &pmsg))
        {
            if (pmsg->tag == ZT_MLME_TAG_CONN_ABORT ||
                    pmsg->tag == ZT_MLME_TAG_SCAN_ABORT)
            {
                /* retrive disconnect information */
                if (pmsg->tag == ZT_MLME_TAG_CONN_ABORT)
                {
                    pmlme_info->conn_res = *(zt_mlme_conn_res_t *)pmsg->value;
                }
                else
                {
                    pmlme_info->conn_res.local_disconn = zt_true;
                    pmlme_info->conn_res.reason_code = ZT_80211_REASON_UNSPECIFIED;
                }
                zt_msg_del(pmsg_que, pmsg);

                MLME_INFO("abort scanning...");
                zt_scan_stop(pnic_info);
                PT_WAIT_THREAD(pt, zt_scan_thrd(pt_sub, pnic_info, &rst));
                *prsn = -4;
                PT_EXIT(pt);
            }
            else if (pmsg->tag == ZT_MLME_TAG_SCAN_RSP)
            {
                pmgmt = (void *)pmsg->value;
                mgmt_len = pmsg->len;

                rst = set_cur_network(pnic_info, pmgmt, mgmt_len);
                zt_msg_del(pmsg_que, pmsg);
                if (rst)
                {
                    MLME_WARN("set cur_network fail, error code: %d", rst);
                    pmlme_info->conn_res.local_disconn = zt_true;
                    pmlme_info->conn_res.reason_code = ZT_80211_REASON_UNSPECIFIED;
                    zt_scan_stop(pnic_info);
                    PT_WAIT_THREAD(pt, zt_scan_thrd(pt_sub, pnic_info, &rst));
                    *prsn = -5;
                    PT_EXIT(pt);
                }
                else
                {
                    MLME_INFO("probe respone ok");
                    zt_scan_stop(pnic_info);
                    PT_WAIT_THREAD(pt, zt_scan_thrd(pt_sub, pnic_info, &rst));
                }
                break;
            }
            else if (pmsg->tag == ZT_MLME_TAG_SCAN)
            {
                MLME_WARN("push message(tag: %d)", pmsg->tag);
                zt_msg_push(pmsg_que, pmsg);
            }
            else
            {
                MLME_WARN("drop unsuited message(tag: %d)", pmsg->tag);
                zt_msg_del(pmsg_que, pmsg);
            }
        }

        PT_YIELD(pt);
    } while (zt_true);

    *prsn = 0;
    PT_END(pt);
}

static
zt_pt_ret_t core_conn_auth_thrd(zt_pt_t *pt, nic_info_st *pnic_info,
                                zt_s32 *prsn)
{
    zt_pt_t *pt_sub = &pt[1];
    mlme_info_t *pmlme_info = pnic_info->mlme_info;
    zt_msg_que_t *pmsg_que = &pmlme_info->msg_que;
    zt_msg_t *pmsg = NULL;
    zt_s32 rst;

    PT_BEGIN(pt);

    rst = zt_auth_sta_start(pnic_info);
    if (rst)
    {
        MLME_WARN("start fail, error code: %d", rst);
        *prsn = -1;
        PT_EXIT(pt);
    }

    PT_INIT(pt_sub);
    while (PT_SCHEDULE(zt_auth_sta_thrd(pt_sub, pnic_info, &rst)))
    {
        if (!zt_msg_pop(pmsg_que, &pmsg))
        {
            if (pmsg->tag == ZT_MLME_TAG_CONN_ABORT ||
                    pmsg->tag == ZT_MLME_TAG_DEAUTH)
            {
                MLME_INFO("abort auth...");
                /* retrive disconnect information */
                pmlme_info->conn_res = *(zt_mlme_conn_res_t *)pmsg->value;
                zt_msg_del(pmsg_que, pmsg);

                zt_auth_sta_stop(pnic_info);
                PT_WAIT_THREAD(pt, zt_auth_sta_thrd(pt_sub, pnic_info, &rst));
                *prsn = -2;
                PT_EXIT(pt);
            }
            else if (pmsg->tag == ZT_MLME_TAG_SCAN)
            {
                MLME_WARN("push message(tag: %d)", pmsg->tag);
                zt_msg_push(pmsg_que, pmsg);
            }
            else
            {
                MLME_WARN("drop unsuited message(tag: %d)", pmsg->tag);
                zt_msg_del(pmsg_que, pmsg);
            }
        }
        PT_YIELD(pt);
    }
    if (rst != ZT_AUTH_TAG_DONE)
    {
        *prsn = -3;
        PT_EXIT(pt);
    }

    *prsn = 0;
    PT_END(pt);
}

static zt_pt_ret_t
core_conn_assoc_thrd(zt_pt_t *pt, nic_info_st *pnic_info, zt_s32 *prsn)
{
    zt_pt_t *pt_sub = &pt[1];
    mlme_info_t *pmlme_info = pnic_info->mlme_info;
    zt_msg_que_t *pmsg_que = &pmlme_info->msg_que;
    zt_msg_t *pmsg = NULL;
    zt_s32 rst;

    PT_BEGIN(pt);

    rst = zt_assoc_start(pnic_info);
    if (rst)
    {
        MLME_WARN("start fail, error code: %d", rst);
        *prsn = -1;
        PT_EXIT(pt);
    }

    PT_INIT(pt_sub);
    while (PT_SCHEDULE(zt_assoc_sta_thrd(pt_sub, pnic_info, &rst)))
    {
        if (!zt_msg_pop(pmsg_que, &pmsg))
        {
            if (pmsg->tag == ZT_MLME_TAG_CONN_ABORT ||
                    pmsg->tag == ZT_MLME_TAG_DEAUTH)
            {
                MLME_INFO("abort assoc...");
                /* retrive disconnect information */
                pmlme_info->conn_res = *(zt_mlme_conn_res_t *)pmsg->value;
                zt_msg_del(pmsg_que, pmsg);

                zt_assoc_stop(pnic_info);
                PT_WAIT_THREAD(pt, zt_assoc_sta_thrd(pt_sub, pnic_info, &rst));
                *prsn = -2;
                PT_EXIT(pt);
            }
            else if (pmsg->tag == ZT_MLME_TAG_SCAN)
            {
                MLME_WARN("push message(tag: %d)", pmsg->tag);
                zt_msg_push(pmsg_que, pmsg);
            }
            else
            {
                MLME_WARN("drop unsuited message(tag: %d)", pmsg->tag);
                zt_msg_del(pmsg_que, pmsg);
            }
        }
        PT_YIELD(pt);
    }
    if (rst != ZT_ASSOC_TAG_DONE)
    {
        zt_deauth_xmit_frame(pnic_info, zt_wlan_get_cur_bssid(pnic_info),
                             ZT_80211_REASON_DEAUTH_LEAVING);
        *prsn = -3;
        PT_EXIT(pt);
    }

    *prsn = 0;
    PT_END(pt);
}

static zt_pt_ret_t
core_conn_preconnect(zt_pt_t *pt, nic_info_st *pnic_info, zt_s32 *prsn)
{
    mlme_info_t *pmlme_info = pnic_info->mlme_info;
    wdn_net_info_st *pwdn_info = pmlme_info->pwdn_info;
    // hw_info_st *phw_info = pnic_info->hw_info;

    PT_BEGIN(pt);

    // if (phw_info->ba_enable_rx)
    // {
    //     rx_info_t *rx_info = pnic_info->rx_info;
    //     pwdn_info->ba_ctl = rx_info->ba_ctl;
    // }

    PT_WAIT_WHILE(pt, nic_mlme_hw_access_trylock(pnic_info));

    if (zt_mcu_rate_table_update(pnic_info, pwdn_info))
    {
        MLME_WARN("zt_mcu_rate_table_update Failed");
        nic_mlme_hw_access_unlock(pnic_info);
        *prsn = -1;
        PT_EXIT(pt);
    }
    zt_action_frame_del_ba_request(pnic_info, zt_wlan_get_cur_bssid(pnic_info));
    zt_mcu_set_mlme_join(pnic_info, 2);
    zt_mcu_set_user_info(pnic_info, zt_true);
    zt_os_api_enable_all_data_queue(pnic_info->ndev);
    mlme_set_connect(pnic_info, zt_true);

    {
        mlme_conn_t *pconn_req = (void *)pmlme_info->pconn_msg->value;
        zt_os_api_ind_connect(pnic_info, pconn_req->framework);
    }

    {
        *pnic_info->hw_bw = pwdn_info->bw_mode;
        *pnic_info->hw_offset = pwdn_info->channle_offset;
        *pnic_info->hw_ch = pwdn_info->channel;
    }

    if (pnic_info->buddy_nic)
    {
#ifdef CFG_ENABLE_AP_MODE
        if (zt_local_cfg_get_work_mode(pnic_info->buddy_nic) == ZT_MASTER_MODE)
        {
            zt_ap_resend_bcn(pnic_info->buddy_nic, pwdn_info->channel);
        }
#endif
    }

    nic_mlme_hw_access_unlock(pnic_info);

    *prsn = 0;
    PT_END(pt);
}

static zt_pt_ret_t
core_conn_maintain_scan_thrd(zt_pt_t *pt, nic_info_st *pnic_info,
                             mlme_scan_t *preq, zt_s32 *prsn)
{
    zt_pt_t *pt_sub = &pt[1];
    mlme_info_t *pmlme_info = pnic_info->mlme_info;
    zt_msg_que_t *pmsg_que = &pmlme_info->msg_que;
    zt_msg_t *pmsg;
    zt_s32 rst;

    PT_BEGIN(pt);

    rst = zt_scan_start(pnic_info, SCAN_TYPE_ACTIVE,
                        NULL,
                        NULL, 0,
                        NULL, 0);
    if (rst)
    {
        MLME_WARN("start fail error code: %d", rst);
        *prsn = -1;
        PT_EXIT(pt);
    }

    PT_INIT(pt_sub);
    while (PT_SCHEDULE(zt_scan_thrd(pt_sub, pnic_info, &rst)))
    {
        if (!zt_msg_get(pmsg_que, &pmsg))
        {
            if (pmsg->tag == ZT_MLME_TAG_SCAN_ABORT)
            {
                MLME_DBG("abort scanning...");
                zt_msg_del(pmsg_que, pmsg);
                zt_scan_stop(pnic_info);
                PT_WAIT_THREAD(pt, zt_scan_thrd(pt_sub, pnic_info, &rst));
                break;
            }
            else if (pmsg->tag == ZT_MLME_TAG_CONN_ABORT ||
                     pmsg->tag == ZT_MLME_TAG_DEAUTH ||
                     pmsg->tag == ZT_MLME_TAG_DEASSOC)
            {
                zt_scan_stop(pnic_info);
                PT_WAIT_THREAD(pt, zt_scan_thrd(pt_sub, pnic_info, &rst));
                break;
            }
        }
        PT_YIELD(pt);
    }

    if (pnic_info->is_up)
    {
        /* notify system scan result */
        zt_os_api_ind_scan_done(pnic_info, rst == ZT_SCAN_TAG_ABORT,
                                preq->framework);
    }

    if (rst != ZT_SCAN_TAG_DONE)
    {
        *prsn = -2;
        PT_EXIT(pt);
    }

    *prsn = 0;
    PT_END(pt);
}

static zt_pt_ret_t
core_conn_maintain_probe_thrd(zt_pt_t *pt, nic_info_st *pnic_info,
                              zt_s32 *prsn)
{
    zt_pt_t *pt_sub = &pt[1];
    mlme_info_t *pmlme_info = pnic_info->mlme_info;
    zt_msg_que_t *pmsg_que = &pmlme_info->msg_que;
    zt_msg_t *pmsg;
    zt_s32 rst;

    PT_BEGIN(pt);

    {
        zt_wlan_ssid_t ssid = {0};
        wdn_net_info_st *pwdn_info = pmlme_info->pwdn_info;
        if (pwdn_info == NULL)
        {
            MLME_ERROR("wdn null");
            *prsn = -1;
            PT_EXIT(pt);
        }
        ssid.length = ZT_MIN(pwdn_info->ssid_len, ZT_80211_MAX_SSID_LEN);
        zt_memcpy(&ssid.data, pwdn_info->ssid, ssid.length);
        rst = zt_scan_start(pnic_info, SCAN_TYPE_ACTIVE,
                            (void *)pwdn_info->bssid,
                            &ssid, 1,
                            &pwdn_info->channel, 1);
    }
    if (rst)
    {
        MLME_WARN("start fail error code: %d", rst);
        *prsn = -2;
        PT_EXIT(pt);
    }

    PT_INIT(pt_sub);
    while (PT_SCHEDULE(zt_scan_thrd(pt_sub, pnic_info, prsn)))
    {
        if (!zt_msg_get(pmsg_que, &pmsg))
        {
            if (pmsg->tag == ZT_MLME_TAG_SCAN_ABORT ||
                    pmsg->tag == ZT_MLME_TAG_SCAN_RSP)
            {
                zt_msg_del(pmsg_que, pmsg);
                MLME_DBG("abort scanning...");
                zt_scan_stop(pnic_info);
                PT_WAIT_THREAD(pt, zt_scan_thrd(pt_sub, pnic_info, prsn));
                break;
            }
            else if (pmsg->tag == ZT_MLME_TAG_CONN_ABORT ||
                     pmsg->tag == ZT_MLME_TAG_DEAUTH ||
                     pmsg->tag == ZT_MLME_TAG_DEASSOC)
            {
                zt_scan_stop(pnic_info);
                PT_WAIT_THREAD(pt, zt_scan_thrd(pt_sub, pnic_info, prsn));
                break;
            }
        }
        PT_YIELD(pt);
    }

    PT_END(pt);
}

static zt_inline zt_pt_ret_t
core_conn_maintain_deauth_thrd(zt_pt_t *pt, nic_info_st *pnic_info)
{
    mlme_info_t *pmlme_info = pnic_info->mlme_info;
    // hw_info_st *phw_info = pnic_info->hw_info;

    PT_BEGIN(pt);

    PT_WAIT_WHILE(pt, nic_mlme_hw_access_trylock(pnic_info));

    zt_mcu_set_user_info(pnic_info, zt_false);
    zt_action_frame_del_ba_request(pnic_info, zt_wlan_get_cur_bssid(pnic_info));
    if (pmlme_info->conn_res.local_disconn)
    {
        zt_deauth_xmit_frame(pnic_info, zt_wlan_get_cur_bssid(pnic_info),
                             pmlme_info->conn_res.reason_code);
    }
    // if (phw_info->ba_enable_rx && pmlme_info->pwdn_info->ba_ctl)
    // {
    //     pmlme_info->pwdn_info->ba_ctl = NULL;
    // }

    nic_mlme_hw_access_unlock(pnic_info);

    PT_END(pt);
}

static zt_inline
zt_pt_ret_t core_conn_maintain_ba_req_thrd(zt_pt_t *pt, nic_info_st *pnic_info)
{
    hw_info_st *phw_info = pnic_info->hw_info;
    mlme_info_t *pmlme_info = pnic_info->mlme_info;
    wdn_net_info_st *pwdn_info = pmlme_info->pwdn_info;

    PT_BEGIN(pt);

    PT_WAIT_WHILE(pt, nic_mlme_hw_access_trylock(pnic_info));

    if (pwdn_info == NULL)
    {
        PT_EXIT(pt);
    }

    if (phw_info->ba_enable_tx)
    {
        if (zt_action_frame_ba_to_issue(pnic_info,
                                        ZT_WLAN_ACTION_ADDBA_REQ) < 0)
        {
            MLME_WARN("*** zt_action_frame_ba_to_issue(ZT_WLAN_ACTION_ADDBA_REQ) failed***");
        }
        pwdn_info->ba_started_flag[pmlme_info->bareq_parm.tid] = zt_true;
    }

    nic_mlme_hw_access_unlock(pnic_info);

    PT_END(pt);
}

static zt_inline zt_pt_ret_t
core_conn_maintain_ba_rsp_thrd(zt_pt_t *pt, nic_info_st *pnic_info,
                               zt_add_ba_parm_st *pbarsp_parm)
{
    hw_info_st *phw_info = pnic_info->hw_info;
    mlme_info_t *pmlme_info = pnic_info->mlme_info;
    wdn_net_info_st *pwdn_info = pmlme_info->pwdn_info;

    PT_BEGIN(pt);

    PT_WAIT_WHILE(pt, nic_mlme_hw_access_trylock(pnic_info));

    if (pwdn_info == NULL)
    {
        PT_EXIT(pt);
    }

    if (phw_info->ba_enable_rx)
    {
        zt_memcpy(&pmlme_info->barsp_parm, pbarsp_parm,
                  sizeof(pmlme_info->barsp_parm));
        if (zt_action_frame_ba_to_issue(pnic_info,
                                        ZT_WLAN_ACTION_ADDBA_RESP) < 0)
        {
            pwdn_info->ba_ctl[pmlme_info->barsp_parm.tid].enable = zt_false;
            MLME_WARN("*** zt_action_frame_ba_to_issue(ZT_WLAN_ACTION_ADDBA_RESP) failed***");
        }
        else
        {
            LOG_I("[%s]: ba enbale, tid=%d, start_req=%d", __func__,
                  pmlme_info->barsp_parm.tid, pbarsp_parm->start_seq);
            pwdn_info->ba_ctl[pmlme_info->barsp_parm.tid].indicate_seq =
                pbarsp_parm->start_seq;
            pwdn_info->ba_ctl[pmlme_info->barsp_parm.tid].enable = zt_true;
            pwdn_info->ba_ctl[pmlme_info->barsp_parm.tid].wait_timeout =
                pmlme_info->bareq_parm.timeout;
        }
    }

    nic_mlme_hw_access_unlock(pnic_info);

    PT_END(pt);
}

static
zt_pt_ret_t core_conn_maintain_msg_thrd(zt_pt_t *pt, nic_info_st *pnic_info)
{
    zt_pt_t *pt_sub = &pt[1];
    mlme_info_t *pmlme_info = pnic_info->mlme_info;
    zt_msg_que_t *pmsg_que = &pmlme_info->msg_que;
    zt_msg_t *pmsg;
    zt_s32 reason;
#ifdef CONFIG_LPS
    mlme_lps_t *param;
#endif

    PT_BEGIN(pt);

    for (;;)
    {
        mlme_set_state(pnic_info, MLME_STATE_IDLE);

        /* wait new message */
        PT_YIELD_UNTIL(pt, !zt_msg_pop(pmsg_que, &pmsg));

        if (pmsg->tag == ZT_MLME_TAG_SCAN)
        {
            pmlme_info->pscan_msg = pmsg;
            MLME_INFO("scan...");
            mlme_set_state(pnic_info, MLME_STATE_SCAN);
            PT_SPAWN(pt, pt_sub,
                     core_conn_maintain_scan_thrd(pt_sub, pnic_info,
                                                  (mlme_scan_t *)pmlme_info->pscan_msg->value,
                                                  &reason));
            zt_msg_del(pmsg_que, pmlme_info->pscan_msg);
            pmlme_info->pscan_msg = NULL;
        }

        else if (pmsg->tag == ZT_MLME_TAG_KEEPALIVE)
        {
            zt_msg_del(pmsg_que, pmsg);
            MLME_INFO("keepalive...");
            mlme_set_state(pnic_info, MLME_STATE_SCAN);
            PT_SPAWN(pt, pt_sub,
                     core_conn_maintain_probe_thrd(pt_sub, pnic_info, &reason));
        }

        else if (pmsg->tag == ZT_MLME_TAG_DEAUTH ||
                 pmsg->tag == ZT_MLME_TAG_DEASSOC ||
                 pmsg->tag == ZT_MLME_TAG_CONN_ABORT)
        {
            /* retrive disconnect information */
            pmlme_info->conn_res = *(zt_mlme_conn_res_t *)pmsg->value;
            zt_msg_del(pmsg_que, pmsg);

            MLME_INFO("deauth");
            mlme_set_state(pnic_info, MLME_STATE_DEAUTH);
            PT_SPAWN(pt, pt_sub,
                     core_conn_maintain_deauth_thrd(pt_sub, pnic_info));
            break;
        }

        else if (pmsg->tag == ZT_MLME_TAG_ADD_BA_REQ)
        {
            zt_msg_del(pmsg_que, pmsg);
            MLME_INFO("ba request");
            mlme_set_state(pnic_info, MLME_STATE_ADD_BA_REQ);
            PT_SPAWN(pt, pt_sub,
                     core_conn_maintain_ba_req_thrd(pt_sub, pnic_info));
        }

        else if (pmsg->tag == ZT_MLME_TAG_ADD_BA_RSP)
        {
            pmlme_info->pba_rsp_msg = pmsg;
            MLME_INFO("ba respone");
            mlme_set_state(pnic_info, MLME_STATE_ADD_BA_RESP);
            PT_SPAWN(pt, pt_sub,
                     core_conn_maintain_ba_rsp_thrd(pt_sub, pnic_info,
                                                    (void *)pmlme_info->pba_rsp_msg->value));
            zt_msg_del(pmsg_que, pmlme_info->pba_rsp_msg);
        }

#ifdef CONFIG_LPS
        else if (pmsg->tag == ZT_MLME_TAG_LPS)
        {
            param = (mlme_lps_t *) pmsg->value;
            MLME_INFO("msg.module: %s", "MLME_MSG_LPS");

            zt_lps_ctrl_state_hdl(pnic_info, param->lps_ctrl_type);

            zt_msg_del(pmsg_que, pmsg);
        }
#endif

        else
        {
            zt_msg_del(pmsg_que, pmsg);
            MLME_INFO("unsuited message(tag: %d)", pmsg->tag);
        }
    }

    PT_END(pt);
}

static zt_pt_ret_t
core_conn_maintain_traffic(zt_pt_t *pt,  nic_info_st *pnic_info)
{
    mlme_info_t *pmlme_info  = (mlme_info_t *)pnic_info->mlme_info;
    tx_info_st *ptx_info = pnic_info->tx_info;
    zt_u16 BusyThreshold;

    if (ptx_info == NULL)
    {
        MLME_WARN("tx_info NULL");
    }

    PT_BEGIN(pt);

    zt_timer_set(&pmlme_info->traffic_timer, 1000);

    for (;;)
    {
#if 0
        MLME_DBG("num_tx_ok_in_period=%d  num_rx_ok_in_period=%d",
                 pmlme_info->link_info.num_tx_ok_in_period,
                 pmlme_info->link_info.num_rx_ok_in_period);
#endif
        PT_WAIT_UNTIL(pt, zt_timer_expired(&pmlme_info->traffic_timer));

        {
            zt_u16 BusyThresholdHigh    = 100;
            zt_u16 BusyThresholdLow     = 75;
            BusyThreshold = pmlme_info->link_info.busy_traffic ?
                            BusyThresholdLow : BusyThresholdHigh;
            if (pmlme_info->link_info.num_rx_ok_in_period > BusyThreshold ||
                    pmlme_info->link_info.num_tx_ok_in_period > BusyThreshold)
            {
                pmlme_info->link_info.busy_traffic = zt_true;
            }
            else
            {
                pmlme_info->link_info.busy_traffic = zt_false;
            }
        }

        {
            zt_s32 i;
            for (i = 0; i < TID_NUM; i++)
            {
                pmlme_info->link_info.num_tx_ok_in_period_with_tid[i] = 0;
            }
            pmlme_info->link_info.num_rx_ok_in_period = 0;
            pmlme_info->link_info.num_tx_ok_in_period = 0;
            pmlme_info->link_info.num_rx_unicast_ok_in_period = 0;
        }

        zt_timer_restart(&pmlme_info->traffic_timer);
    }

    PT_END(pt);
}

static zt_pt_ret_t
core_conn_maintain_keepalive_thrd(zt_pt_t *pt,  nic_info_st *pnic_info)
{
    mlme_info_t *pmlme_info = pnic_info->mlme_info;
    wdn_net_info_st *pwdn_info = pmlme_info->pwdn_info;
    zt_s32 rst;

    PT_BEGIN(pt);

    for (;;)
    {
        zt_nic_null_xmit(pnic_info, pwdn_info, zt_false, 0);
        PT_WAIT_WHILE(pt, pmlme_info->link_info.busy_traffic);

        pwdn_info->rx_pkt_stat_last = pwdn_info->rx_pkt_stat;
        zt_timer_set(&pmlme_info->keep_alive_timer, 10 * 1000);
        PT_WAIT_UNTIL(pt, zt_timer_expired(&pmlme_info->keep_alive_timer));

        if (pwdn_info->rx_pkt_stat_last != pwdn_info->rx_pkt_stat)
        {
            continue;
        }

        if (pnic_info->buddy_nic && zt_p2p_is_valid(pnic_info->buddy_nic))
        {
            p2p_info_st *pbuddy_p2p_info = (((nic_info_st *)(pnic_info->buddy_nic))->p2p);
            if (pbuddy_p2p_info->scan_deny || zt_is_scanning(pnic_info->buddy_nic) ||
                pbuddy_p2p_info->p2p_state == P2P_STATE_FIND_PHASE_SEARCH ||
                (pbuddy_p2p_info->go_negoing &&
                !(pbuddy_p2p_info->go_negoing & (ZT_BIT(P2P_GO_NEGO_CONF) | ZT_BIT(P2P_INVIT_RESP)))))
            {
                continue;
            }
        }

        rst = mlme_msg_send(pnic_info, ZT_MLME_TAG_KEEPALIVE, NULL, 0);
        if (rst)
        {
            MLME_WARN("mlme_msg_send fail, error code: %d", rst);
            continue;
        }

        zt_timer_set(&pmlme_info->keep_alive_timer, 10 * 1000);
        PT_WAIT_UNTIL(pt, zt_timer_expired(&pmlme_info->keep_alive_timer));
        if (pwdn_info->rx_pkt_stat_last != pwdn_info->rx_pkt_stat)
        {
            continue;
        }
        rst = zt_mlme_deauth(pnic_info, zt_true, ZT_80211_REASON_DEAUTH_LEAVING);
        if (rst)
        {
            MLME_WARN("zt_mlme_deauth fail, error code: %d", rst);
            continue;
        }

        break;
    }

    PT_END(pt);
}

zt_inline static
zt_pt_ret_t core_conn_maintain(zt_pt_t *pt, nic_info_st *pnic_info)
{
    zt_pt_t *pt_traffic = &pt[1];
    zt_pt_t *pt_keepalive = &pt_traffic[1];
    zt_pt_t *pt_msg = &pt_keepalive[1]; /* use 3 pt object */
#ifdef CONFIG_LPS
    zt_pt_t *pt_lps = &pt_msg[3];
#endif

    PT_BEGIN(pt);

    PT_INIT(pt_traffic);
    PT_INIT(pt_keepalive);
    PT_INIT(pt_msg);
#ifdef CONFIG_LPS
    PT_INIT(pt_lps);
#endif

    for (;;)
    {
        core_conn_maintain_traffic(pt_traffic, pnic_info);
        core_conn_maintain_keepalive_thrd(pt_keepalive, pnic_info);
        if (!PT_SCHEDULE(core_conn_maintain_msg_thrd(pt_msg, pnic_info)))
        {
            break;
        }
#ifdef CONFIG_LPS
        zt_lps_sleep_mlme_monitor(pt_lps, pnic_info);
#endif
        PT_YIELD(pt);
    }

    PT_END(pt);
}

zt_inline static
zt_pt_ret_t mlme_conn_clearup(zt_pt_t *pt, nic_info_st *pnic_info)
{
    mlme_info_t *pmlme_info = pnic_info->mlme_info;
    zt_wlan_mgmt_info_t *pwlan_info =
        (zt_wlan_mgmt_info_t *)pnic_info->wlan_mgmt_info;

    PT_BEGIN(pt);

    MLME_INFO();

    zt_memset(&pmlme_info->link_info, 0, sizeof(pmlme_info->link_info));
    if (pmlme_info->pwdn_info)
    {
        zt_wdn_remove(pnic_info, pwlan_info->cur_network.bssid);
        pmlme_info->pwdn_info = NULL;
    }
    zt_memset(&pwlan_info->cur_network.mac_addr, 0x0, sizeof(zt_80211_addr_t));
    zt_memset(&pwlan_info->cur_network.bssid, 0x0, sizeof(zt_80211_addr_t));

    {
        zt_bool bconnect = zt_false;

        if (pnic_info->buddy_nic != NULL)
            zt_mlme_get_connect(pnic_info->buddy_nic, &bconnect);
        if (!bconnect)
        {
            *pnic_info->hw_bw = 0;
            *pnic_info->hw_offset = 0;
            *pnic_info->hw_ch = 0;
        }
    }

    /* clearup data in tx queue */
    zt_tx_xmit_pending_queue_clear(pnic_info);
    MLME_DBG("wait hif tx data send done");

    if (pnic_info->buddy_nic && zt_local_cfg_get_work_mode(pnic_info->buddy_nic) == ZT_MASTER_MODE)
    {
        zt_tx_xmit_stop(pnic_info->buddy_nic);
    }
    zt_timer_set(&pmlme_info->keep_alive_timer, 5 * 1000);
    PT_WAIT_UNTIL(pt, zt_tx_xmit_hif_queue_empty(pnic_info) ||
                  zt_timer_expired(&pmlme_info->keep_alive_timer));

    if (pnic_info->buddy_nic && zt_local_cfg_get_work_mode(pnic_info->buddy_nic) == ZT_MASTER_MODE)
    {
      zt_tx_xmit_start(pnic_info->buddy_nic);
    }

    PT_END(pt);
}

#ifdef CFG_ENABLE_ADHOC_MODE
zt_pt_ret_t
adhoc_conn_maintain_msg(zt_pt_t *pt, nic_info_st *pnic_info)
{
    mlme_info_t *pmlme_info = pnic_info->mlme_info;
    zt_msg_que_t *pmsg_que = &pmlme_info->msg_que;
    zt_adhoc_info_t *adhoc_info = pnic_info->adhoc_info;
    zt_msg_t *pmsg;
    zt_80211_mgmt_t *pmgmt;
    wdn_net_info_st *pwdn_info;

    PT_BEGIN(pt);

    MLME_DBG();

    for (;;)
    {
        PT_YIELD_UNTIL(pt, !zt_msg_pop(pmsg_que, &pmsg));

        if (pmsg->tag == ZT_MLME_TAG_IBSS_LEAVE)
        {
            MLME_INFO("mlme  leave ibss...");
            zt_msg_del(pmsg_que, pmsg);
            PT_EXIT(pt);
        }
        else if (pmsg->tag == ZT_MLME_TAG_IBSS_BEACON_FRAME)
        {
            /* create wdn if no find the node */
            pmgmt = (zt_80211_mgmt_t *)pmsg->value;

            pwdn_info = zt_wdn_find_info(pnic_info, pmgmt->sa);
            if (pwdn_info)
            {
                MLME_WARN("bss has been build");
                zt_msg_del(pmsg_que, pmsg);
                PT_EXIT(pt);
            }

            pwdn_info = zt_wdn_add(pnic_info, pmgmt->sa);
            if (!pwdn_info)
            {
                MLME_WARN("wdn add fail");
            }
            else
            {
                adhoc_info->asoc_sta_count++;
                zt_adhoc_wdn_info_update(pnic_info, pwdn_info);
                if (zt_adhoc_prc_bcn(pnic_info, pmsg, pwdn_info))
                {
                    zt_wdn_remove(pnic_info, pmgmt->sa);
                }

                if (adhoc_info->asoc_sta_count == 2)
                {
                    zt_os_api_ind_connect(pnic_info, adhoc_info->framework);
                }
            }
        }

        zt_msg_del(pmsg_que, pmsg);
    }

    PT_END(pt);
}

static
zt_pt_ret_t core_conn_scan_ibss_thrd(zt_pt_t *pt, nic_info_st *pnic_info,
                                     mlme_conn_ibss_t *preq, zt_s32 *prsn)
{
    zt_pt_t *pt_sub = &pt[1];
    mlme_info_t *pmlme_info = pnic_info->mlme_info;
    zt_msg_que_t *pmsg_que = &pmlme_info->msg_que;
    zt_msg_t *pmsg;
    zt_s32 rst;

    PT_BEGIN(pt);

    if (preq == NULL)
    {
        MLME_WARN("invalid scan request");
        *prsn = -1;
        PT_EXIT(pt);
    }

    /* start scan */
    rst = zt_scan_start(pnic_info, SCAN_TYPE_ACTIVE,
                        NULL,
                        &preq->ssid, 1,
                        &preq->ch, 1);
    if (rst)
    {
        *prsn = -2;
        MLME_WARN("start fail, error code: %d", rst);
        PT_EXIT(pt);
    }
    MLME_INFO("wait probe respone...");


    /* scan process */
    PT_INIT(pt_sub);
    do
    {
        if (!PT_SCHEDULE(zt_scan_thrd(pt_sub, pnic_info, &rst)))
        {
            MLME_WARN("scan end, reason code: %d", rst);
            *prsn = -3;
            PT_EXIT(pt);
        }

        if (pnic_info->is_surprise_removed)
        {
            *prsn = -1;
            PT_EXIT(pt);
        }

        if (!zt_msg_pop(pmsg_que, &pmsg))
        {
            if (pmsg->tag == ZT_MLME_TAG_SCAN_RSP)
            {
                zt_80211_mgmt_t *pmgmt = (void *)pmsg->value;
                zt_u16 mgmt_len = pmsg->len;
                zt_bool privacy;

                privacy = !!(pmgmt->probe_resp.capab & ZT_80211_MGMT_CAPAB_PRIVACY);
                if (ZT_80211_CAPAB_IS_IBSS(pmgmt->probe_resp.capab) && privacy == zt_false)
                {
                    rst = set_cur_network(pnic_info, pmgmt, mgmt_len);
                    if (rst)
                    {
                        MLME_WARN("set cur_network fail, error code: %d", rst);
                    }
                    else
                    {
                        zt_msg_del(pmsg_que, pmsg);
                        zt_scan_stop(pnic_info);
                        PT_WAIT_THREAD(pt, zt_scan_thrd(pt_sub, pnic_info, &rst));
                        *prsn = 0;
                        break;
                    }
                }
            }
            zt_msg_del(pmsg_que, pmsg);
        }

        PT_YIELD(pt);
    } while (zt_true);

    PT_END(pt);
}
#endif

static zt_pt_ret_t mlme_core_thrd(nic_info_st *pnic_info)
{
    mlme_info_t *pmlme_info = pnic_info->mlme_info;
    zt_pt_t *pt = &pmlme_info->pt[0], *pt_sub = &pt[1];
#ifdef CFG_ENABLE_ADHOC_MODE
    zt_adhoc_info_t *adhoc_info = pnic_info->adhoc_info;
    mlme_conn_ibss_t *param;
#endif
    zt_msg_que_t *pmsg_que = &pmlme_info->msg_que;
    zt_msg_t *pmsg;
    zt_s32 reason;

    PT_BEGIN(pt);

    mlme_set_state(pnic_info, MLME_STATE_IDLE);

    while (zt_true)
    {
        if (pmlme_info->freeze_pend)
        {
            mlme_set_state(pnic_info, MLME_STATE_FREEZE);
            MLME_INFO("thread abort");
            PT_EXIT(pt);
        }
        if (!zt_msg_pop(pmsg_que, &pmsg))
        {
            break;
        }
        PT_YIELD(pt);
    }

    if (pmsg->tag == ZT_MLME_TAG_SCAN)
    {
        mlme_set_state(pnic_info, MLME_STATE_SCAN);
        /* retrive message information */
        pmlme_info->pscan_msg = pmsg;
        /* do scan process */
        MLME_INFO("scanning...");
        PT_SPAWN(pt, pt_sub,
                 core_scan_thrd(pt_sub, pnic_info,
                                (void *)pmlme_info->pscan_msg->value,
                                &reason));
        /* delete scan request message */
        zt_msg_del(pmsg_que, pmlme_info->pscan_msg);
        pmlme_info->pscan_msg = NULL;
    }

    else if (pmsg->tag == ZT_MLME_TAG_CONN)
    {
        {
            mlme_conn_t *pconn_msg = (mlme_conn_t *)pmsg->value;
            MLME_INFO("start conneting to bss: \"%s\" \""ZT_MAC_FMT"\"",
                      pconn_msg->ssid.data, ZT_MAC_ARG(pconn_msg->bssid));
        }

        mlme_set_state(pnic_info, MLME_STATE_CONN_SCAN);
        /* retrive message information */
        pmlme_info->pconn_msg = pmsg;
        /* launch probe request to find target bss */
        MLME_INFO("search bss...");
        PT_SPAWN(pt, pt_sub,
                 core_conn_scan_thrd(pt_sub, pnic_info,
                                     (void *)pmlme_info->pconn_msg->value,
                                     &reason));
        if (reason)
        {
            MLME_WARN("search bss fail, error code: %d", reason);
            goto conn_break;
        }
        MLME_INFO("found bss");

        /* make a new wdn */
        MLME_INFO("build wdn information");
        reason = build_wdn(pnic_info);
        if (reason)
        {
            MLME_WARN("new wdn fail, error code: %d", reason);
            goto conn_break;
        }

        /* auth process */
        MLME_INFO("auth...");
        mlme_set_state(pnic_info, MLME_STATE_AUTH);
        PT_SPAWN(pt, pt_sub, core_conn_auth_thrd(pt_sub, pnic_info, &reason));
        if (reason)
        {
            MLME_WARN("auth fail: auth error code: %d", reason);
            goto conn_break;
        }
        MLME_INFO("auth success");

        /* assoc process */
        MLME_INFO("assoc...");
        mlme_set_state(pnic_info, MLME_STATE_ASSOC);
        PT_SPAWN(pt, pt_sub, core_conn_assoc_thrd(pt_sub, pnic_info, &reason));
        if (reason)
        {
            MLME_WARN("assoc fail: assoc error code: %d", reason);
            goto conn_break;
        }
        MLME_INFO("assoc success");

        /* prepare connect handle */
        PT_SPAWN(pt, pt_sub, core_conn_preconnect(pt_sub, pnic_info, &reason));
        if (reason)
        {
            MLME_WARN("connect fail: preconnect error code: %d", reason);
            goto conn_break;
        }
        zt_rx_ba_all_reinit(pnic_info);

        MLME_INFO("connect success");

        /* connection maintain handler */
        MLME_INFO("connection maintain");
        PT_SPAWN(pt, pt_sub, core_conn_maintain(pt_sub, pnic_info));
        MLME_INFO("connection break");

conn_break:
        {
            mlme_conn_t *pconn_req =
                (mlme_conn_t *)pmlme_info->pconn_msg->value;
            mlme_set_state(pnic_info, MLME_STATE_IDLE);
            if (pconn_req->indicate_en)
            {
                zt_os_api_ind_disconnect(pnic_info, pconn_req->framework);
                mlme_set_connect(pnic_info, zt_false);
            }
        }
        PT_SPAWN(pt, pt_sub, mlme_conn_clearup(pt_sub, pnic_info));
        zt_msg_del(pmsg_que, pmlme_info->pconn_msg);
        pmlme_info->pconn_msg = NULL;
    }

#ifdef CFG_ENABLE_ADHOC_MODE
    else if (pmsg->tag == ZT_MLME_TAG_CONN_IBSS)
    {
        MLME_INFO("seach ibss network...");
        mlme_set_state(pnic_info, MLME_STATE_IBSS_CONN_SCAN);
        pmlme_info->pscan_msg = pmsg;
        /*scanning*/
        PT_SPAWN(pt, pt_sub,
                 core_conn_scan_ibss_thrd(pt_sub, pnic_info,
                                          (void *)pmlme_info->pscan_msg->value,
                                          &reason));
        if (reason)
        {
            MLME_INFO("scan ibss network fall, error code: %d", reason);
        }

        /*join ibss*/
        MLME_INFO("join ibss...");
        param = (mlme_conn_ibss_t *)pmlme_info->pscan_msg->value;
        if (zt_adhoc_ibss_join(pnic_info, param->framework, reason))
        {
            zt_msg_del(pmsg_que, pmlme_info->pscan_msg);
            MLME_INFO("join ibss fall");
        }
        else
        {
            zt_msg_del(pmsg_que, pmlme_info->pscan_msg);
            zt_timer_set(&adhoc_info->timer, ADHOC_KEEPALIVE_TIMEOUT);

            /*keepalive & receive beacon*/
            PT_INIT(pt_sub);
            do
            {
                PT_YIELD(pt);
                zt_adhoc_keepalive_thrd(pnic_info);
            } while (PT_SCHEDULE(adhoc_conn_maintain_msg(pt_sub, pnic_info)));
            mlme_set_connect(pnic_info, zt_false);

            MLME_INFO("leave ibss role");
        }
    }
#endif

    else
    {
        MLME_WARN("drop unsuited message(tag: %d)", pmsg->tag);
        zt_msg_del(pmsg_que, pmsg);
    }

    mlme_set_state(pnic_info, MLME_STATE_IDLE);

    /* restart thread */
    PT_RESTART(pt);

    PT_END(pt);
}

zt_inline static zt_bool mlme_can_run(nic_info_st *pnic_info)
{
    mlme_info_t *pmlme_info = pnic_info->mlme_info;
    pwr_info_st *pwr_info = pnic_info->pwr_info;

    if (!pnic_info->is_up)
    {
        return zt_false;
    }

    if (pmlme_info->freeze_pend)
    {
        return zt_false;
    }

    if (pwr_info->bInSuspend)
    {
        return zt_false;
    }

    return zt_true;
}

static zt_s32 mlme_core(nic_info_st *pnic_info)
{
    mlme_info_t *pmlme_info = pnic_info->mlme_info;

    MLME_DBG();

    zt_os_api_thread_enter_hook(pmlme_info->tid);

    for (;;)
    {
        if (ZT_CANNOT_RUN(pnic_info))
        {
            break;
        }

        if (!mlme_can_run(pnic_info))
        {
            zt_msleep(1);
            continue;
        }

        /* poll mlme core */
        PT_INIT(&pmlme_info->pt[0]);
        while (PT_SCHEDULE(mlme_core_thrd(pnic_info)))
        {
            zt_msleep(1);
        }
    }

    MLME_DBG("wait for thread destory...");
    while (!zt_os_api_thread_wait_stop(pmlme_info->tid))
    {
        zt_msleep(1);
    }

    mlme_set_state(pnic_info, MLME_STATE_TERM);
    zt_os_api_thread_exit(pmlme_info->tid);

    return 0;
}

static zt_s32 mlme_msg_send(nic_info_st *pnic_info,
                            zt_msg_tag_t tag, void *value, zt_u8 len)
{
    mlme_info_t *pmlme_info;
    zt_msg_que_t *pmsg_que;
    zt_msg_t *pmsg;
    zt_s32 rst;

    if (pnic_info == NULL)
    {
        return -1;
    }

    pmlme_info = pnic_info->mlme_info;
    if (pmlme_info == NULL)
    {
        return -2;
    }

    pmsg_que = &pmlme_info->msg_que;
    rst = zt_msg_new(pmsg_que, tag, &pmsg);
    if (rst)
    {
        MLME_WARN("zt_msg_new fail error code: %d", rst);
        return -3;
    }
    if (value && len)
    {
        pmsg->len = len;
        zt_memcpy(pmsg->value, value, len);
    }

    rst = zt_msg_push(pmsg_que, pmsg);
    if (rst)
    {
        zt_msg_del(pmsg_que, pmsg);
        MLME_WARN("zt_msg_push fail error code: %d", rst);
        return -4;
    }

    return 0;
}

zt_s32 zt_mlme_scan_start(nic_info_st *pnic_info, scan_type_e type,
                          zt_wlan_ssid_t ssids[], zt_u8 ssid_num,
                          zt_u8 chs[], zt_u8 ch_num,
                          zt_mlme_framework_e frm_work)
{
    mlme_info_t *pmlme_info;
    zt_msg_que_t *pmsg_que;
    zt_msg_t *pmsg;
    zt_s32 rst;

    if (!pnic_info)
    {
        return -1;
    }

    if (pnic_info->is_driver_critical)
    {
        MLME_WARN("driver enter critical");
        return -2;
    }

    if (!pnic_info->is_up)
    {
        MLME_WARN("ndev is down");
        return -3;
    }

    {
        sys_work_mode_e work_mode = zt_local_cfg_get_work_mode(pnic_info);
        if (work_mode != ZT_INFRA_MODE)
#ifdef CFG_ENABLE_AP_MODE
            if (work_mode != ZT_MASTER_MODE)
#endif
            {
                return -4;
            }
    }

    pmlme_info = pnic_info->mlme_info;
    if (!pmlme_info)
    {
        return -5;
    }
    pmlme_info->freeze_pend = zt_false;

    MLME_DBG();

    pmsg_que = &pmlme_info->msg_que;
    rst = zt_msg_new(pmsg_que, ZT_MLME_TAG_SCAN, &pmsg);
    if (rst)
    {
        MLME_WARN("msg new fail error code: %d", rst);
        return -6;
    }

    {
        mlme_scan_t *param = (mlme_scan_t *)pmsg->value;
        param->type = type;
        param->ssid_num = ssid_num;
        if (ssid_num)
        {
            zt_memcpy(param->ssids, ssids, ssid_num * sizeof(param->ssids[0]));
        }
        param->ch_num = ch_num;
        if (ch_num)
        {
            zt_memcpy(param->chs, chs, ch_num * sizeof(param->chs[0]));
        }
        param->framework = frm_work;
    }

    rst = zt_msg_push(pmsg_que, pmsg);
    if (rst)
    {
        zt_msg_del(pmsg_que, pmsg);
        MLME_WARN("msg push fail error code: %d", rst);
        return -7;
    }

    return 0;
}


zt_s32 zt_mlme_scan_abort(nic_info_st *pnic_info)
{
    zt_s32 rst;

    if (pnic_info == NULL)
    {
        return -1;
    }

    {
        sys_work_mode_e work_mode = zt_local_cfg_get_work_mode(pnic_info);
        if (work_mode != ZT_INFRA_MODE)
#ifdef CFG_ENABLE_AP_MODE
            if (work_mode != ZT_MASTER_MODE)
#endif
            {
                return -2;
            }
    }

    {
        mlme_info_t *pmlme_info = pnic_info->mlme_info;
        if (!(pmlme_info && pmlme_info->pscan_msg &&
                pmlme_info->pscan_msg->tag == ZT_MLME_TAG_SCAN))
        {
            MLME_WARN("no scan aborted");
            return -3;
        }
    }

    MLME_DBG();

    rst = mlme_msg_send(pnic_info, ZT_MLME_TAG_SCAN_ABORT, NULL, 0);
    if (rst)
    {
        return -4;
    }

    return 0;
}

zt_s32 zt_mlme_conn_scan_rsp(nic_info_st *pnic_info,
                             zt_80211_mgmt_t *pmgmt, zt_u16 mgmt_len)
{
    mlme_info_t *pmlme_info;
    zt_msg_que_t *pmsg_que;
    zt_msg_t *pmsg;
    zt_s32 rst;

    if (pnic_info == NULL || (pmgmt == NULL && mgmt_len == 0))
    {
        return -1;
    }

    if (!pnic_info->is_up)
    {
        MLME_WARN("ndev down");
        return -2;
    }

    if (!zt_is_scanning(pnic_info))
    {
        return -3;
    }
    pmlme_info = pnic_info->mlme_info;
    pmsg_que = &pmlme_info->msg_que;

    MLME_DBG();

    rst = zt_msg_new(pmsg_que, ZT_MLME_TAG_SCAN_RSP, &pmsg);
    if (rst)
    {
        MLME_WARN("msg new fail error code: %d", rst);
        return -4;
    }
    /* copy frame */
    pmsg->len = mgmt_len;
    zt_memcpy(pmsg->value, pmgmt, mgmt_len);
    rst = zt_msg_push(pmsg_que, pmsg);
    if (rst)
    {
        zt_msg_del(pmsg_que, pmsg);
        MLME_WARN("msg push fail error code: %d", rst);
        return -5;
    }

    return 0;
}

zt_s32 zt_mlme_conn_start(nic_info_st *pnic_info, zt_80211_bssid_t bssid,
                          zt_wlan_ssid_t *pssid, zt_mlme_framework_e frm_work,
                          zt_bool indicate_en)
{
    mlme_info_t *pmlme_info;
    zt_msg_que_t *pmsg_que;
    zt_msg_t *pmsg;
    mlme_conn_t *param;
    zt_s32 rst;

    if (!pnic_info)
    {
        return -1;
    }

    if (pnic_info->is_driver_critical)
    {
        return -2;
    }

    if (!pnic_info->is_up)
    {
        MLME_WARN("ndev down");
        return -3;
    }
#ifdef CFG_ENABLE_AP_MODE
    if (zt_local_cfg_get_work_mode(pnic_info) == ZT_MASTER_MODE)
    {
        MLME_WARN("ap no support");
        return -4;
    }
#endif

    pmlme_info = pnic_info->mlme_info;
    if (!pmlme_info)
    {
        return -5;
    }
    pmlme_info->freeze_pend = zt_false;

    if (!bssid && !pssid)
    {
        return -6;
    }

    MLME_DBG();

    pmsg_que = &pmlme_info->msg_que;
    if (pmlme_info->pconn_msg)
    {
        MLME_DBG("abort current connection");
        zt_mlme_conn_abort(pnic_info, zt_true, ZT_80211_REASON_UNSPECIFIED);
    }
    /* delete existing connect message */
    if (!zt_msg_get(pmsg_que, &pmsg) && pmsg->tag == ZT_MLME_TAG_CONN)
    {
        zt_msg_del(pmsg_que, pmsg);
    }

    rst = zt_msg_new(pmsg_que, ZT_MLME_TAG_CONN, &pmsg);
    if (rst)
    {
        MLME_WARN("msg new fail error code: %d", rst);
        return -7;
    }
    param = (mlme_conn_t *)pmsg->value;

    param->indicate_en = indicate_en;
    /* set bssid */
    if (bssid)
    {
        zt_memcpy(param->bssid, bssid, sizeof(param->bssid));
    }
    else
    {
        zt_memset(param->bssid, 0, sizeof(param->bssid));
    }
    /* set ssid */
    if (pssid && pssid->length)
    {
        zt_memcpy(&param->ssid, pssid, sizeof(param->ssid));
        param->ssid.data[param->ssid.length] = '\0';
    }
    else
    {
        param->ssid.length = 0;
    }
    /* set framework */
    param->framework = frm_work;

    rst = zt_msg_push(pmsg_que, pmsg);
    if (rst)
    {
        zt_msg_del(pmsg_que, pmsg);
        MLME_WARN("msg push fail error code: %d", rst);
        return -8;
    }

    return 0;
}

zt_s32 zt_mlme_conn_abort(nic_info_st *pnic_info,
                          zt_bool local_gen,
                          zt_80211_reasoncode_e reason)
{
    zt_s32 rst;

    if (!pnic_info)
    {
        MLME_ERROR("null point");
        return -1;
    }

    if (zt_local_cfg_get_work_mode(pnic_info) != ZT_INFRA_MODE)
    {
        MLME_WARN("unsuited role");
        return -2;
    }

    {
        mlme_info_t *pmlme_info = pnic_info->mlme_info;
        if (!(pmlme_info && pmlme_info->pconn_msg &&
                pmlme_info->pconn_msg->tag == ZT_MLME_TAG_CONN))
        {
            MLME_WARN("no connection aborted");
            return -3;
        }
    }

    MLME_DBG();

    {
        mlme_conn_abort_t value =
        {
            .local_disconn  = local_gen,
            .reason_code    = reason,
        };
        rst = mlme_msg_send(pnic_info, ZT_MLME_TAG_CONN_ABORT,
                            &value, sizeof(mlme_conn_abort_t));
        if (rst)
        {
            return -4;
        }
    }

    return 0;
}

zt_s32 zt_mlme_deauth(nic_info_st *pnic_info,
                      zt_bool local_gen,
                      zt_80211_reasoncode_e reason)
{
    zt_s32 rst;

    if (pnic_info == NULL)
    {
        return -1;
    }

    if (!pnic_info->is_up)
    {
        MLME_WARN("ndev down");
        return -2;
    }

    if (zt_local_cfg_get_work_mode(pnic_info) != ZT_INFRA_MODE)
    {
        MLME_WARN("unsuited role");
        return -3;
    }

    MLME_DBG();

    {
        mlme_deauth_t value =
        {
            .local_disconn  = local_gen,
            .reason_code    = reason,
        };
        rst = mlme_msg_send(pnic_info, ZT_MLME_TAG_DEAUTH,
                            &value, sizeof(mlme_deauth_t));
        if (rst)
        {
            return -4;
        }
    }

    return 0;
}

zt_s32 zt_mlme_deassoc(nic_info_st *pnic_info,
                       zt_bool local_gen,
                       zt_80211_reasoncode_e reason)

{
    zt_s32 rst;

    if (pnic_info == NULL)
    {
        return -1;
    }

    if (!pnic_info->is_up)
    {
        MLME_WARN("ndev down");
        return -2;
    }

    if (zt_local_cfg_get_work_mode(pnic_info) != ZT_INFRA_MODE)
    {
        MLME_WARN("unsuited role");
        return -3;
    }

    MLME_DBG();

    {
        mlme_deauth_t value =
        {
            .local_disconn  = local_gen,
            .reason_code    = reason,
        };
        rst = mlme_msg_send(pnic_info, ZT_MLME_TAG_DEASSOC,
                            &value, sizeof(mlme_deassoc_t));
        if (rst)
        {
            return -4;
        }
    }

    return 0;
}

zt_s32 zt_mlme_add_ba_req(nic_info_st *pnic_info)
{
    zt_s32 rst;

    if (pnic_info == NULL)
    {
        return -1;
    }

    if (!pnic_info->is_up)
    {
        MLME_WARN("ndev down");
        return -2;
    }

    MLME_DBG();

    rst = mlme_msg_send(pnic_info, ZT_MLME_TAG_ADD_BA_REQ, NULL, 0);
    if (rst)
    {
        return -3;
    }

    return 0;
}

zt_s32 zt_mlme_add_ba_rsp(nic_info_st *pnic_info, zt_add_ba_parm_st *barsp_parm)
{
    zt_s32 rst;

    if (pnic_info == NULL)
    {
        return -1;
    }

    if (!pnic_info->is_up)
    {
        MLME_WARN("ndev down");
        return -2;
    }

    MLME_DBG();

    rst = mlme_msg_send(pnic_info, ZT_MLME_TAG_ADD_BA_RSP,
                        barsp_parm, sizeof(zt_add_ba_parm_st));
    if (rst)
    {
        return -3;
    }

    return 0;
}

static zt_s32 mlme_set_state(nic_info_st *pnic_info, mlme_state_e state)
{
    mlme_info_t *pmlme_info;

    if (pnic_info == NULL)
    {
        return -1;
    }

    pmlme_info = pnic_info->mlme_info;
    if (pmlme_info == NULL)
    {
        return -2;
    }

    zt_os_api_lock_lock(&pmlme_info->state_lock);
    pmlme_info->state = state;
    zt_os_api_lock_unlock(&pmlme_info->state_lock);

    return 0;
}

zt_s32 zt_mlme_get_state(nic_info_st *pnic_info, mlme_state_e *state)
{
    mlme_info_t *pmlme_info;

    if (pnic_info == NULL)
    {
        return -1;
    }

    pmlme_info = pnic_info->mlme_info;
    if (pmlme_info == NULL)
    {
        return -2;
    }

    zt_os_api_lock_lock(&pmlme_info->state_lock);
    *state = pmlme_info->state;
    zt_os_api_lock_unlock(&pmlme_info->state_lock);

    return 0;
}

zt_s32 mlme_set_connect(nic_info_st *pnic_info, zt_bool bconnect)
{
    mlme_info_t *pmlme_info;

    if (pnic_info == NULL)
    {
        return -1;
    }

    pmlme_info = pnic_info->mlme_info;
    if (pmlme_info == NULL)
    {
        return -2;
    }
    zt_os_api_lock_lock(&pmlme_info->connect_lock);
    pmlme_info->connect = bconnect;
    zt_os_api_lock_unlock(&pmlme_info->connect_lock);

    return 0;
}

zt_s32 zt_mlme_get_connect(nic_info_st *pnic_info, zt_bool *bconnect)
{
    mlme_info_t *pmlme_info;

    *bconnect = 0;
    if (pnic_info == NULL)
    {
        MLME_ERROR("pnic_info == NULL");
        return -1;
    }

    pmlme_info = pnic_info->mlme_info;
    if (pmlme_info == NULL)
    {
        MLME_DBG("pnic_info->mlme_info == NULL");
        return -2;
    }

    zt_os_api_lock_lock(&pmlme_info->connect_lock);
    *bconnect = pmlme_info->connect;
    zt_os_api_lock_unlock(&pmlme_info->connect_lock);

    return 0;
}

zt_s32 zt_mlme_get_traffic_busy(nic_info_st *pnic_info, zt_bool *bbusy)
{
    mlme_info_t *pmlme_info;

    *bbusy = 0;
    if (pnic_info == NULL)
    {
        return -1;
    }

    pmlme_info = pnic_info->mlme_info;
    if (pmlme_info == NULL)
    {
        return -2;
    }

    *bbusy = pmlme_info->link_info.busy_traffic;

    return 0;
}

zt_s32 zt_mlme_abort(nic_info_st *pnic_info)
{
    mlme_info_t *pmlme_info = pnic_info->mlme_info;

    switch (zt_local_cfg_get_work_mode(pnic_info))
    {
        case ZT_INFRA_MODE :
            zt_mlme_conn_abort(pnic_info, zt_true, ZT_80211_REASON_DEAUTH_LEAVING);
            zt_scan_wait_done(pnic_info, zt_true, 400);
            break;
#ifdef CFG_ENABLE_AP_MODE
        case ZT_MASTER_MODE :
            zt_mlme_scan_abort(pnic_info);
            break;
#endif

        default :
            break;
    }
    pmlme_info->freeze_pend = zt_true;

    MLME_INFO("wait until mlme abort done");
    while ((pmlme_info->state & MLME_STA_RUN_BIT) || pmlme_info->connect)
    {
        zt_msleep(10);
    }
    MLME_INFO("mlme abort done");

    return 0;
}

#ifdef CFG_ENABLE_ADHOC_MODE
zt_s32 zt_mlme_scan_ibss_start(nic_info_st *pnic_info,
                               zt_wlan_ssid_t *pssid,
                               zt_u8 *pch,
                               zt_mlme_framework_e frm_work)
{
    mlme_info_t *pmlme_info;
    zt_msg_que_t *pmsg_que;
    zt_msg_t *pmsg;
    zt_s32 rst;

    if (!pnic_info)
    {
        return -1;
    }

    if (pnic_info->is_driver_critical)
    {
        return -2;
    }

    if (!pnic_info->is_up)
    {
        MLME_WARN("ndev is down");
        return -3;
    }

    pmlme_info = pnic_info->mlme_info;
    if (!pmlme_info)
    {
        return -4;
    }
    pmlme_info->freeze_pend = zt_false;

    if (!pssid || !pch)
    {
        return -5;
    }

    MLME_DBG();

    pmsg_que = &pmlme_info->msg_que;
    rst = zt_msg_new(pmsg_que, ZT_MLME_TAG_CONN_IBSS, &pmsg);
    if (rst)
    {
        MLME_WARN("msg new fail error code: %d", rst);
        return -6;
    }

    {
        mlme_conn_ibss_t *param = (mlme_conn_ibss_t *)pmsg->value;
        zt_memcpy(&param->ssid, pssid, sizeof(param->ssid));
        zt_memcpy(&param->ch, pch, sizeof(zt_u8));
        param->framework = frm_work;

    }


    rst = zt_msg_push(pmsg_que, pmsg);
    if (rst)
    {
        zt_msg_del(pmsg_que, pmsg);
        MLME_WARN("msg push fail error code: %d", rst);
        return -7;
    }

    return 0;
}
#endif

zt_inline static zt_s32 mlme_msg_init(zt_msg_que_t *pmsg_que)
{
    zt_msg_init(pmsg_que);
    return (zt_msg_alloc(pmsg_que, ZT_MLME_TAG_SCAN_ABORT, 0, 2) ||
            zt_msg_alloc(pmsg_que, ZT_MLME_TAG_CONN_ABORT, sizeof(mlme_conn_abort_t), 2) ||
            zt_msg_alloc(pmsg_que, ZT_MLME_TAG_DEAUTH, sizeof(mlme_deauth_t), 1) ||
            zt_msg_alloc(pmsg_que, ZT_MLME_TAG_DEASSOC, sizeof(mlme_deassoc_t), 1) ||
            zt_msg_alloc(pmsg_que, ZT_MLME_TAG_SCAN, sizeof(mlme_scan_t), 2) ||
            zt_msg_alloc(pmsg_que, ZT_MLME_TAG_SCAN_RSP, sizeof(mlme_scan_rsp_t), 2) ||
            zt_msg_alloc(pmsg_que, ZT_MLME_TAG_CONN, sizeof(mlme_conn_t), 2) ||
            zt_msg_alloc(pmsg_que, ZT_MLME_TAG_ADD_BA_REQ, 0, TID_NUM) ||
            zt_msg_alloc(pmsg_que, ZT_MLME_TAG_ADD_BA_RSP, sizeof(zt_add_ba_parm_st),
                         TID_NUM) ||
#ifdef CFG_ENABLE_ADHOC_MODE
            zt_msg_alloc(pmsg_que, ZT_MLME_TAG_CONN_IBSS, sizeof(mlme_conn_ibss_t), 1) ||
            zt_msg_alloc(pmsg_que, ZT_MLME_TAG_IBSS_LEAVE, 0, 1) ||
            zt_msg_alloc(pmsg_que, ZT_MLME_TAG_IBSS_BEACON_FRAME, sizeof(beacon_frame_t),
                         1) ||
#endif
#ifdef CONFIG_LPS
            zt_msg_alloc(pmsg_que, ZT_MLME_TAG_LPS, 0, 1) ||
#endif
            zt_msg_alloc(pmsg_que, ZT_MLME_TAG_KEEPALIVE, 0, 1)) ? -1 : 0;
}

zt_inline static void mlme_msg_deinit(zt_msg_que_t *pmsg_que)
{
    zt_msg_deinit(pmsg_que);
}

zt_s32 zt_mlme_init(nic_info_st *pnic_info)
{
    mlme_info_t *pmlme_info;

    MLME_DBG();

    pmlme_info = zt_kzalloc(sizeof(mlme_info_t));
    if (pmlme_info == NULL)
    {
        MLME_WARN("malloc mlme_info_t failed");
        return -1;
    }
    pnic_info->mlme_info = pmlme_info;
    pmlme_info->parent = pnic_info;
    zt_os_api_lock_init(&pmlme_info->state_lock, ZT_LOCK_TYPE_IRQ);
    zt_os_api_lock_init(&pmlme_info->connect_lock, ZT_LOCK_TYPE_IRQ);
    mlme_set_connect(pnic_info, zt_false);
    pmlme_info->freeze_pend = zt_true;
    mlme_set_state(pnic_info, MLME_STATE_INIT);
    if (mlme_msg_init(&pmlme_info->msg_que))
    {
        MLME_WARN("malloc msg init failed");
        return -2;
    }
    zt_sprintf(pmlme_info->mlmeName, "mlme_%d%d",
               pnic_info->hif_node_id, pnic_info->ndev_id);

    pmlme_info->tid = zt_os_api_thread_create(pmlme_info->tid,
                      pmlme_info->mlmeName,
                      (void *)mlme_core,
                      pnic_info);
    if (pmlme_info->tid == NULL)
    {
        MLME_WARN("create mlme thread failed");
        return -3;
    }
    zt_os_api_thread_wakeup(pmlme_info->tid);

    return 0;
}

zt_s32 zt_mlme_term(nic_info_st *pnic_info)
{
    mlme_info_t *pmlme_info;

    MLME_DBG();
    pmlme_info = pnic_info->mlme_info;
    if (pmlme_info == NULL)
    {
        return -1;
    }

    zt_mlme_abort(pnic_info);

    MLME_DBG("destory thread");
    if (pmlme_info->tid)
    {
        zt_os_api_thread_destory(pmlme_info->tid);
        pmlme_info->tid = 0;
    }

    MLME_DBG("del msg que");
    mlme_msg_deinit(&pmlme_info->msg_que);

    MLME_DBG("del lock");
    zt_os_api_lock_term(&pmlme_info->state_lock);
    zt_os_api_lock_term(&pmlme_info->connect_lock);

    MLME_DBG("free pmlme_info");
    zt_kfree(pmlme_info);
    pnic_info->mlme_info = NULL;

    MLME_DBG("end");

    return 0;
}

zt_s32 zt_mlme_suspend(nic_info_st *pnic_info)
{
    MLME_DBG();

    zt_mlme_abort(pnic_info);

    return 0;
}

zt_s32 zt_mlme_resume(nic_info_st *pnic_info)
{
    MLME_DBG();

    if (zt_local_cfg_get_work_mode(pnic_info) == ZT_INFRA_MODE)
    {
        mlme_info_t *pmlme_info = pnic_info->mlme_info;
        pmlme_info->freeze_pend = zt_false;
        mlme_set_state(pnic_info, MLME_STATE_INIT);
    }

    return 0;
}

