/*
 * zt_mix.c
 *
 * used for implimention the basci operation interface of the software timer
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
#include "zt_typedef.h"

/* macro */

/* type */

/* function declaration */

int zt_isspace(int x)
{
    if (x == ' ' || x == '\t' || x == '\n' || x == '\f' || x == '\b' || x == '\r')
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

int zt_isdigit(int x)
{
    if (x <= '9' && x >= '0')
    {
        return 1;
    }
    else
    {
        return 0;
    }

}

int zt_atoi(const char *nptr)
{
    int c;              /* current char */
    int total;         /* current total */
    int sign;           /* if '-', then negative, otherwise positive */

    /* skip whitespace */
    while (zt_isspace((int)(unsigned char)*nptr))
    {
        ++nptr;
    }

    c = (int)(unsigned char) * nptr++;
    sign = c;           /* save sign indication */
    if (c == '-' || c == '+')
    {
        c = (int)(unsigned char) * nptr++;    /* skip sign */
    }

    total = 0;

    while (zt_isdigit(c))
    {
        total = 10 * total + (c - '0');     /* accumulate digit */
        c = (int)(unsigned char) * nptr++;  /* get next char */
    }

    if (sign == '-')
    {
        return -total;
    }
    else
    {
        return total;    /* return result, negated if necessary */
    }
}


/* --- HACK PARA FEDORA (WEXT BYPASS) --- */
struct iw_request_info;
struct iw_event;
char *iwe_stream_add_point(struct iw_request_info *info, char *stream, char *ends, struct iw_event *iwe, const char *extra) { return stream; }
char *iwe_stream_add_event(struct iw_request_info *info, char *stream, char *ends, struct iw_event *iwe, int event_len) { return stream; }
