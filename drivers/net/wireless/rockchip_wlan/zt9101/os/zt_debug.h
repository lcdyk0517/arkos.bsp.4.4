/*
 * zt_debug.h
 *
 * used for debug
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
#ifndef __ZT_DEBUG_H__
#define __ZT_DEBUG_H__

#define ZT_LOG_LEVEL_DEBUG 0
#define ZT_LOG_LEVEL_INFO  1
#define ZT_LOG_LEVEL_WARN  2
#define ZT_LOG_LEVEL_ERROR 3
#define ZT_LOG_LEVEL_NONE  4
/*
 * The color for terminal (foreground)
 * BLACK    30
 * RED      31
 * GREEN    32
 * YELLOW   33
 * BLUE     34
 * PURPLE   35
 * CYAN     36
 * WHITE    37
 */
#ifdef ZT_DEBUG_COLOR
#define _ZT_DEBUG_HDR_D(lvl_name, color_n) \
    ZT_KERN_LEVELS_DEBUG "\033["#color_n"m["lvl_name"]"
#define _ZT_DEBUG_HDR_I(lvl_name, color_n) \
    ZT_KERN_LEVELS_INFO "\033["#color_n"m["lvl_name"]"
#define _ZT_DEBUG_HDR_W(lvl_name, color_n) \
    ZT_KERN_LEVELS_WARNING "\033["#color_n"m["lvl_name"]"
#define _ZT_DEBUG_HDR_E(lvl_name, color_n) \
    ZT_KERN_LEVELS_ERR "\033["#color_n"m["lvl_name"]"
#define _ZT_DEBUG_END   "\033[0m\n"
#else
#define _ZT_DEBUG_HDR_D(lvl_name, color_n)    ZT_KERN_LEVELS_DEBUG "["lvl_name"]"
#define _ZT_DEBUG_HDR_I(lvl_name, color_n)    ZT_KERN_LEVELS_INFO "["lvl_name"]"
#define _ZT_DEBUG_HDR_W(lvl_name, color_n)    ZT_KERN_LEVELS_WARNING "["lvl_name"]"
#define _ZT_DEBUG_HDR_E(lvl_name, color_n)    ZT_KERN_LEVELS_ERR "["lvl_name"]"
#define _ZT_DEBUG_END   "\n"
#endif

#if (ZT_DEBUG_LEVEL <= ZT_LOG_LEVEL_DEBUG)
#define LOG_D(fmt, ...) \
    ZT_LOG_PRINT(_ZT_DEBUG_HDR_D("D", 0) fmt _ZT_DEBUG_END, ##__VA_ARGS__)
#else
#define LOG_D(fmt, ...)
#endif

#if (ZT_DEBUG_LEVEL <= ZT_LOG_LEVEL_INFO)
#define LOG_I(fmt, ...) \
    ZT_LOG_PRINT(_ZT_DEBUG_HDR_I("I", 32) fmt _ZT_DEBUG_END, ##__VA_ARGS__)
#else
#define LOG_I(fmt, ...)
#endif

#if (ZT_DEBUG_LEVEL <= ZT_LOG_LEVEL_WARN)
#define LOG_W(fmt, ...) \
    ZT_LOG_PRINT(_ZT_DEBUG_HDR_W("W", 33) fmt _ZT_DEBUG_END, ##__VA_ARGS__)
#else
#define LOG_W(fmt, ...)
#endif

#if (ZT_DEBUG_LEVEL <= ZT_LOG_LEVEL_ERROR)
#define LOG_E(fmt, ...) \
    ZT_LOG_PRINT(_ZT_DEBUG_HDR_E("E", 31) fmt _ZT_DEBUG_END, ##__VA_ARGS__)
#else
#define LOG_E(fmt, ...)
#endif

#ifndef zt_log_array
#if (ZT_DEBUG_LEVEL <= ZT_LOG_LEVEL_DEBUG)
static zt_inline void zt_log_array(void *ptr, zt_u16 len)
{
    zt_u16 i = 0;
    zt_u16 num;
    zt_u8 *pdata = ptr;

#define NUM_PER_LINE    8
    ZT_LOG_PRINT(ZT_KERN_LEVELS_DEBUG "\r\n");
    for (i = 0, num = len / NUM_PER_LINE; i < num; i++, pdata += 8)
    {
        ZT_LOG_PRINT(ZT_KERN_LEVELS_DEBUG "%02X %02X %02X %02X %02X %02X %02X %02X\r\n",
                     pdata[0], pdata[1], pdata[2], pdata[3],
                     pdata[4], pdata[5], pdata[6], pdata[7]);
    }
    num = len % NUM_PER_LINE;
    if (num)
    {
        for (i = 0; i < num; i++)
        {
            ZT_LOG_PRINT(ZT_KERN_LEVELS_DEBUG "%02X", pdata[i]);
        }
    }
    ZT_LOG_PRINT(ZT_KERN_LEVELS_DEBUG "\r\n");
}
#else
#define zt_log_array(...)
#endif
#endif

#endif      /* END OF __ZT_DEBUG_H__ */
