/*
 * zt_config.h
 *
 * used for config
 *
 * Author: pansiwei
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
#ifndef __ZT_CONFIG_H__
#define __ZT_CONFIG_H__

#ifdef _WIN32
#define ZT_FILE_EOF "\r\n"
#else
#define ZT_FILE_EOF "\n"
#endif

/*------------------------------------------------------------------------------
 * Flags of Wi-Fi Direct support
 *------------------------------------------------------------------------------
 */
#define ZT_CONFIG_P2P
#ifdef ZT_CONFIG_P2P
#define CONFIG_P2P_OP_CHK_SOCIAL_CH
#define CONFIG_P2P_INVITE_IOT
#endif

#endif      /* END OF __ZT_CONFIG_H__ */
