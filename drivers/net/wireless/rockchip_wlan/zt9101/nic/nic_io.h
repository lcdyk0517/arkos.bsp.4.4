/*
 * nic_io.h
 *
 * used for nic io read or write
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
#ifndef __NIC_IO_H__
#define __NIC_IO_H__

typedef enum
{
    TYPE_CMD     = 0,
    TYPE_FW      = 1,
    TYPE_REG     = 2,
    TYPE_DATA    = 3
} tx_rx_desc_type_e;

#define ZT_N_BYTE_ALIGMENT(value, alignment)  ((alignment == 1) ? (value) : (((value + alignment - 1) / alignment) * alignment))

#define ZT_U32_BIT_LEN_MASK(bit_len)        ((zt_u32)(0xFFFFFFFF >> (32 - (bit_len))))
#define ZT_U16_BIT_LEN_MASK(bit_len)        ((zt_u16)(0xFFFF >> (16 - (bit_len))))
#define ZT_U8_BIT_LEN_MASK(bit_len)         ((zt_u8)(0xFF >> (8 - (bit_len))))

#define ZT_U32_BIT_OFFSET_LEN_MASK(bit_offset, bit_len)     ((zt_u32)(ZT_U32_BIT_LEN_MASK(bit_len) << (bit_offset)))
#define ZT_U16_BIT_OFFSET_LEN_MASK(bit_offset, bit_len)     ((zt_u16)(ZT_U16_BIT_LEN_MASK(bit_len) << (bit_offset)))
#define ZT_U8_BIT_OFFSET_LEN_MASK(bit_offset, bit_len)      ((zt_u8)(ZT_U8_BIT_LEN_MASK(bit_len) << (bit_offset)))

#define zt_le_u32_read(ptr)                 zt_le32_to_cpu(*((zt_u32 *)(ptr)))
#define zt_le_u16_read(ptr)                 zt_le16_to_cpu(*((zt_u16 *)(ptr)))
#define zt_le_u8_read(ptr)                  (*((zt_u8 *)(ptr)))

#define zt_be_u32_read(ptr)                 zt_be32_to_cpu(*((zt_u32 *)(ptr)))
#define zt_be_u16_read(ptr)                 zt_be16_to_cpu(*((zt_u16 *)(ptr)))
#define zt_be_u8_read(ptr)                  (*((zt_u8 *)(ptr)))

#define zt_le_u32_write(ptr, val)           (*((zt_u32 *)(ptr))) = zt_cpu_to_le32(val)
#define zt_le_u16_write(ptr, val)           (*((zt_u16 *)(ptr))) = zt_cpu_to_le16(val)
#define zt_le_u8_write(ptr, val)            (*((zt_u8 *)(ptr))) = ((zt_u8)(val))

#define zt_be_u32_write(ptr, val)           (*((zt_u32 *)(ptr))) = zt_cpu_to_be32(val)
#define zt_be_u16_write(ptr, val)           (*((zt_u16 *)(ptr))) = zt_cpu_to_be16(val)
#define zt_be_u8_write(ptr, val)            (*((zt_u8 *)(ptr))) = ((zt_u8)(val))

#define zt_le_u32_to_host_u32(pstart)       (zt_le32_to_cpu(*((zt_u32 *)(pstart))))
#define zt_le_u16_to_host_u16(pstart)       (zt_le16_to_cpu(*((zt_u16 *)(pstart))))
#define zt_le_u8_to_host_u8(pstart)         ((*((zt_u8 *)(pstart))))

#define zt_be_u32_to_host_u32(pstart)       (zt_be32_to_cpu(*((zt_u32 *)(pstart))))
#define zt_be_u16_to_host_u16(pstart)       (zt_be16_to_cpu(*((zt_u16 *)(pstart))))
#define zt_be_u8_to_host_u8(pstart)         ((*((zt_u8 *)(pstart))))

#define zt_le_bits_to_u32(pstart, bit_offset, bit_len) \
    ((zt_le_u32_to_host_u32(pstart) >> (bit_offset)) & ZT_U32_BIT_LEN_MASK(bit_len))

#define zt_le_bits_to_u16(pstart, bit_offset, bit_len) \
    ((zt_le_u16_to_host_u16(pstart) >> (bit_offset)) & ZT_U16_BIT_LEN_MASK(bit_len))

#define zt_le_bits_to_u8(pstart, bit_offset, bit_len) \
    ((zt_le_u8_to_host_u8(pstart) >> (bit_offset)) & ZT_U8_BIT_LEN_MASK(bit_len))

#define zt_be_bits_to_u32(pstart, bit_offset, bit_len) \
    ((zt_be_u32_to_host_u32(pstart) >> (bit_offset)) & ZT_U32_BIT_LEN_MASK(bit_len))

#define zt_be_bits_to_u16(pstart, bit_offset, bit_len) \
    ((zt_be_u16_to_host_u16(pstart) >> (bit_offset)) & ZT_U16_BIT_LEN_MASK(bit_len))

#define zt_be_bits_to_u8(pstart, bit_offset, bit_len) \
    ((zt_be_u8_to_host_u8(pstart) >> (bit_offset)) & ZT_U8_BIT_LEN_MASK(bit_len))

#define zt_le_bits_clear_to_u32(pstart, bit_offset, bit_len) \
    (zt_le_u32_to_host_u32(pstart) & (~ZT_U32_BIT_OFFSET_LEN_MASK(bit_offset, bit_len)))

#define zt_le_bits_clear_to_u16(pstart, bit_offset, bit_len) \
    (zt_le_u16_to_host_u16(pstart) & (~ZT_U16_BIT_OFFSET_LEN_MASK(bit_offset, bit_len)))

#define zt_le_bits_clear_to_u8(pstart, bit_offset, bit_len) \
    (zt_le_u8_to_host_u8(pstart) & ((zt_u8)(~ZT_U8_BIT_OFFSET_LEN_MASK(bit_offset, bit_len))))

#define zt_be_bits_clear_to_u32(pstart, bit_offset, bit_len) \
    (zt_be_u32_to_host_u32(pstart) & (~ZT_U32_BIT_OFFSET_LEN_MASK(bit_offset, bit_len)))

#define zt_be_bits_clear_to_u16(pstart, bit_offset, bit_len) \
    (zt_be_u16_to_host_u16(pstart) & (~ZT_U16_BIT_OFFSET_LEN_MASK(bit_offset, bit_len)))

#define zt_be_bits_clear_to_u8(pstart, bit_offset, bit_len) \
    (zt_be_u8_to_host_u8(pstart) & (~ZT_U8_BIT_OFFSET_LEN_MASK(bit_offset, bit_len)))

#define zt_set_bits_to_le_u32(pstart, bit_offset, bit_len, value) \
    do { \
        if (bit_offset == 0 && bit_len == 32) \
            zt_le_u32_write(pstart, value); \
        else { \
            zt_le_u32_write(pstart, \
                            zt_le_bits_clear_to_u32(pstart, bit_offset, bit_len) \
                            | \
                            ((((zt_u32)value) & ZT_U32_BIT_LEN_MASK(bit_len)) << (bit_offset)) \
                           ); \
        } \
    } while (0)

#define zt_set_bits_to_le_u16(pstart, bit_offset, bit_len, value) \
    do { \
        if (bit_offset == 0 && bit_len == 16) \
            zt_le_u16_write(pstart, value); \
        else { \
            zt_le_u16_write(pstart, \
                            zt_le_bits_clear_to_u16(pstart, bit_offset, bit_len) \
                            | \
                            ((((zt_u16)value) & ZT_U16_BIT_LEN_MASK(bit_len)) << (bit_offset)) \
                           ); \
        } \
    } while (0)

#define zt_set_bits_to_le_u8(pstart, bit_offset, bit_len, value) \
    do { \
        if (bit_offset == 0 && bit_len == 8) \
            zt_le_u8_write(pstart, value); \
        else { \
            zt_le_u8_write(pstart, \
                           zt_le_bits_clear_to_u8(pstart, bit_offset, bit_len) \
                           | \
                           ((((zt_u8)value) & ZT_U8_BIT_LEN_MASK(bit_len)) << (bit_offset)) \
                          ); \
        } \
    } while (0)

#define zt_set_bits_to_be_u32(pstart, bit_offset, bit_len, value) \
    do { \
        if (bit_offset == 0 && bit_len == 32) \
            zt_be_u32_write(pstart, value); \
        else { \
            zt_be_u32_write(pstart, \
                            zt_be_bits_clear_to_u32(pstart, bit_offset, bit_len) \
                            | \
                            ((((zt_u32)value) & ZT_U32_BIT_LEN_MASK(bit_len)) << (bit_offset)) \
                           ); \
        } \
    } while (0)

#define zt_set_bits_to_be_u16(pstart, bit_offset, bit_len, value) \
    do { \
        if (bit_offset == 0 && bit_len == 16) \
            zt_be_u16_write(pstart, value); \
        else { \
            zt_be_u16_write(pstart, \
                            zt_be_bits_clear_to_u16(pstart, bit_offset, bit_len) \
                            | \
                            ((((zt_u16)value) & ZT_U16_BIT_LEN_MASK(bit_len)) << (bit_offset)) \
                           ); \
        } \
    } while (0)

#define zt_set_bits_to_be_u8(pstart, bit_offset, bit_len, value) \
    do { \
        if (bit_offset == 0 && bit_len == 8) \
            zt_be_u8_write(pstart, value); \
        else { \
            zt_be_u8_write(pstart, \
                           zt_be_bits_clear_to_u8(pstart, bit_offset, bit_len) \
                           | \
                           ((((zt_u8)value) & ZT_U8_BIT_LEN_MASK(bit_len)) << (bit_offset)) \
                          ); \
        } \
    } while (0)

/* read reg */
zt_u8  zt_io_read8(const nic_info_st *nic_info, zt_u32 addr, zt_s32 *err);
zt_u16 zt_io_read16(const nic_info_st *nic_info, zt_u32 addr, zt_s32 *err);
zt_u32 zt_io_read32(const nic_info_st *nic_info, zt_u32 addr, zt_s32 *err);

/* write reg */
zt_s32 zt_io_write8(const nic_info_st *nic_info, zt_u32 addr, zt_u8 value);
zt_s32 zt_io_write16(const nic_info_st *nic_info, zt_u32 addr, zt_u16 value);
zt_s32 zt_io_write32(const nic_info_st *nic_info, zt_u32 addr, zt_u32 value);

/* send cmd */
zt_s32 zt_io_write_cmd_by_txd(nic_info_st *nic_info, zt_u32 cmd,
                              zt_u32 *send_buf, zt_u32 send_len,
                              zt_u32 *recv_buf, zt_u32 recv_len);

/* send data */
zt_s32 zt_io_write_data(const nic_info_st *nic_info, zt_u8 agg_num, zt_s8 *pbuf,
                        zt_u32 len, zt_u32 addr,
                        zt_s32(*callback_func)(void *tx_info, void *param), void *tx_info, void *param);
zt_s32 zt_io_write_data_queue_check(const nic_info_st *nic_info);
zt_s32 zt_io_tx_xmit_wake(const nic_info_st *nic_info);

#endif

