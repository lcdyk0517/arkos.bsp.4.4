/*
 * iw_func.h
 *
 * used for wext framework interface
 *
 * Author: houchuang
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
#ifndef __IW_FUNC_H__
#define __IW_FUNC_H__

zt_s32 zt_iw_getName(struct net_device *ndev, struct iw_request_info *info,
                     union iwreq_data *wrqu, char *extra);
zt_s32 zt_iw_setFrequency(struct net_device *ndev, struct iw_request_info *info,
                          union iwreq_data *wrqu, char *extra);
zt_s32 zt_iw_getFrequency(struct net_device *ndev, struct iw_request_info *info,
                          union iwreq_data *wrqu, char *extra);
zt_s32 zt_iw_setOperationMode(struct net_device *ndev,
                              struct iw_request_info *info, union iwreq_data *wrqu, char *extra);
zt_s32 zt_iw_getOperationMode(struct net_device *ndev,
                              struct iw_request_info *info, union iwreq_data *wrqu, char *extra);
zt_s32 zt_iw_getSensitivity(struct net_device *ndev,
                            struct iw_request_info *info,
                            union iwreq_data *wrqu, char *extra);
zt_s32 zt_iw_getRange(struct net_device *ndev, struct iw_request_info *info,
                      union iwreq_data *wrqu, char *extra);
zt_s32 zt_iw_setPriv(struct net_device *ndev, struct iw_request_info *info,
                     union iwreq_data *wrqu, char *extra);
zt_s32 zt_iw_getWirelessStats(struct net_device *ndev,
                              struct iw_request_info *info, union iwreq_data *wrqu, char *extra);
zt_s32 zt_iw_setWap(struct net_device *ndev, struct iw_request_info *info,
                    union iwreq_data *wrqu, char *extra);
zt_s32 zt_iw_getWap(struct net_device *ndev, struct iw_request_info *info,
                    union iwreq_data *wrqu, char *extra);
zt_s32 zt_iw_setMlme(struct net_device *ndev, struct iw_request_info *info,
                     union iwreq_data *wrqu, char *extra);
zt_s32 zt_iw_setScan(struct net_device *ndev, struct iw_request_info *info,
                     union iwreq_data *wrqu, char *extra);
zt_s32 zt_iw_getScan(struct net_device *ndev, struct iw_request_info *info,
                     union iwreq_data *wrqu, char *extra);
zt_s32 zt_iw_setEssid(struct net_device *ndev, struct iw_request_info *info,
                      union iwreq_data *wrqu, char *extra);
zt_s32 zt_iw_getEssid(struct net_device *ndev, struct iw_request_info *info,
                      union iwreq_data *wrqu, char *extra);
zt_s32 zt_iw_getNick(struct net_device *ndev, struct iw_request_info *info,
                     union iwreq_data *wrqu, char *extra);
zt_s32 zt_iw_setRate(struct net_device *ndev, struct iw_request_info *info,
                     union iwreq_data *wrqu, char *extra);
zt_s32 zt_iw_getRate(struct net_device *ndev, struct iw_request_info *info,
                     union iwreq_data *wrqu, char *extra);
zt_s32 zt_iw_setRts(struct net_device *ndev, struct iw_request_info *info,
                    union iwreq_data *wrqu, char *extra);
zt_s32 zt_iw_getRts(struct net_device *ndev, struct iw_request_info *info,
                    union iwreq_data *wrqu, char *extra);
zt_s32 zt_iw_setFragmentation(struct net_device *ndev,
                              struct iw_request_info *info, union iwreq_data *wrqu, char *extra);
zt_s32 zt_iw_getFragmentation(struct net_device *ndev,
                              struct iw_request_info *info, union iwreq_data *wrqu, char *extra);
zt_s32 zt_iw_getRetry(struct net_device *ndev, struct iw_request_info *info,
                      union iwreq_data *wrqu, char *extra);
zt_s32 zt_iw_setEnc(struct net_device *ndev, struct iw_request_info *info,
                    union iwreq_data *wrqu, char *extra);
zt_s32 zt_iw_getEnc(struct net_device *ndev, struct iw_request_info *info,
                    union iwreq_data *wrqu, char *extra);
zt_s32 zt_iw_getPower(struct net_device *ndev, struct iw_request_info *info,
                      union iwreq_data *wrqu, char *extra);
zt_s32 zt_iw_setGenIe(struct net_device *ndev, struct iw_request_info *info,
                      union iwreq_data *wrqu, char *extra);
zt_s32 zt_iw_setAuth(struct net_device *ndev, struct iw_request_info *info,
                     union iwreq_data *wrqu, char *extra);
zt_s32 zt_iw_getAuth(struct net_device *ndev, struct iw_request_info *info,
                     union iwreq_data *wrqu, char *extra);
zt_s32 zt_iw_setEncExt(struct net_device *ndev, struct iw_request_info *info,
                       union iwreq_data *wrqu, char *extra);
zt_s32 zt_iw_getEncExt(struct net_device *ndev, struct iw_request_info *info,
                       union iwreq_data *wrqu, char *extra);
zt_s32 zt_iw_setPmkid(struct net_device *ndev, struct iw_request_info *info,
                      union iwreq_data *wrqu, char *extra);



#endif
