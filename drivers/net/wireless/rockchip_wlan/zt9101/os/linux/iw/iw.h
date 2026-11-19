/*
 * iw.h
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
#ifndef __IW_H__
#define __IW_H__

#include "ndev_linux.h"


#define IW_PRV_IOCTL_START       (SIOCIWFIRSTPRIV)

#define IW_PRV_FW_DEBUG          (SIOCIWFIRSTPRIV + 0)
#define IW_PRV_READ_REG_TEST     (SIOCIWFIRSTPRIV + 1)
#define IW_PRV_WRITE_REG_TEST    (SIOCIWFIRSTPRIV + 2)
#define IW_PRV_ARS               (SIOCIWFIRSTPRIV + 3)
#define IW_PRV_TEST              (SIOCIWFIRSTPRIV + 4)
#define IW_PRV_TXAGG             (SIOCIWFIRSTPRIV + 5)


#define IW_IOC_HOSTAPD           (SIOCIWFIRSTPRIV + 28)
#define IW_IOC_WPA_SUPPLICANT    (SIOCIWFIRSTPRIV + 31)

#endif
