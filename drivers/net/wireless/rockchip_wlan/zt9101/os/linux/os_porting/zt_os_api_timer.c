/*
 * zt_os_api_timer.c
 *
 * used for .....
 *
 * Author: zenghua
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
/* include */
#include "zt_os_api.h"

/* macro */

/* type */

/* function declaration */


zt_inline zt_u64 zt_os_api_timestamp(void)
{
    return jiffies;
}

zt_inline zt_u32 zt_os_api_msecs_to_timestamp(zt_u32 msecs)
{
    return msecs_to_jiffies(msecs);
}

zt_inline zt_u32 zt_os_api_timestamp_to_msecs(zt_u32 timestamp)
{
    return jiffies_to_msecs(timestamp);
}

zt_s32 zt_os_api_timer_reg(zt_os_api_timer_t *ptimer,
                           void (* fn)(zt_os_api_timer_t *), void *pdata)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
    timer_setup(ptimer, fn, 0);
#else
    init_timer(ptimer);
    ptimer->function    = (void *)fn;
    ptimer->data        = (zt_ptr)pdata;
#endif
    return 0;
}

zt_inline zt_s32 zt_os_api_timer_set(zt_os_api_timer_t *ptimer, zt_u32 intv_ms)
{
    mod_timer(ptimer, jiffies + msecs_to_jiffies(intv_ms));
    return 0;
}

zt_inline zt_s32 zt_os_api_timer_unreg(zt_os_api_timer_t *ptimer)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 15, 0))
    timer_delete(ptimer);
#else
    del_timer(ptimer);
#endif
    return 0;
}

zt_s32 zt_os_api_timer_init(void)
{
    return 0;
}

zt_s32 zt_os_api_timer_term(void)
{
    return 0;
}

