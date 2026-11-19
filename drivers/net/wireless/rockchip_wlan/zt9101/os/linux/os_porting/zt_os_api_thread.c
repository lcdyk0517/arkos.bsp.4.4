/*
 * zt_os_api_thread.c
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

void zt_os_api_thread_enter_hook(void *ptid)
{

}


void *zt_os_api_thread_create(void *tid, zt_s8 *name, void *func, void *param)
{
    struct task_struct *htask = NULL;

    htask = kthread_run(func, param, "%s", name);
    if (IS_ERR(htask))
    {
        return NULL;
    }

    return htask;
}

zt_inline zt_s32 zt_os_api_thread_wakeup(void *tid)
{
    struct task_struct *htask = (struct task_struct *)(tid);

    if (htask)
    {
        wake_up_process(htask);
    }

    return 0;
}

zt_s32 zt_os_api_thread_destory(void *tid)
{
    struct task_struct *htask = (struct task_struct *)(tid);
    if (htask)
    {
        //printk("zt_os_api_thread_destory - htask=%p",htask);
        kthread_stop(htask);
        htask = NULL;
    }
    else
    {
        return -1;
    }

    return 0;
}


zt_inline zt_bool zt_os_api_thread_wait_stop(void *tid)
{
    return kthread_should_stop() ? zt_true : zt_false;
}


zt_inline void zt_os_api_thread_exit(void *comp)
{
}

