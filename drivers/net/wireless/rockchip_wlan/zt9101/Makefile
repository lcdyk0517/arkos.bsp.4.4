# SPDX-License-Identifier: GPL-2.0
#
# ZTopInc ZT9101 USB WiFi driver (idVendor 0x350b, idProduct 0x9101 and the
# other ZT9101 ids in the driver table). Source: ZTop vendor driver
# ZT9101_android_V1.2.1800.20241115, built in-tree as CONFIG_ZT9101.
#
# Configuration below matches the out-of-tree build tested on 4.4.189 aarch64.

# RK3326 / little-endian ARM64 (vendor platform "r36s" define)
ccflags-y += -DCONFIG_LITTLE_ENDIAN

CONFIG_DRIVER_VER         = V1.2.xxx.xxxxxxxx
CONFIG_DEBUG_LEVEL        = 3
CONFIG_MODULE_IMPORT_NS   = n
CONFIG_FW_FILE            = y
CONFIG_STA_AND_AP_MODE    = n
CONFIG_TX_SOFT_AGG        = n
CONFIG_RX_SOFT_AGG        = n
CONFIG_WIFI_MODE          = all
CONFIG_WIFI_FRAMEWORK     = nl80211
CONFIG_HIF_PORT           = usb
CONFIG_CLOCK_24MHZ        = n
CONFIG_ZT9101XV20_SUPPORT = y
CONFIG_ZT9101XV30_SUPPORT = y
CONFIG_POWER_SAVING       = n
CONFIG_NAME               = zt9101_ztopmac

ifeq ($(CONFIG_HIF_PORT), usb)
MODULE_NAME := $(CONFIG_NAME)_usb
else ifeq ($(CONFIG_HIF_PORT), sdio)
MODULE_NAME := $(CONFIG_NAME)_sdio
else
MODULE_NAME := $(CONFIG_NAME)
endif

ccflags-y += -D'ZT_VERSION="$(CONFIG_DRIVER_VER)"'

ccflags-y += -DZT_DEBUG_LEVEL=$(CONFIG_DEBUG_LEVEL)

ifeq ($(CONFIG_MODULE_IMPORT_NS), y)
ccflags-y += -DCONFIG_MODULE_IMPORT_NS
endif

ifeq ($(CONFIG_FW_FILE), y)
ccflags-y += -DCONFIG_FW_FILE
endif

ifeq ($(CONFIG_ZT9101XV20_SUPPORT), y)
ccflags-y += -DCONFIG_ZT9101XV20_SUPPORT
endif

ifeq ($(CONFIG_ZT9101XV30_SUPPORT), y)
ccflags-y += -DCONFIG_ZT9101XV30_SUPPORT
endif

ccflags-y += -O1
ccflags-y += -I$(src)/os
ccflags-y += -I$(src)/os/linux
ccflags-y += -I$(src)/os/linux/os_porting
ccflags-y += -I$(src)/os/linux/hif
ccflags-y += -I$(src)/os/linux/netdev
ccflags-y += -I$(src)/os/linux/cfg80211
ccflags-y += -I$(src)/os/linux/iw
ccflags-y += -I$(src)/os/linux/pwr
ccflags-y += -I$(src)/nic
ccflags-y += -I$(src)/nic/utility
ccflags-y += -I$(src)/nic/p2p

ifeq ($(CONFIG_DEBUG_LEVEL), 0)
ccflags-y += -Wall
ccflags-y += -Wextra
ccflags-y += -Wno-unused-parameter
ccflags-y += -Wno-implicit-fallthrough
ccflags-y += -Wno-sign-compare
ccflags-y += -Wno-missing-field-initializers
ccflags-y += -Wno-address-of-packed-member
ccflags-y += -Wno-uninitialized
ccflags-y += -Wno-type-limits
ccflags-y += -Wno-empty-body
ccflags-y += -w
else
ccflags-y += -Wno-all
ccflags-y += -Wno-extra
ccflags-y += -w
endif

ifeq ($(CONFIG_TX_SOFT_AGG), y)
ccflags-y += -DCONFIG_SOFT_TX_AGGREGATION
endif

ifeq ($(CONFIG_RX_SOFT_AGG), y)
ccflags-y += -DCONFIG_SOFT_RX_AGGREGATION
endif

ifeq ($(CONFIG_HIF_PORT), usb)
ccflags-y += -DCONFIG_USB_FLAG
else ifeq ($(CONFIG_HIF_PORT), sdio)
ccflags-y += -DCONFIG_SDIO_FLAG
else
ccflags-y += -DCONFIG_BOTH_FLAG
endif

ifeq ($(CONFIG_CLOCK_24MHZ), y)
ccflags-y += -DCONFIG_CLOCK_24MHZ
endif

ifeq ($(CONFIG_WIFI_MODE), ap)
ccflags-y += -DCFG_ENABLE_AP_MODE
ccflags-y += -DZT_WEP_AUTH_ENABLE_WEXT
endif

ifeq ($(CONFIG_WIFI_MODE), monitor)
ccflags-y += -DCFG_ENABLE_MONITOR_MODE
endif

ifeq ($(CONFIG_WIFI_MODE), adhoc)
ccflags-y += -DCFG_ENABLE_ADHOC_MODE
endif

ifeq ($(CONFIG_WIFI_MODE), all)
ccflags-y += -DCFG_ENABLE_AP_MODE
ccflags-y += -DCFG_ENABLE_ADHOC_MODE
ccflags-y += -DCFG_ENABLE_MONITOR_MODE
ccflags-y += -DZT_WEP_AUTH_ENABLE_WEXT
endif

ifeq ($(CONFIG_WIFI_FRAMEWORK), nl80211)
ccflags-y += -DCONFIG_IOCTL_CFG80211 -DZT_USE_CFG80211_STA_EVENT
else ifeq ($(CONFIG_WIFI_FRAMEWORK), mp)
ccflags-y += -DCONFIG_MP_MODE
endif

ifeq ($(CONFIG_STA_AND_AP_MODE), y)
ccflags-y += -DCONFIG_STA_AND_AP_MODE
endif

ifeq ($(CONFIG_POWER_SAVING), y)
ccflags-y += -DCONFIG_LPS
endif

obj-$(CONFIG_ZT9101) := $(MODULE_NAME).o

ifeq ($(CONFIG_HIF_PORT), usb)
$(MODULE_NAME)-y += os/linux/hif/usb.o
else ifeq ($(CONFIG_HIF_PORT), sdio)
$(MODULE_NAME)-y += os/linux/hif/sdio.o
else
$(MODULE_NAME)-y += os/linux/hif/usb.o
$(MODULE_NAME)-y += os/linux/hif/sdio.o
endif

$(MODULE_NAME)-y += os/linux/os_porting/zt_os_api_mix.o
$(MODULE_NAME)-y += os/linux/os_porting/zt_os_api_lock.o
$(MODULE_NAME)-y += os/linux/os_porting/zt_os_api_sema.o
$(MODULE_NAME)-y += os/linux/os_porting/zt_os_api_thread.o
$(MODULE_NAME)-y += os/linux/os_porting/zt_os_api_timer.o
ifeq ($(CONFIG_FW_FILE), y)
$(MODULE_NAME)-y += os/linux/os_porting/zt_os_api_file.o
endif
$(MODULE_NAME)-y += os/linux/os_porting/zt_os_api_workqueue.o
$(MODULE_NAME)-y += os/linux/hif/hif.o
$(MODULE_NAME)-y += os/linux/hif/hif_io.o
$(MODULE_NAME)-y += os/linux/hif/cfg_parse.o
$(MODULE_NAME)-y += os/linux/hif/hw_ctrl.o
$(MODULE_NAME)-y += os/linux/hif/fw_download.o
$(MODULE_NAME)-y += os/linux/hif/hif_queue.o
$(MODULE_NAME)-y += os/linux/netdev/ndev_linux.o
$(MODULE_NAME)-y += os/linux/iw/iw.o
$(MODULE_NAME)-y += os/linux/proc.o
$(MODULE_NAME)-y += os/linux/iw/iw_func.o

ifeq ($(CONFIG_WIFI_FRAMEWORK), nl80211)
$(MODULE_NAME)-y += os/linux/cfg80211/zt_cfg80211.o
else ifeq ($(CONFIG_WIFI_FRAMEWORK), mp)
$(MODULE_NAME)-y += os/linux/proc_trx.o
endif

$(MODULE_NAME)-y += os/android/android_priv_cmd.o
$(MODULE_NAME)-y += os/linux/rx_linux.o
$(MODULE_NAME)-y += os/linux/tx_linux.o
$(MODULE_NAME)-y += nic/p2p/p2p.o
$(MODULE_NAME)-y += nic/p2p/p2p_frame_mgt.o
$(MODULE_NAME)-y += nic/p2p/p2p_proto_mgt.o
$(MODULE_NAME)-y += nic/p2p/p2p_wfd.o
$(MODULE_NAME)-y += nic/p2p/p2p_wowlan.o
$(MODULE_NAME)-y += nic/nic.o
$(MODULE_NAME)-y += nic/nic_io.o
$(MODULE_NAME)-y += nic/rx.o
$(MODULE_NAME)-y += nic/tx.o
$(MODULE_NAME)-y += nic/sec.o
$(MODULE_NAME)-y += nic/sec_arc4.o
$(MODULE_NAME)-y += nic/sec_tkip.o
$(MODULE_NAME)-y += nic/sec_wep.o
$(MODULE_NAME)-y += nic/mlme.o
$(MODULE_NAME)-y += nic/wdn.o
$(MODULE_NAME)-y += nic/scan.o
$(MODULE_NAME)-y += nic/auth.o
$(MODULE_NAME)-y += nic/assoc.o
$(MODULE_NAME)-y += nic/action.o
$(MODULE_NAME)-y += nic/wlan_mgmt.o
$(MODULE_NAME)-y += nic/zt_80211.o
$(MODULE_NAME)-y += nic/ie.o
$(MODULE_NAME)-y += nic/local_config.o
$(MODULE_NAME)-y += nic/hw_info.o
$(MODULE_NAME)-y += nic/mcu_cmd.o
$(MODULE_NAME)-y += nic/lps.o
$(MODULE_NAME)-y += nic/efuse.o
$(MODULE_NAME)-y += nic/utility/zt_que.o
$(MODULE_NAME)-y += nic/utility/zt_msg.o
$(MODULE_NAME)-y += nic/utility/zt_timer.o
$(MODULE_NAME)-y += nic/utility/zt_mix.o

ifeq ($(CONFIG_WIFI_MODE), adhoc)
$(MODULE_NAME)-y += nic/adhoc.o
endif

ifeq ($(CONFIG_WIFI_MODE), ap)
$(MODULE_NAME)-y += nic/ap.o
endif

ifeq ($(CONFIG_WIFI_MODE), all)
$(MODULE_NAME)-y += nic/ap.o
$(MODULE_NAME)-y += nic/adhoc.o
endif

$(MODULE_NAME)-y += os/linux/pwr/power_hw_rich200.o
