/*
 * zt_que.c
 *
 * used for implimention the basci operation interface of the queue
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
/* include */
#include "common.h"

/* macro */
#define QUE_IS_EMPTY(pque)  zt_list_is_empty(zt_que_list_head(pque))
#define QUE_HEAD(pque)      zt_list_next(zt_que_list_head(pque))
#define QUE_TAIL(pque)      zt_list_prev(zt_que_list_head(pque))

/* type */

/* function  */
zt_u32 zt_que_count(zt_que_t *pque)
{
    zt_u32 cnt;

    zt_os_api_lock_lock(&pque->lock);
    cnt = pque->cnt;
    zt_os_api_lock_unlock(&pque->lock);

    return cnt;
}

zt_bool zt_que_is_empty(zt_que_t *pque)
{
    zt_bool is_empty;

    zt_os_api_lock_lock(&pque->lock);
    is_empty = QUE_IS_EMPTY(pque);
    zt_os_api_lock_unlock(&pque->lock);

    return is_empty;
}

zt_que_list_t *zt_que_head(zt_que_t *pque)
{
    zt_que_list_t *plist;

    zt_os_api_lock_lock(&pque->lock);
    plist = QUE_IS_EMPTY(pque) ? NULL : QUE_HEAD(pque);
    zt_os_api_lock_unlock(&pque->lock);

    return plist;
}

zt_que_list_t *zt_que_tail(zt_que_t *pque)
{
    zt_que_list_t *plist;

    zt_os_api_lock_lock(&pque->lock);
    plist = QUE_IS_EMPTY(pque) ? NULL : QUE_TAIL(pque);
    zt_os_api_lock_unlock(&pque->lock);

    return plist;
}

void zt_enque(zt_que_list_t *pnew, zt_que_list_t *pos, zt_que_t *pque)
{
    zt_os_api_lock_lock(&pque->lock);

    zt_list_insert_next(pnew, pos);
    pque->cnt++;

    zt_os_api_lock_unlock(&pque->lock);
}

void zt_enque_prev(zt_que_list_t *pnew, zt_que_list_t *pos, zt_que_t *pque)
{
    zt_os_api_lock_lock(&pque->lock);

    zt_list_insert_next(pnew, zt_list_prev(pos));
    pque->cnt++;

    zt_os_api_lock_unlock(&pque->lock);
}

zt_que_list_t *zt_deque(zt_que_list_t *pos, zt_que_t *pque)
{
    zt_que_list_t *plist;

    zt_os_api_lock_lock(&pque->lock);

    if (QUE_IS_EMPTY(pque))
    {
        plist = NULL;
    }
    else
    {
        zt_list_delete(pos);
        pque->cnt--;
        plist = pos;
    }

    zt_os_api_lock_unlock(&pque->lock);

    return plist;
}

zt_que_list_t *zt_deque_head(zt_que_t *pque)
{
    zt_que_list_t *plist;

    zt_os_api_lock_lock(&pque->lock);

    if (QUE_IS_EMPTY(pque))
    {
        plist = NULL;
    }
    else
    {
        zt_list_delete(plist = QUE_HEAD(pque));
        pque->cnt--;
    }

    zt_os_api_lock_unlock(&pque->lock);

    return plist;
}

zt_que_list_t *zt_deque_tail(zt_que_t *pque)
{
    zt_que_list_t *plist;

    zt_os_api_lock_lock(&pque->lock);

    if (QUE_IS_EMPTY(pque))
    {
        plist = NULL;
    }
    else
    {
        zt_list_delete(plist = QUE_TAIL(pque));
        pque->cnt--;
    }

    zt_os_api_lock_unlock(&pque->lock);

    return plist;
}

void zt_que_init(zt_que_t *pque, zt_os_api_lock_type_e lock_type)
{
    zt_list_init(&pque->head);
    zt_os_api_lock_init(&pque->lock, lock_type);
    pque->cnt = 0;
}


