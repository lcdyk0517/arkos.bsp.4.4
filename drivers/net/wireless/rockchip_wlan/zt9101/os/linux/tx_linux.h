/*
 * tx_linux.h
 *
 * used for frame rx handle for linux
 *
 * Author: renhaibo
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
#ifndef __TX_LINUX__
#define __TX_LINUX__

void tx_work_init(struct net_device *ndev);
void tx_work_term(struct net_device *ndev);
void tx_work_wake(struct net_device *ndev);
#ifdef CFG_ENABLE_AP_MODE
void tx_work_pause(struct net_device *ndev);
void tx_work_resume(struct net_device *ndev);
#endif

#endif
