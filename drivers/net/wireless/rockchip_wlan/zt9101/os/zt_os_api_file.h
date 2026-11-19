/*
 * zt_os_api_file.h
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
#ifndef __ZT_OS_API_FILE_H__
#define __ZT_OS_API_FILE_H__

zt_file *zt_os_api_file_open(const zt_s8 *path);
zt_s32     zt_os_api_file_read(zt_file *file, loff_t offset, zt_u8 *data,
                               zt_u32 size);
zt_s32     zt_os_api_file_readline(zt_file *file, loff_t *offset,
                                   zt_u8 *data, zt_u32 size);
size_t  zt_os_api_file_size(zt_file *file);
void    zt_os_api_file_close(zt_file *file);

zt_s32 zt_os_api_file_write(zt_file *file, loff_t offset, zt_u8 *data,
                            zt_u32 size);
char *zt_os_api_file_getfullpath(const char *filename);


#endif

