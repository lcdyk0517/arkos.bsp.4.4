/*
 * hw_ctrl.h
 *
 * used for M0 init
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
#ifndef __MCU_CTRL_H__
#define __MCU_CTRL_H__


zt_s32 zt_hw_mcu_disable(hif_node_st *hif_node);
zt_s32 zt_hw_mcu_enable(hif_node_st *hif_node);
zt_s32 zt_hw_mcu_startup(hif_node_st *hif_node);


#endif
