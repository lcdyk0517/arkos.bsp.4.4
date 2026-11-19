/*
 * sec_arc4.c
 *
 * impliment arc4 arithmetic used for tkip or wep encryption
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
#include "common.h"

void arc4_init(struct arc4context *parc4ctx, zt_u8 *key, zt_u32 key_len)
{
    zt_u32 t, u;
    zt_u32 keyindex;
    zt_u32 counter;
    zt_u32 stateindex;
    zt_u8 *state;

    state = parc4ctx->state;
    parc4ctx->x = 0;
    parc4ctx->y = 0;
    for (counter = 0; counter < 256; counter++)
    {
        state[counter] = (zt_u8) counter;
    }
    keyindex = 0;
    stateindex = 0;
    for (counter = 0; counter < 256; counter++)
    {
        t = state[counter];
        stateindex = (stateindex + key[keyindex] + t) & 0xff;
        u = state[stateindex];
        state[stateindex] = (zt_u8)t;
        state[counter] = (zt_u8)u;
        if (++keyindex >= key_len)
        {
            keyindex = 0;
        }
    }
}

static zt_u32 arc4_byte(struct arc4context *parc4ctx)
{
    zt_u32 x;
    zt_u32 y;
    zt_u32 sx, sy;
    zt_u8 *state;

    state = parc4ctx->state;
    x = (parc4ctx->x + 1) & 0xff;
    sx = state[x];
    y = (sx + parc4ctx->y) & 0xff;
    sy = state[y];
    parc4ctx->x = x;
    parc4ctx->y = y;
    state[y] = (zt_u8) sx;
    state[x] = (zt_u8) sy;
    return state[(sx + sy) & 0xff];
}

void arc4_encrypt(struct arc4context *parc4ctx,
                  zt_u8 *dest, zt_u8 *src, zt_u32 len)
{
    zt_u32 i;

    for (i = 0; i < len; i++)
    {
        dest[i] = src[i] ^ (zt_u8)arc4_byte(parc4ctx);
    }
}

