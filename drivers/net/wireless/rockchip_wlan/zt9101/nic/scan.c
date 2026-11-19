/*
 * scan.c
 *
 * impliment of IEEE80211 management frame scan stage processing
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
#define SCAN_DBG(fmt, ...)      LOG_D("[%s:%d][%d]"fmt, __func__, __LINE__, pnic_info->ndev_id, ##__VA_ARGS__)
#define SCAN_ARRAY(data, len)   zt_log_array(data, len)
#define SCAN_INFO(fmt, ...)     LOG_I("[%s:%d][%d]"fmt, __func__, __LINE__, pnic_info->ndev_id, ##__VA_ARGS__)
#define SCAN_WARN(fmt, ...)     LOG_W("[%s:%d][%d]"fmt, __func__, __LINE__, pnic_info->ndev_id, ##__VA_ARGS__)
#define SCAN_ERROR(fmt, ...)    LOG_E("[%s:%d][%d]"fmt, __func__, __LINE__, pnic_info->ndev_id, ##__VA_ARGS__)

#define LOCAL_INFO                  ((local_info_st *)pnic_info->local_info)
#define SCAN_CH_TIMEOUT             LOCAL_INFO->scan_ch_to
#define SCAN_PROBE_RESEND_TIMES     LOCAL_INFO->scan_prb_times

/* function declaration */

zt_inline static zt_s32 tx_cutoff(nic_info_st *pnic_info)
{
    zt_tx_xmit_stop(pnic_info);

    return 0;
}

zt_inline static zt_s32 tx_resume(nic_info_st *pnic_info)
{
    zt_tx_xmit_start(pnic_info);

    return 0;
}

zt_inline static zt_bool is_tx_empty(nic_info_st *pnic_info)
{
    return !!zt_tx_xmit_hif_queue_empty(pnic_info);
}

static zt_s32 ps_mode(nic_info_st *pnic_info, zt_bool ps_en)
{
    mlme_info_t *pmlme_info;
    wdn_net_info_st *pwdn_info;
    zt_bool is_connected = zt_false;
    zt_s32 try_cnt[] = { 1, 1 };

    zt_mlme_get_connect(pnic_info, &is_connected);
    if (is_connected)
    {
        pmlme_info = pnic_info->mlme_info;
        pwdn_info = pmlme_info->pwdn_info;
        while (zt_nic_null_xmit(pnic_info, pwdn_info, ps_en, 0) && try_cnt[0]--);
    }

    if (pnic_info->buddy_nic)
    {
        nic_info_st *pnic_buddy = pnic_info->buddy_nic;

        if (zt_local_cfg_get_work_mode(pnic_buddy) == ZT_INFRA_MODE)
        {
            is_connected = zt_false;
            zt_mlme_get_connect(pnic_buddy, &is_connected);
            if (is_connected)
            {
                pmlme_info = pnic_buddy->mlme_info;
                pwdn_info = pmlme_info->pwdn_info;
                while (zt_nic_null_xmit(pnic_buddy, pwdn_info, ps_en, 0) && try_cnt[1]--);
            }
        }
    }

    return try_cnt[0] | try_cnt[1];
}

zt_inline static
zt_s32 scan_setting(nic_info_st *pnic_info)
{
    zt_scan_info_t *pscan_info = pnic_info->scan_info;
    zt_bool bch_spec = !!pscan_info->preq->ch_num;

    if (pscan_info->preq->type == SCAN_TYPE_PASSIVE)
    {
        /* set media status */
        if (zt_mcu_set_media_status(pnic_info, WIFI_FW_NULL_STATE))
        {
            return -1;
        }
    }
    else
    {
        zt_bool bfix_ch = zt_false;
        if (zt_80211_is_valid_bssid(pscan_info->preq->bssid))
        {
            /* match in scan queue */
            zt_wlan_mgmt_scan_que_node_t *pscan_que_node;
            zt_wlan_mgmt_scan_que_for_rst_e rst;
            SCAN_DBG("target bssid: "ZT_MAC_FMT,
                     ZT_MAC_ARG(pscan_info->preq->bssid));
            zt_wlan_mgmt_scan_que_for_begin(pnic_info, pscan_que_node)
            {
                if (!zt_memcmp(pscan_que_node->bssid, pscan_info->preq->bssid,
                               sizeof(pscan_que_node->bssid)))
                {
                    SCAN_DBG("found bss in scan queue");
                    break;
                }
            }
            zt_wlan_mgmt_scan_que_for_end(rst);

            if (rst == ZT_WLAN_MGMT_SCAN_QUE_FOR_RST_FAIL)
            {
                SCAN_WARN("get semphone fail!!!!!!!!!!!!!!!!!!!!!!!!!");
            }

            if (rst == ZT_WLAN_MGMT_SCAN_QUE_FOR_RST_BREAK)
            {
                /* if channel map be specified, check if valid */
                if (bch_spec)
                {
                    zt_u8 i;
                    for (i = 0; i < pscan_info->preq->ch_num; i++)
                    {
                        if (pscan_que_node->channel == pscan_info->preq->ch_map[i])
                        {
                            bfix_ch = zt_true;
                            break;
                        }
                    }
                }
                else
                {
                    bfix_ch = zt_true;
                }

                if (bfix_ch)
                {
                    /* switch scan channel to specified by the node found in
                       the scan queue. */
                    SCAN_DBG("fix scan channel number: %d",
                             pscan_que_node->channel);
                    pscan_info->preq->ch_num = 1;
                    pscan_info->preq->ch_map[0] = pscan_que_node->channel;
                }
            }

            /* set scan filte */
            if (zt_mcu_set_bssid(pnic_info, pscan_info->preq->bssid))
            {
                return -2;
            }
        }
    }

    if (pscan_info->preq->ch_num == 0)
    {
        /* channel no specify so channel setting reference to local
        hardware support */
        hw_info_st *phw_info = (hw_info_st *)pnic_info->hw_info;
        zt_u8 i;
        pscan_info->preq->ch_num = phw_info->max_chan_nums;
        for (i = 0; i < phw_info->max_chan_nums; i++)
        {
            pscan_info->preq->ch_map[i] = phw_info->channel_set[i].channel_num;
        }
    }

    if (bch_spec)
    {
        /* clearup scan queue, avoid queue contain invalid channel information */
        //        zt_wlan_mgmt_scan_que_flush(pnic_info);
    }

    /* backup current channel setting */
    if (zt_hw_info_get_channel_bw_ext(pnic_info,
                                   &pscan_info->chnl_bak.number,
                                   &pscan_info->chnl_bak.width,
                                   &pscan_info->chnl_bak.offset) != ZT_RETURN_OK)
    {
        return -3;
    }

    /* disable bssid filter of beacon and probe response */
    if (zt_mcu_set_mlme_scan(pnic_info, zt_true))
    {
        return -4;
    }
    SCAN_INFO("Disbale BSSID Filter");

    /* enter power save mode */
    ps_mode(pnic_info, zt_true);

#ifdef CONFIG_LPS
    {
        zt_bool is_connected = zt_false;

        zt_mlme_get_connect(pnic_info, &is_connected);
        if (is_connected)
        {
            zt_lps_wakeup(pnic_info, LPS_CTRL_SCAN, 0);
            if (pnic_info->buddy_nic)
            {
                zt_lps_wakeup((nic_info_st *)(pnic_info->buddy_nic), LPS_CTRL_SCAN, 0);
            }
        }
    }
#endif

    return 0;
}

zt_inline static zt_s32 scan_setting_recover(nic_info_st *pnic_info)
{
    zt_scan_info_t *pscan_info = pnic_info->scan_info;

    zt_mcu_set_media_status(pnic_info, WIFI_FW_STATION_STATE);

    /* recover channel setting from backup */
    if (zt_hw_info_set_channel_bw(pnic_info,
                                   pscan_info->chnl_bak.number,
                                   pscan_info->chnl_bak.width,
                                   pscan_info->chnl_bak.offset) == ZT_RETURN_FAIL)
    {
        SCAN_WARN("UMSG_OPS_HAL_CHNLBW_MODE failed");
        return -1;
    }

    /* enable bssid filter for beacon and probe response */
    zt_mcu_set_mlme_scan(pnic_info, zt_false);
    SCAN_INFO("Enable BSSID Filter");

    /* exit power save mode */
    ps_mode(pnic_info, zt_false);

#ifdef CFG_ENABLE_AP_MODE
    if (zt_ap_resume_bcn(pnic_info->buddy_nic))
    {
        return -2;
    }
#endif

    return 0;
}

zt_s32 zt_scan_probe_send(nic_info_st *pnic_info)
{
    struct xmit_buf *pxmit_buf;
    zt_80211_mgmt_t *pframe;
    zt_scan_info_t *pscan_info;
    hw_info_st *hw_info;
    zt_u32 var_len;
    zt_u8 *pvar;

    SCAN_DBG();

    if (pnic_info == NULL)
    {
        return -1;
    }
    pscan_info  = pnic_info->scan_info;
    hw_info     = pnic_info->hw_info;

    /* alloc xmit_buf */
    {
        tx_info_st *ptx_info = pnic_info->tx_info;
        pxmit_buf = zt_xmit_extbuf_new(ptx_info);
        if (pxmit_buf == NULL)
        {
            SCAN_WARN("pxmit_buf is NULL");
            return -2;
        }
    }

    /* set frame head */
    zt_memset(pxmit_buf->pbuf, 0,
              TXDESC_OFFSET + ZT_OFFSETOF(zt_80211_mgmt_t, probe_req));
    pframe = (void *)&pxmit_buf->pbuf[TXDESC_OFFSET];

    /* set control field */
    zt_80211_hdr_type_set(pframe, ZT_80211_FRM_PROBE_REQ);

    /* set address field */
    zt_memset(pframe->da, 0xff, sizeof(pframe->da));
    zt_memcpy(pframe->sa, nic_to_local_addr(pnic_info), sizeof(pframe->sa));
    zt_memset(pframe->bssid, 0xff, sizeof(pframe->bssid));

    /* set variable field */
    var_len = 0;
    pvar = &pframe->probe_req.variable[0];
    /*1.SSID*/
    {
        if (pscan_info->preq->ssid_num)
        {
            pvar = set_ie(pvar, ZT_80211_MGMT_EID_SSID,
                          pscan_info->preq->ssids[0].length,
                          pscan_info->preq->ssids[0].data,
                          &var_len);
        }
        else
        {
            pvar = set_ie(pvar, ZT_80211_MGMT_EID_SSID, 0, NULL, &var_len);
        }
    }
    /*2.Supported Rates and BSS Membership Selectors*/
    pvar = set_ie(pvar, ZT_80211_MGMT_EID_SUPP_RATES,
                  8, &hw_info->datarate[0], &var_len);
    /*3.Extended Supported Rates and BSS Membership Selectors*/
    pvar = set_ie(pvar, ZT_80211_MGMT_EID_EXT_SUPP_RATES,
                  4, &hw_info->datarate[8], &var_len);

    /* frame send */
    pxmit_buf->pkt_len =
        ZT_OFFSETOF(zt_80211_mgmt_t, probe_req.variable) + var_len;
    if (zt_nic_mgmt_frame_xmit(pnic_info, NULL, pxmit_buf, pxmit_buf->pkt_len))
    {
        SCAN_WARN("probe frame send fail");
        return -3;
    }

    return 0;
}

zt_inline static
zt_s32 check_bssid(zt_scan_info_t *pscan_info, zt_80211_mgmt_t *pmgmt)
{
    if (zt_80211_is_valid_bssid(pscan_info->preq->bssid))
    {
        if (zt_memcmp(pscan_info->preq->bssid,
                      pmgmt->bssid, sizeof(pscan_info->preq->bssid)))
        {
            return 1;
        }
    }

    return 0;
}

zt_inline static
zt_s32 check_ssid(zt_scan_info_t *pscan_info, zt_u8 *pies, zt_u16 ies_len)
{
    zt_u8 i;
    zt_80211_mgmt_ie_t *pie;

    if (pscan_info->preq->ssid_num)
    {
        if (zt_80211_mgmt_ies_search(pies, ies_len, ZT_80211_MGMT_EID_SSID, &pie))
        {
            //            SCAN_WARN("no ssid element field");
            return -1;
        }
        for (i = 0; i < pscan_info->preq->ssid_num; i++)
        {
            if (pscan_info->preq->ssids[i].length == pie->len ||
                    !zt_memcmp(pscan_info->preq->ssids[i].data, pie->data, pie->len))
            {
                break;
            }
        }
        if (i == pscan_info->preq->ssid_num)
        {
            return 1;
        }
    }

    return 0;
}

zt_inline static
zt_s32 check_channel(zt_scan_info_t *pscan_info, zt_u8 *pies, zt_u16 ies_len)
{
    zt_80211_mgmt_ie_t *pie;

    if (zt_80211_mgmt_ies_search(pies, ies_len,
                                 ZT_80211_MGMT_EID_DS_PARAMS, &pie))
    {
        //        SCAN_WARN("no DS element field");
        return -1;
    }
    {
        zt_80211_mgmt_dsss_parameter_t *pds = (void *)pie->data;
        zt_u8 ch = pds->current_channel;
        zt_u8 i;
        for (i = 0; i < pscan_info->preq->ch_num; i++)
        {
            if (pscan_info->preq->ch_map[i] == ch)
            {
                break;
            }
        }
        if (i == pscan_info->preq->ch_num)
        {
            return 1;
        }
    }

    return 0;
}

zt_s32 zt_scan_filter(nic_info_st *pnic_info,
                      zt_80211_mgmt_t *pmgmt, zt_u16 mgmt_len)
{
    zt_scan_info_t *pscan_info;
    zt_s32 rst = 0;

    if (pnic_info == NULL || pmgmt == NULL || mgmt_len == 0)
    {
        SCAN_WARN("invalid paramete");
        return -1;
    }
    pscan_info = pnic_info->scan_info;

    if (ZT_CANNOT_RUN(pnic_info))
    {
        return -2;
    }

    if (!zt_is_scanning(pnic_info))
    {
        return 0;
    }

    if (zt_os_api_sema_try(&pscan_info->req_lock))
    {
        return 0;
    }

    /* check frame if legality */
    {
        zt_u8 *pies = &pmgmt->probe_resp.variable[0];
        zt_u16 ies_len = mgmt_len - ZT_OFFSETOF(struct beacon_ie, variable);

		if (!zt_p2p_is_valid(pnic_info))
		{
	        rst = check_bssid(pscan_info, pmgmt);
	        if (rst)
	        {
	            rst = rst < 0 ? -3 : 0;
	            goto exit;
	        }
	        rst = check_ssid(pscan_info, pies, ies_len);
	        if (rst)
	        {
	            rst = rst < 0 ? -4 : 0;
	            goto exit;
	        }
	        rst = check_channel(pscan_info, pies, ies_len);
	        if (rst)
	        {
	            rst = rst < 0 ? -5 : 0;
	            goto exit;
	        }

		    if ((zt_80211_hdr_type_get(pmgmt) == ZT_80211_FRM_PROBE_RESP &&
		            zt_80211_is_same_addr(pmgmt->da, nic_to_local_addr(pnic_info))) ||
		            (zt_80211_hdr_type_get(pmgmt) == ZT_80211_FRM_BEACON  &&
		            zt_80211_is_same_addr(pmgmt->sa, pscan_info->preq->bssid)))
		    {
		        mlme_state_e state;
		        zt_mlme_get_state(pnic_info, &state);
		        if (state == MLME_STATE_CONN_SCAN || state == MLME_STATE_IBSS_CONN_SCAN)
		        {
		            zt_mlme_conn_scan_rsp(pnic_info, pmgmt, mgmt_len);
		        }
		    }
		}
		else
		{
			SCAN_DBG("%s, mac = "ZT_MAC_FMT,  zt_80211_hdr_type_get(pmgmt) == ZT_80211_FRM_PROBE_RESP ?
					 "probersp" : "beacon", ZT_MAC_ARG(pmgmt->sa));

		/* if match probe respone frame, send nofity message */
		    if (pscan_info->preq->type == SCAN_TYPE_ACTIVE &&
				zt_80211_is_same_addr(pmgmt->sa, pscan_info->preq->bssid))
		    {
		        mlme_state_e state;
		        zt_mlme_get_state(pnic_info, &state);
		        if (state == MLME_STATE_CONN_SCAN || state == MLME_STATE_IBSS_CONN_SCAN)
		        {
		            zt_mlme_conn_scan_rsp(pnic_info, pmgmt, mgmt_len);
		        }
		    }
		}
	}

exit:
    zt_os_api_sema_post(&pscan_info->req_lock);
    return rst;
}

zt_inline static
zt_pt_ret_t zt_sta_scan_thrd(zt_pt_t *pt, nic_info_st *pnic_info, zt_s32 *prsn)
{
    zt_scan_info_t *pscan_info;
    zt_msg_que_t *pmsg_que;
    zt_msg_t *pmsg;
    zt_s32 reason = ZT_SCAN_TAG_DONE;
    p2p_info_st *p2p_info = pnic_info->p2p;
    zt_u32 scan_timeout;
    zt_s32 rst;

    if (pt == NULL || pnic_info == NULL || prsn == NULL)
    {
        PT_EXIT(pt);
    }
    pscan_info = pnic_info->scan_info;
    pmsg_que = &pscan_info->msg_que;

    PT_BEGIN(pt);

    SCAN_DBG();

    /* wait until scan process done. */
    PT_WAIT_WHILE(pt, nic_mlme_hw_access_trylock(pnic_info));
    /* get scan start message. */
    do
    {
        rst = zt_msg_pop(pmsg_que, &pmsg);
        if (rst)
        {
            /* no message */
            SCAN_WARN("zt_msg_pop fail, error code: %d", rst);
            nic_mlme_hw_access_unlock(pnic_info);
            *prsn = -1;
            PT_EXIT(pt);
        }
        if (pmsg->tag != ZT_SCAN_TAG_START)
        {
            /* undesired message */
            SCAN_DBG("undesired message");
            zt_msg_del(pmsg_que, pmsg);
            PT_YIELD(pt);
            continue;
        }
        break;
    } while (zt_true);
    pscan_info->preq = (zt_scan_req_t *)pmsg->value;

    /* stop framework data send behavior come into */
    tx_cutoff(pnic_info);

    /* sta+ap need stop ap tx */
    if (pnic_info->buddy_nic && zt_local_cfg_get_work_mode(pnic_info->buddy_nic) == ZT_MASTER_MODE)
    {
        tx_cutoff(pnic_info->buddy_nic);
    }
    /* wait tx data empty */
    SCAN_DBG("wait until tx cache empty......");
    zt_timer_set(&pscan_info->timer, 1000);
    while (!is_tx_empty(pnic_info))
    {
        if (!zt_msg_pop(pmsg_que, &pmsg))
        {
            if (pmsg->tag == ZT_SCAN_TAG_ABORT)
            {
                zt_msg_del(pmsg_que, pmsg);
                SCAN_DBG("scan aborted");
                reason = -2;
                goto exit;
            }
        }
        if (zt_timer_expired(&pscan_info->timer))
        {
            SCAN_WARN("wait timeout");
            reason = -3;
            goto exit;
        }
        zt_msg_del(pmsg_que, pmsg);
        PT_YIELD(pt);
    }

    /* stop ars */
    if (zt_mcu_ars_switch(pnic_info, zt_false) == ZT_RETURN_FAIL)
    {
        reason = -4;
        goto exit;
    }

    /* scan set */
    SCAN_DBG("scan setting...");
    rst = scan_setting(pnic_info);
    if (rst)
    {
        SCAN_WARN("scan setting fail, error code: %d", rst);
        reason = -5;
        goto exit;
    }

    /* scan begin */
    SCAN_INFO("scanning...");
    pscan_info->brun = zt_true;
    zt_timer_set(&pscan_info->pass_time, 0);
    for (pscan_info->ch_idx = 0;
            pscan_info->ch_idx < pscan_info->preq->ch_num;
            pscan_info->ch_idx++)
    {
        /* channel set */
        SCAN_DBG("channel: %d", pscan_info->preq->ch_map[pscan_info->ch_idx]);
        rst = zt_hw_info_set_channel_bw(pnic_info,
                                         pscan_info->preq->ch_map[pscan_info->ch_idx],
                                         CHANNEL_WIDTH_20,
                                         HAL_PRIME_CHNL_OFFSET_DONT_CARE);
        if (rst)
        {
            SCAN_WARN("set channel fail, error code: %d", rst);
            reason = -6;
            goto exit;
        }

        if (pscan_info->preq->type == SCAN_TYPE_ACTIVE)
        {
            for (pscan_info->retry_cnt = 0;
                    pscan_info->retry_cnt < SCAN_PROBE_RESEND_TIMES;
                    pscan_info->retry_cnt++)
            {
                if (pscan_info->preq->ch_num == 1)
                {
                    scan_timeout = SCAN_CH_TIMEOUT * 3;
                }
                else
                {
                    scan_timeout = SCAN_CH_TIMEOUT;
                }
                /* wait until channel scan timeout */
                zt_timer_set(&pscan_info->timer, scan_timeout);

                /* send probe request */
                if (zt_p2p_is_valid(pnic_info))
                {
                    // LOG_D("[%s, %d] p2p_state:%s", __func__, __LINE__,
                    //     zt_p2p_state_to_str(pwdinfo->p2p_state));
                    if (p2p_info->p2p_state == P2P_STATE_SCAN ||
                            p2p_info->p2p_state == P2P_STATE_FIND_PHASE_SEARCH)
                    {
                        rst = zt_p2p_send_probereq(pnic_info, pscan_info->preq->bssid);
                    }
                    else
                    {
                        rst = zt_scan_probe_send(pnic_info);
                    }
                }
                else
                {
                    rst = zt_scan_probe_send(pnic_info);
                }

                if (rst)
                {
                    SCAN_WARN("zt_scan_probe_send failed, error code: %d", rst);
                    reason = -7;
                    goto exit;
                }

                do
                {
                    PT_WAIT_UNTIL(pt, !zt_msg_pop(pmsg_que, &pmsg) ||
                                  zt_timer_expired(&pscan_info->timer));
                    if (pmsg == NULL)
                    {
                        /* timeout */
                        zt_timer_reset(&pscan_info->timer);
                        break;
                    }
                    if (pmsg->tag == ZT_SCAN_TAG_ABORT)
                    {
                        zt_msg_del(pmsg_que, pmsg);
                        reason = pmsg->tag;
                        goto done;
                    }
                    zt_msg_del(pmsg_que, pmsg);
                } while (zt_true);
            }
        }
        else
        {
            /* wait until scaning timeout */
            zt_timer_set(&pscan_info->timer, SCAN_CH_TIMEOUT);
            do
            {
                PT_WAIT_UNTIL(pt, !zt_msg_pop(pmsg_que, &pmsg) ||
                              zt_timer_expired(&pscan_info->timer));
                if (pmsg == NULL)
                {
                    /* timeout */
                    break;
                }
                if (pmsg->tag == ZT_SCAN_TAG_ABORT)
                {
                    zt_msg_del(pmsg_que, pmsg);
                    reason = ZT_SCAN_TAG_ABORT;
                    goto done;
                }
                zt_msg_del(pmsg_que, pmsg);
            } while (zt_true);
        }

#ifdef CFG_ENABLE_AP_MODE
        if (pnic_info->buddy_nic &&
                zt_local_cfg_get_work_mode(pnic_info->buddy_nic) == ZT_MASTER_MODE &&
                zt_ap_status_get(pnic_info->buddy_nic) == ZT_AP_STATE_ESTABLISHED &&
                (pscan_info->ch_idx + 1) % 3 == 0)
        {
            SCAN_INFO("scan work pause");
            /* recover channel setting from backup */
            if (zt_hw_info_set_channel_bw(pnic_info,
                                           pscan_info->chnl_bak.number,
                                           pscan_info->chnl_bak.width,
                                           pscan_info->chnl_bak.offset) == ZT_RETURN_FAIL)
            {
                SCAN_WARN("UMSG_OPS_HAL_CHNLBW_MODE failed");
                reason = -8;
                goto exit;
            }
            /* enable bssid filter */
            zt_mcu_set_mlme_scan(pnic_info, zt_false);

            /* keep ap work 500ms */
            zt_timer_set(&pscan_info->timer, 500);
            pscan_info->ap_resume_done = zt_false;
            SCAN_INFO("ap share work for 500ms...");
            do
            {
                if (!pscan_info->ap_resume_done &&
                        !zt_ap_resume_bcn(pnic_info->buddy_nic))
                {
                    pscan_info->ap_resume_done = zt_true;
                }

                if (!zt_msg_pop(pmsg_que, &pmsg))
                {
                    if (pmsg->tag == ZT_SCAN_TAG_ABORT)
                    {
                        zt_msg_del(pmsg_que, pmsg);
                        reason = ZT_SCAN_TAG_ABORT;
                        goto done;
                    }
                    zt_msg_del(pmsg_que, pmsg);
                }

                PT_YIELD(pt);
            } while (!zt_timer_expired(&pscan_info->timer));

            /* disable bssid filter */
            if (zt_mcu_set_mlme_scan(pnic_info, zt_true))
            {
                reason = -9;
                goto exit;
            }
            SCAN_INFO("scan work recover");
        }
#endif
    }
    reason = ZT_SCAN_TAG_DONE;

done:
    /* scan done */
    SCAN_INFO("scan done pass time: %dms",
              zt_timer_elapsed(&pscan_info->pass_time));
    /* refresh scan queue */
    zt_wlan_mgmt_scan_que_refresh(pnic_info,
                                  pscan_info->preq->ch_map,
                                  pscan_info->ch_idx);

    if (!zt_p2p_is_valid(pnic_info) &&
            pscan_info->preq->type == SCAN_TYPE_ACTIVE)
    {
        /* rx statistics monitor */
        rx_watch(pnic_info);
    }

exit:
    if (pscan_info->brun)
    {
        scan_setting_recover(pnic_info);
        pscan_info->brun = zt_false;
    }

    /* resume ars */
    zt_mcu_ars_switch(pnic_info, zt_true);

    /* resume tx */
    tx_resume(pnic_info);
    if (pnic_info->buddy_nic && zt_local_cfg_get_work_mode(pnic_info->buddy_nic) == ZT_MASTER_MODE)
    {
        tx_resume(pnic_info->buddy_nic);
    }

    /* free scan request infomation */
    PT_WAIT_WHILE(pt, zt_os_api_sema_try(&pscan_info->req_lock));
    zt_msg_del(pmsg_que,
               ZT_CONTAINER_OF((void *)pscan_info->preq, zt_msg_t, value));
    pscan_info->preq = NULL;
    zt_os_api_sema_post(&pscan_info->req_lock);
    nic_mlme_hw_access_unlock(pnic_info);

    *prsn = reason;
    if (reason < 0)
    {
        SCAN_WARN("scan fail, error code: %d", reason);
        PT_EXIT(pt);
    }

    PT_END(pt);
}

#ifdef CFG_ENABLE_AP_MODE
zt_inline static zt_s32 ap_scan_setting(nic_info_st *pnic_info)
{
    zt_scan_info_t *pscan_info = pnic_info->scan_info;
    zt_bool bch_spec = !!pscan_info->preq->ch_num;

    if (pscan_info->preq->type == SCAN_TYPE_ACTIVE)
    {
        zt_bool bfix_ch = zt_false;
        if (zt_80211_is_valid_bssid(pscan_info->preq->bssid))
        {
            /* match in scan queue */
            zt_wlan_mgmt_scan_que_node_t *pscan_que_node;
            zt_wlan_mgmt_scan_que_for_rst_e rst;
            SCAN_DBG("target bssid: "ZT_MAC_FMT,
                     ZT_MAC_ARG(pscan_info->preq->bssid));
            zt_wlan_mgmt_scan_que_for_begin(pnic_info, pscan_que_node)
            {
                if (!zt_memcmp(pscan_que_node->bssid, pscan_info->preq->bssid,
                               sizeof(pscan_que_node->bssid)))
                {
                    SCAN_DBG("found bss in scan queue");
                    break;
                }
            }
            zt_wlan_mgmt_scan_que_for_end(rst);

            if (rst == ZT_WLAN_MGMT_SCAN_QUE_FOR_RST_FAIL)
            {
                SCAN_WARN("get semphone fail!!!!!!!!!!!!!!!!!!!!!!!!!");
            }

            if (rst == ZT_WLAN_MGMT_SCAN_QUE_FOR_RST_BREAK)
            {
                /* if channel map be specified, check if valid */
                if (bch_spec)
                {
                    zt_u8 i;
                    for (i = 0; i < pscan_info->preq->ch_num; i++)
                    {
                        if (pscan_que_node->channel == pscan_info->preq->ch_map[i])
                        {
                            bfix_ch = zt_true;
                            break;
                        }
                    }
                }
                else
                {
                    bfix_ch = zt_true;
                }

                if (bfix_ch)
                {
                    /* switch scan channel to specified by the node found in
                       the scan queue. */
                    SCAN_DBG("fix scan channel number: %d",
                             pscan_que_node->channel);
                    pscan_info->preq->ch_num = 1;
                    pscan_info->preq->ch_map[0] = pscan_que_node->channel;
                }
            }
        }
    }

    if (pscan_info->preq->ch_num == 0)
    {
        /* channel no specify so channel setting reference to local
        hardware support */
        hw_info_st *phw_info = (hw_info_st *)pnic_info->hw_info;
        zt_u8 i;
        pscan_info->preq->ch_num = phw_info->max_chan_nums;
        for (i = 0; i < phw_info->max_chan_nums; i++)
        {
            pscan_info->preq->ch_map[i] = phw_info->channel_set[i].channel_num;
        }
    }

    if (bch_spec)
    {
        /* clearup scan queue, avoid queue contain invalid channel information */
        //        zt_wlan_mgmt_scan_que_flush(pnic_info);
    }

    /* backup current channel setting */
    if (zt_hw_info_get_channel_bw_ext(pnic_info,
                                   &pscan_info->chnl_bak.number,
                                   &pscan_info->chnl_bak.width,
                                   &pscan_info->chnl_bak.offset) != ZT_RETURN_OK)
    {
        return -2;
    }

    /* disable bssid filter of beacon and probe response */
    if (zt_mcu_set_mlme_scan(pnic_info, zt_true))
    {
        return -3;
    }
    SCAN_INFO("Disbale BSSID Filter");

    return 0;
}

zt_inline static zt_s32 ap_scan_setting_recover(nic_info_st *pnic_info)
{
    zt_scan_info_t *pscan_info = pnic_info->scan_info;

    /* recover channel setting from backup */
    if (zt_hw_info_set_channel_bw(pnic_info,
                                   pscan_info->chnl_bak.number,
                                   pscan_info->chnl_bak.width,
                                   pscan_info->chnl_bak.offset) == ZT_RETURN_FAIL)
    {
        SCAN_WARN("UMSG_OPS_HAL_CHNLBW_MODE failed");
        return -1;
    }

    /* enable bssid filter for beacon and probe response */
    zt_mcu_set_mlme_scan(pnic_info, zt_false);
    SCAN_INFO("Enable BSSID Filter");

    if (zt_ap_resume_bcn(pnic_info))
    {
        return -2;
    }

    return 0;
}

zt_pt_ret_t zt_ap_scan_thrd(zt_pt_t *pt, nic_info_st *pnic_info, zt_s32 *prsn)
{
    zt_scan_info_t *pscan_info;
    zt_msg_que_t *pmsg_que;
    zt_msg_t *pmsg;
    zt_s32 reason = ZT_SCAN_TAG_DONE;
    zt_s32 rst;

    if (pt == NULL || pnic_info == NULL || prsn == NULL)
    {
        PT_EXIT(pt);
    }
    pscan_info = pnic_info->scan_info;
    pmsg_que = &pscan_info->msg_que;

    PT_BEGIN(pt);

    SCAN_DBG();

    /* wait until scan process done. */
    PT_WAIT_WHILE(pt, nic_mlme_hw_access_trylock(pnic_info));
    /* get scan start message. */
    do
    {
        rst = zt_msg_pop(pmsg_que, &pmsg);
        if (rst)
        {
            /* no message */
            SCAN_WARN("zt_msg_pop fail, error code: %d", rst);
            nic_mlme_hw_access_unlock(pnic_info);
            *prsn = -1;
            PT_EXIT(pt);
        }
        if (pmsg->tag != ZT_SCAN_TAG_START)
        {
            /* undesired message */
            SCAN_DBG("undesired message");
            zt_msg_del(pmsg_que, pmsg);
            PT_YIELD(pt);
            continue;
        }
        break;
    } while (zt_true);
    pscan_info->preq = (zt_scan_req_t *)pmsg->value;

    /* stop framework data send behavior come into */
    tx_cutoff(pnic_info);
    /* wait tx data empty */
    SCAN_DBG("wait until tx cache empty......");
    zt_timer_set(&pscan_info->timer, 1000);
    while (!is_tx_empty(pnic_info))
    {
        if (!zt_msg_pop(pmsg_que, &pmsg))
        {
            if (pmsg->tag == ZT_SCAN_TAG_ABORT)
            {
                zt_msg_del(pmsg_que, pmsg);
                SCAN_DBG("scan aborted");
                reason = -2;
                goto exit;
            }
        }
        if (zt_timer_expired(&pscan_info->timer))
        {
            SCAN_WARN("wait timeout");
            reason = -3;
            goto exit;
        }
        zt_msg_del(pmsg_que, pmsg);
        PT_YIELD(pt);
    }

    /* stop ars */
    if (zt_mcu_ars_switch(pnic_info, zt_false) == ZT_RETURN_FAIL)
    {
        reason = -4;
        goto exit;
    }

    /* scan set */
    SCAN_DBG("scan setting...");
    rst = ap_scan_setting(pnic_info);
    if (rst)
    {
        SCAN_WARN("scan setting fail, error code: %d", rst);
        reason = -5;
        goto exit;
    }

    /* scan begin */
    SCAN_INFO("scanning...");
    pscan_info->brun = zt_true;
    zt_timer_set(&pscan_info->pass_time, 0);
    for (pscan_info->ch_idx = 0;
            pscan_info->ch_idx < pscan_info->preq->ch_num;
            pscan_info->ch_idx++)
    {
        /* channel set */
        SCAN_DBG("channel: %d", pscan_info->preq->ch_map[pscan_info->ch_idx]);
        rst = zt_hw_info_set_channel_bw(pnic_info,
                                         pscan_info->preq->ch_map[pscan_info->ch_idx],
                                         CHANNEL_WIDTH_20,
                                         HAL_PRIME_CHNL_OFFSET_DONT_CARE);
        if (rst)
        {
            SCAN_WARN("set channel fail, error code: %d", rst);
            reason = -6;
            goto exit;
        }

        if (pscan_info->preq->type == SCAN_TYPE_ACTIVE)
        {
            for (pscan_info->retry_cnt = 0;
                    pscan_info->retry_cnt < SCAN_PROBE_RESEND_TIMES;
                    pscan_info->retry_cnt++)
            {
                /* wait until channel scan timeout */
                zt_timer_set(&pscan_info->timer, SCAN_CH_TIMEOUT);

                /* send probe request */
                rst = zt_scan_probe_send(pnic_info);
                if (rst)
                {
                    SCAN_WARN("zt_scan_probe_send failed, error code: %d", rst);
                    reason = -7;
                    goto exit;
                }

                do
                {
                    PT_WAIT_UNTIL(pt, !zt_msg_pop(pmsg_que, &pmsg) ||
                                  zt_timer_expired(&pscan_info->timer));
                    if (pmsg == NULL)
                    {
                        /* timeout */
                        zt_timer_reset(&pscan_info->timer);
                        break;
                    }
                    if (pmsg->tag == ZT_SCAN_TAG_ABORT)
                    {
                        zt_msg_del(pmsg_que, pmsg);
                        reason = pmsg->tag;
                        goto done;
                    }
                    zt_msg_del(pmsg_que, pmsg);
                } while (zt_true);
            }
        }
        else
        {
            /* wait until scaning timeout */
            zt_timer_set(&pscan_info->timer, SCAN_CH_TIMEOUT);
            do
            {
                PT_WAIT_UNTIL(pt, !zt_msg_pop(pmsg_que, &pmsg) ||
                              zt_timer_expired(&pscan_info->timer));
                if (pmsg == NULL)
                {
                    /* timeout */
                    break;
                }
                if (pmsg->tag == ZT_SCAN_TAG_ABORT)
                {
                    zt_msg_del(pmsg_que, pmsg);
                    reason = ZT_SCAN_TAG_ABORT;
                    goto done;
                }
                zt_msg_del(pmsg_que, pmsg);
            } while (zt_true);
        }

        /* ap start working per 2 channel scan process done. */
        if ((pscan_info->ch_idx + 1) % 2 == 0)
        {
            SCAN_DBG("scan work pause");
            /* recover channel setting from backup */
            if (zt_hw_info_set_channel_bw(pnic_info,
                                           pscan_info->chnl_bak.number,
                                           pscan_info->chnl_bak.width,
                                           pscan_info->chnl_bak.offset) == ZT_RETURN_FAIL)
            {
                SCAN_WARN("UMSG_OPS_HAL_CHNLBW_MODE failed");
                reason = -8;
                goto exit;
            }
            /* enable bssid filter */
            zt_mcu_set_mlme_scan(pnic_info, zt_false);

            /* keep ap work 100ms */
            zt_timer_set(&pscan_info->timer, 100);
            pscan_info->ap_resume_done = zt_false;
            SCAN_DBG("ap share work wait for 100ms...");
            do
            {
                if (!pscan_info->ap_resume_done &&
                        !zt_ap_resume_bcn(pnic_info->buddy_nic))
                {
                    pscan_info->ap_resume_done = zt_true;
                }

                if (!zt_msg_pop(pmsg_que, &pmsg))
                {
                    if (pmsg->tag == ZT_SCAN_TAG_ABORT)
                    {
                        zt_msg_del(pmsg_que, pmsg);
                        reason = ZT_SCAN_TAG_ABORT;
                        goto done;
                    }
                    zt_msg_del(pmsg_que, pmsg);
                }

                PT_YIELD(pt);
            } while (!zt_timer_expired(&pscan_info->timer));

            /* disable bssid filter */
            if (zt_mcu_set_mlme_scan(pnic_info, zt_true))
            {
                reason = -9;
                goto exit;
            }
            SCAN_DBG("scan work recover");
        }
    }
    reason = ZT_SCAN_TAG_DONE;

done:
    /* scan done */
    SCAN_INFO("scan done pass time: %dms",
              zt_timer_elapsed(&pscan_info->pass_time));
    /* refresh scan queue */
    zt_wlan_mgmt_scan_que_refresh(pnic_info,
                                  pscan_info->preq->ch_map,
                                  pscan_info->preq->ch_num);

exit:
    if (pscan_info->brun)
    {
        ap_scan_setting_recover(pnic_info);
        pscan_info->brun = zt_false;
    }

    /* resume ars */
    zt_mcu_ars_switch(pnic_info, zt_true);

    /* resume tx */
    tx_resume(pnic_info);

    /* free scan request infomation */
    PT_WAIT_WHILE(pt, zt_os_api_sema_try(&pscan_info->req_lock));
    zt_msg_del(pmsg_que,
               ZT_CONTAINER_OF((void *)pscan_info->preq, zt_msg_t, value));
    pscan_info->preq = NULL;
    zt_os_api_sema_post(&pscan_info->req_lock);
    nic_mlme_hw_access_unlock(pnic_info);

    *prsn = reason;
    if (reason < 0)
    {
        SCAN_WARN("scan fail, error code: %d", reason);
        PT_EXIT(pt);
    }

    PT_END(pt);
}
#endif

zt_pt_ret_t zt_scan_thrd(zt_pt_t *pt, nic_info_st *pnic_info, zt_s32 *prsn)
{
    return
#ifdef CFG_ENABLE_AP_MODE
        zt_local_cfg_get_work_mode(pnic_info) == ZT_MASTER_MODE ?
        zt_ap_scan_thrd(pt, pnic_info, prsn) :
#endif
        zt_sta_scan_thrd(pt, pnic_info, prsn);
}

zt_s32 zt_scan_start(nic_info_st *pnic_info, scan_type_e type,
                     zt_80211_bssid_t bssid,
                     zt_wlan_ssid_t ssids[], zt_u8 ssid_num,
                     zt_u8 chs[], zt_u8 ch_num)
{
    zt_scan_info_t *pscan_info;
    zt_msg_que_t *pmsg_que;
    zt_msg_t *pmsg;
    zt_scan_req_t *pscan_req;
    zt_s32 rst;

    SCAN_DBG();

    if (pnic_info == NULL || ZT_CANNOT_RUN(pnic_info))
    {
        return -1;
    }


    if (!pnic_info->is_up)
    {
        return -2;
    }
    pscan_info = pnic_info->scan_info;
    pmsg_que = &pscan_info->msg_que;

    /* debug dump */
    {
        zt_s8 *bssid_str = zt_kzalloc(20);
        zt_s8 *ssid_str = zt_kzalloc(ZT_SCAN_REQ_SSID_NUM * sizeof(ssids[0]));
        zt_s8 *ch_str = zt_kzalloc(20 * 3 + 1);
        zt_u8 i;
        /* bssid */
        if (bssid && zt_80211_is_valid_bssid(bssid))
        {
            zt_sprintf(bssid_str, ZT_MAC_FMT, ZT_MAC_ARG(bssid));
        }
        else
        {
            zt_sprintf(bssid_str, " ");
        }
        /* ssid */
        for (i = 0; i < ssid_num; i++)
        {
            zt_strncat(ssid_str, (const zt_s8 *)&ssids[i].data, ssids[i].length);
        }
        if (i == 0)
        {
            zt_sprintf(ssid_str, " ");
        }
        /* channel */
        for (i = 0; i < ch_num; i++)
        {
            zt_s8 tmp[5];
            zt_sprintf(tmp, "%02d ", chs[i]);
            zt_strncat(ch_str, tmp, 3);
        }
        if (i == 0)
        {
            zt_sprintf(ch_str, " ");
        }
        SCAN_DBG("type(%s) bssid(%s) ssid(%s), channel(%s)",
                 type == SCAN_TYPE_ACTIVE ? "active" : "passive",
                 bssid_str,
                 ssid_str,
                 ch_str);

        zt_kfree(bssid_str);
        zt_kfree(ssid_str);
        zt_kfree(ch_str);
    }

    /* new message information */
    rst = zt_msg_new(pmsg_que, ZT_SCAN_TAG_START, &pmsg);
    if (rst)
    {
        SCAN_WARN("msg new fail error code: %d", rst);
        return -3;
    }
    pmsg->len = sizeof(zt_scan_req_t);
    pscan_req = (zt_scan_req_t *)pmsg->value;

    /* set scanning type */
    pscan_req->type = type;
    /* set bssid for match target bss */
    if (bssid && zt_80211_is_valid_bssid(bssid))
    {
        zt_memcpy(pscan_req->bssid, bssid, sizeof(pscan_req->bssid));
    }
    else
    {
        /* zero address represent any bssid */
        zt_memset(pscan_req->bssid, 0x0, sizeof(pscan_req->bssid));
    }
    /* set ssid for match target bss */
    pscan_req->ssid_num = ZT_MIN(ssid_num, ZT_ARRAY_SIZE(pscan_req->ssids));
    if (ssid_num && ssids)
    {
        zt_memset(pscan_req->ssids, '\0', sizeof(pscan_req->ssids));
        zt_memcpy(pscan_req->ssids, ssids,
                  pscan_req->ssid_num * sizeof(pscan_req->ssids[0]));
    }
    /* set scanning channels */
    pscan_req->ch_num = ZT_MIN(ch_num, ZT_ARRAY_SIZE(pscan_req->ch_map));
    if (chs && ch_num)
    {
        zt_memcpy(pscan_req->ch_map, chs, ch_num);
    }

    rst = zt_msg_push(pmsg_que, pmsg);
    if (rst)
    {
        zt_msg_del(pmsg_que, pmsg);
        SCAN_WARN("msg push fail, error code: %d", rst);
        return -4;
    }

    return 0;
}

zt_s32 zt_scan_stop(nic_info_st *pnic_info)
{
    if (pnic_info == NULL || ZT_CANNOT_RUN(pnic_info))
    {
        return -1;
    }

    SCAN_DBG();

    if (!zt_is_scanning(pnic_info))
    {
        return 0;
    }

    {
        zt_scan_info_t *pscan_info = pnic_info->scan_info;
        zt_msg_que_t *pmsg_que = &pscan_info->msg_que;
        zt_msg_t *pmsg;
        zt_s32 rst;

        rst = zt_msg_new(pmsg_que, ZT_SCAN_TAG_ABORT, &pmsg);
        if (rst)
        {
            SCAN_WARN("msg new, error code: %d", rst);
            return -2;
        }
        rst = zt_msg_push(pmsg_que, pmsg);
        if (rst)
        {
            zt_msg_del(pmsg_que, pmsg);
            SCAN_WARN("msg push, error code: %d", rst);
            return -3;
        }
    }

    return 0;
}

zt_s32 zt_scan_wait_done(nic_info_st *pnic_info, zt_bool babort, zt_u16 to_ms)
{
    zt_timer_t timer;

    if (pnic_info == NULL || ZT_CANNOT_RUN(pnic_info))
    {
        return -1;
    }

    if (!zt_is_scanning(pnic_info))
    {
        return 0;
    }

    if (babort)
    {
        zt_scan_stop(pnic_info);
    }

    zt_timer_set(&timer, to_ms);
    do
    {
        zt_msleep(1);
        if (zt_timer_expired(&timer))
        {
            return -2;
        }
    } while (zt_is_scanning(pnic_info));

    return 0;
}

zt_bool zt_is_scanning(nic_info_st *pnic_info)
{
    zt_scan_info_t *pscan_info;

    if (pnic_info == NULL)
    {
        return -1;
    }
    pscan_info = pnic_info->scan_info;

    return pscan_info->brun;
}

zt_inline static zt_s32 scan_msg_init(zt_msg_que_t *pmsg_que)
{
    zt_msg_init(pmsg_que);
    return (zt_msg_alloc(pmsg_que, ZT_SCAN_TAG_ABORT, 0, 1) ||
            zt_msg_alloc(pmsg_que, ZT_SCAN_TAG_START, sizeof(zt_scan_req_t), 1)) ? -1 : 0;
}

zt_inline static void scan_msg_deinit(zt_msg_que_t *pmsg_que)
{
    zt_msg_free(pmsg_que);
}

zt_s32 zt_scan_init(nic_info_st *pnic_info)
{
    zt_scan_info_t *pscan_info;

    if (pnic_info == NULL)
    {
        return -1;
    }

    SCAN_DBG();

    pscan_info = zt_kzalloc(sizeof(zt_scan_info_t));
    if (pscan_info == NULL)
    {
        SCAN_WARN("malloc scan_param_st failed");
        return -2;
    }
    pnic_info->scan_info = pscan_info;
    pscan_info->brun = zt_false;
    pscan_info->preq = NULL;
    if (scan_msg_init(&pscan_info->msg_que))
    {
        SCAN_WARN("scan msg init failed");
        return -3;
    }
    zt_os_api_sema_init(&pscan_info->req_lock, 1);

    return 0;
}

zt_s32 zt_scan_term(nic_info_st *pnic_info)
{
    zt_scan_info_t *pscan_info;

    if (pnic_info == NULL)
    {
        return 0;
    }

    SCAN_DBG();

    pscan_info = pnic_info->scan_info;
    if (pscan_info)
    {
        scan_msg_deinit(&pscan_info->msg_que);
        zt_kfree(pscan_info);
        pnic_info->scan_info = NULL;
    }

    return 0;
}

