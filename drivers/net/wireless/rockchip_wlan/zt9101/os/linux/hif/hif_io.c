/*
 * hif_io.c
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
#include "zt_os_api.h"
#include "hif.h"
#include "zt_debug.h"

zt_s32 hif_io_write(void *node,  zt_u8 flag, zt_u32 addr, zt_s8 *data,
                    zt_s32 datalen)
{
    struct hif_node_ *hif_node = node;

    ZT_ASSERT(hif_node != NULL);
    ZT_ASSERT(hif_node->ops != NULL);
    ZT_ASSERT(hif_node->ops->hif_write != NULL);

    return hif_node->ops->hif_write(node, flag, addr, data, datalen);
}


zt_s32 hif_io_read(void *node,  zt_u8 flag, zt_u32 addr, zt_s8 *data,
                   zt_s32 datalen)
{
    struct hif_node_ *hif_node = node;

    ZT_ASSERT(hif_node != NULL);
    ZT_ASSERT(hif_node->ops != NULL);
    ZT_ASSERT(hif_node->ops->hif_read != NULL);

    return hif_node->ops->hif_read(node, flag, addr, data, datalen);
}

zt_u8 hif_io_read8(void *node, zt_u32 addr, zt_s32 *err)
{
    struct hif_node_ *hif_node = node;
    zt_s32 ret = 0;
    zt_u8 value;

    ZT_ASSERT(hif_node != NULL);
    ZT_ASSERT(hif_node->ops != NULL);
    ZT_ASSERT(hif_node->ops->hif_write != NULL);

    ret = hif_node->ops->hif_read(node, 0, addr, (zt_s8 *)&value, 1);
    if (err)
    {
        *err = ret;
    }
    return value;
}

zt_ptr hif_io_read16(void *node, zt_u32 addr, zt_s32 *err)
{
    struct hif_node_ *hif_node = node;
    zt_u16 value;
    zt_s32 ret = 0;

    ZT_ASSERT(hif_node != NULL);
    ZT_ASSERT(hif_node->ops != NULL);
    ZT_ASSERT(hif_node->ops->hif_write != NULL);

    ret = hif_node->ops->hif_read(node, 0, addr, (zt_s8 *)&value, 2);
    if (err)
    {
        *err = ret;
    }
    return value;
}

zt_u32 hif_io_read32(void *node, zt_u32 addr, zt_s32 *err)
{
    struct hif_node_ *hif_node = node;
    zt_u32 value;
    zt_s32 ret = 0;

    ZT_ASSERT(hif_node != NULL);
    ZT_ASSERT(hif_node->ops != NULL);
    ZT_ASSERT(hif_node->ops->hif_write != NULL);

    ret = hif_node->ops->hif_read(node, 0, addr, (zt_s8 *)&value, 4);
    if (err)
    {
        *err = ret;
    }

    return value;
}

zt_s32 hif_io_write8(void *node, zt_u32 addr, zt_u8 value)
{
    struct hif_node_ *hif_node = node;

    ZT_ASSERT(hif_node != NULL);
    ZT_ASSERT(hif_node->ops != NULL);
    ZT_ASSERT(hif_node->ops->hif_write != NULL);

    return hif_node->ops->hif_write(node, 0, addr, (zt_s8 *)&value, 1);
}

zt_s32 hif_io_write16(void *node, zt_u32 addr, zt_ptr value)
{
    struct hif_node_ *hif_node = node;

    ZT_ASSERT(hif_node != NULL);
    ZT_ASSERT(hif_node->ops != NULL);
    ZT_ASSERT(hif_node->ops->hif_write != NULL);

    return hif_node->ops->hif_write(node, 0, addr, (zt_s8 *)&value, 2);
}

zt_s32 hif_io_write32(void *node, zt_u32 addr, zt_u32 value)
{
    struct hif_node_ *hif_node = node;

    ZT_ASSERT(hif_node != NULL);
    ZT_ASSERT(hif_node->ops != NULL);
    ZT_ASSERT(hif_node->ops->hif_write != NULL);

    return hif_node->ops->hif_write(node, 0, addr, (zt_s8 *)&value, 4);
}

