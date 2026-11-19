/*
 * zt_os_api_lock.c
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
#include "zt_typedef.h"
#include "zt_os_api.h"

/* macro */

/* type */

/* function declaration */

zt_inline static void lock_spin_lock(zt_lock_spin *plock)
{
    spin_lock(plock);
}

zt_inline static void lock_spin_unlock(zt_lock_spin *plock)
{
    spin_unlock(plock);
}

zt_inline static void lock_spin_init(zt_lock_spin *plock)
{
    spin_lock_init(plock);
}

zt_inline static void lock_spin_free(zt_lock_spin *plock) {}

zt_inline static void lock_bh_lock(zt_lock_spin *plock)
{
    spin_lock_bh(plock);
}

zt_inline static void lock_bh_unlock(zt_lock_spin *plock)
{
    spin_unlock_bh(plock);
}

zt_inline static void lock_irq_lock(zt_lock_spin *plock, zt_irq *pirqL)
{
    spin_lock_irqsave(plock, *pirqL);
}

zt_inline static void lock_irq_unlock(zt_lock_spin *plock, zt_irq *pirqL)
{
    spin_unlock_irqrestore(plock, *pirqL);
}

zt_inline static void lock_mutex_lock(zt_lock_mutex_t *mtx)
{
    mutex_lock(mtx);
}

zt_inline static zt_s32 lock_mutex_try_lock(zt_lock_mutex_t *mtx)
{
    return mutex_trylock(mtx) ? 0 : -1;
}

zt_inline static void lock_mutex_unlock(zt_lock_mutex_t *mtx)
{
    mutex_unlock(mtx);
}

zt_inline static void lock_mutex_init(zt_lock_mutex_t *mtx)
{
    mutex_init(mtx);
}

void zt_os_api_lock_lock(zt_os_api_lock_t *plock)
{
    switch (plock->lock_type)
    {
        case ZT_LOCK_TYPE_MUTEX :
            lock_mutex_lock(&plock->lock_mutex);
            break;

        case ZT_LOCK_TYPE_BH :
            lock_bh_lock(&plock->lock_spin.lock);
            break;

        case ZT_LOCK_TYPE_SPIN :
            lock_spin_lock(&plock->lock_spin.lock);
            break;

        case ZT_LOCK_TYPE_IRQ :
            lock_irq_lock(&plock->lock_spin.lock, &plock->lock_spin.val_irq);
            break;

        case ZT_LOCK_TYPE_NONE :
        default :
            break;
    }
}

zt_s32 zt_os_api_lock_trylock(zt_os_api_lock_t *plock)
{
    switch (plock->lock_type)
    {
        case ZT_LOCK_TYPE_MUTEX :
            return lock_mutex_try_lock(&plock->lock_mutex);

        default :
            break;
    }

    return -2;
}

void zt_os_api_lock_unlock(zt_os_api_lock_t *plock)
{
    switch (plock->lock_type)
    {
        case ZT_LOCK_TYPE_MUTEX :
            lock_mutex_unlock(&plock->lock_mutex);
            break;

        case ZT_LOCK_TYPE_BH :
            lock_bh_unlock(&plock->lock_spin.lock);
            break;

        case ZT_LOCK_TYPE_SPIN :
            lock_spin_unlock(&plock->lock_spin.lock);
            break;

        case ZT_LOCK_TYPE_IRQ :
            lock_irq_unlock(&plock->lock_spin.lock, &plock->lock_spin.val_irq);
            break;

        case ZT_LOCK_TYPE_NONE :
        default :
            break;
    }
}

void zt_os_api_lock_init(zt_os_api_lock_t *plock,
                         zt_os_api_lock_type_e lock_type)
{
    switch (lock_type)
    {
        case ZT_LOCK_TYPE_MUTEX :
            lock_mutex_init(&plock->lock_mutex);
            break;

        case ZT_LOCK_TYPE_NONE :
        default :
            lock_spin_init(&plock->lock_spin.lock);
            break;
    }
    plock->lock_type = lock_type;
}

void zt_os_api_lock_term(zt_os_api_lock_t *plock)
{
    switch (plock->lock_type)
    {
        case ZT_LOCK_TYPE_MUTEX :
            mutex_destroy(&plock->lock_mutex);
            break;

        case ZT_LOCK_TYPE_NONE :
        default :
            break;
    }
}

