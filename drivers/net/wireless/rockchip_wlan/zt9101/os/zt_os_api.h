/*
 * zt_os_api.h
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
#ifndef __ZT_OS_API_H__
#define __ZT_OS_API_H__

#ifndef __func__
#define __func__  __FUNCTION__
#endif

#include "zt_typedef.h"

/* LINUX Porting API */
#if defined(__linux__)
#include <linux/version.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/utsname.h>
#include <linux/vmalloc.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/fs_struct.h>
#include <linux/fdtable.h>
#include <linux/uaccess.h>
#include <linux/sem.h>
#include <linux/sched.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/wireless.h>
#include <net/iw_handler.h>
#include <linux/if_arp.h>
#include <linux/rtnetlink.h>
#include <linux/interrupt.h>
#include <linux/ip.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/workqueue.h>
#include <linux/timer.h>
#include "tx_linux.h"
#endif

#include "os_priv.h"
#include "zt_debug.h"

/* all os use */
#define SIZE_PTR   SIZE_T
#define SSIZE_PTR  SSIZE_T

/**
 * mix
 */
#include "zt_os_api_mix.h"

/**
 * thread
 */
#include "zt_os_api_thread.h"

/**
 * workqueue
 */
#include "zt_os_api_workqueue.h"

/**
 * lock
 */
#include "zt_os_api_lock.h"

/**
 * semaphone
 */
#include "zt_os_api_sema.h"
/**
 * timer
 */
#include "zt_os_api_timer.h"

/**
 * file
 */
#include "zt_os_api_file.h"

#endif  /* __ZT_OS_API_H__ */

