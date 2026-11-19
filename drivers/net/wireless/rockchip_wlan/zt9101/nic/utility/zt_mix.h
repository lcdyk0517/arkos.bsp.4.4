/*
 * zt_mix.h
 *
 * used for general use macro
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
#ifndef __ZT_MIX__
#define __ZT_MIX__

/* include */
#include "zt_typedef.h"

/* macro */

#ifndef ZT_ARRAY_SIZE
#define ZT_ARRAY_SIZE(arr)  (sizeof(arr) / sizeof(arr[0]))
#endif

#define ZT_PCHAR_2_BE16(val)    ((zt_u16)((val)[0] << 8 | (val)[1]))
#define ZT_PCHAR_2_LE16(val)    ((zt_u16)((val)[1] << 8 | (val)[0]))

#ifndef ZT_BIT
#define ZT_BIT(x)       (1ul << (x))
#endif
#ifndef ZT_BITS
#define ZT_BITS(lbit, hbit)   (~(ZT_BIT(lbit) - 1) & ((ZT_BIT(hbit) - 1) | ZT_BIT(hbit)))
#endif

#ifndef ZT_DIV_ROUND_UP
#define ZT_DIV_ROUND_UP(n, d)       (((n) + ((d) - 1)) / (d))
#endif
#ifndef ZT_DIV_ROUND_CLOSEST
#define ZT_DIV_ROUND_CLOSEST(n, d)  (((n) + ((d) / 2)) / (d))
#endif

#ifndef ZT_RND4
#define ZT_RND4(x)          ((((x) >> 2) + (((x) & 3) ?  1 : 0)) << 2)
#endif
#ifndef ZT_RND8
#define ZT_RND8(x)          ((((x) >> 3) + (((x) & 7) ?  1 : 0)) << 3)
#endif
#ifndef ZT_RND512
#define ZT_RND512(x)        ((((x) >> 9) + (((x) & 511) ?  1 : 0)) << 9)
#endif
#define ZT_RND_MAX(sz, r)   (((sz) + ((r) - 1)) / (r) * (r))

#ifndef ZT_MIN
#define ZT_MIN(x, y)    ((x) < (y) ? (x) : (y))
#endif
#ifndef ZT_MAX
#define ZT_MAX(x, y)    ((x) < (y) ? (y) : (x))
#endif

#define ZT_UNUSED(x)    ((void)(x))
#define ZT_GET_BE16(a) ((zt_u16) (((a)[0] << 8) | (a)[1]))
#define ZT_GET_LE16(a) ((zt_u16) (((a)[1] << 8) | (a)[0]))
#define ZT_PUT_LE16(a, val) \
    do {    \
        (a)[1] = ((zt_u16) (val)) >> 8;\
        (a)[0] = ((zt_u16) (val)) & 0xff;\
    } while (0)

#define ZT_PUT_BE16(a, val)         \
    do {                    \
        (a)[0] = ((zt_u16) (val)) >> 8;    \
        (a)[1] = ((zt_u16) (val)) & 0xff;  \
    } while (0)

#define ZT_GET_BE32(a) ((((zt_u32) (a)[0]) << 24) | (((zt_u32) (a)[1]) << 16) | \
                        (((zt_u32) (a)[2]) << 8) | ((zt_u32) (a)[3]))
#define ZT_PUT_BE32(a, val)                 \
    do {                            \
        (a)[0] = (zt_u8) ((((zt_u32) (val)) >> 24) & 0xff);   \
        (a)[1] = (zt_u8) ((((zt_u32) (val)) >> 16) & 0xff);   \
        (a)[2] = (zt_u8) ((((zt_u32) (val)) >> 8) & 0xff);    \
        (a)[3] = (zt_u8) (((zt_u32) (val)) & 0xff);       \
    } while (0)

#define ZT_TYPE_CHECK(type, x) \
    ({ type __dummy; \
        (void)(&__dummy == &x); \
        1; \
    })

#define ZT_WARN_ON(condition) \
    ({  if (condition) \
        { \
            LOG_W("\n" __FILE__ ":%d: WARN " #condition "!\n", __LINE__); \
        } \
        !!(condition);\
    })

#if (ZT_DEBUG_LEVEL <= ZT_LOG_LEVEL_DEBUG)
#define ZT_ASSERT(expr) do { \
        if (!(expr)) \
        { \
            LOG_E("\n" __FILE__ ":%d: Assertion " #expr " failed!\n", __LINE__); \
            ZT_BUG();\
        } \
    } while (0)
#else
#define ZT_ASSERT(...)
#endif

#define ZT_BUG_ON(x) do { \
        if (x) ZT_BUG(); \
    } while (0)

/* type */

/* function declaration */

zt_s32 zt_isspace(zt_s32 x);
zt_s32 zt_isdigit(zt_s32 x);
zt_s32 zt_atoi(const zt_s8 *nptr);

#endif

