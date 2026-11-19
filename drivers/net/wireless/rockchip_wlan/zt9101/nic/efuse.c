/*
 * efuse.c
 *
 * used for read efuse value
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

#include "common.h"

#define BOOT_FROM_EEPROM        ZT_BIT(4)
#define EEPROMSEL               ZT_BIT(4)
#define EEPROM_EN               ZT_BIT(5)


/*************************************************
* Function     : zt_mcu_efuse_get
* Description  :
* Input        : 1. nic_info
                 2. efuse_code
                 3. outdata_len
* Output       : outdata
* Return       : ZT_RETURN_FAIL, ZT_RETURN_OK
*************************************************/
zt_s32 zt_mcu_efuse_get(nic_info_st *nic_info, EUSE_CODE efuse_code,
                        zt_u32 *outdata, zt_u32 outdata_len)
{
    zt_s32 ret = 0;
    zt_u32 efuse_num = efuse_code;
    zt_u8 *outbuff = NULL;
    outbuff = zt_kzalloc(MAILBOX_MAX_TXLEN * 4);
    if (outbuff == NULL)
    {
        LOG_E("alloc recv buff fail");
        return  ZT_RETURN_FAIL;
    }
    ret = mcu_cmd_communicate(nic_info, UMSG_OPS_MP_EFUSE_GET, &efuse_num, 1,
                              (zt_u32 *)outbuff, MAILBOX_MAX_TXLEN);

    zt_memcpy(outdata, &outbuff[1], outdata_len);

    if (ZT_RETURN_FAIL == ret)
    {
        LOG_E("[%s] UMSG_OPS_MP_EFUSE_GET failed", __func__);
        if (outbuff)
        {
            zt_kfree(outbuff);
        }
        return ret;
    }
    else if (ZT_RETURN_CMD_BUSY == ret)
    {
        LOG_W("[%s] cmd busy,try again if need!", __func__);
        if (outbuff)
        {
            zt_kfree(outbuff);
        }
        return ret;
    }
    if (outbuff)
    {
        zt_kfree(outbuff);
    }

    return ZT_RETURN_OK;
}

