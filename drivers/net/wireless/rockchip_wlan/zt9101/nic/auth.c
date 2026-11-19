/*
 * auth.c
 *
 * impliment of IEEE80211 management frame authentication stage processing
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
#define AUTH_DBG(fmt, ...)      LOG_D("[%s:%d]"fmt, __func__, __LINE__, ##__VA_ARGS__)
#define AUTH_ARRAY(data, len)   zt_log_array(data, len)
#define AUTH_INFO(fmt, ...)     LOG_I("[%s:%d]"fmt, __func__, __LINE__, ##__VA_ARGS__)
#define AUTH_WARN(fmt, ...)     LOG_W("[%s:%d]"fmt, __func__, __LINE__, ##__VA_ARGS__)
#define AUTH_ERROR(fmt, ...)    LOG_E("[%s:%d]"fmt, __func__, __LINE__, ##__VA_ARGS__)

#define AUTH_REQ_RESEND_TIMES   3
#define AUTH_RSP_TIMEOUT        500

/* function declaration */

static zt_s32 auth_send(nic_info_st *pnic_info, wdn_net_info_st *pwdn_info,
                        zt_80211_statuscode_e status)
{
    struct xmit_buf *pxmit_buf;
    tx_info_st *ptx_info = (void *)pnic_info->tx_info;
    sec_info_st *psec_info = pnic_info->sec_info;
    zt_80211_mgmt_t *pauth_frame;
    zt_80211_mgmt_ie_t *pie;
    zt_u16 frame_len = 0;
    zt_u32 tmp_32;
    zt_u16 tmp_16;

    /* alloc xmit_buf */
    pxmit_buf = zt_xmit_extbuf_new(ptx_info);
    if (pxmit_buf == NULL)
    {
        AUTH_WARN("xmit_buf alloc fail");
        return -1;
    }
    zt_memset(pxmit_buf->pbuf, 0,
              TXDESC_OFFSET + ZT_OFFSETOF(zt_80211_mgmt_t, auth));

    /* set auth type */
    pauth_frame = (void *)&pxmit_buf->pbuf[TXDESC_OFFSET];
    zt_80211_hdr_type_set(pauth_frame, ZT_80211_FRM_AUTH);
    /* set mac address */
    zt_memcpy(pauth_frame->da, pwdn_info->mac, ZT_ARRAY_SIZE(pauth_frame->da));
    zt_memcpy(pauth_frame->sa, nic_to_local_addr(pnic_info),
              ZT_ARRAY_SIZE(pauth_frame->sa));
    zt_memcpy(pauth_frame->bssid, pwdn_info->bssid,
              ZT_ARRAY_SIZE(pauth_frame->bssid));

    switch (pwdn_info->auth_seq)
    {
        case ZT_80211_AUTH_SEQ_1 :
#ifdef CFG_ENABLE_AP_MODE
        case ZT_80211_AUTH_SEQ_4 :
#endif
            pauth_frame->frame_control = zt_cpu_to_le16(pauth_frame->frame_control);
            /* set algorithm number */
            tmp_16 = pwdn_info->auth_algo;

            pauth_frame->auth.auth_alg = zt_cpu_to_le16(tmp_16);
            /* set transaction number */
            tmp_16 = pwdn_info->auth_seq;
            pauth_frame->auth.auth_transaction = zt_cpu_to_le16(tmp_16);
            /* set status code */
            tmp_16 = status;
            pauth_frame->auth.status_code = zt_cpu_to_le16(tmp_16);
            /* update frame length */
            frame_len = ZT_OFFSETOF(zt_80211_mgmt_t, auth.variable);
            break;

#ifdef CFG_ENABLE_AP_MODE
        case ZT_80211_AUTH_SEQ_2 :
            pauth_frame->frame_control = zt_cpu_to_le16(pauth_frame->frame_control);
            /* set algorithm number */
            tmp_16 = pwdn_info->auth_algo;
            pauth_frame->auth.auth_alg = zt_cpu_to_le16(tmp_16);
            /* set transaction number */
            pauth_frame->auth.auth_transaction = zt_cpu_to_le16(ZT_80211_AUTH_SEQ_2);
            /* set status code */
            tmp_16 = status;
            pauth_frame->auth.status_code = zt_cpu_to_le16(tmp_16);
            /* add challenge, auth shared key only */
            if (status == ZT_80211_STATUS_SUCCESS &&
                    pwdn_info->auth_algo == ZT_80211_AUTH_ALGO_SHARED_KEY)
            {
                pie = (void *)pauth_frame->auth.variable;
                pie->element_id = ZT_80211_MGMT_EID_CHALLENGE;
                pie->len = ZT_80211_AUTH_CHALLENGE_LEN;
                zt_memcpy(pie->data, pwdn_info->chlg_txt, pie->len);
                /* add challenge text length */
                frame_len = ZT_OFFSETOF(zt_80211_mgmt_ie_t, data) +
                            ZT_80211_AUTH_CHALLENGE_LEN;
            }
            /* update frame length */
            frame_len += ZT_OFFSETOF(zt_80211_mgmt_t, auth.variable);
            break;
#endif

        case ZT_80211_AUTH_SEQ_3 :
            if (pwdn_info->auth_algo != ZT_80211_AUTH_ALGO_SHARED_KEY)
            {
                AUTH_WARN("auth algorthm error");
                return -3;
            }
            /* set privacy */
            zt_80211_hdr_protected_set(pauth_frame, 1);
            /* set iv data */
            pwdn_info->key_index = psec_info->dot11PrivacyKeyIndex;
            tmp_32 = (pwdn_info->iv++) | (pwdn_info->key_index << 30);
            pauth_frame->auth_seq3.iv = zt_cpu_to_le32(tmp_32);
            /* set algorithm number */
            pauth_frame->auth_seq3.auth_alg = zt_cpu_to_le16(ZT_80211_AUTH_ALGO_SHARED_KEY);
            /* set transaction number */
            pauth_frame->auth_seq3.auth_transaction = zt_cpu_to_le16(ZT_80211_AUTH_SEQ_3);
            /* set status code */
            pauth_frame->auth_seq3.status_code = zt_cpu_to_le16(ZT_80211_STATUS_SUCCESS);
            /* add challenge */
            pie = (void *)pauth_frame->auth_seq3.variable;
            pie->element_id = ZT_80211_MGMT_EID_CHALLENGE;
            pie->len = ZT_80211_AUTH_CHALLENGE_LEN;
            zt_memcpy(pie->data, pwdn_info->chlg_txt, pie->len);
            /* update frame length */
            frame_len = ZT_OFFSETOF(zt_80211_mgmt_t, auth_seq3) +
                        ZT_FIELD_SIZEOF(zt_80211_mgmt_t, auth_seq3);
            /* wep encrypt */
            zt_wep_encrypt_auth(pnic_info, pauth_frame, frame_len);
            break;

        default :
            AUTH_WARN("unknown auth sequence number %d", pwdn_info->auth_seq);
            return -4;
    }

    return zt_nic_mgmt_frame_xmit(pnic_info, pwdn_info,
                                  pxmit_buf, pxmit_buf->pkt_len = frame_len);
}

#ifdef CFG_ENABLE_AP_MODE
static zt_s32 auth_ap_recv(nic_info_st *pnic_info, wdn_net_info_st *pwdn_info,
                           zt_80211_mgmt_t *pmgmt, zt_u16 mgmt_len)
{
    wdn_net_info_st *wdn_info = pwdn_info;

    if (mgmt_len > ZT_80211_MGMT_AUTH_SIZE_MAX)
    {
        AUTH_WARN("auth frame length too long");
        return -1;
    }

    if (zt_80211_hdr_type_get(pmgmt) != ZT_80211_FRM_AUTH)
    {
        return -2;
    }

    AUTH_DBG("auth received");

    /* create wdn if no find the node */
    if (wdn_info == NULL)
    {
        if (zt_ap_new_sta(pnic_info, pmgmt->sa, (void *)&wdn_info))
        {
            return -4;
        }
    }
    else if (wdn_info->mode != ZT_MASTER_MODE)
    {
        AUTH_WARN("the wdn no used for master mode");
        return -5;
    }

    if (zt_ap_msg_load(pnic_info, &wdn_info->ap_msg,
                       ZT_AP_MSG_TAG_AUTH_FRAME, pmgmt, mgmt_len))
    {
        AUTH_WARN("auth msg enque fail");
        return -6;
    }

    return 0;
}

static zt_inline void generate_challenge_text(zt_u32 *pchlg_txt)
{
    zt_u8 i;

    for (i = 0; i < ZT_80211_AUTH_CHALLENGE_LEN / sizeof(zt_u32); i++)
    {
        pchlg_txt[i++] = zt_os_api_rand32();
    }
}

static
void status_error(nic_info_st *pnic_info, wdn_net_info_st *pwdn_info,
                  zt_ap_msg_t *pmsg, zt_80211_statuscode_e status_code)
{
    /* free message */
    if (pmsg)
    {
        zt_ap_msg_free(pnic_info, &pwdn_info->ap_msg, pmsg);
    }
    /* send respond with error code */
    pwdn_info->auth_seq = ZT_80211_AUTH_SEQ_2;
    auth_send(pnic_info, pwdn_info, status_code);
}

zt_pt_ret_t zt_auth_ap_thrd(nic_info_st *pnic_info, wdn_net_info_st *pwdn_info)
{
    zt_pt_t *pt = &pwdn_info->sub_thrd_pt;
    sec_info_st *psec_info = pnic_info->sec_info;
    zt_ap_msg_t *pmsg = NULL;
    zt_80211_mgmt_t *pmgmt;
    zt_80211_auth_seq_e frame_seq;
    zt_u32 frame_algo;
    zt_80211_mgmt_ie_t *pie;

    PT_BEGIN(pt);

    /* wait auth request frame */
    PT_WAIT_UNTIL(pt, pmsg = zt_ap_msg_get(&pwdn_info->ap_msg));
    if (pmsg->tag != ZT_AP_MSG_TAG_AUTH_FRAME)
    {
        zt_ap_msg_free(pnic_info, &pwdn_info->ap_msg, pmsg);
        AUTH_WARN("receive a frame no auth type");
        zt_deauth_xmit_frame(pnic_info, pwdn_info->mac,
                             ZT_80211_REASON_DEAUTH_LEAVING);
        PT_EXIT(pt);
    }

    AUTH_DBG("auth begin->"ZT_MAC_FMT, ZT_MAC_ARG(pwdn_info->mac));
    pwdn_info->state = E_WDN_AP_STATE_AUTH;

    pmgmt = &pmsg->mgmt;
    frame_seq = (zt_80211_auth_seq_e)zt_le16_to_cpu(pmgmt->auth.auth_transaction);
    frame_algo = zt_le16_to_cpu(pmgmt->auth.auth_alg);

    /* checkout auth sequence, must be sequence number 1 */
    if (frame_seq != ZT_80211_AUTH_SEQ_1)
    {
        AUTH_WARN("auth sequence error");
        status_error(pnic_info, pwdn_info,
                     pmsg, ZT_80211_STATUS_UNKNOWN_AUTH_TRANSACTION);
        PT_EXIT(pt);
    }

    /* checkout auth algorithm
       todo: for privacy algorithm use wep, the authentication set to auto mode,
       means the value reference to frame set */
    if (psec_info->dot11AuthAlgrthm == dot11AuthAlgrthm_Auto)
    {
        if (psec_info->dot11PrivacyAlgrthm == _WEP40_ ||
                psec_info->dot11PrivacyAlgrthm == _WEP104_)
        {
            pwdn_info->auth_algo = frame_algo;
        }
        else
        {
            pwdn_info->auth_algo = dot11AuthAlgrthm_Open;
        }
    }
    else
    {
        pwdn_info->auth_algo =
            psec_info->dot11AuthAlgrthm == dot11AuthAlgrthm_8021X  ?
            dot11AuthAlgrthm_Open : psec_info->dot11AuthAlgrthm;
    }

    if (frame_algo > dot11AuthAlgrthm_Shared ||
            frame_algo != pwdn_info->auth_algo)
    {
        AUTH_WARN("auth algorthm error! frame_algo:%d auth_algo:%d ",
                  frame_algo, pwdn_info->auth_algo);
        status_error(pnic_info, pwdn_info,
                     pmsg, ZT_80211_STATUS_NOT_SUPPORTED_AUTH_ALG);
        PT_EXIT(pt);
    }

    /* free auth sequence 1 message */
    zt_ap_msg_free(pnic_info, &pwdn_info->ap_msg, pmsg);

    /* if open auth enable, send auth sequence 2 and judge auth success */
    if (pwdn_info->auth_algo == dot11AuthAlgrthm_Open)
    {
        AUTH_DBG("send open auth sequence 2");
        zt_timer_set(&pwdn_info->ap_timer, 5 * 1000);
        while (zt_true)
        {
            pwdn_info->auth_seq = ZT_80211_AUTH_SEQ_2;
            auth_send(pnic_info, pwdn_info, ZT_80211_STATUS_SUCCESS);
            PT_YIELD_UNTIL(pt, (pmsg = zt_ap_msg_get(&pwdn_info->ap_msg)) ||
                           zt_timer_expired(&pwdn_info->ap_timer));
            /* timeout */
            if (pmsg == NULL)
            {
                AUTH_DBG("open auth2 timeout");
                status_error(pnic_info, pwdn_info,
                             pmsg, ZT_80211_STATUS_AUTH_TIMEOUT);
                PT_EXIT(pt);
            }

            /* receive a duplicate auth frame, ignore */
            if (pmsg->tag == ZT_AP_MSG_TAG_AUTH_FRAME)
            {
                zt_ap_msg_free(pnic_info, &pwdn_info->ap_msg, pmsg);
                AUTH_DBG("send open auth sequence 2 duplicate");
                continue;
            }

            /* receive a no auth frame, can start assoc process */
            goto success;
        }
    }

    /*
     * below is sharekey auth
     */

    /* send auth sequence 2 with challenge */
    AUTH_DBG("send auth sequence 2 with challenge");
    generate_challenge_text((zt_u32 *)pwdn_info->chlg_txt);
    pwdn_info->auth_seq = ZT_80211_AUTH_SEQ_2;
    auth_send(pnic_info, pwdn_info, ZT_80211_STATUS_SUCCESS);

    /* wait until receive sequence 3 */
    zt_timer_set(&pwdn_info->ap_timer, 5 * 1000);
    PT_YIELD_UNTIL(pt, (pmsg = zt_ap_msg_get(&pwdn_info->ap_msg)) ||
                   zt_timer_expired(&pwdn_info->ap_timer));
    /* timeout */
    if (pmsg == NULL)
    {
        AUTH_DBG("sharkkey auth3 timeout");
        status_error(pnic_info, pwdn_info,
                     pmsg, ZT_80211_STATUS_AUTH_TIMEOUT);
        PT_EXIT(pt);
    }
    /* if receive a no auth frame, auth fail */
    if (pmsg->tag != ZT_AP_MSG_TAG_AUTH_FRAME)
    {
        AUTH_DBG("sharkkey auth3 timeout");
        status_error(pnic_info, pwdn_info,
                     pmsg, ZT_80211_STATUS_AUTH_TIMEOUT);
        PT_EXIT(pt);
    }

    /* decrypt frame */
    pmgmt = &pmsg->mgmt;
    if (!zt_80211_hdr_protected_get(pmgmt) ||
            zt_wep_decrypt_auth(pnic_info, &pmsg->mgmt, pmsg->len))
    {
        AUTH_WARN("challage decrypt fail");
        status_error(pnic_info, pwdn_info,
                     pmsg, ZT_80211_STATUS_CHALLENGE_FAIL);
        PT_EXIT(pt);
    }

    /* checkout sequence number */
    frame_seq = (zt_80211_auth_seq_e)zt_le16_to_cpu(
                    pmgmt->auth_seq3.auth_transaction);
    if (frame_seq != ZT_80211_AUTH_SEQ_3)
    {
        AUTH_WARN("auth sequence error");
        status_error(pnic_info, pwdn_info,
                     pmsg, ZT_80211_STATUS_UNKNOWN_AUTH_TRANSACTION);
        PT_EXIT(pt);
    }

    /* checkout auth algorithm */
    frame_algo = zt_le16_to_cpu(pmgmt->auth_seq3.auth_alg);
    if (frame_algo != pwdn_info->auth_algo)
    {
        AUTH_WARN("auth algorthm error");
        status_error(pnic_info, pwdn_info,
                     pmsg, ZT_80211_STATUS_NOT_SUPPORTED_AUTH_ALG);
        PT_EXIT(pt);
    }

    /* compare challenge text */
    pie = (void *)pmgmt->auth_seq3.variable;
    if (zt_memcmp(pwdn_info->chlg_txt, pie->data, ZT_80211_AUTH_CHALLENGE_LEN))
    {
        AUTH_WARN("challage fail");
        status_error(pnic_info, pwdn_info,
                     pmsg, ZT_80211_STATUS_CHALLENGE_FAIL);
        PT_EXIT(pt);
    }

    /* send auth sequence 4 */
    AUTH_DBG("send auth sequence 4");
    zt_timer_set(&pwdn_info->ap_timer, 5 * 1000);
    while (zt_true)
    {
        pwdn_info->auth_seq = ZT_80211_AUTH_SEQ_4;
        auth_send(pnic_info, pwdn_info, ZT_80211_STATUS_SUCCESS);
        PT_YIELD_UNTIL(pt, (pmsg = zt_ap_msg_get(&pwdn_info->ap_msg)) ||
                       zt_timer_expired(&pwdn_info->ap_timer));
        /* timeout */
        if (pmsg == NULL)
        {
            AUTH_DBG("sharkkey auth4 timeout");
            status_error(pnic_info, pwdn_info,
                         pmsg, ZT_80211_STATUS_AUTH_TIMEOUT);
            PT_EXIT(pt);
        }

        /* receive a duplicate auth frame, ignore */
        if (pmsg->tag == ZT_AP_MSG_TAG_AUTH_FRAME)
        {
            zt_ap_msg_free(pnic_info, &pwdn_info->ap_msg, pmsg);
            AUTH_DBG("send auth sequence 4 duplicate");
            continue;
        }
        break;
    }
success:
    AUTH_DBG("auth end->"ZT_MAC_FMT, ZT_MAC_ARG(pwdn_info->mac));
    /* thread end */
    PT_END(pt);
}
static zt_s32 deauth_work_ap(nic_info_st *pnic_info, wdn_net_info_st *pwdn_info,
                             zt_80211_mgmt_t *pmgmt, zt_u16 mgmt_len)
{
    AUTH_DBG();

    if (pwdn_info == NULL)
    {
        AUTH_DBG("wdn_info null");
        return -1;
    }

    if (pwdn_info->mode != ZT_MASTER_MODE)
    {
        AUTH_WARN("the wdn no used for master mode");
        return -2;
    }

    if (mgmt_len < ZT_80211_MGMT_DEAUTH_SIZE_MIN)
    {
        AUTH_WARN("deauth frame length error");
        return -3;
    }

    if (zt_80211_hdr_type_get(pmgmt) != ZT_80211_FRM_DEAUTH)
    {
        return -4;
    }

    if (zt_ap_msg_load(pnic_info, &pwdn_info->ap_msg,
                       ZT_AP_MSG_TAG_DEAUTH_FRAME,
                       pmgmt, ZT_80211_MGMT_DEAUTH_SIZE_MIN))
    {
        return -5;
    }

    return 0;
}
#endif

zt_pt_ret_t zt_auth_sta_thrd(zt_pt_t *pt, nic_info_st *pnic_info, zt_s32 *prsn)
{
    auth_info_t *pauth_info;
    wdn_net_info_st *pwdn_info;
    zt_msg_que_t *pmsg_que;
    zt_msg_t *pmsg;
    zt_80211_mgmt_t *pmgmt = NULL;
    zt_u16 mgmt_len = 0;
    struct auth_ie *pauth_ie = NULL;
    zt_s32 reason;
    zt_s32 rst;

    if (pt == NULL || pnic_info == NULL || prsn == NULL)
    {
        PT_EXIT(pt);
    }
    pauth_info = pnic_info->auth_info;
    pmsg_que = &pauth_info->msg_que;
    pwdn_info = pauth_info->pwdn_info;

    PT_BEGIN(pt);

    AUTH_DBG();

    PT_WAIT_WHILE(pt, nic_mlme_hw_access_trylock(pnic_info));
    do
    {
        /* get auth start message. */
        if (zt_msg_pop(pmsg_que, &pmsg))
        {
            /* no message */
            AUTH_WARN("no request message");
            nic_mlme_hw_access_unlock(pnic_info);
            *prsn = -1;
            PT_EXIT(pt);
        }
        if (pmsg->tag != ZT_AUTH_TAG_START)
        {
            /* undesired message */
            AUTH_DBG("undesired message");
            zt_msg_del(pmsg_que, pmsg);
            PT_YIELD(pt);
            continue;
        }
        zt_msg_del(pmsg_que, pmsg);
        break;
    } while (zt_true);

    /* mark auth begin run */
    pauth_info->brun = zt_true;

    zt_mcu_set_bssid(pnic_info, zt_wlan_get_cur_bssid(pnic_info));
    zt_mcu_set_mlme_join(pnic_info, 0); /* set mlme-join site */
    zt_mcu_set_media_status(pnic_info, WIFI_FW_STATION_STATE);

    /* send auth with sequence number 1 */
    pwdn_info = pauth_info->pwdn_info =
                    zt_wdn_find_info(pnic_info, zt_wlan_get_cur_bssid(pnic_info));
    if (pwdn_info == NULL)
    {
        AUTH_ERROR("wdn null");
        reason = -2;
        goto exit;
    }
    pwdn_info = pauth_info->pwdn_info;
    pwdn_info->auth_seq = ZT_80211_AUTH_SEQ_1;
    pwdn_info->auth_algo = dot11AuthAlgrthm_Open;
    for (pauth_info->retry_cnt = 0;
            pauth_info->retry_cnt < AUTH_REQ_RESEND_TIMES;
            pauth_info->retry_cnt++)
    {
        AUTH_DBG("send auth with seq1");
        rst = auth_send(pnic_info, pwdn_info, ZT_80211_STATUS_SUCCESS);
        if (rst)
        {
            AUTH_WARN("auth send fail, error code: %d", rst);
            reason = -3;
            goto exit;
        }

        /* wait until receive auth with seq2 */
        zt_timer_set(&pauth_info->timer, AUTH_RSP_TIMEOUT);
wait_seq2 :
        PT_WAIT_UNTIL(pt, !zt_msg_pop(pmsg_que, &pmsg) ||
                      zt_timer_expired(&pauth_info->timer));
        if (pmsg == NULL)
        {
            /* timeout, resend again */
            continue;
        }

        if (pmsg->tag == ZT_AUTH_TAG_ABORT)
        {
            zt_msg_del(pmsg_que, pmsg);
            reason = ZT_AUTH_TAG_ABORT;
            goto exit;
        }
        else if (pmsg->tag == ZT_AUTH_TAG_RSP)
        {
            zt_u8 ofs;
            zt_u16 seq, status;

            pmgmt = (zt_80211_mgmt_t *)pmsg->value;
            mgmt_len = pmsg->len;
            ofs = zt_80211_hdr_order_get(pmgmt) ? ZT_80211_HT_CTRL_LEN : 0;
            pauth_ie = (void *)((zt_u8 *)&pmgmt->auth + ofs); /* offset HT Order field */
            seq = zt_le16_to_cpu(pauth_ie->auth_transaction);
            status = zt_le16_to_cpu(pauth_ie->status_code);
            if (seq == ZT_80211_AUTH_SEQ_2)
            {
                if (status == ZT_80211_STATUS_NOT_SUPPORTED_AUTH_ALG)
                {
                    /* switch auth algorithm between shark key and open */
                    AUTH_DBG("switch auth algorithm");
                    pwdn_info->auth_algo =
                        pwdn_info->auth_algo == dot11AuthAlgrthm_Shared ?
                        dot11AuthAlgrthm_Open : dot11AuthAlgrthm_Shared;
                }
                else if (status == ZT_80211_STATUS_SUCCESS)
                {
                    AUTH_DBG("pwdn_info->auth_algo =%d", pwdn_info->auth_algo);
                    if (pwdn_info->auth_algo == dot11AuthAlgrthm_Open)
                    {
                        zt_msg_del(pmsg_que, pmsg);
                        /* open type auth success */
                        AUTH_DBG("auth(open) successfull");
                        reason = ZT_AUTH_TAG_DONE;
                        goto exit;
                    }
                    break;
                }
            }
            zt_msg_del(pmsg_que, pmsg);
        }
        else
        {
            AUTH_ERROR("unsutied message tag(%d)", pmsg->tag);
            zt_msg_del(pmsg_que, pmsg);
            goto wait_seq2;
        }
    }
    /* no expected auth with seq2 received */
    if (pauth_info->retry_cnt == AUTH_REQ_RESEND_TIMES)
    {
        AUTH_DBG("auth fail");
        reason = -4;
        goto exit;
    }

    /**
     * below is auth(for shark key type) seq3/4 process
     */

    /* retrive challenge text from auth seq2 */
    {
        zt_u16 var_len = mgmt_len -
                         ((zt_u8 *)pauth_ie->variable - (zt_u8 *)pmgmt);
        zt_80211_mgmt_ie_t *pie;
        if (zt_80211_mgmt_ies_search(pauth_ie->variable, var_len,
                                     ZT_80211_MGMT_EID_CHALLENGE, &pie) ||
                pie->len > ZT_ARRAY_SIZE(pwdn_info->chlg_txt))
        {
            reason = -5;
            goto exit;
        }
        zt_memcpy(pwdn_info->chlg_txt, pie->data, pie->len);
    }
    /* delete auth seq2 */
    zt_msg_del(pmsg_que, pmsg);

    /* send auth with seq3 */
    pwdn_info->auth_seq = ZT_80211_AUTH_SEQ_3;
    for (pauth_info->retry_cnt = 0;
            pauth_info->retry_cnt < AUTH_REQ_RESEND_TIMES;
            pauth_info->retry_cnt++)
    {
        AUTH_DBG("send auth with seq3");
        rst = auth_send(pnic_info, pwdn_info, ZT_80211_STATUS_SUCCESS);
        if (rst)
        {
            AUTH_WARN("auth send fail, error code: %d", rst);
            reason = -6;
            goto exit;
        }

        /* wait until receive auth with seq4 */
        zt_timer_set(&pauth_info->timer, AUTH_RSP_TIMEOUT);
wait_seq4 :
        PT_WAIT_UNTIL(pt, !zt_msg_pop(pmsg_que, &pmsg) ||
                      zt_timer_expired(&pauth_info->timer));
        if (pmsg == NULL)
        {
            /* timeout, resend again */
            continue;
        }

        if (pmsg->tag == ZT_AUTH_TAG_ABORT)
        {
            zt_msg_del(pmsg_que, pmsg);
            reason = ZT_AUTH_TAG_ABORT;
            goto exit;
        }
        else if (pmsg->tag == ZT_AUTH_TAG_RSP)
        {
            zt_u8 ofs;
            zt_u16 seq, status;

            pmgmt = (void *)pmsg->value;
            ofs = zt_80211_hdr_order_get(pmgmt) ? ZT_80211_HT_CTRL_LEN : 0;
            pauth_ie = (void *)((zt_u8 *)&pmgmt->auth + ofs);
            seq = zt_le16_to_cpu(pauth_ie->auth_transaction);
            status = zt_le16_to_cpu(pauth_ie->status_code);

            if (seq == ZT_80211_AUTH_SEQ_4)
            {
                if (status == ZT_80211_STATUS_SUCCESS)
                {
                    zt_msg_del(pmsg_que, pmsg);
                    /* share key type auth success */
                    AUTH_DBG("auth(share key) successfull");
                    reason = ZT_AUTH_TAG_DONE;
                    break;
                }
                else
                {
                    /* stop auth process */
                    AUTH_DBG("auth failed as status code = %d", status);
                    zt_msg_del(pmsg_que, pmsg);
                    pauth_info->retry_cnt = AUTH_REQ_RESEND_TIMES;
                }
            }
            else
            {
                AUTH_DBG("recv auth frame of seq%d not as expect !", seq);
                if (seq == ZT_80211_AUTH_SEQ_2)
                {
                    /* may recv duplicate frame that we have already replied */
                    zt_msg_del(pmsg_que, pmsg);
                    goto wait_seq4;
                }
                else
                {
                    zt_msg_del(pmsg_que, pmsg);
                    AUTH_DBG("auth failed: error auth seq");
                    reason = -7;
                    goto exit;
                }

            }

        }
        else
        {
            AUTH_ERROR("unsutied message tag(%d)", pmsg->tag);
            zt_msg_del(pmsg_que, pmsg);
            goto wait_seq4;
        }
    }
    /* no expected auth with seq4 received */
    if (pauth_info->retry_cnt == AUTH_REQ_RESEND_TIMES)
    {
        AUTH_DBG("auth(share key) fail");
        reason = -7;
    }

exit :
    pauth_info->brun = zt_false;
    nic_mlme_hw_access_unlock(pnic_info);

    *prsn = reason;
    if (reason)
    {
        PT_EXIT(pt);
    }
    PT_END(pt);
}

zt_s32 zt_auth_sta_start(nic_info_st *pnic_info)
{
    auth_info_t *pauth_info;

    if (pnic_info == NULL || ZT_CANNOT_RUN(pnic_info))
    {
        return -1;
    }

    AUTH_DBG();

    if (!pnic_info->is_up)
    {
        return -2;
    }
    pauth_info = pnic_info->auth_info;

    /* send message */
    {
        zt_msg_que_t *pmsg_que = &pauth_info->msg_que;
        zt_msg_t *pmsg;
        zt_s32 rst;

        rst = zt_msg_new(pmsg_que, ZT_AUTH_TAG_START, &pmsg);
        if (rst)
        {
            AUTH_WARN("msg new fail error fail: %d", rst);
            return -3;
        }
        rst = zt_msg_push(pmsg_que, pmsg);
        if (rst)
        {
            zt_msg_del(pmsg_que, pmsg);
            AUTH_WARN("msg push fail error fail: %d", rst);
            return -4;
        }
    }

    return 0;
}

zt_s32 zt_auth_sta_stop(nic_info_st *pnic_info)
{
    auth_info_t *pauth_info;

    if (pnic_info == NULL || ZT_CANNOT_RUN(pnic_info))
    {
        return -1;
    }

    AUTH_DBG();

    pauth_info = pnic_info->auth_info;
    if (!pauth_info->brun)
    {
        return -2;
    }

    /* send message */
    {
        zt_msg_que_t *pmsg_que = &pauth_info->msg_que;
        zt_msg_t *pmsg;
        zt_s32 rst;

        rst = zt_msg_new(pmsg_que, ZT_AUTH_TAG_ABORT, &pmsg);
        if (rst)
        {
            AUTH_WARN("msg new fail, error code: %d", rst);
            return -3;
        }
        rst = zt_msg_push(pmsg_que, pmsg);
        if (rst)
        {
            zt_msg_del(pmsg_que, pmsg);
            AUTH_WARN("msg push fail, error code: %d", rst);
            return -4;
        }
    }

    return 0;
}

zt_s32 auth_sta_recv(nic_info_st *pnic_info, wdn_net_info_st *pwdn_info,
                     zt_80211_mgmt_t *pmgmt, zt_u16 mgmt_len)
{
    if (pnic_info == NULL || pmgmt == NULL || mgmt_len == 0)
    {
        AUTH_WARN("None pointer");
        return -1;
    }

    if (pwdn_info == NULL)
    {
        AUTH_ERROR("pwdn_info None pointer");
        return -2;
    }

    AUTH_DBG();

    {
        mlme_state_e state;
        zt_mlme_get_state(pnic_info, &state);
        if (state != MLME_STATE_AUTH)
        {
            return -3;
        }
    }

    if (!zt_80211_is_same_addr(pmgmt->da, nic_to_local_addr(pnic_info)) ||
            !zt_80211_is_same_addr(pwdn_info->bssid, zt_wlan_get_cur_bssid(pnic_info)))
    {
        return -4;
    }

    if (mgmt_len > sizeof(auth_rsp_t))
    {
        AUTH_DBG("auth frame length over limite");
        return -5;
    }

    /* send message */
    {
        auth_info_t *pauth_info = pnic_info->auth_info;
        zt_msg_que_t *pmsg_que = &pauth_info->msg_que;
        zt_msg_t *pmsg;
        zt_s32 rst;

        rst = zt_msg_new(pmsg_que, ZT_AUTH_TAG_RSP, &pmsg);
        if (rst)
        {
            AUTH_DBG("new msg fail, error code: %d", rst);
            return -6;
        }
        pmsg->len = mgmt_len;
        zt_memcpy(pmsg->value, pmgmt, mgmt_len);
        rst = zt_msg_push(pmsg_que, pmsg);
        if (rst)
        {
            zt_msg_del(pmsg_que, pmsg);
            AUTH_DBG("push msg fail, error code: %d", rst);
            return -7;
        }
    }

    return 0;
}

zt_s32 zt_auth_frame_parse(nic_info_st *pnic_info, wdn_net_info_st *pwdn_info,
                           zt_80211_mgmt_t *pmgmt, zt_u16 mgmt_len)
{
    zt_s32 rst;

    switch (zt_local_cfg_get_work_mode(pnic_info))
    {
        case ZT_INFRA_MODE :
            rst = auth_sta_recv(pnic_info, pwdn_info, pmgmt, mgmt_len);
            if (rst)
            {
                AUTH_DBG("auth_sta_recv fail, error code: %d", rst);
                return -1;
            }
            break;

#ifdef CFG_ENABLE_AP_MODE
        case ZT_MASTER_MODE :
            rst = auth_ap_recv(pnic_info, pwdn_info, pmgmt, mgmt_len);
            if (rst)
            {
                AUTH_WARN("auth_ap_recv fail, error code: %d", rst);
                return -2;
            }
            break;
#endif

        default :
            AUTH_WARN("undefined work mode");
            return -3;
    }

    return 0;
}

zt_inline static zt_s32 auth_msg_init(zt_msg_que_t *pmsg_que)
{
    zt_msg_init(pmsg_que);
    return (zt_msg_alloc(pmsg_que, ZT_AUTH_TAG_RSP, sizeof(auth_rsp_t), 2) ||
            zt_msg_alloc(pmsg_que, ZT_AUTH_TAG_ABORT, 0, 1) ||
            zt_msg_alloc(pmsg_que, ZT_AUTH_TAG_START, 0, 1)) ? -1 : 0;
}

zt_inline static void auth_msg_deinit(zt_msg_que_t *pmsg_que)
{
    zt_msg_deinit(pmsg_que);
}

zt_s32 zt_auth_init(nic_info_st *pnic_info)
{
    auth_info_t *pauth_info;

    AUTH_DBG();

    pauth_info = zt_kzalloc(sizeof(auth_info_t));
    if (pauth_info == NULL)
    {
        AUTH_WARN("malloc auth_info failed");
        return -1;
    }
    pnic_info->auth_info = pauth_info;

    if (auth_msg_init(&pauth_info->msg_que))
    {
        AUTH_WARN("malloc auth msg failed");
        return -2;
    }
    pauth_info->brun = zt_false;

    return 0;
}

zt_s32 zt_auth_term(nic_info_st *pnic_info)
{
    auth_info_t *pauth_info;

    if (pnic_info == NULL)
    {
        return 0;
    }

    AUTH_DBG();

    pauth_info = pnic_info->auth_info;
    if (pauth_info)
    {
        auth_msg_deinit(&pauth_info->msg_que);
        zt_kfree(pauth_info);
        pnic_info->auth_info = NULL;
    }

    return 0;
}

static void deauth_wlan_hdr(nic_info_st *pnic_info, struct xmit_buf *pxmit_buf)
{
    zt_u8 *pframe;
    struct wl_ieee80211_hdr *pwlanhdr;

    pframe = pxmit_buf->pbuf + TXDESC_OFFSET;
    pwlanhdr = (struct wl_ieee80211_hdr *)pframe;

    pwlanhdr->frame_ctl = 0;
    SetFrameType(pframe, WIFI_MGT_TYPE);
    SetFrameSubType(pframe, WIFI_DEAUTH);  /* set subtype */
}


/*
tx deauth frame
*/
zt_s32 zt_deauth_xmit_frame(nic_info_st *pnic_info, zt_u8 *pmac,
                            zt_u16 reason_code)
{
    zt_u8 *pframe;
    struct wl_ieee80211_hdr *pwlanhdr;
    struct xmit_buf *pxmit_buf;
    zt_u16 pkt_len;
    tx_info_st      *ptx_info;
    wdn_net_info_st *pwdn_info;

    ptx_info = (tx_info_st *)pnic_info->tx_info;

    pwdn_info = zt_wdn_find_info(pnic_info, pmac);
    if (pwdn_info == NULL)
    {
        return -1;
    }

    /* alloc xmit_buf */
    pxmit_buf = zt_xmit_extbuf_new(ptx_info);
    if (pxmit_buf == NULL)
    {
        AUTH_WARN("pxmit_buf is NULL");
        return -1;
    }
    zt_memset(pxmit_buf->pbuf, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

    /* type of management is 0100 */
    deauth_wlan_hdr(pnic_info, pxmit_buf);

    /* set txd at tx module */
    pframe = pxmit_buf->pbuf + TXDESC_OFFSET; /* pframe point to wlan_hdr */
    pwlanhdr = (struct wl_ieee80211_hdr *)pframe;

    pkt_len = sizeof(struct wl_ieee80211_hdr_3addr);
    pframe += pkt_len; /* point to iv or frame body */

    zt_memcpy(pwlanhdr->addr1, pwdn_info->mac, ZT_80211_MAC_ADDR_LEN);
    zt_memcpy(pwlanhdr->addr2, nic_to_local_addr(pnic_info), ZT_80211_MAC_ADDR_LEN);
    zt_memcpy(pwlanhdr->addr3, zt_wlan_get_cur_bssid(pnic_info),
              ZT_80211_MAC_ADDR_LEN);
    /*1.add reason_code*/
    pframe = set_fixed_ie(pframe, 2, (zt_u8 *)&reason_code, &pkt_len);

    pxmit_buf->pkt_len = pkt_len;
    zt_nic_mgmt_frame_xmit(pnic_info, pwdn_info, pxmit_buf, pxmit_buf->pkt_len);

    return 0;
}

zt_s32 zt_deauth_frame_parse(nic_info_st *pnic_info, wdn_net_info_st *pwdn_info,
                             zt_80211_mgmt_t *pmgmt, zt_u16 mgmt_len)
{
    zt_s32 rst;

    if (pnic_info == NULL || pwdn_info == NULL ||
            pmgmt == NULL || mgmt_len == 0)
    {
        return -1;
    }

    if (ZT_CANNOT_RUN(pnic_info))
    {
        return -2;
    }

    switch (zt_local_cfg_get_work_mode(pnic_info))
    {
        case ZT_INFRA_MODE :
            if (pwdn_info)
            {
                AUTH_INFO("ZT_80211_FRM_DEAUTH[%d] frame reason:%d",
                          pnic_info->ndev_id, pmgmt->deauth.reason_code);
                rst = zt_mlme_deauth(pnic_info,
                                     zt_false,
                                     (zt_80211_reasoncode_e)pmgmt->deauth.reason_code);
                if (rst)
                {
                    AUTH_WARN("zt_mlme_deauth fail, reason code: %d", rst);
                    return -3;
                }
            }
            break;

#ifdef CFG_ENABLE_AP_MODE
        case ZT_MASTER_MODE :
            rst = deauth_work_ap(pnic_info, pwdn_info, pmgmt, mgmt_len);
            if (rst)
            {
                AUTH_WARN("deauth_work_ap fail, reason code: %d", rst);
                return -4;
            }
            break;
#endif

        default :
            AUTH_WARN("unknown mode");
            return -5;
    }

    return 0;
}


