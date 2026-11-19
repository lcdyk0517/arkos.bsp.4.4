/*
 * zt_que.h
 *
 * This file contains all the prototypes for the zt_que.c file
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
#ifndef __ZT_QUE_H__
#define __ZT_QUE_H__

/* include */

/* macro */

/* type */
typedef zt_list_t zt_que_list_t;
typedef struct zt_que_
{
    zt_que_list_t head;
    zt_os_api_lock_t lock;
    zt_u32 cnt;
} zt_que_t;

/* function declaration */
zt_u32 zt_que_count(zt_que_t *pque);

zt_inline static zt_que_list_t *zt_que_list_head(zt_que_t *pque)
{
    return &pque->head;
}

zt_bool zt_que_is_empty(zt_que_t *pque);

zt_que_list_t *zt_que_head(zt_que_t *pque);

zt_que_list_t *zt_que_tail(zt_que_t *pque);

void zt_enque(zt_que_list_t *pnew, zt_que_list_t *pos, zt_que_t *pque);

zt_inline static
void zt_enque_next(zt_que_list_t *pnew, zt_que_list_t *pos, zt_que_t *pque)
{
    zt_enque(pnew, pos, pque);
}

void zt_enque_prev(zt_que_list_t *pnew, zt_que_list_t *pos, zt_que_t *pque);

zt_inline static void zt_enque_head(zt_que_list_t *pnew, zt_que_t *pque)
{
    zt_enque_next(pnew, zt_que_list_head(pque), pque);
}

zt_inline static void zt_enque_tail(zt_que_list_t *pnew, zt_que_t *pque)
{
    zt_enque_prev(pnew, zt_que_list_head(pque), pque);
}

zt_que_list_t *zt_deque(zt_que_list_t *pos, zt_que_t *pque);

zt_que_list_t *zt_deque_head(zt_que_t *pque);

zt_que_list_t *zt_deque_tail(zt_que_t *pque);

void zt_que_init(zt_que_t *pque, zt_os_api_lock_type_e lock_type);

zt_inline static void zt_que_deinit(zt_que_t *pque)
{
    zt_os_api_lock_term(&pque->lock);
}

#endif /* __ZT_QUE_H__ */


