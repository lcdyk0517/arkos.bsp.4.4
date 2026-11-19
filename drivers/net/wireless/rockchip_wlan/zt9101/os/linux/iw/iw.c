/*
 * iw.c
 *
 * used for wext
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
#include "ndev_linux.h"
#include "iw_func.h"

#ifdef CONFIG_WIRELESS_EXT
static struct iw_statistics *zt_get_wireless_stats(struct net_device *ndev)
{
    struct iw_statistics *piw_stats;

    ndev_priv_st *ndev_priv = netdev_priv(ndev);
    nic_info_st *pnic_info = ndev_priv->nic;
    zt_u8 qual, level;

    zt_wlan_get_signal_and_qual(pnic_info, &qual, &level);
    if (ndev_priv == NULL)
    {
        free_netdev(ndev);
        LOG_E("netdev_priv error");
        return NULL;
    }
    else
    {
        piw_stats = &ndev_priv->iw_stats;
    }


    piw_stats->qual.qual = qual;
    piw_stats->qual.level = signal_scale_mapping(level);
    piw_stats->qual.noise = 0;
    piw_stats->qual.updated = IW_QUAL_ALL_UPDATED ;/* |IW_QUAL_DBM; */

    //LOG_D("QUAL:%d      level:%d",piw_stats->qual.qual, piw_stats->qual.level);

    return piw_stats;
}

#ifndef CONFIG_MP_MODE
static iw_handler wl_handlers[] =
{
    NULL,//    zt_iw_setCommit,             /*0x8B00  SIOCSIWCOMMIT*/
    zt_iw_getName,               /*0x8B01  SIOCGIWNAME*/
    NULL,//    zt_iw_setNetworkId,          /*0x8B02  SIOCSIWNWID*/
    NULL,//    zt_iw_getNetworkId,          /*0x8B03  SIOCGIWNWID*/
    zt_iw_setFrequency,          /*0x8B04  SIOCSIWFREQ*/
    zt_iw_getFrequency,          /*0x8B05  SIOCGIWFREQ*/
    zt_iw_setOperationMode,      /*0x8B06  SIOCSIWMODE*/
    zt_iw_getOperationMode,      /*0x8B07  SIOCGIWMODE*/
    NULL,//    zt_iw_setSensitivity,        /*0x8B08  SIOCSIWSENS*/
    zt_iw_getSensitivity,        /*0x8B09  SIOCGIWSENS*/
    NULL,//    zt_iw_setRange,              /*0x8B0A  SIOCSIWRANGE*/
    zt_iw_getRange,              /*0x8B0B  SIOCGIWRANGE*/
    zt_iw_setPriv,               /*0x8B0C  SIOCSIWPRIV*/
    NULL,//    zt_iw_getPriv,               /*0x8B0D  SIOCGIWPRIV*/
    NULL,//    zt_iw_setWirelessStats,      /*0x8B0E  SIOCSIWSTATS*/
    zt_iw_getWirelessStats,      /*0x8B0F  SIOCGIWSTATS*/
    NULL,//    zt_iw_setSpyAddresses,       /*0x8B10  SIOCSIWSPY*/
    NULL,//    zt_iw_getSpyInfo,            /*0x8B11  SIOCGIWSPY*/
    NULL,//    zt_iw_setSpyThreshold,       /*0x8B12  SIOCGIWTHRSPY*/
    NULL,//    zt_iw_getSpyThreshold,       /*0x8B13  SIOCWIWTHRSPY*/
    zt_iw_setWap,                /*0x8B14  SIOCSIWAP*/
    zt_iw_getWap,                /*0x8B15  SIOCGIWAP*/
    zt_iw_setMlme,               /*0x8B16  SIOCSIWMLME*/
    NULL,//    zt_iw_getWapList,            /*0x8B17  SIOCGIWAPLIST*/
    zt_iw_setScan,               /*0x8B18  SIOCSIWSCAN*/
    zt_iw_getScan,               /*0x8B19  SIOCGIWSCAN*/
    zt_iw_setEssid,              /*0x8B1A  SIOCSIWESSID*/
    zt_iw_getEssid,              /*0x8B1B  SIOCGIWESSID*/
    NULL,//    zt_iw_setNick,               /*0x8B1C  SIOCSIWNICKN*/
    zt_iw_getNick,               /*0x8B1D  SIOCGIWNICKN*/
    NULL,//      NULL,                        /*0x8B1E  ---hole---*/
    NULL,//      NULL,                        /*0x8B1F  ---hole---*/
    zt_iw_setRate,               /*0x8B20  SIOCSIWRATE*/
    zt_iw_getRate,               /*0x8B21  SIOCGIWRATE*/
    zt_iw_setRts,                /*0x8B22  SIOCSIWRTS*/
    zt_iw_getRts,                /*0x8B23  SIOCGIWRTS*/
    zt_iw_setFragmentation,      /*0x8B24  SIOCSIWFRAG*/
    zt_iw_getFragmentation,      /*0x8B25  SIOCGIWFRAG*/
    NULL,//    zt_iw_setTransmitPower,      /*0x8B26  SIOCSIWTXPOW*/
    NULL,//    zt_iw_getTransmitPower,      /*0x8B27  SIOCGIWTXPOW*/
    NULL,//    zt_iw_setRetry,              /*0x8B28  SIOCSIWRETRY*/
    zt_iw_getRetry,              /*0x8B29  SIOCGIWRETRY*/
    zt_iw_setEnc,                /*0x8B2A  SIOCSIWENCODE*/
    zt_iw_getEnc,                /*0x8B2B  SIOCGIWENCODE*/
    NULL,//    zt_iw_setPower,              /*0x8B2C  SIOCSIWPOWER*/
    zt_iw_getPower,              /*0x8B2D  SIOCGIWPOWER*/
    NULL,//      NULL,                        /*0x8B2E  ---hole---*/
    NULL,//      NULL,                        /*0x8B2F  ---hole---*/
    zt_iw_setGenIe,              /*0x8B30  SIOCSIWGENIE*/
    NULL,//    zt_iw_getGenIe,              /*0x8B31  SIOCGWGENIE*/
    zt_iw_setAuth,               /*0x8B32  SIOCSIWAUTH*/
    zt_iw_getAuth,               /*0x8B33  SIOCGIWAUTH*/
    zt_iw_setEncExt,             /*0x8B34  SIOCSIWENCODEEXT*/
    zt_iw_getEncExt,             /*0x8B35  SIOCGIWENCODEEXT*/
    zt_iw_setPmkid,              /*0x8B36  SIOCSIWPMKSA*/
    NULL,//      NULL,                        /*---hole---*/
};
#endif

const struct iw_handler_def wl_handlers_def =
{
#ifndef CONFIG_MP_MODE
    .standard = (iw_handler *)wl_handlers,
    .num_standard = ARRAY_SIZE(wl_handlers),
#endif
#if WIRELESS_EXT >= 17
    .get_wireless_stats = zt_get_wireless_stats,
#endif
};
#endif

