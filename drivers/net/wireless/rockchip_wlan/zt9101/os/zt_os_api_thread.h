/*
 * zt_os_api_thread.h
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
#ifndef __ZT_OS_API_THREAD_H__
#define __ZT_OS_API_THREAD_H__

#define DEFAULT_CPU_ID  (0)

void *zt_os_api_thread_create(void *ptid, zt_s8 *name, void *func, void *param);
zt_s32 zt_os_api_thread_wakeup(void *ptid);
zt_s32 zt_os_api_thread_destory(void *ptid);
zt_bool zt_os_api_thread_wait_stop(void *ptid);
void zt_os_api_thread_exit(void *ptid);
void zt_os_api_thread_enter_hook(void *ptid);

#endif

