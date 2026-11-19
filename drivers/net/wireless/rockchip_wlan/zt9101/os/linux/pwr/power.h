/*
 * power.h
 *
 * used for power on chip
 *
 * Author: songqiang
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
#ifndef __POWER_H__
#define __POWER_H__



zt_s32 power_on(struct hif_node_ *node);
zt_s32 power_off(struct hif_node_ *node);
zt_s32 side_road_cfg(struct hif_node_ *node);
zt_s32 power_suspend(struct hif_node_ *node);
zt_s32 power_resume(struct hif_node_ *node);

#endif

