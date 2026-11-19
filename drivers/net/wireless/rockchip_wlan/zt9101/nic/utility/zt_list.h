/*
 * zt_list.h
 *
 * used for Implement the basic operation interface of the linked list
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
#ifndef __ZT_LIST_H__
#define __ZT_LIST_H__

/* include */

/* macro */
#define zt_list_entry(ptr, type, field)     ZT_CONTAINER_OF(ptr, type, field)
#define zt_list_for_each(pos, head) \
    for (pos = (head)->pnext; pos != (head); pos = pos->pnext)
#define zt_list_for_each_prev(pos, head) \
    for (pos = (head)->prev; pos != (head); pos = pos->prev)
#define zt_list_for_each_safe(pos, n, head) \
    for (pos = (head)->pnext, n = pos->pnext; pos != (head); \
            pos = n, n = pos->pnext)
#define zt_list_for_each_safe_prev(pos, p, head) \
    for (pos = (head)->prev, p = pos->prev; pos != (head); \
            pos = p, p = pos->prev)

/* type */
typedef struct ZT_LIST
{
    struct ZT_LIST *pnext, *prev;
} zt_list_t;

/* function declaration */
zt_inline static zt_list_t *zt_list_next(zt_list_t *pos)
{
    return pos->pnext;
}

zt_inline static zt_list_t *zt_list_prev(zt_list_t *pos)
{
    return pos->prev;
}

zt_inline static zt_bool zt_list_is_empty(zt_list_t *phead)
{
    return (zt_bool)(zt_list_next(phead) == phead);
}

zt_inline static void zt_list_insert(zt_list_t *pnew, zt_list_t *prev,
                                     zt_list_t *pnext)
{
    pnext->prev = pnew;
    pnew->pnext = pnext;
    pnew->prev  = prev;
    prev->pnext = pnew;
}

zt_inline static void zt_list_insert_next(zt_list_t *pnew, zt_list_t *pos)
{
    zt_list_insert(pnew, pos, zt_list_next(pos));
}

zt_inline static void zt_list_insert_prev(zt_list_t *pnew, zt_list_t *pos)
{
    zt_list_insert(pnew, zt_list_prev(pos), pos);
}

zt_inline static void zt_list_insert_head(zt_list_t *pnew, zt_list_t *phead)
{
    zt_list_insert_next(pnew, phead);
}

zt_inline static void zt_list_insert_tail(zt_list_t *pnew, zt_list_t *phead)
{
    zt_list_insert_prev(pnew, phead);
}

zt_inline static void zt_list_del(zt_list_t *prev, zt_list_t *pnext)
{
    pnext->prev = prev;
    prev->pnext = pnext;
}

zt_inline static void zt_list_init(zt_list_t *plist)
{
    plist->pnext = plist->prev = plist;
}

zt_inline static void zt_list_delete(zt_list_t *pos)
{
    zt_list_del(zt_list_prev(pos), zt_list_next(pos));
    zt_list_init(pos);
}

#endif /* END OF __ZT_LIST_H__ */

