/*
 * zt_msg.c
 *
 * used for implimention the basci operation interface of the message queue
 *
 * Author: luozhi
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
/* include */

#include "common.h"

/* macro */
#define MSG_DBG(fmt, ...)       LOG_D("[%s:%d]"fmt, __func__, __LINE__, ##__VA_ARGS__)
#define MSG_ARRAY(data, len)    zt_log_array(data, len)
#define MSG_WARN(fmt, ...)      LOG_E("[%s:%d]"fmt, __func__, __LINE__, ##__VA_ARGS__)
#define MSG_INFO(fmt, ...)      LOG_I("[%s:%d]"fmt, __func__, __LINE__, ##__VA_ARGS__)

/* type */

/* function  */

zt_s32 _msg_pop(zt_msg_t *pmsg)
{
    if (pmsg->pque)
    {
        if (zt_deque(&pmsg->list, pmsg->pque) == NULL)
        {
            MSG_WARN("deque fail !!!!!!!!");
            return -1;
        }
        pmsg->pque = NULL;
    }

    return 0;
}

zt_s32 _msg_push(zt_que_t *pque, zt_que_list_t *pos, zt_msg_t *pmsg)
{
    _msg_pop(pmsg); /* todo: check and leave from orignal queue(free or pend) */
    pmsg->pque = pque;
    zt_enque(&pmsg->list, pos, pque);

    return 0;
}

zt_s32 zt_msg_new(zt_msg_que_t *pmsg_que, zt_msg_tag_t tag, zt_msg_t **pnew_msg)
{
    zt_que_list_t *plist;
    zt_s32 rst;

    if (pmsg_que == NULL || pnew_msg == NULL)
    {
        return -1;
    }

    zt_os_api_lock_lock(&pmsg_que->lock);
    if (zt_que_is_empty(&pmsg_que->free))
    {
        rst = -2;
        goto exit;
    }
    zt_list_for_each(plist, zt_que_list_head(&pmsg_que->free))
    {
        zt_msg_t *pmsg = zt_list_entry(plist, zt_msg_t, list);
        if (pmsg->tag == tag)
        {
            rst = _msg_pop(pmsg);
            *pnew_msg = pmsg;
            goto exit;
        }
    }
    rst = -3;

exit:
    zt_os_api_lock_unlock(&pmsg_que->lock);
    if (rst)
    {
        *pnew_msg = NULL;
    }
    return rst;
}

zt_s32 zt_msg_push(zt_msg_que_t *pmsg_que, zt_msg_t *pmsg)
{
    zt_que_list_t *pos;
    zt_s32 rst;

    if (pmsg_que == NULL || pmsg == NULL)
    {
        return -1;
    }

    zt_os_api_lock_lock(&pmsg_que->lock);
    zt_list_for_each_prev(pos, zt_que_list_head(&pmsg_que->pend))
    {
        zt_msg_t *pmsg_tmp = zt_list_entry(pos, zt_msg_t, list);
        /* todo: compare use domain and priority field constitute value
        in message betwen pend queue and input specified message. */
        if ((pmsg->tag     | ZT_MSG_TAG_ID_MSK) >=
                (pmsg_tmp->tag | ZT_MSG_TAG_ID_MSK))
        {
            break;
        }
    }
    rst = _msg_push(&pmsg_que->pend, pos, pmsg);
    zt_os_api_lock_unlock(&pmsg_que->lock);

    return rst;
}

zt_s32 zt_msg_push_head(zt_msg_que_t *pmsg_que, zt_msg_t *pmsg)
{
    zt_que_list_t *pos;
    zt_s32 rst;

    if (pmsg_que == NULL || pmsg == NULL)
    {
        return -1;
    }

    zt_os_api_lock_lock(&pmsg_que->lock);
    zt_list_for_each(pos, zt_que_list_head(&pmsg_que->pend))
    {
        zt_msg_t *pmsg_tmp = zt_list_entry(pos, zt_msg_t, list);
        if (ZT_MSG_TAG_DOM(pmsg_tmp->tag) >= ZT_MSG_TAG_DOM(pmsg->tag))
        {
            break;
        }
    }
    rst = _msg_push(&pmsg_que->pend, zt_list_prev(pos), pmsg);
    zt_os_api_lock_unlock(&pmsg_que->lock);

    return rst;
}

zt_s32 msg_get(zt_msg_que_t *pmsg_que, zt_msg_t **pmsg,
               zt_bool bpop, zt_bool btail)
{
    zt_que_list_t *plist;
    zt_s32 rst = 0;

    if (pmsg_que == NULL || pmsg == NULL)
    {
        return -1;
    }

    zt_os_api_lock_lock(&pmsg_que->lock);
    if (zt_que_is_empty(&pmsg_que->pend))
    {
        rst = -2;
        goto exit;
    }

    plist = btail ? zt_que_tail(&pmsg_que->pend) : zt_que_head(&pmsg_que->pend);
    *pmsg = zt_list_entry(plist, zt_msg_t, list);
    if (bpop)
    {
        rst = _msg_pop(*pmsg);
    }

exit:
    zt_os_api_lock_unlock(&pmsg_que->lock);
    if (rst)
    {
        *pmsg = NULL;
    }
    return rst;
}

zt_s32 msg_get_dom(zt_msg_que_t *pmsg_que, zt_msg_tag_dom_t dom,
                   zt_msg_t **pmsg,
                   zt_bool bpop, zt_bool btail)
{
    zt_que_list_t *pos;
    zt_s32 rst;

    zt_os_api_lock_lock(&pmsg_que->lock);
    if (btail)
    {
        zt_list_for_each_prev(pos, zt_que_list_head(&pmsg_que->pend))
        {
            zt_msg_t *pmsg_tmp = zt_list_entry(pos, zt_msg_t, list);
            if (ZT_MSG_TAG_DOM(pmsg_tmp->tag) == dom)
            {
                rst = bpop ? _msg_pop(pmsg_tmp) : 0;
                *pmsg = pmsg_tmp;
                goto exit;
            }
        }
    }
    else
    {
        zt_list_for_each(pos, zt_que_list_head(&pmsg_que->pend))
        {
            zt_msg_t *pmsg_tmp = zt_list_entry(pos, zt_msg_t, list);
            if (ZT_MSG_TAG_DOM(pmsg_tmp->tag) == dom)
            {
                rst = bpop ? _msg_pop(pmsg_tmp) : 0;
                *pmsg = pmsg_tmp;
                goto exit;
            }
        }
    }
    rst = -1;

exit :
    zt_os_api_lock_unlock(&pmsg_que->lock);
    if (rst)
    {
        *pmsg = NULL;
    }
    return rst;
}

zt_s32 zt_msg_del(zt_msg_que_t *pmsg_que, zt_msg_t *pmsg)
{
    zt_que_list_t *pos;
    zt_s32 rst;

    if (pmsg_que == NULL || pmsg == NULL)
    {
        return -1;
    }

    zt_os_api_lock_lock(&pmsg_que->lock);
    pos = zt_list_prev(zt_que_list_head(&pmsg_que->free));
    rst = _msg_push(&pmsg_que->free, pos, pmsg);
    zt_os_api_lock_unlock(&pmsg_que->lock);

    return rst;
}
zt_s32 zt_msg_alloc(zt_msg_que_t *pmsg_que,
                    zt_msg_tag_t tag, zt_u32 size, zt_u8 num)
{
    zt_u8 i;
    for (i = 0; i < num; i++)
    {
        zt_msg_t *pmsg = zt_kzalloc(sizeof(zt_msg_t) + size);
        if (pmsg == NULL)
        {
            return -1;
        }
        pmsg->pque = NULL;
        pmsg->alloc_value_size = size;
        pmsg->tag = tag;
        pmsg->len = 0;
        zt_msg_del(pmsg_que, pmsg);
    }

    return 0;
}

zt_s32 zt_msg_free(zt_msg_que_t *pmsg_que)
{
    zt_list_t *pos, *pnext;

    zt_os_api_lock_lock(&pmsg_que->lock);
    zt_list_for_each_safe(pos, pnext, zt_que_list_head(&pmsg_que->pend))
    {
        zt_kfree(zt_list_entry(pos, zt_msg_t, list));
    }

    zt_list_for_each_safe(pos, pnext, zt_que_list_head(&pmsg_que->free))
    {
        zt_kfree(zt_list_entry(pos, zt_msg_t, list));
    }
    zt_os_api_lock_unlock(&pmsg_que->lock);

    return 0;
}

