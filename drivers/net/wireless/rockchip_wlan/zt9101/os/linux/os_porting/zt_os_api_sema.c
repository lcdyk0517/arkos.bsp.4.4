/*
 * zt_os_api_sema.c
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

zt_inline void zt_os_api_sema_init(zt_os_api_sema_t *sema, zt_s32 init_val)
{
    sema_init(sema, init_val);
}

zt_inline void zt_os_api_sema_free(zt_os_api_sema_t *sema)
{
}

zt_inline void zt_os_api_sema_post(zt_os_api_sema_t *sema)
{
    up(sema);
}

zt_inline zt_s32 zt_os_api_sema_wait(zt_os_api_sema_t *sema)
{
    return down_interruptible(sema) ? -1 : 0;
}

zt_inline zt_s32 zt_os_api_sema_try(zt_os_api_sema_t *sema)
{
    return down_trylock(sema) ? -1 : 0;
}

