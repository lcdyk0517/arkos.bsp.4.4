/*
 * sec.c
 *
 * used for impliment IEEE80211 data frame code and decode logic process
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

#define SEC_DBG(fmt, ...)       LOG_D("[%s:%d]"fmt, __func__, __LINE__, ##__VA_ARGS__)
#define SEC_WARN(fmt, ...)      LOG_E("[%s:%d]"fmt, __func__, __LINE__, ##__VA_ARGS__)
#define SEC_ARRAY(data, len)    zt_log_array(data, len)

extern zt_s32 tkip_encrypt(struct xmit_frame *pxmitframe, zt_u8 *pdata,
                           zt_u32 len);
extern zt_s32 tkip_decrypt(prx_pkt_t ppkt);

extern zt_s32 wep_encrypt(struct xmit_frame *pxmitframe, zt_u8 *pdata,
                          zt_u32 len);
extern zt_s32 wep_decrypt(prx_pkt_t prx_pkt);

#define ZT_SECURITY_KEY_SIZE        16

static zt_u32 get_usecmic(zt_u8 *p)
{
    zt_s32 i;
    zt_u32 res = 0;

    for (i = 0; i < 4; i++)
    {
        res |= ((zt_u32)(*p++)) << (8 * i);
    }

    return res;
}

static void put_usecmic(zt_u8 *p, zt_u32 val)
{
    long i;

    for (i = 0; i < 4; i++)
    {
        *p++ = (zt_u8)(val & 0xff);
        val >>= 8;
    }
}

static void clr_secmic(struct mic_data *pmicdata)
{
    pmicdata->M = 0;
    pmicdata->nBytesInM = 0;
    pmicdata->L = pmicdata->K0;
    pmicdata->R = pmicdata->K1;
}

void zt_sec_mic_set_key(struct mic_data *pmicdata, zt_u8 *key)
{
    pmicdata->K0 = get_usecmic(key);
    pmicdata->K1 = get_usecmic(key + 4);
    clr_secmic(pmicdata);
}

static void mic_append_byte(struct mic_data *pmicdata, zt_u8 b)
{
    pmicdata->M |= ((zt_ptr)b) << (8 * pmicdata->nBytesInM);
    pmicdata->nBytesInM++;
    if (pmicdata->nBytesInM >= 4)
    {
        pmicdata->L ^= pmicdata->M;
        pmicdata->R ^= ROL32(pmicdata->L, 17);
        pmicdata->L += pmicdata->R;
        pmicdata->R ^=
            ((pmicdata->L & 0xff00ff00) >> 8) | ((pmicdata->
                    L & 0x00ff00ff) << 8);
        pmicdata->L += pmicdata->R;
        pmicdata->R ^= ROL32(pmicdata->L, 3);
        pmicdata->L += pmicdata->R;
        pmicdata->R ^= ROR32(pmicdata->L, 2);
        pmicdata->L += pmicdata->R;
        pmicdata->M = 0;
        pmicdata->nBytesInM = 0;
    }
}

void zt_sec_mic_append(struct mic_data *pmicdata, zt_u8 *src, zt_u32 nbytes)
{
    while (nbytes > 0)
    {
        mic_append_byte(pmicdata, *src++);
        nbytes--;
    }
}

void zt_sec_get_mic(struct mic_data *pmicdata, zt_u8 *dst)
{
    mic_append_byte(pmicdata, 0x5a);
    mic_append_byte(pmicdata, 0);
    mic_append_byte(pmicdata, 0);
    mic_append_byte(pmicdata, 0);
    mic_append_byte(pmicdata, 0);
    while (pmicdata->nBytesInM != 0)
    {
        mic_append_byte(pmicdata, 0);
    }
    put_usecmic(dst, pmicdata->L);
    put_usecmic(dst + 4, pmicdata->R);
    clr_secmic(pmicdata);
}


zt_s32 zt_sec_encrypt(void *xmitframe, zt_u8 *pdata, zt_u32 len)
{
    zt_s32 res = 0;
    struct xmit_frame *pxmitframe = (struct xmit_frame *)xmitframe;

    if (pxmitframe == NULL || pdata == NULL || len == 0)
    {
        res = -1;
        goto exit;
    }

    switch (pxmitframe->encrypt_algo)
    {
        case _TKIP_ :
            res = tkip_encrypt(pxmitframe, pdata, len);
            break;

        case _WEP40_ :
        case _WEP104_ :
            res = wep_encrypt(pxmitframe, pdata, len);
            break;

        case _NO_PRIVACY_ :
        case _AES_ :
        default :
            break;
    }

exit:
    return res;
}

zt_s32 zt_sec_decryptor(void *ptr)
{
    prx_pkt_t prx_pkt = ptr;
    prx_pkt_info_t prx_pkt_info = &prx_pkt->pkt_info;
    zt_s32 res = 0;

    if (!!zt_80211_hdr_protected_get(prx_pkt->pdata))
    {
        switch (prx_pkt_info->encrypt_algo)
        {
            case _WEP40_:
            case _WEP104_:
                res = wep_decrypt(prx_pkt);
                break;

            case _TKIP_:
                res = tkip_decrypt(prx_pkt);
                break;

            case _AES_:
            default:
                break;
        }
    }

    prx_pkt_info->bdecrypted = zt_true;

    return res;
}


zt_s32 zt_sec_info_init(nic_info_st *nic_info)
{
    sec_info_st *sec_info;

    SEC_DBG("sec_info init");
    sec_info = zt_kzalloc(sizeof(sec_info_st));
    if (sec_info == NULL)
    {
        SEC_WARN("malloc sec_info failed");
        nic_info->sec_info = NULL;
        return -1;
    }

    nic_info->sec_info = sec_info;
    return 0;
}

zt_s32 zt_sec_info_term(nic_info_st *nic_info)
{
    sec_info_st *sec_info = nic_info->sec_info;

    if (sec_info == NULL)
    {
        return 0;
    }
    LOG_D("[zt_sec_info_term] start");

    if (sec_info)
    {
        zt_kfree(sec_info);
        nic_info->sec_info = NULL;
    }

    LOG_D("[zt_sec_info_term] end");

    return 0;
}

static zt_s32 new_cam_id(nic_info_st *pnic_info, zt_u8 *pcam_id)
{
    zt_u8 i;
    zt_s32 bit_mask;

    for (i = 4; i < 32; i++)
    {
        bit_mask = ZT_BIT(i);
        if (!(*pnic_info->cam_id_bitmap & bit_mask))
        {
            *pnic_info->cam_id_bitmap |= bit_mask;
            *pcam_id = i;
            return 0;
        }
    }

    return -1;
}

static zt_s32 free_cam_id(nic_info_st *pnic_info, zt_u8 cam_id)
{
    if (cam_id >= 32)
    {
        return -1;
    }

    *pnic_info->cam_id_bitmap &= ~ ZT_BIT(cam_id);

    return 0;
}

/* cam_id_bit_map bit0 �� bit3,fill in the ap group keys.
 * there are 2 cam_id for every sta wdn, one is group key, another is unicast key.
 * for ap, broadcast address has a wdn and a cam_id(group key), every sta has a wdn and a cam_id(unicast key).
 */
/*
 *    mode   |     cam_id       |
 *    sta    |  0-3
|  4-31   |   * 0-3, NULL         | 4-5, unicast key and group key | 6-31, NULL *
 *    ap     |  0-3
|  4-31   |   * 0-3, ap group key | 4-31, ap unicast keys *
 * sta & sta |  0-3
|   4-31   |  * 0-3, NULL         | 4-31, unicast key and group key *
 * sta & ap  |  0-3
|  4-31   |   * 0-3, ap group key | 4-31, ap (unicast keys) or sta (unicast key and group key) *
 */

zt_s32 zt_sec_sta_set_unicast_key(nic_info_st *pnic_info, zt_s8 *unicast_cam_id,
                                  zt_u16 Privacy,
                                  zt_u8 *macaddr, zt_u8 *unicast_key)
{
    zt_s32 ret;
    zt_u8 cam_id;
    struct cam_param *pcam_param;
    zt_u32 param_len = 0;

    param_len = ZT_OFFSETOF(struct cam_param, key) + ZT_SECURITY_KEY_SIZE;
    pcam_param = (struct cam_param *)zt_vmalloc(param_len);
    if (pcam_param == NULL)
    {
        SEC_WARN("[%s]: no memory for param", __func__);
        ret = -1;
        goto exit;
    }
    zt_memset(pcam_param, 0, param_len);

    if (new_cam_id(pnic_info, &cam_id))
    {
        SEC_WARN("alloc cam id fail");
        ret = -1;
        goto exit;
    }

    *unicast_cam_id = cam_id;

    pcam_param->cam_id = cam_id;
    pcam_param->privacy = Privacy;
    pcam_param->keyid = 0;
    pcam_param->is_sta = zt_true;
    pcam_param->is_group = zt_false;
    pcam_param->is_clean = zt_false;
    zt_memcpy(pcam_param->macaddr, macaddr, ZT_80211_MAC_ADDR_LEN);
    zt_memcpy(pcam_param->key, unicast_key, ZT_SECURITY_KEY_SIZE);

    ret = zt_mcu_set_sec_cam(pnic_info, pcam_param);

exit:
    if (pcam_param)
    {
        zt_vfree((zt_u8 *)pcam_param);
    }
    return ret;
}

zt_s32 zt_sec_sta_set_group_key(nic_info_st *pnic_info, zt_s8 *group_cam_id,
                                zt_u8 *bssid)
{
    sec_info_st *psec_info = pnic_info->sec_info;
    zt_s32 ret;
    zt_u8 cam_id;
    struct cam_param *pcam_param;
    zt_u32 param_len = 0;

    param_len = ZT_OFFSETOF(struct cam_param, key) + ZT_SECURITY_KEY_SIZE;
    pcam_param = (struct cam_param *)zt_vmalloc(param_len);
    if (pcam_param == NULL)
    {
        SEC_WARN("[%s]: no memory for param", __func__);
        ret = -1;
        goto exit;
    }
    zt_memset(pcam_param, 0, param_len);

    cam_id = psec_info->dot118021XGrpKeyid & 0x03; /* cam_id0~3 8021x group key */;

    *group_cam_id = cam_id;

    pcam_param->cam_id = cam_id;
    pcam_param->privacy = psec_info->dot118021XGrpPrivacy;
    pcam_param->keyid = psec_info->dot118021XGrpKeyid;
    pcam_param->is_sta = zt_true;
    pcam_param->is_group = zt_true;
    pcam_param->is_clean = zt_false;
    zt_memcpy(pcam_param->macaddr, bssid, ZT_80211_MAC_ADDR_LEN);
    zt_memcpy(pcam_param->key,
              psec_info->dot118021XGrpKey[psec_info->dot118021XGrpKeyid].skey,
              ZT_SECURITY_KEY_SIZE);

    ret = zt_mcu_set_sec_cam(pnic_info, pcam_param);
    if (ret)
    {
        SEC_WARN("zt_mcu_set_sec_cam error");
        goto exit;
    }

    zt_mcu_set_on_rcr_am(pnic_info, zt_true);

exit:
    if (pcam_param)
    {
        zt_vfree((zt_u8 *)pcam_param);
    }
    return ret;
}

zt_s32 zt_sec_free_key(nic_info_st *pnic_info, zt_s8 unicast_cam_id,
                       zt_s8 group_cam_id)
{
    zt_s32 ret;
    zt_u8 null_sta[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    zt_u8 null_key[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                         0x00, 0x00, 0x00, 0x00, 0x00
                       };
    struct cam_param *pcam_param;
    zt_u32 param_len = 0;

    param_len = ZT_OFFSETOF(struct cam_param, key) + ZT_SECURITY_KEY_SIZE;
    pcam_param = (struct cam_param *)zt_kzalloc(param_len);
    if (pcam_param == NULL)
    {
        SEC_WARN("[%s]: no memory for param", __func__);
        ret = -1;
        goto exit;
    }

    if (unicast_cam_id >= 0)
    {
        zt_memset(pcam_param, 0, param_len);

        pcam_param->cam_id = unicast_cam_id;
        pcam_param->is_clean = zt_true;
        zt_memcpy(pcam_param->macaddr, null_sta, ZT_80211_MAC_ADDR_LEN);
        zt_memcpy(pcam_param->key, null_key, ZT_SECURITY_KEY_SIZE);

        ret = zt_mcu_set_sec_cam(pnic_info, pcam_param);
        if (ret)
        {
            return -1;
        }
        free_cam_id(pnic_info, unicast_cam_id);
    }

    if (group_cam_id >= 0)
    {
        zt_memset(pcam_param, 0, param_len);

        pcam_param->cam_id = group_cam_id;
        pcam_param->is_clean = zt_true;
        pcam_param->is_group = zt_true;
        zt_memcpy(pcam_param->macaddr, null_sta, ZT_80211_MAC_ADDR_LEN);
        zt_memcpy(pcam_param->key, null_key, ZT_SECURITY_KEY_SIZE);

        ret = zt_mcu_set_sec_cam(pnic_info, pcam_param);
        if (ret)
        {
            return -1;
        }
        free_cam_id(pnic_info, group_cam_id);
    }

exit:
    if (pcam_param)
    {
        zt_kfree((zt_u8 *)pcam_param);
    }
    return ret;
}

#ifdef CFG_ENABLE_AP_MODE
zt_s32 zt_sec_ap_set_unicast_key(nic_info_st *pnic_info, zt_s8 *unicast_cam_id,
                                 zt_u16 Privacy,
                                 zt_u8 *macaddr, zt_u8 *unicast_key)
{
    zt_s32 ret;
    zt_u8 cam_id;
    struct cam_param *pcam_param;
    zt_u32 param_len = 0;

    param_len = ZT_OFFSETOF(struct cam_param, key) + ZT_SECURITY_KEY_SIZE;
    pcam_param = (struct cam_param *)zt_vmalloc(param_len);
    if (pcam_param == NULL)
    {
        SEC_WARN("[%s]: no memory for param", __func__);
        ret = -1;
        goto exit;
    }
    zt_memset(pcam_param, 0, param_len);

    if (new_cam_id(pnic_info, &cam_id))
    {
        SEC_WARN("alloc cam id fail");
        ret = -1;
        goto exit;
    }

    *unicast_cam_id = cam_id;

    pcam_param->cam_id = cam_id;
    pcam_param->privacy = Privacy;
    pcam_param->keyid = 0;
    pcam_param->is_sta = zt_false;
    pcam_param->is_group = zt_false;
    pcam_param->is_clean = zt_false;
    zt_memcpy(pcam_param->macaddr, macaddr, ZT_80211_MAC_ADDR_LEN);
    zt_memcpy(pcam_param->key, unicast_key, ZT_SECURITY_KEY_SIZE);

    ret = zt_mcu_set_sec_cam(pnic_info, pcam_param);

exit:
    if (pcam_param)
    {
        zt_vfree((zt_u8 *)pcam_param);
    }
    return ret;
}

zt_s32 zt_sec_ap_set_group_key(nic_info_st *pnic_info, zt_s8 *group_cam_id,
                               zt_u8 *pmac)
{
    sec_info_st *psec_info = pnic_info->sec_info;
    zt_s32 ret;
    zt_u8 cam_id;
    struct cam_param *pcam_param;
    zt_u32 param_len = 0;

    param_len = ZT_OFFSETOF(struct cam_param, key) + ZT_SECURITY_KEY_SIZE;
    pcam_param = (struct cam_param *)zt_vmalloc(param_len);
    if (pcam_param == NULL)
    {
        SEC_WARN("[%s]: no memory for param", __func__);
        ret = -1;
        goto exit;
    }
    zt_memset(pcam_param, 0, param_len);

    cam_id = psec_info->dot118021XGrpKeyid & 0x03; /* cam_id0~3 8021x group key */;

    *group_cam_id = cam_id;

    pcam_param->cam_id = cam_id;
    pcam_param->privacy = psec_info->dot118021XGrpPrivacy;
    pcam_param->keyid = psec_info->dot118021XGrpKeyid;
    pcam_param->is_sta = zt_false;
    pcam_param->is_group = zt_true;
    pcam_param->is_clean = zt_false;
    zt_memcpy(pcam_param->macaddr, pmac, ZT_80211_MAC_ADDR_LEN);
    zt_memcpy(pcam_param->key,
              psec_info->dot118021XGrpKey[psec_info->dot118021XGrpKeyid].skey,
              ZT_SECURITY_KEY_SIZE);

    ret = zt_mcu_set_sec_cam(pnic_info, pcam_param);
    if (ret)
    {
        SEC_WARN("zt_mcu_set_sec_cam error");
        goto exit;
    }

    zt_mcu_set_on_rcr_am(pnic_info, zt_true);

exit:
    if (pcam_param)
    {
        zt_vfree((zt_u8 *)pcam_param);
    }
    return ret;
}
#endif

