/*
 * zt_os_api_workqueue.h
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
#ifndef __ZT_OS_API_WORKQUEUE_H__
#define __ZT_OS_API_WORKQUEUE_H__

typedef struct zt_workqueue_ops_st_
{
    zt_s32(*workqueue_init)(zt_workqueue_mgnt_st *wq, void *param);
    zt_s32(*workqueue_term)(zt_workqueue_mgnt_st *wq);
    zt_s32(*workqueue_work)(zt_workqueue_mgnt_st *wq);
} zt_workqueue_ops_st;

struct zt_workqueue_mgnt_st_
{
    zt_work_struct work;
    zt_workqueue_struct *workqueue;
    zt_workqueue_func_param_st *param;
    zt_workqueue_ops_st *ops;
};


void zt_os_api_workqueue_register(zt_workqueue_mgnt_st *wq, void *param);

#endif // !__ZT_OS_API_WORKQUEUE_H__

