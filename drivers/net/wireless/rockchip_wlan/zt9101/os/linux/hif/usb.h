/*
 * usb.h
 *
 * used for .....
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
#ifndef __USB_H__
#define __USB_H__

#define RECV_BULK_IN_ADDR       0x80
#define RECV_INT_IN_ADDR        0x81


/* usb control transfer request type */
#define USB_REQUEST_TYPE_VENDOR_IN  (0xC0)
#define USB_REQUEST_TYPE_VENDOR_OUT (0x40)

/* usb control transfer request */
#define USB_REQUEST_GET_STATUS      (0)
#define USB_REQUEST_SET_ADDRESS     (5)


#define USB_CONTROL_MSG_TIMEOUT     (400)
#define USB_CONTROL_MSG_BF_LEN      (254+16)

#define MAX_IN_EP   2
#define MAX_OUT_EP  4
#define MAX_PRE_ALLOC_URB   4

/* develop period */
#define MAX_IN_EP_DEVELOP   1
#define MAX_OUT_EP_DEVELOP  2

#ifndef ZT_MIN
#define ZT_MIN(x,y) ( (x) < (y) ? x : y )
#endif

#ifndef ZT_BIT
#define ZT_BIT(x)  (1 << (x))
#endif

enum ZT_USB_OPERATION_FLAG
{
    ZT_USB_CTL_MSG      = 0,
    ZT_USB_BLK_ASYNC    = 1,
    ZT_USB_BLK_SYNC     = 2,
    ZT_USB_NET_PIP      = 3,
};
enum ZT_USB_SPEED
{
    ZT_USB_SPEED_UNKNOWN = 0,
    ZT_USB_SPEED_1_1 = 1,
    ZT_USB_SPEED_2 = 2,
    ZT_USB_SPEED_3 = 3,
};

typedef enum ZT_USB_STATUS
{
    STA_SUCCESS = 0,
    STA_FAIL = 1,
} USB_INIT_STATUS;

struct urb_struct
{
    zt_u8 used;
    struct urb *purb;
    zt_list_t list;
};

/*
xmit related
*/
struct tx_ctrl
{
    spinlock_t lock;
    struct urb_struct   urb_buf[MAX_PRE_ALLOC_URB];
    zt_list_t    free_urb_queue;
    zt_u8 free_urb_cnt;
    zt_u8 pipe_idx;
};

typedef struct hif_usb_management
{
    struct usb_interface *pusb_intf;
    struct usb_device *pusb_dev;
    zt_u8  n_in_pipes;
    zt_u8  n_out_pipes;
    zt_u8  n_interfaces;
    zt_u8  n_endpoints;

    zt_u8  intface_num;
    zt_u8  chip_type;

    zt_u8  in_endp_addr[MAX_IN_EP];  /* in endpoint address */
    zt_u8  out_endp_addr[MAX_OUT_EP]; /* out endpoint address */

    zt_u32 i_int_pipe;

    zt_u32 i_bulk_pipe[MAX_IN_EP];
    zt_u16 i_bulk_pipe_sz[MAX_IN_EP];

    zt_u32 i_ctrl_pipe;
    zt_u32 o_ctrl_pipe;

    zt_u32 o_bulk_pipe[MAX_OUT_EP];
    zt_u16 o_bulk_pipe_sz[MAX_OUT_EP];

    zt_u16 ep_num[MAX_IN_EP + MAX_OUT_EP];

    enum ZT_USB_SPEED usb_speed;
    zt_u8 usb_id;
    zt_u8 *ctrl_msg_buffer;    //[USB_CONTROL_MSG_BF_LEN];
    zt_u8 ctl_continue_io_error;
    zt_u8 blk_continue_io_error;
} hif_usb_mngt;


zt_s32 usb_init(void);
zt_s32 usb_exit(void);

#endif











