/*
 * lpc.c
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

#include "common.h"

#define LPS_DBG(fmt, ...)      LOG_D("[%s:%d]"fmt, __func__, __LINE__, ##__VA_ARGS__)
#define LPS_ARRAY(data, len)   zt_log_array(data, len)
#define LPS_INFO(fmt, ...)     LOG_I("[%s:%d]"fmt, __func__, __LINE__, ##__VA_ARGS__)
#define LPS_WARN(fmt, ...)     LOG_W("[%s:%d]"fmt, __func__, __LINE__, ##__VA_ARGS__)
#define LPS_ERROR(fmt, ...)    LOG_E("[%s:%d]"fmt, __func__, __LINE__, ##__VA_ARGS__)

#ifdef CONFIG_LPS

/***************************************************************
    Const Define
***************************************************************/
const zt_s8 LPS_CTRL_TYPE_STR[13][40] =
{
    "LPS_CTRL_SCAN",
    "LPS_CTRL_JOINBSS",
    "LPS_CTRL_CONNECT",
    "LPS_CTRL_DISCONNECT",
    "LPS_CTRL_SPECIAL_PACKET",
    "LPS_CTRL_LEAVE",
    "LPS_CTRL_TRAFFIC_BUSY",
    "LPS_CTRL_TX_TRAFFIC_LEAVE",
    "LPS_CTRL_RX_TRAFFIC_LEAVE",
    "LPS_CTRL_ENTER",
    "LPS_CTRL_LEAVE_CFG80211_PWRMGMT",
    "LPS_CTRL_NO_LINKED",
    "LPS_CTRL_MAX"
};


/***************************************************************
    Static Function
***************************************************************/
static zt_s32 lps_check_lps_ok(nic_info_st *pnic_info)
{
    zt_s32 ret = ZT_RETURN_OK;
    pwr_info_st *ppwr_info = pnic_info->pwr_info;
    sec_info_st *psec_info = pnic_info->sec_info;
    zt_u64 current_time;
    zt_u64 delta_time;

    LPS_DBG();
    current_time = zt_os_api_timestamp();
    delta_time = current_time - ppwr_info->delay_lps_last_timestamp;
    if (delta_time < LPS_DELAY_TIME)
    {
        LPS_WARN(" return: delta_time < LPS_DELAY_TIME");
        return ZT_RETURN_FAIL;
    }
    if (zt_local_cfg_get_work_mode(pnic_info) != ZT_INFRA_MODE)
    {
        LPS_WARN(" return: %d", pnic_info->nic_state);
        return ZT_RETURN_FAIL;
    }
    if (psec_info->dot11AuthAlgrthm == dot11AuthAlgrthm_8021X &&
            psec_info->binstallGrpkey == zt_false)
    {
        LPS_WARN(" Group handshake still in progress!");
        return ZT_RETURN_FAIL;
    }

    return ret;
}


zt_inline static void _lps_init_lps_lock(zt_os_api_sema_t *sema)
{
    zt_os_api_sema_init(sema, 1);
}

zt_inline static void _lps_enter_lps_lock(zt_os_api_sema_t *sema)
{
    zt_os_api_sema_wait(sema);
}

zt_inline static void _lps_exit_lps_lock(zt_os_api_sema_t *sema)
{
    zt_os_api_sema_post(sema);
}

/***************************************************************
    Global Function
***************************************************************/
void zt_lps_ctrl_state_hdl(nic_info_st *pnic_info, zt_u8 lps_ctrl_type)
{
    static zt_bool con_flag = zt_true;

    LPS_DBG();

    LPS_INFO("con_flag = %d", con_flag);
    if (con_flag == zt_true)
    {
        zt_lps_ctrl_wk_hdl(pnic_info, LPS_CTRL_CONNECT);
        con_flag = zt_false;
    }
    zt_lps_ctrl_wk_hdl(pnic_info, lps_ctrl_type);
}

static void lps_set_enter_register(nic_info_st *pnic_info, zt_u8 lps_mode,
                                   zt_u8 smart_lps)
{
    pwr_info_st *ppwr_info = pnic_info->pwr_info;
    zt_bool is_connected;

    LPS_DBG();

    if (ppwr_info->pwr_current_mode == lps_mode)
    {
        if (ppwr_info->pwr_mgnt == PWR_MODE_ACTIVE)
        {
            LPS_DBG(" Skip: now in PWR_MODE_ACTIVE");
            return;
        }
    }
    if (lps_mode == PWR_MODE_ACTIVE)
    {
        ppwr_info->lps_exit_cnts++;
        ppwr_info->pwr_current_mode = lps_mode;
        LPS_DBG("ppwr_info->pwr_current_mode = %d", ppwr_info->pwr_current_mode);
        zt_mcu_set_lps_opt(pnic_info, 0);
        ppwr_info->b_fw_current_in_ps_mode = zt_false;
        LPS_DBG(" FW exit low power saving mode\r\n");
    }
    else
    {
        zt_mlme_get_connect(pnic_info, &is_connected);
        if (lps_check_lps_ok(pnic_info) == ZT_RETURN_OK && is_connected)
        {
            ppwr_info->lps_enter_cnts++;
        }
        ppwr_info->pwr_current_mode = ppwr_info->pwr_mgnt;
        ppwr_info->smart_lps = smart_lps;
        ppwr_info->b_fw_current_in_ps_mode = zt_true;
        zt_mcu_set_lps_opt(pnic_info, 1);
        LPS_DBG(" FW enter low power saving mode\r\n");
    }
}

static void lps_enter(nic_info_st *pnic_info, zt_s8 *msg)
{
    pwr_info_st *ppwr_info = (pwr_info_st *)pnic_info->pwr_info;
    zt_bool bconnect;
    zt_u8 n_assoc_iface = 0;
    zt_s8 buff[32] = {0};

    LPS_DBG(" reason: %s", msg);

    zt_mlme_get_connect(pnic_info, &bconnect);
    if (bconnect && pnic_info->nic_num == 0)
    {
        n_assoc_iface++;
    }

    if (n_assoc_iface == 0)
    {
        LPS_DBG(" Can not enter lps: NO LINKED || virtual nic");
        return;
    }

    if (lps_check_lps_ok(pnic_info) == ZT_RETURN_FAIL)
    {
        LPS_DBG("Check lps fail");
        return;
    }

    LOG_D("ppwr_info->pwr_mgnt = %d, ppwr_info->pwr_current_mode = %d",
          ppwr_info->pwr_mgnt, ppwr_info->pwr_current_mode);
    if (ppwr_info->pwr_mgnt != PWR_MODE_ACTIVE)
    {
        if (ppwr_info->pwr_current_mode == PWR_MODE_ACTIVE)
        {
            zt_sprintf(buff, "WIFI-%s", msg);
            ppwr_info->b_power_saving = zt_true;

            lps_set_enter_register(pnic_info, ppwr_info->pwr_mgnt, ppwr_info->smart_lps);
        }
    }
}

static void lps_exit(nic_info_st *pnic_info, zt_s8 *msg)
{
    zt_s8 buff[32] = {0};
    pwr_info_st *ppwr_info = pnic_info->pwr_info;

    LPS_DBG(" reason: %s", msg);

    if (ppwr_info->pwr_mgnt != PWR_MODE_ACTIVE)
    {
        if (ppwr_info->pwr_current_mode != PWR_MODE_ACTIVE)
        {
            zt_sprintf(buff, "WIFI-%s", msg);
            lps_set_enter_register(pnic_info, PWR_MODE_ACTIVE, 0);
            LPS_DBG(" Exit lps from sleep success");
        }
        else
        {
            LPS_DBG(" Now is awake");
        }
    }
    else
    {
        LPS_DBG("Enter lps fail: pwr_info->pwr_mgnt == PWR_MODE_ACTIVE");
    }
}

static zt_u8 *lps_query_data_from_ie(zt_u8 *ie, zt_u8 type)
{
    if (type == CAPABILITY)
    {
        return (ie + 8 + 2);
    }
    else if (type == TIMESTAMPE)
    {
        return (ie + 0);
    }
    else if (type == BCN_INTERVAL)
    {
        return (ie + 8);
    }
    else
    {
        return NULL;
    }
}

static void lps_rsvd_page_chip_hw_construct_beacon(nic_info_st *pnic_info,
        zt_u8 *frame_index_ptr, zt_u32 *length_out)
{
    struct wl_ieee80211_hdr *wlan_hdr_ptr;
    wdn_net_info_st *pwdn =  zt_wdn_find_info(pnic_info,
                             zt_wlan_get_cur_bssid(pnic_info));
    zt_u16 *frame_control; // Mac header (1)
    zt_u8 bc_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    hw_info_st *hw_info_ptr = (hw_info_st *) pnic_info->hw_info;
    zt_u32 pkt_len = 0;
    zt_wlan_mgmt_info_t *wlan_info_ptr = (zt_wlan_mgmt_info_t *)
                                         pnic_info->wlan_mgmt_info;
    zt_wlan_network_t *cur_network_ptr = & (wlan_info_ptr->cur_network);

    LPS_DBG();
    if (pwdn == NULL)
    {
        LPS_WARN("Not find wdn");
        return;
    }

    wlan_hdr_ptr = (struct wl_ieee80211_hdr *)frame_index_ptr;
    frame_control = &(wlan_hdr_ptr->frame_ctl);
    *frame_control = 0; // Clear 0
    zt_memcpy(wlan_hdr_ptr->addr1, bc_addr, ZT_80211_MAC_ADDR_LEN);
    zt_memcpy(wlan_hdr_ptr->addr2, hw_info_ptr->macAddr, ZT_80211_MAC_ADDR_LEN);
    zt_memcpy(wlan_hdr_ptr->addr3, zt_wlan_get_cur_bssid(pnic_info),
              ZT_80211_MAC_ADDR_LEN);
    SetSeqNum(wlan_hdr_ptr, 0); // Set Sequence Control field
    SetFrameType(frame_index_ptr, WIFI_BEACON); // Set Frame Type field

    frame_index_ptr = frame_index_ptr + sizeof(struct wl_ieee80211_hdr_3addr);
    pkt_len = pkt_len + sizeof(struct wl_ieee80211_hdr_3addr);

    frame_index_ptr = frame_index_ptr + 8;
    pkt_len = pkt_len + 8;

    zt_memcpy(frame_index_ptr, (zt_u8 *)lps_query_data_from_ie(cur_network_ptr->ies,
              BCN_INTERVAL), 2);
    frame_index_ptr = frame_index_ptr + 2;
    pkt_len = pkt_len + 2;

    zt_memcpy(frame_index_ptr, (zt_u8 *)lps_query_data_from_ie(cur_network_ptr->ies,
              CAPABILITY), 2);
    frame_index_ptr = frame_index_ptr + 2;
    pkt_len = pkt_len + 2;

    frame_index_ptr = set_ie(frame_index_ptr, ZT_80211_MGMT_EID_SSID,
                             cur_network_ptr->ssid.length,
                             cur_network_ptr->ssid.data, &pkt_len);
    frame_index_ptr = set_ie(frame_index_ptr, ZT_80211_MGMT_EID_SUPP_RATES,
                             ((pwdn->datarate_len > 8) ? 8 : pwdn->datarate_len),
                             pwdn->datarate, &pkt_len); // cur_network->SupportedRates
    frame_index_ptr = set_ie(frame_index_ptr, ZT_80211_MGMT_EID_DS_PARAMS, 1,
                             (zt_u8 *)&cur_network_ptr->channel, &pkt_len); // Configuration.DSConfig
    if (pwdn->ext_datarate_len > 0)
    {
        frame_index_ptr = set_ie(frame_index_ptr, ZT_80211_MGMT_EID_EXT_SUPP_RATES,
                                 pwdn->ext_datarate_len,
                                 pwdn->ext_datarate, &pkt_len);
    }

    if (pkt_len + TXDESC_SIZE > 512)
    {
        LPS_DBG("Beacon frame too large: %d", pkt_len);
        return;
    }

    *length_out = pkt_len; // Output packet length
}

static void lps_rsvd_page_chip_hw_construct_pspoll(nic_info_st *pnic_info,
        zt_u8 *frame_index_ptr, zt_u32 *length_out)
{
    struct wl_ieee80211_hdr *wlan_hdr_ptr;
    zt_u16 *frame_control; // Mac header (1)
    zt_wlan_mgmt_info_t *wlan_info_ptr = (zt_wlan_mgmt_info_t *)
                                         pnic_info->wlan_mgmt_info;
    zt_wlan_network_t *cur_network_ptr = &(wlan_info_ptr->cur_network);
    hw_info_st *hw_info_ptr = (hw_info_st *) pnic_info->hw_info;
    LPS_DBG();

    wlan_hdr_ptr = (struct wl_ieee80211_hdr *)frame_index_ptr;
    frame_control = &wlan_hdr_ptr->frame_ctl;
    *frame_control = 0; // Clear 0

    SetPwrMgt(frame_control); // Set Power Management bit
    SetFrameSubType(frame_index_ptr,
                    WIFI_PSPOLL); // Set SubType in Frame Control field
    SetDuration(frame_index_ptr,
                cur_network_ptr->aid | 0xC000); // Set Duration/ID field

    zt_memcpy(wlan_hdr_ptr->addr1, zt_wlan_get_cur_bssid(pnic_info),
              ZT_80211_MAC_ADDR_LEN);
    zt_memcpy(wlan_hdr_ptr->addr2, hw_info_ptr->macAddr, ZT_80211_MAC_ADDR_LEN);

    *length_out = 16; // Output packet length
}

static void lps_rsvd_page_chip_hw_construct_nullfunctiondata(
    nic_info_st *pnic_info, zt_u8 *frame_index_ptr, zt_u32 *length_out,
    zt_u8 *addr_start_ptr, zt_bool b_qos, zt_u8 ac, zt_u8 eosp,
    zt_bool b_force_power_save)
{
    struct wl_ieee80211_hdr *wlan_hdr_ptr;
    zt_u16 *frame_control; // Mac header (1)
    zt_u32 pkt_len = 0;
    hw_info_st *hw_info_ptr = (hw_info_st *) pnic_info->hw_info;

    wlan_hdr_ptr = (struct wl_ieee80211_hdr *)frame_index_ptr;
    frame_control = &wlan_hdr_ptr->frame_ctl;
    *frame_control = 0; // Clear 0

    LPS_DBG();
    if (b_force_power_save == zt_true)
    {
        SetPwrMgt(frame_control);
    }
    switch (zt_local_cfg_get_work_mode(pnic_info)) // In nic local_info
    {
        case ZT_INFRA_MODE:
            SetToDs(frame_control);
            zt_memcpy(wlan_hdr_ptr->addr1, zt_wlan_get_cur_bssid(pnic_info),
                      ZT_80211_MAC_ADDR_LEN);
            zt_memcpy(wlan_hdr_ptr->addr2, hw_info_ptr->macAddr, ZT_80211_MAC_ADDR_LEN);
            zt_memcpy(wlan_hdr_ptr->addr3, addr_start_ptr, ZT_80211_MAC_ADDR_LEN);
            break;
#ifdef CFG_ENABLE_AP_MODE
        case ZT_MASTER_MODE:
            SetFrDs(frame_control);
            zt_memcpy(wlan_hdr_ptr->addr1, addr_start_ptr, ZT_80211_MAC_ADDR_LEN);
            zt_memcpy(wlan_hdr_ptr->addr2, zt_wlan_get_cur_bssid(pnic_info),
                      ZT_80211_MAC_ADDR_LEN);
            zt_memcpy(wlan_hdr_ptr->addr3, hw_info_ptr->macAddr, ZT_80211_MAC_ADDR_LEN);
            break;
#endif
        case ZT_ADHOC_MODE:
        default:
            zt_memcpy(wlan_hdr_ptr->addr1, addr_start_ptr, ZT_80211_MAC_ADDR_LEN);
            zt_memcpy(wlan_hdr_ptr->addr2, hw_info_ptr->macAddr, ZT_80211_MAC_ADDR_LEN);
            zt_memcpy(wlan_hdr_ptr->addr3, zt_wlan_get_cur_bssid(pnic_info),
                      ZT_80211_MAC_ADDR_LEN);
            break;
    }
    SetSeqNum(wlan_hdr_ptr, 0);

    if (b_qos == zt_true)
    {
        zt_80211_qos_hdr_t *wlan_qos_hdr_ptr;

        SetFrameSubType(frame_index_ptr, WIFI_QOS_DATA_NULL);

        wlan_qos_hdr_ptr = (zt_80211_qos_hdr_t *)frame_index_ptr;
        SetPriority(&wlan_qos_hdr_ptr->qos_ctrl, ac);
        SetEOSP(&wlan_qos_hdr_ptr->qos_ctrl, eosp);

        pkt_len = sizeof(zt_80211_qos_hdr_t);
    }
    else
    {
        SetFrameSubType(frame_index_ptr, WIFI_DATA_NULL);

        pkt_len = sizeof(zt_80211_hdr_3addr_t);
    }

    *length_out = pkt_len;
}

static void lps_fill_fake_txdesc(nic_info_st *pnic_info,
                                 zt_u8 *tx_des_start_addr, zt_u32 pkt_len,
                                 zt_bool is_ps_poll, zt_bool is_bt_qos_null, zt_bool is_dataframe)
{
    LPS_DBG();
    zt_memset(tx_des_start_addr, 0, TXDESC_SIZE);
    zt_set_bits_to_le_u32(tx_des_start_addr, 27, 1, 1); // bFirstSeg
    zt_set_bits_to_le_u32(tx_des_start_addr, 26, 1, 1);  // bLastSeg
    zt_set_bits_to_le_u32(tx_des_start_addr, 16, 8, 0X28); // Offset = 32
    zt_set_bits_to_le_u32(tx_des_start_addr, 0, 16,
                          pkt_len);  // Buffer size + command header
    zt_set_bits_to_le_u32(tx_des_start_addr + 4, 8, 5,
                          QSLT_MGNT); // Fixed queue of Mgnt queue

    /* Set NAVUSEHDR to prevent Ps-poll AId filed to be changed to error vlaue by Hw. */
    if (is_ps_poll == zt_true)
    {
        zt_set_bits_to_le_u32(tx_des_start_addr + 12, 15, 1, 1);
    }
    else
    {
        zt_set_bits_to_le_u32(tx_des_start_addr + 32, 15, 1,
                              1); // Hw set sequence number
        zt_set_bits_to_le_u32(tx_des_start_addr + 12, 6, 2, 0);
    }

    if (is_bt_qos_null == zt_true)
    {
        zt_set_bits_to_le_u32(tx_des_start_addr + 8, 23, 1, 1);
    }
    zt_set_bits_to_le_u32(tx_des_start_addr + 12, 8, 1,
                          1); /* use data rate which is set by Sw */
    zt_set_bits_to_le_u32(tx_des_start_addr, 31, 1, 1);

    zt_set_bits_to_le_u32(tx_des_start_addr + 16, 0, 7, DESC_RATE1M);

    /* Encrypt the data frame if under security mode excepct null data. Suggested by CCW. */
    if (is_dataframe == zt_true)
    {
        zt_u32 EncAlg;
        sec_info_st *sec_info = pnic_info->sec_info;

        EncAlg = sec_info->dot11PrivacyAlgrthm;
        switch (EncAlg)
        {
            case _NO_PRIVACY_:
                zt_set_bits_to_le_u32(tx_des_start_addr + 4, 22, 2,  0x0);
                break;
            case _WEP40_:
            case _WEP104_:
            case _TKIP_:
                zt_set_bits_to_le_u32(tx_des_start_addr + 4, 22, 2,  0x1);
                break;
            case _SMS4_:
                zt_set_bits_to_le_u32(tx_des_start_addr + 4, 22, 2,  0x2);
                break;
            case _AES_:
                zt_set_bits_to_le_u32(tx_des_start_addr + 4, 22, 2,  0x3);
                break;
            default:
                zt_set_bits_to_le_u32(tx_des_start_addr + 4, 22, 2,  0x0);
                break;
        }
    }

    zt_txdesc_chksum(tx_des_start_addr);
}

static void lps_rsvd_page_mgntframe_attrib_update(nic_info_st *pnic_info,
        struct xmit_frame *pattrib)
{
    wdn_net_info_st *wdn_net_info_ptr;

    LPS_DBG();

    pattrib->hdrlen = WLAN_HDR_A3_LEN;
    pattrib->nr_frags = 1;
    pattrib->priority = 7;

    wdn_net_info_ptr =  zt_wdn_find_info(pnic_info,
                                         zt_wlan_get_cur_bssid(pnic_info));
    pattrib->qsel = QSLT_MGNT;

    pattrib->encrypt_algo = _NO_PRIVACY_;

    pattrib->ht_en = zt_false;
    pattrib->seqnum = wdn_net_info_ptr->wdn_xmitpriv.txseq_tid[QSLT_MGNT];
}

static zt_bool lps_mpdu_send_complete_cb(nic_info_st *nic_info,
        struct xmit_buf *pxmitbuf)
{
    tx_info_st *tx_info = nic_info->tx_info;

    zt_xmit_buf_delete(tx_info, pxmitbuf);

    zt_io_tx_xmit_wake(nic_info);

    return zt_true;
}

static zt_bool lps_mpdu_insert_sending_queue(nic_info_st *nic_info,
        struct xmit_frame *pxmitframe)
{
    zt_u8 *mem_addr;
    zt_u32 ff_hwaddr;
    zt_bool bRet = zt_true;
    zt_s32 ret;
    zt_bool inner_ret = zt_true;
    zt_bool blast = zt_false;
    zt_s32 t, sz, w_sz, pull = 0;
    struct xmit_buf *pxmitbuf = pxmitframe->pxmitbuf;
    hw_info_st *hw_info = nic_info->hw_info;
    zt_u32  txlen = 0;

    LPS_DBG();
    mem_addr = pxmitframe->buf_addr;

    for (t = 0; t < pxmitframe->nr_frags; t++)
    {
        if (inner_ret != zt_true && ret == zt_true)
        {
            ret = zt_false;
        }

        if (t != (pxmitframe->nr_frags - 1))
        {
            LPS_DBG("pattrib->nr_frags=%d\n", pxmitframe->nr_frags);
            sz = hw_info->frag_thresh;
            sz = sz - 4 - 0; /* 4: wlan head filed????????? */
        }
        else
        {
            /* no frag */
            blast = zt_true;
            sz = pxmitframe->last_txcmdsz;
        }

        pull = zt_tx_txdesc_init(pxmitframe, mem_addr, sz, zt_false, 1);
        if (pull)
        {
            mem_addr += PACKET_OFFSET_SZ; /* pull txdesc head */
            pxmitframe->buf_addr = mem_addr;
            w_sz = sz + TXDESC_SIZE;
        }
        else
        {
            w_sz = sz + TXDESC_SIZE + PACKET_OFFSET_SZ;
        }

        if (zt_sec_encrypt(pxmitframe, mem_addr, w_sz))
        {
            ret = zt_false;
            LPS_WARN("encrypt fail!!!!!!!!!!!");
        }
        ff_hwaddr = zt_quary_addr(pxmitframe->qsel);

        txlen = TXDESC_SIZE + pxmitframe->last_txcmdsz;

        if (blast)
        {
            ret = zt_io_write_data(nic_info, 1, mem_addr, w_sz,
                                   ff_hwaddr, (void *)lps_mpdu_send_complete_cb, nic_info, pxmitbuf);
        }
        else
        {
            ret = zt_io_write_data(nic_info, 1, mem_addr, w_sz,
                                   ff_hwaddr, NULL, nic_info, pxmitbuf);
        }

        if (ZT_RETURN_FAIL == ret)
        {
            bRet = zt_false;
            break;
        }

        zt_tx_stats_cnt(nic_info, pxmitframe, sz);

        mem_addr += w_sz;
        mem_addr = (zt_u8 *) ZT_RND4(((SIZE_PTR)(mem_addr)));
    }

    return bRet;
}

static zt_s32 lps_resv_page_xmit(nic_info_st *pnic_info,
                                 struct xmit_frame *mgnt_frame_ptr)
{
    zt_s32 ret = zt_false;
    LPS_DBG();

    if (ZT_CANNOT_RUN(pnic_info))
    {
        zt_xmit_buf_delete(pnic_info->tx_info, mgnt_frame_ptr->pxmitbuf);
        zt_xmit_frame_delete(pnic_info->tx_info, mgnt_frame_ptr);
        LPS_DBG(" fail: pnic_info->is_surprise_removed) || (pnic_info->is_driver_stopped");
        return ret;
    }

    ret = lps_mpdu_insert_sending_queue(pnic_info, mgnt_frame_ptr);
    return ret;
}

static void lps_set_fw_rsvd_page(nic_info_st *pnic_info)
{
    zt_u8 rsvd_page_num = 0;
    zt_u32 max_rsvd_page_buff_size = 0;
    zt_u32 page_size = 128;// Unit byte
    struct xmit_frame *cmd_frame_ptr;
    zt_u8 *reserved_page_packet; // Unit byte
    lps_rsvdpage rsvd_page_loc;
    zt_u16 buff_index = 0; // Unit byte
    zt_u32 beacon_length = 0;
    zt_u8 current_packet_page_num = 0;
    zt_u8 total_page_number = 0;
    zt_u32 ps_poll_length = 0;
    zt_u32 null_data_length = 0;
    zt_u32 qos_null_length = 0;
    zt_u32 total_packets_len = 0;
    zt_bool b_connect = zt_false;
    zt_s32 ret = 0;
    LPS_DBG();

    rsvd_page_num = 255 - TX_PAGE_BOUNDARY_9086X + 1;
    LPS_DBG(" Page size: %d, rsvd page num: %d", page_size, rsvd_page_num);
    max_rsvd_page_buff_size = rsvd_page_num * page_size;
    LPS_DBG(" max_rsvd_page_buff_size: %d", max_rsvd_page_buff_size);
    if (max_rsvd_page_buff_size > MAX_CMDBUF_SZ)
    {
        LPS_DBG("max_rsvd_page_buff_size(%d) is larger than MAX_CMDBUF_SZ(%d)\r\n",
                max_rsvd_page_buff_size, MAX_CMDBUF_SZ);
    }
    // alloc memory for cmd frame
    cmd_frame_ptr = zt_xmit_cmdframe_new(pnic_info->tx_info, CMDBUF_RSVD, 1);
    if (cmd_frame_ptr == NULL)
    {
        LPS_DBG("Alloc reserved page packet fail!");
        return;
    }
    reserved_page_packet = cmd_frame_ptr->buf_addr;
    zt_memset(&rsvd_page_loc, 0, sizeof(lps_rsvdpage));

    // beacon * 2 pages
    buff_index = TXDESC_OFFSET;
    lps_rsvd_page_chip_hw_construct_beacon(pnic_info,
                                           &reserved_page_packet[buff_index], &beacon_length);
    /*
    * When we count the first page size, we need to reserve description size for the RSVD
    * packet, it will be filled in front of the packet in TXPKTBUF.
    */
    current_packet_page_num = (zt_u8)PageNum(TXDESC_SIZE + beacon_length,
                              page_size);
    // If we don't add 1 more page, ARP offload function will fail at 8723bs
    if (current_packet_page_num == 1)
    {
        current_packet_page_num++;
    }
    total_page_number = total_page_number + current_packet_page_num;
    buff_index = buff_index + current_packet_page_num * page_size;

    // ps-poll * 1 page
    rsvd_page_loc.lps_poll = total_page_number;
    lps_rsvd_page_chip_hw_construct_pspoll(pnic_info,
                                           &reserved_page_packet[buff_index], &ps_poll_length);
    lps_fill_fake_txdesc(pnic_info, &reserved_page_packet[buff_index - TXDESC_SIZE],
                         ps_poll_length, zt_true, zt_false, zt_false); // ???????
    current_packet_page_num = (zt_u8)PageNum(TXDESC_SIZE + ps_poll_length,
                              page_size);
    total_page_number = total_page_number + current_packet_page_num;
    buff_index = buff_index + current_packet_page_num * page_size;

    // null data * 1 page
    rsvd_page_loc.lps_null_data = total_page_number;
    lps_rsvd_page_chip_hw_construct_nullfunctiondata(pnic_info,
            &reserved_page_packet[buff_index],
            &null_data_length, zt_wlan_get_cur_bssid(pnic_info),
            zt_false, 0, 0, zt_false);
    lps_fill_fake_txdesc(pnic_info, &reserved_page_packet[buff_index - TXDESC_SIZE],
                         null_data_length, zt_false, zt_false, zt_false);
    current_packet_page_num = (zt_u8)PageNum(null_data_length + TXDESC_SIZE,
                              page_size);
    total_page_number = total_page_number + current_packet_page_num;
    buff_index = buff_index + current_packet_page_num * page_size;

    // Qos null data * 1 page
    rsvd_page_loc.lps_qos_data = total_page_number;
    lps_rsvd_page_chip_hw_construct_nullfunctiondata(pnic_info,
            &reserved_page_packet[buff_index],
            &qos_null_length, zt_wlan_get_cur_bssid(pnic_info),
            zt_true, 0, 0, zt_false);
    lps_fill_fake_txdesc(pnic_info, &reserved_page_packet[buff_index - TXDESC_SIZE],
                         qos_null_length, zt_false, zt_false, zt_false);
    current_packet_page_num = (zt_u8)PageNum(qos_null_length + TXDESC_SIZE,
                              page_size);
    total_page_number = total_page_number + current_packet_page_num;
    total_packets_len = buff_index +
                        qos_null_length; // Do not contain TXDESC_SIZE of next packet
    buff_index = buff_index + current_packet_page_num * page_size;

    if (total_packets_len > max_rsvd_page_buff_size)
    {
        LPS_DBG(" Rsvd page size is not enough! total_packets_len: %d, max_rsvd_page_buff_size: %d",
                total_packets_len, max_rsvd_page_buff_size);
    }
    else
    {
        // update attribute
        lps_rsvd_page_mgntframe_attrib_update(pnic_info, cmd_frame_ptr);
        cmd_frame_ptr->qsel = QSLT_BEACON;
        cmd_frame_ptr->pktlen = total_packets_len - TXDESC_OFFSET;      // ???????

        ret = lps_resv_page_xmit(pnic_info, cmd_frame_ptr);
        if (ret == zt_false)
        {
            LPS_DBG(" fail: lps_resv_page_xmit: %d", ret);
        }
    }

    LPS_DBG("Set RSVD page location to FW, total packet len: %d, total page num: %d\r\n",
            total_packets_len, total_page_number);

    zt_mlme_get_connect(pnic_info, &b_connect);

    if (b_connect == zt_true)
    {
        if (zt_mcu_set_rsvd_page_loc(pnic_info, &rsvd_page_loc) == ZT_RETURN_FAIL)
        {
            LPS_WARN(" fail: lps_set_rsvd_page_loc");
        }
    }
}

static zt_s32 zt_lps_start(nic_info_st *pnic_info, zt_u8 lps_ctrl_type)
{
    mlme_info_t *pmlme_info;
    zt_msg_que_t *pmsg_que;
    zt_msg_t *pmsg;
    mlme_lps_t *param;
    zt_s32 rst;

    LPS_DBG();

    if (pnic_info == NULL)
    {
        return -1;
    }

    if (!pnic_info->is_up)
    {
        LPS_WARN("ndev down");
        return -2;
    }

    pmlme_info = pnic_info->mlme_info;
    pmsg_que = &pmlme_info->msg_que;

    rst = zt_msg_new(pmsg_que, ZT_MLME_TAG_LPS, &pmsg);
    if (rst)
    {
        LPS_WARN("msg new fail error code: %d", rst);
        return -3;
    }
    param = (mlme_lps_t *)pmsg->value;

    param->lps_ctrl_type = lps_ctrl_type;

    rst = zt_msg_push(pmsg_que, pmsg);
    if (rst)
    {
        zt_msg_del(pmsg_que, pmsg);
        LPS_WARN("msg push fail error code: %d", rst);
        return -4;
    }

    return 0;
}

void zt_lps_ctrl_wk_hdl(nic_info_st *pnic_info, zt_u8 lps_ctrl_type)
{
    pwr_info_st *ppwr_info = (pwr_info_st *)pnic_info->pwr_info;
    zt_bool lps_flag = zt_false, bconnect;

    LPS_DBG();

    if (zt_local_cfg_get_work_mode(pnic_info) == ZT_ADHOC_MODE)
    {
        LPS_DBG(" return: nic mode is ADHOC, break!!!");
        return;
    }

    LPS_DBG("lps_ctrl_type == %d", lps_ctrl_type);
    switch (lps_ctrl_type)
    {
        case LPS_CTRL_SCAN:
            zt_mlme_get_connect(pnic_info, &bconnect);
            if (bconnect)
            {
                lps_flag = zt_false;
            }
            break;
        case LPS_CTRL_CONNECT:
            ppwr_info->lps_idle_cnts = 0;

            zt_mcu_set_lps_config(pnic_info);
            zt_mcu_set_fw_lps_config(pnic_info);
            lps_set_fw_rsvd_page(pnic_info);
            zt_mcu_set_fw_lps_get(pnic_info);

            lps_flag = zt_false;
            break;
        case LPS_CTRL_SPECIAL_PACKET:
            ppwr_info->delay_lps_last_timestamp = zt_os_api_timestamp();
        case LPS_CTRL_LEAVE:
        case LPS_CTRL_JOINBSS:
        case LPS_CTRL_LEAVE_CFG80211_PWRMGMT:
        case LPS_CTRL_TRAFFIC_BUSY:
        case LPS_CTRL_TX_TRAFFIC_LEAVE:
        case LPS_CTRL_RX_TRAFFIC_LEAVE:
        case LPS_CTRL_NO_LINKED:
        case LPS_CTRL_DISCONNECT:
            lps_flag = zt_false;
            break;
        case LPS_CTRL_ENTER:
            lps_flag = zt_true;
            break;
        default:
            break;
    }
    if (lps_flag == zt_true)
    {
        lps_enter(pnic_info, (zt_s8 *)(LPS_CTRL_TYPE_STR[lps_ctrl_type]));
    }
    else
    {
        lps_exit(pnic_info, (zt_s8 *)(LPS_CTRL_TYPE_STR[lps_ctrl_type]));
    }
}

zt_u32 zt_lps_wakeup(nic_info_st *pnic_info, zt_u8 lps_ctrl_type,
                     zt_bool enqueue) // Enqueue for interrupt
{
    zt_bool bconnect, wakeup_flag = zt_false;
    zt_u32 ret = ZT_RETURN_OK;

    LPS_DBG();

    if (ZT_CANNOT_RUN(pnic_info))
    {
        LPS_WARN("ZT_CANNOT_RUN = true Skip!");
        return -1;
    }

    if (lps_ctrl_type == LPS_CTRL_ENTER)
    {
        LPS_WARN("Error: lps ctrl type not support!");
        return -2;
    }

    if (lps_ctrl_type == LPS_CTRL_NO_LINKED)
    {
        wakeup_flag = zt_true;
    }
    else
    {
        zt_mlme_get_connect(pnic_info, &bconnect);
        if (bconnect && pnic_info->nic_num == 0)
        {
            wakeup_flag = zt_true;
        }

        if (!bconnect)
        {
            LPS_INFO("driver not connected!!!");
            return ret;
        }
    }

    if (wakeup_flag == zt_true)
    {
        if (enqueue == zt_true)
        {
            zt_lps_start(pnic_info, lps_ctrl_type);
        }
        else
        {
            zt_lps_ctrl_wk_hdl(pnic_info, lps_ctrl_type);
        }
    }

    return ret;
}

zt_u32 zt_lps_sleep(nic_info_st *pnic_info, zt_u8 lps_ctrl_type,
                    zt_bool enqueue) // Enqueue for interrupt
{
    pwr_info_st *pwr_info_ptr;
    zt_u32 lps_deny = 0;
    zt_u32 ret = ZT_RETURN_OK;
    zt_bool bconnect;

    LPS_DBG();

    pwr_info_ptr = (pwr_info_st *)pnic_info->pwr_info;

    if (zt_local_cfg_get_work_mode(pnic_info) != ZT_INFRA_MODE)
    {
        LPS_INFO("lps only supports the STA mode");
        return ZT_RETURN_FAIL;
    }

    zt_mlme_get_connect(pnic_info, &bconnect);
    if (!bconnect)
    {
        LPS_INFO("driver not connected!!!");
        return ret;
    }

    _lps_enter_lps_lock(&pwr_info_ptr->lock);
    lps_deny = pwr_info_ptr->lps_deny;
    _lps_exit_lps_lock(&pwr_info_ptr->lock);
    if (lps_deny != 0)
    {
        LPS_INFO("Skip: can not sleep! Reason: %d", lps_deny);
        return ZT_RETURN_FAIL;
    }

    if (lps_ctrl_type != LPS_CTRL_ENTER)
    {
        LPS_WARN("Error: lps ctrl type not support!");
        return ZT_RETURN_FAIL;
    }

    if (enqueue == zt_true)
    {
        zt_lps_start(pnic_info, lps_ctrl_type);
    }
    else
    {
        zt_lps_ctrl_wk_hdl(pnic_info, lps_ctrl_type);
    }

    return ret;
}

zt_pt_ret_t zt_lps_sleep_mlme_monitor(zt_pt_t *pt, nic_info_st *pnic_info)
{
    mlme_info_t *pmlme_info  = (mlme_info_t *)pnic_info->mlme_info;
    pwr_info_st *ppwr_info = (pwr_info_st *)pnic_info->pwr_info;
    zt_u8 n_assoc_iface = 0;
    zt_bool bconnect;
    mlme_state_e state;

    PT_BEGIN(pt);

    LPS_DBG();

    for (;;)
    {
        zt_timer_set(&ppwr_info->lps_timer, 2000);
        PT_WAIT_UNTIL(pt, zt_timer_expired(&ppwr_info->lps_timer));

        zt_mlme_get_state(pnic_info, &state);
        if (state != MLME_STATE_IDLE)
        {
            zt_timer_set(&ppwr_info->lps_timer, 2000);
            PT_WAIT_UNTIL(pt, zt_timer_expired(&ppwr_info->lps_timer));
            continue;
        }
        pmlme_info->link_info.num_tx_ok_in_period = 0;
        pmlme_info->link_info.num_rx_unicast_ok_in_period = 0;

        PT_WAIT_WHILE(pt, pmlme_info->link_info.busy_traffic);

        zt_mlme_get_connect(pnic_info, &bconnect);
        if (bconnect)
        {
            n_assoc_iface++;
        }
        if (n_assoc_iface == 0)
        {
            if (ppwr_info->pwr_current_mode != PWR_MODE_ACTIVE)
            {
                zt_lps_wakeup(pnic_info, LPS_CTRL_NO_LINKED, zt_true);
                continue;
            }
        }
        else
        {
            if (bconnect)
            {
                LPS_DBG("pkt_num == %d", pmlme_info->link_info.num_rx_unicast_ok_in_period +
                        pmlme_info->link_info.num_tx_ok_in_period);
                LPS_DBG("ppwr_info->pwr_current_mode = %d", ppwr_info->pwr_current_mode);
                if (pmlme_info->link_info.num_rx_unicast_ok_in_period +
                        pmlme_info->link_info.num_tx_ok_in_period > 8)
                {
                    if (ppwr_info->pwr_current_mode != PWR_MODE_ACTIVE)
                    {
                        zt_lps_wakeup(pnic_info, LPS_CTRL_TRAFFIC_BUSY, zt_true);
                        continue;
                    }
                    else
                    {
                        LPS_DBG(" now is awake");
                        continue;
                    }
                }
                else
                {
                    if (ppwr_info->pwr_current_mode == PWR_MODE_ACTIVE)
                    {
                        zt_lps_sleep(pnic_info, LPS_CTRL_ENTER, zt_true);
                        continue;
                    }
                    else
                    {
                        LPS_DBG(" now is sleeping");
                        continue;
                    }
                }
            }
            else
            {
                zt_lps_wakeup(pnic_info, LPS_CTRL_TRAFFIC_BUSY, zt_true);
                continue;
            }
        }
    }
    PT_END(pt);
}
#endif

zt_s32 zt_lps_init(nic_info_st *pnic_info)
{
    pwr_info_st *pwr_info;

    LPS_DBG();

    pwr_info = zt_kzalloc(sizeof(pwr_info_st));
    if (pwr_info == NULL)
    {
        LPS_WARN("[LPS] malloc lps_param_st failed");
        return -1;
    }
    else
    {
        pnic_info->pwr_info = (void *)pwr_info;
    }
#ifdef CONFIG_LPS
    _lps_init_lps_lock(&pwr_info->lock); // Init lock
    _lps_init_lps_lock(&pwr_info->check_32k_lock);

    pwr_info->lps_enter_cnts = 0;
    pwr_info->lps_idle_cnts = 0;
    pwr_info->rpwm = 0;
    pwr_info->b_fw_current_in_ps_mode = zt_false;
    pwr_info->pwr_current_mode = PWR_MODE_ACTIVE;
    pwr_info->smart_lps = 2;
    pwr_info->pwr_mgnt = PWR_MODE_MAX;
#endif
    pwr_info->bInSuspend = zt_false;
    return 0;
}

zt_s32 zt_lps_term(nic_info_st *pnic_info)
{
    pwr_info_st *pwr_info;

    LPS_DBG();

    if (pnic_info == NULL)
    {
        return 0;
    }
    pwr_info = pnic_info->pwr_info;

    if (pwr_info)
    {
        zt_kfree(pwr_info);
        pnic_info->pwr_info = NULL;
    }
    return 0;
}



