/*
 * zt_os_api_sema.h
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
#ifndef __ZT_OS_API_SEMA_H__
#define __ZT_OS_API_SEMA_H__

void zt_os_api_sema_init(zt_os_api_sema_t *sema, zt_s32 init_val);
void zt_os_api_sema_free(zt_os_api_sema_t *sema);
void zt_os_api_sema_post(zt_os_api_sema_t *sema);
zt_s32 zt_os_api_sema_wait(zt_os_api_sema_t *sema);
zt_s32 zt_os_api_sema_try(zt_os_api_sema_t *sema);


#endif

