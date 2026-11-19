/*
 * fw_download.h
 *
 * used for fireware download after system power on
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
#ifndef __FW_DOWNLAOD_H__
#define __FW_DOWNLAOD_H__

#pragma pack(1)
typedef struct fw_file_header
{
    zt_u16 magic_number;
    zt_u16 interface_type;
    zt_u8  rom_type;
    zt_u8  firmware_num;
} fw_file_header_t;

typedef struct fw_header
{
    zt_u8 type;
    zt_u8 sub_type;
    zt_u32 version;
    zt_u32 length;
    zt_u32 offset;
} fw_header_t;
#pragma pack()

zt_s32 zt_fw_download(void *hif_node);

#endif

