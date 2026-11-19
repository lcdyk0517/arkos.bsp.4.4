/*
 * cfg_parse.c
 *
 * used for .....
 *
 * Author: renhaibo
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
#include <linux/string.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/types.h>
#include <linux/utsname.h>
#include <linux/list.h>
#include <linux/usb.h>
#include <linux/time.h>

#include "common.h"
#include "hif.h"
#include "power.h"

struct cfg_preparse_t
{
    const zt_s8 *key;
    zt_s32(*preparse_handle)(hif_mngent_st *hif, const zt_s8 *value);
};

static zt_s32 fw_path_preparse_handle(hif_mngent_st *hif, const zt_s8 *value);
static zt_s32 fw1_path_preparse_handle(hif_mngent_st *hif, const zt_s8 *value);
static zt_s32 ifname_preparse_handle(hif_mngent_st *hif, const zt_s8 *value);
static zt_s32 if2name_preparse_handle(hif_mngent_st *hif, const zt_s8 *value);

static const struct cfg_preparse_t __gl_cfg_preparse_st[] =
{
    {"fw", fw_path_preparse_handle},
    {"fw1", fw1_path_preparse_handle},
    {"ifname", ifname_preparse_handle},
    {"if2name", if2name_preparse_handle},
};

static zt_s32 fw_path_preparse_handle(hif_mngent_st *hif, const zt_s8 *value)
{
    LOG_I("fw: %s", value);
    zt_memcpy(hif->fw_path, value, zt_strlen(value));
    return 0;
}

static zt_s32 fw1_path_preparse_handle(hif_mngent_st *hif, const zt_s8 *value)
{
    LOG_I("fw1: %s", value);
    zt_memcpy(hif->fw1_path, value, zt_strlen(value));
    return 0;
}

static zt_s32 ifname_preparse_handle(hif_mngent_st *hif, const zt_s8 *value)
{
    LOG_I("ifname: %s", value);
    zt_memcpy(hif->ifname, value, zt_strlen(value));
    return 0;
}

static zt_s32 if2name_preparse_handle(hif_mngent_st *hif, const zt_s8 *value)
{
    LOG_I("if2name: %s", value);
    zt_memcpy(hif->if2name, value, zt_strlen(value));
    return 0;
}

struct cfg_parse_t
{
    const zt_s8 *key;
    zt_s32(*parse_handle)(nic_info_st *nic_info, const zt_s8 *value);
};

#ifdef CFG_ENABLE_AP_MODE
static zt_s32 ssid_parse_handle(nic_info_st *nic_info, const zt_s8 *value);
#endif
static zt_s32 channel_parse_handle(nic_info_st *nic_info, const zt_s8 *value);
static zt_s32 bw_parse_handle(nic_info_st *nic_info, const zt_s8 *value);
static zt_s32 work_mode_parse_handle(nic_info_st *nic_info, const zt_s8 *value);
static zt_s32 channelplan_parse_handle(nic_info_st *nic_info,
                                       const zt_s8 *value);
static zt_s32 ba_func_tx_parse_handle(nic_info_st *nic_info,
                                      const zt_s8 *value);
static zt_s32 ba_func_rx_parse_handle(nic_info_st *nic_info,
                                      const zt_s8 *value);
static zt_s32 scan_ch_to_parse_handle(nic_info_st *nic_info,
                                      const zt_s8 *value);
static zt_s32 scan_prb_times_handle(nic_info_st *nic_info, const zt_s8 *value);
static zt_s32 scan_que_deep_handle(nic_info_st *nic_info, const zt_s8 *value);
static zt_s32 scan_que_node_ttl_handle(nic_info_st *nic_info,
                                       const zt_s8 *value);
static zt_s32 ars_policy_handle(nic_info_st *nic_info,
                                const zt_s8 *value);
static zt_s32 max_ampdu_len_ulimit_handle(nic_info_st *nic_info,
        const zt_s8 *value);
static zt_s32 wlan_guard_handle(nic_info_st *nic_info, const zt_s8 *value);
static zt_s32 rf_power1_handle(nic_info_st *nic_info, const zt_s8 *value);
static zt_s32 rf_power2_handle(nic_info_st *nic_info, const zt_s8 *value);
static zt_s32 vco_cur_handle(nic_info_st *nic_info, const zt_s8 *value);
static zt_s32 sdio_clk_pwr_save_handle(nic_info_st *nic_info,
                                       const zt_s8 *value);

static const struct cfg_parse_t __gl_cfg_parse_st[] =
{
#ifdef CFG_ENABLE_AP_MODE
    {"ssid", ssid_parse_handle},
#endif
    {"channel", channel_parse_handle},
    {"bw", bw_parse_handle},
    {"work_mode", work_mode_parse_handle},
    {"channelplan", channelplan_parse_handle},
    {"ba_func_tx", ba_func_tx_parse_handle},
    {"ba_func_rx", ba_func_rx_parse_handle},
    {"scan_ch_to", scan_ch_to_parse_handle},
    {"scan_prb_times", scan_prb_times_handle},
    {"scan_que_deep", scan_que_deep_handle},
    {"scan_que_node_ttl", scan_que_node_ttl_handle},
    {"ars_policy", ars_policy_handle},
    {"max_ampdu_len_ulimit", max_ampdu_len_ulimit_handle},
    {"wlan_guard", wlan_guard_handle},
    {"rf_power1", rf_power1_handle},
    {"rf_power2", rf_power2_handle},
    {"vco_cur", vco_cur_handle},
    {"sdio_clk_pwr_save", sdio_clk_pwr_save_handle},
};

#ifdef CFG_ENABLE_AP_MODE
static zt_s32 ssid_parse_handle(nic_info_st *nic_info, const zt_s8 *value)
{
    local_info_st *plocal = (local_info_st *)nic_info->local_info;
    LOG_I("ssid: %s", value);
    zt_memcpy(plocal->ssid, value, zt_strlen(value));
    return 0;
}
#endif

static zt_s32 channel_parse_handle(nic_info_st *nic_info, const zt_s8 *value)
{
    local_info_st *plocal = (local_info_st *)nic_info->local_info;
    LOG_I("channel: %s", value);
    plocal->channel = zt_atoi(value);
    return 0;
}

static zt_s32 bw_parse_handle(nic_info_st *nic_info, const zt_s8 *value)
{
    local_info_st *plocal = (local_info_st *)nic_info->local_info;
    LOG_I("bw: %s", value);
    if (zt_strncmp(value, "20M", zt_strlen("20M")) == 0)
    {
        plocal->bw = 0;
    }
    else if (zt_strncmp(value, "40M", zt_strlen("40M")) == 0)
    {
        plocal->bw = 1;
    }
    else
    {
        LOG_E("cfg file format error for bw");
    }
    return 0;
}

static zt_s32 work_mode_parse_handle(nic_info_st *nic_info, const zt_s8 *value)
{
    local_info_st *plocal = (local_info_st *)nic_info->local_info;
    LOG_I("work_mode: %s", value);
    if (zt_strncmp(value, "sta", zt_strlen("sta")) == 0)
    {
        plocal->work_mode = ZT_INFRA_MODE;
    }
#ifdef CFG_ENABLE_AP_MODE
    else if (zt_strncmp(value, "ap", zt_strlen("ap")) == 0)
    {
        plocal->work_mode = ZT_MASTER_MODE;
    }
#endif
#ifdef CFG_ENABLE_ADHOC_MODE
    else if (zt_strncmp(value, "adhoc", zt_strlen("adhoc")) == 0)
    {
        plocal->work_mode = ZT_ADHOC_MODE;
    }
#endif
#ifdef CFG_ENABLE_MONITOR_MODE
    else if (zt_strncmp(value, "moniter", zt_strlen("moniter")) == 0)
    {
        plocal->work_mode = ZT_MONITOR_MODE;
    }
#endif
    else
    {
        LOG_E("cfg file format error for param work_mode");
    }
    return 0;
}

static zt_s32 channelplan_parse_handle(nic_info_st *nic_info,
                                       const zt_s8 *value)
{
    local_info_st *plocal = (local_info_st *)nic_info->local_info;
    LOG_I("channelplan: %s", value);
    plocal->channel_plan = zt_atoi(value);
    return 0;
}

static zt_s32 ba_func_tx_parse_handle(nic_info_st *nic_info, const zt_s8 *value)
{
    local_info_st *plocal = (local_info_st *)nic_info->local_info;
    LOG_I("ba_func_tx: %s", value);
    plocal->ba_enable_tx = zt_atoi(value);
    return 0;
}

static zt_s32 ba_func_rx_parse_handle(nic_info_st *nic_info, const zt_s8 *value)
{
    local_info_st *plocal = (local_info_st *)nic_info->local_info;
    LOG_I("ba_func_rx: %s", value);
    plocal->ba_enable_rx = zt_atoi(value);
    return 0;
}

static zt_s32 scan_ch_to_parse_handle(nic_info_st *nic_info, const zt_s8 *value)
{
    local_info_st *plocal = (local_info_st *)nic_info->local_info;

    LOG_I("scan_ch_to: %s", value);
    plocal->scan_ch_to = zt_atoi(value);
    return 0;
}

static zt_s32 scan_prb_times_handle(nic_info_st *nic_info, const zt_s8 *value)
{
    local_info_st *plocal = (local_info_st *)nic_info->local_info;

    LOG_I("scan_prb_times: %s", value);
    plocal->scan_prb_times = zt_atoi(value);
    return 0;
}

static zt_s32 scan_que_deep_handle(nic_info_st *nic_info, const zt_s8 *value)
{
    local_info_st *plocal = (local_info_st *)nic_info->local_info;

    LOG_I("scan_que_deep: %s", value);
    plocal->scan_que_deep = zt_atoi(value);
    return 0;
}

static zt_s32 scan_que_node_ttl_handle(nic_info_st *nic_info,
                                       const zt_s8 *value)
{
    local_info_st *plocal = (local_info_st *)nic_info->local_info;

    LOG_I("scan_que_node_ttl: %s", value);
    plocal->scan_que_node_ttl = zt_atoi(value);
    return 0;
}

static zt_s32 ars_policy_handle(nic_info_st *nic_info,
                                const zt_s8 *value)
{
    local_info_st *plocal = (local_info_st *)nic_info->local_info;

    LOG_I("ars_policy: %s", value);
    plocal->ars_policy = zt_atoi(value);
    return 0;
}

static zt_s32 max_ampdu_len_ulimit_handle(nic_info_st *nic_info,
        const zt_s8 *value)
{
    local_info_st *plocal = (local_info_st *)nic_info->local_info;

    LOG_I("max_ampdu_len_ulimit: %s", value);
    plocal->max_ampdu_len_ulimit = zt_atoi(value);
    return 0;
}

static zt_s32 wlan_guard_handle(nic_info_st *nic_info, const zt_s8 *value)
{
    local_info_st *plocal = (local_info_st *)nic_info->local_info;

    LOG_I("wlan_guard: %s", value);
    plocal->wlan_guard = zt_atoi(value);
    return 0;
}

static zt_s32 rf_power1_handle(nic_info_st *nic_info, const zt_s8 *value)
{
    local_info_st *plocal = (local_info_st *)nic_info->local_info;
    hif_node_st *hif_node = (hif_node_st *)nic_info->hif_node;

    if (hif_node->drv_ops->driver_flag != 0)
        return 0;

    LOG_I("rf_power1: %s", value);
    plocal->rf_power = zt_atoi(value);
    return 0;
}

static zt_s32 rf_power2_handle(nic_info_st *nic_info, const zt_s8 *value)
{
    local_info_st *plocal = (local_info_st *)nic_info->local_info;
    hif_node_st *hif_node = (hif_node_st *)nic_info->hif_node;

    if (hif_node->drv_ops->driver_flag != 1)
        return 0;

    LOG_I("rf_power2: %s", value);
    plocal->rf_power = zt_atoi(value);
    return 0;
}

static zt_s32 vco_cur_handle(nic_info_st *nic_info, const zt_s8 *value)
{
    local_info_st *plocal = (local_info_st *)nic_info->local_info;

    LOG_I("vco_cur: %s", value);
    plocal->vco_cur = zt_atoi(value);
    return 0;
}

static zt_s32 sdio_clk_pwr_save_handle(nic_info_st *nic_info,
                                       const zt_s8 *value)
{
    hif_node_st *hif_node = nic_info->hif_node;

    LOG_I("sdio_clk_pwr_save: %s", value);
    if (hif_node->hif_type == HIF_SDIO)
    {
        hif_node->u.sdio.clk_pwr_save = !!zt_atoi(value);
    }
    return 0;
}

static void cfg_buffer_handle(const zt_s8 *in_buffer, zt_s8 *out_buffer)
{
    zt_s32 i, j;
    zt_s32 len = zt_strlen(in_buffer);

    for (i = 0, j = 0; i < len; i++)
    {
        if (!zt_isspace(in_buffer[i]))
        {
            out_buffer[j] = in_buffer[i];
            j++;
        }
    }
    out_buffer[j] = '\0';
}

static void cfg_parse_handle(nic_info_st *nic_info, const zt_s8 *buffer)
{
    zt_s32 i;
    const zt_s8 *key;
    zt_s8 *pos;
    zt_s32 num = sizeof(__gl_cfg_parse_st) / sizeof(struct cfg_parse_t);

    pos = zt_strchr(buffer, '=');
    if (pos == NULL)
    {
        LOG_E("can't find sep for this param");
        return;
    }
    *pos++ = '\0';
    key = (zt_s8 *)buffer;
    for (i = 0; i < num; i++)
    {
        if (zt_strcmp(__gl_cfg_parse_st[i].key, key) == 0)
        {
            __gl_cfg_parse_st[i].parse_handle(nic_info, pos);
            return;
        }
    }
    //    LOG_W("[%s]:can't find handler for this key:%s, please register it!",
    //          __func__, key);
}

static void cfg_preparse_handle(hif_mngent_st *hif, const zt_s8 *buffer)
{
    zt_s32 i;
    const zt_s8 *key;
    zt_s8 *pos;
    zt_s32 num = sizeof(__gl_cfg_preparse_st) / sizeof(struct cfg_preparse_t);

    pos = zt_strchr(buffer, '=');
    if (pos == NULL)
    {
        LOG_E("can't find sep for this param");
        return;
    }
    *pos++ = '\0';
    key = (zt_s8 *)buffer;
    for (i = 0; i < num; i++)
    {
        if (zt_strcmp(__gl_cfg_preparse_st[i].key, key) == 0)
        {
            __gl_cfg_preparse_st[i].preparse_handle(hif, pos);
            return;
        }
    }
    //    LOG_W("[%s]:can't find handler for this key:%s, please register it!",
    //          __func__, key);
}

static zt_s32 cfg_read_line(const zt_s8 *cfg_content, size_t size, loff_t *pos,
                            zt_s8 *buffer, zt_u32 length)
{
    zt_s32 ret;
    zt_u32 read_length;
    zt_s8 *eol;
    loff_t offset = *pos;

    if (offset >= size)
    {
        return 0;
    }

    if ((offset + length) > size)
    {
        read_length = size - offset;
    }
    else
    {
        read_length = length;
    }
    zt_memcpy(buffer, &cfg_content[offset], read_length);
    offset += read_length;
    eol = strstr(buffer, ZT_FILE_EOF);
    if (eol != NULL)
    {
        *eol++ = '\0';
        if (!zt_strcmp(ZT_FILE_EOF, "\r\n"))
        {
            *eol++ = '\0';
        }
        ret = (size_t)(eol - buffer);
        offset -= (read_length - ret);
        *pos = offset;
    }
    else
    {
        return -1;
    }

    return ret;
}

zt_s32 zt_cfg_file_parse(void *pnic_info)
{
    zt_s8 read_buffer[513] = {0};
    zt_s8 handle_buffer[513] = {0};
    loff_t pos = 0;
    nic_info_st *nic_info = (nic_info_st *)pnic_info;
    hif_mngent_st *hif = hif_mngent_get();

    if (hif->cfg_size == 0)
    {
        LOG_E("cfg_size is 0, no need parse");
        return -1;
    }

    while (cfg_read_line(hif->cfg_content, hif->cfg_size, &pos, read_buffer,
                         512) > 0)
    {
        if (zt_strlen(read_buffer) == 0)
        {
            continue;
        }

        cfg_buffer_handle((const zt_s8 *)read_buffer, handle_buffer);
        if ((handle_buffer[0] == '#') || (zt_strlen(handle_buffer) == 0))
        {
            continue;
        }

        cfg_parse_handle(nic_info, handle_buffer);
    }

    return 0;
}

zt_s32 zt_cfg_file_preparse(void *phif)
{
    zt_s8 read_buffer[513] = {0};
    zt_s8 handle_buffer[513] = {0};
    loff_t pos = 0;
    hif_mngent_st *hif = phif;

    if (hif->cfg_size == 0)
    {
        LOG_E("cfg_size is 0, no need parse");
        return -1;
    }

    while (cfg_read_line(hif->cfg_content, hif->cfg_size, &pos,
                         read_buffer, 512) > 0)
    {
        if (zt_strlen(read_buffer) == 0)
        {
            continue;
        }

        cfg_buffer_handle((const zt_s8 *)read_buffer, handle_buffer);
        if ((handle_buffer[0] == '#') || (zt_strlen(handle_buffer) == 0))
        {
            continue;
        }

        cfg_preparse_handle(hif, handle_buffer);
    }

    return 0;
}

