/*
 * zt_timer.h
 *
 * This file contains all the prototypes for the zt_timer.c file
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
#ifndef __ZT_TIMER_H__
#define __ZT_TIMER_H__

/* include */

/* macro */

/* type */
typedef struct
{
    zt_u32 start;
    zt_u32 expire;
    zt_u32 interval;
} zt_timer_t;

/* function declaration */

/**
 * Set a timer.
 *
 * This function is used to set a timer for a time sometime in the
 * future. The function zt_timer_expired() will evaluate to true after
 * the timer has expired.
 *
 * \param t A pointer to the timer
 * \param interval The interval before the timer expires.
 *
 */
zt_inline static void zt_timer_set(zt_timer_t *ptimer, zt_u32 intv_ms)
{
    ptimer->start = (zt_u32)zt_os_api_timestamp();
    ptimer->interval = zt_os_api_msecs_to_timestamp(intv_ms);
    ptimer->expire = ptimer->start + ptimer->interval;
}

/**
 * Reset the timer with the same interval.
 *
 * This function resets the timer with the same interval that was
 * given to the zt_timer_set() function. The start point of the interval
 * is the exact time that the timer last expired. Therefore, this
 * function will cause the timer to be stable over time, unlike the
 * zt_timer_restart() function.
 *
 * \note Must not be executed before timer expired
 *
 * \param t A pointer to the timer.
 * \sa zt_timer_restart()
 */
zt_inline static void zt_timer_reset(zt_timer_t *ptimer)
{
    ptimer->start = ptimer->expire;
    ptimer->expire = ptimer->start + ptimer->interval;
}

/**
 * Restart the timer from the current point in time
 *
 * This function restarts a timer with the same interval that was
 * given to the zt_timer_set() function. The timer will start at the
 * current time.
 *
 * \note A periodic timer will drift if this function is used to reset
 * it. For preioric timers, use the zt_timer_reset() function instead.
 *
 * \param t A pointer to the timer.
 *
 * \sa zt_timer_reset()
 */
zt_inline static void zt_timer_restart(zt_timer_t *ptimer)
{
    ptimer->start = (zt_u32)zt_os_api_timestamp();
    ptimer->expire = ptimer->start + ptimer->interval;
}

/**
 * modefiy the timer interval
 *
 * This function modefiy the timer interval with start point the same as before
 * timestamp.
 *
 * \param t A pointer to the timer.
 * \param interval The interval before the timer expires.
 *
 * \sa zt_timer_reset()
 */
zt_inline static void zt_timer_mod(zt_timer_t *ptimer, zt_u32 intv_ms)
{
    ptimer->interval = zt_os_api_msecs_to_timestamp(intv_ms);
    ptimer->expire = ptimer->start + ptimer->interval;
}

/**
 * Check if a timer has expired.
 *
 * This function tests if a timer has expired and returns true or
 * false depending on its status.
 *
 * \param t A pointer to the timer
 *
 * \return Non-zero if the timer has expired, zero otherwise.
 *
 */
zt_inline static zt_bool zt_timer_expired(zt_timer_t *ptimer)
{
    return (zt_bool)((zt_s32)(ptimer->expire - zt_os_api_timestamp()) < 0);
}

/**
 * The time until the timer expires
 *
 * This function returns the time until the timer expires.
 *
 * \param t A pointer to the timer
 *
 * \return The time until the timer expires
 *
 */
zt_inline static zt_s32 zt_timer_remaining(zt_timer_t *ptimer)
{
    return (zt_u32)zt_os_api_timestamp() < ptimer->expire ?
           zt_os_api_timestamp_to_msecs(ptimer->expire - (zt_u32)zt_os_api_timestamp()) :
           0;
}

/**
 * The time that has elapsed since starting
 *
 * This function returns the time has elapsed since starting.
 *
 * \param t A pointer to the timer
 *
 * \return The time until the timer expires
 *
 */
zt_inline static zt_u32 zt_timer_elapsed(zt_timer_t *ptimer)
{
    return zt_os_api_timestamp_to_msecs((zt_u32)zt_os_api_timestamp() -
                                        ptimer->start);
}

zt_s32 zt_timer_init(void);
zt_s32 zt_timer_term(void);

#endif

