/*
 * zt_typedef.h
 *
 * used for typedef
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
#ifndef __TYPEDEF_H__
#define __TYPEDEF_H__

typedef unsigned char      zt_u8;
typedef unsigned short     zt_u16;
typedef unsigned int       zt_u32;
typedef unsigned long long zt_u64;

typedef signed char        zt_s8;
typedef signed short       zt_s16;
typedef signed int         zt_s32;
typedef signed long long   zt_s64;

typedef unsigned long      zt_ptr;


#ifndef NULL
#define NULL ((void *)0)
#endif

typedef enum
{
    zt_false = 0,
    zt_true  = 1
} zt_bool;


#ifndef ZT_RETURN_OK
#define ZT_RETURN_OK            0
#endif
#ifndef ZT_RETURN_FAIL
#define ZT_RETURN_FAIL          (-1)
#endif

#ifndef ZT_RETURN_REMOVED_FAIL
#define ZT_RETURN_REMOVED_FAIL  (-2)
#endif

#ifndef ZT_RETURN_CMD_BUSY
#define ZT_RETURN_CMD_BUSY      (-3)
#endif


#endif

