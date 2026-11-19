/*
 * zt_os_api_timer.h
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
#ifndef __ZT_OS_API_TIMER_H__
#define __ZT_OS_API_TIMER_H__

zt_u64 zt_os_api_timestamp(void);
zt_u32 zt_os_api_msecs_to_timestamp(zt_u32 msecs);
zt_u32 zt_os_api_timestamp_to_msecs(zt_u32 timestamp);
zt_s32 zt_os_api_timer_reg(zt_os_api_timer_t *ptimer,
                           void (* fn)(zt_os_api_timer_t *), void *pdata);
zt_s32 zt_os_api_timer_set(zt_os_api_timer_t *ptimer, zt_u32 intv_ms);
zt_s32 zt_os_api_timer_unreg(zt_os_api_timer_t *ptimer);
zt_s32 zt_os_api_timer_init(void);
zt_s32 zt_os_api_timer_term(void);


#endif

