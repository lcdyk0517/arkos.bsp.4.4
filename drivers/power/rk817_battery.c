// SPDX-License-Identifier: GPL-2.0+
/*
 * rk817 battery  driver
 *
 * Copyright (C) 2018 Rockchip Electronics Co., Ltd.
 *
 */

#define pr_fmt(fmt) "rk817-bat: " fmt

#include <linux/delay.h>
#include <linux/extcon.h>
#include <linux/fb.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/iio/consumer.h>
#include <linux/iio/iio.h>
#include <linux/irq.h>
#include <linux/jiffies.h>
#include <linux/mfd/rk808.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/power/rk_usbbc.h>
#include <linux/regmap.h>
#include <linux/rk_keys.h>
#include <linux/rtc.h>
#include <linux/timer.h>
#include <linux/wakelock.h>
#include <linux/workqueue.h>
#include <linux/gpio/consumer.h>
#include <linux/uaccess.h>

/* Userland persistence path for learned battery calibration anchors */
#define HYBRID_CALIB_PATH	"/var/lib/rk817_battery_calib"
#define HYBRID_CALIB_LOAD_TRIES	36	/* ~3 min at 5s work period */

/* Hybrid mode (voltage + coulomb counting) constants */
#define HYBRID_V_FULL_CHG_DEFAULT	4100
#define HYBRID_V_FULL_DIS_DEFAULT	4000
#define HYBRID_V_EMPTY_DIS_DEFAULT	3300
#define HYBRID_V_EMPTY_CHG_DEFAULT	3350
#define HYBRID_PEAK_DWELL_SEC		(15 * 60)
#define HYBRID_PEAK_TRACK_START_MV	4000
#define HYBRID_PEAK_STABILITY_MV	30
#define HYBRID_UNPLUG_SETTLE_SEC	15
#define HYBRID_RESTORE_DELTA_CHG_MV	100
#define HYBRID_RESTORE_DELTA_DIS_MV	50
#define HYBRID_MIN_RANGE_MV		100
#define HYBRID_EMA_ALPHA_NUM		2
#define HYBRID_EMA_ALPHA_DEN		10

/* Full detection via small current at high voltage */
#define HYBRID_FULL_CURR_THRESH_MA	150	/* current below this = near full */
#define HYBRID_FULL_VOLT_THRESH_MV	4050	/* voltage must be above this */
#define HYBRID_FULL_TIMEOUT_SEC		(20 * 60)	/* 20 min timeout at 99% */

static int dbg_enable;
struct rk817_battery_device;  /* 先前置声明结构体标签 */

module_param_named(dbg_level, dbg_enable, int, 0644);

#define DBG(args...) \
	do { \
		if (dbg_enable) { \
			pr_info(args); \
		} \
	} while (0)

#define BAT_INFO(fmt, args...) pr_info(fmt, ##args)

#define HYBRID_DEBUG	0

#if HYBRID_DEBUG
#define HYBRID_DBG(fmt, args...) pr_info(fmt, ##args)
#else
#define HYBRID_DBG(fmt, args...) do {} while (0)
#endif

#define DRIVER_VERSION	"2.0"

#define DIV(x)	((x) ? (x) : 1)
#define ENABLE	0x01
#define DISABLE	0x00
#define MAX_INTERPOLATE		1000
/* Maximum Resolution Precision of Percentage: 0.001% */
#define MAX_PERCENTAGE		(100 * 1000)
#define MAX_INT			0x7FFF
#define OCV_SAMP_MIN_MSK	0x0c
#define OCV_SAMP_8MIN		(0x00 << 2)
#define MINUTE(x)	\
	((x) * 60)

#define ADC_TO_CURRENT(adc_value, samp_res)	\
	(adc_value * 1720 / 1000 / samp_res)
#define CURRENT_TO_ADC(current, samp_res)	\
	(current * 1000 * samp_res / 1720)

#define ADC_TO_CAPACITY(adc_value, samp_res)	\
	((adc_value) * 1720L / 3600 / 1000 / (samp_res))
#define CAPACITY_TO_ADC(capacity, samp_res)	\
	((capacity) * (samp_res) * 3600L / 1720 * 1000)

#define ADC_TO_CAPACITY_UAH(adc_value, samp_res)	\
	((adc_value) * 1720L / 3600 / (samp_res))
#define ADC_TO_CAPACITY_MAH(adc_value, samp_res)	\
	((adc_value) * 1720L / 3600 / 1000 / (samp_res))

/* Adjust full capacity to a reduced value */
#define UPDATE_REDUCE_FCC(fcc)		((fcc) * 995 / 1000)
/* Raise the maximum capacity value */
#define UPDATE_RAISE_FCC(fcc)		((fcc) * 1005 / 1000)
/* Effective full capacity */
#define EFFECTIVE_FULL_MIN_CAP(fcc)	((fcc) * 800 / 1000)
#define EFFECTIVE_FULL_MAX_CAP(fcc)	((fcc) * 1200 / 1000)
/* Battery Percentage (or State of Charge (SOC) Percentage) */
#define BATTERY_PERCENTAGE(n)		((n) * 1000)

#define ADC_CALIB_THRESHOLD		4
#define ADC_CALIB_LMT_MIN		3
#define ADC_CALIB_CNT			5

/* default param */
#define DEFAULT_BAT_RES			100
#define DEFAULT_SLP_ENTER_CUR		300
#define DEFAULT_SLP_EXIT_CUR		300
#define DEFAULT_SLP_FILTER_CUR		100
#define DEFAULT_PWROFF_VOL_THRESD	3300
#define DEFAULT_MONITOR_SEC		5
#define DEFAULT_SAMPLE_RES		10

/* sleep */

#define TIMER_MS_COUNTS			1000
/* fcc */
#define MIN_FCC				500
#define CAP_INVALID			0x80

/* virtual params */
#define VIRTUAL_CURRENT			1000
#define VIRTUAL_VOLTAGE			3888
#define VIRTUAL_SOC			66
#define VIRTUAL_TEMPERATURE		188
#define VIRTUAL_STATUS			POWER_SUPPLY_STATUS_CHARGING

#define FINISH_CHRG_CUR1		1000
#define FINISH_CHRG_CUR2		1500
#define FINISH_MAX_SOC_DELAY		20
/* Discharge Current Threshold */
#define FINISH_CURR_THRESD		(-30)
/* OCV Table Percentage Accuracy: 5.000% */
#define OCV_TABLE_STEP			5000

enum ts_fun {
	TS_FUN_SOURCE_CURRENT,
	TS_FUN_VOLTAGE_INPUT,
};

enum tscur_sel {
	FLOW_OUT_10uA,
	FLOW_OUT_20uA,
	FLOW_OUT_30uA,
	FLOW_OUT_40uA,
};

enum charge_current {
	CHRG_CUR_1000MA,
	CHRG_CUR_1500MA,
	CHRG_CUR_2000MA,
	CHRG_CUR_2500MA,
	CHRG_CUR_2750MA,
	CHRG_CUR_3000MA,
	CHRG_CUR_3500MA,
	CHRG_CUR_500MA,
};

enum charge_voltage {
	CHRG_VOL_4100MV,
	CHRG_VOL_4150MV,
	CHRG_VOL_4200MV,
	CHRG_VOL_4250MV,
	CHRG_VOL_4300MV,
	CHRG_VOL_4350MV,
	CHRG_VOL_4400MV,
	CHRG_VOL_4450MV,
};

enum charge_status {
	CHRG_OFF,
	DEAD_CHRG,
	TRICKLE_CHRG,
	CC_OR_CV_CHRG,
	CHARGE_FINISH,
	USB_OVER_VOL,
	BAT_TMP_ERR,
	BAT_TIM_ERR,
};

enum bat_mode {
	MODE_BATTARY = 0,
	MODE_VIRTUAL,
};

enum rk817_sample_time {
	S_8_MIN,
	S_16_MIN,
	S_32_MIN,
	S_48_MIN,
};

enum rk817_output_mode {
	AVERAGE_MODE,
};

enum rk817_battery_fields {
	ADC_SLP_RATE, BAT_CUR_ADC_EN, BAT_VOL_ADC_EN,
	USB_VOL_ADC_EN, TS_ADC_EN, SYS_VOL_ADC_EN, GG_EN, /*ADC_CONFIG0*/
	CUR_ADC_DITH_SEL, CUR_ADC_DIH_EN, CUR_ADC_CHOP_EN,
	CUR_ADC_CHOP_SEL, CUR_ADC_CHOP_VREF_EN, /*CUR_ADC_CFG0*/
	CUR_ADC_VCOM_SEL, CUR_ADC_VCOM_BUF_INC, CUR_ADC_VREF_BUF_INC,
	CUR_ADC_BIAS_DEC, CUR_ADC_IBIAS_SEL,/*CUR_ADC_CFG1*/
	VOL_ADC_EXT_VREF_EN, VOL_ADC_DITH_SEL, VOL_ADC_DITH_EN,
	VOL_ADC_CHOP_EN, VOL_ADC_CHOP_SEL, VOL_ADC_CHOP_VREF_EN,
	VOL_ADC_VCOM_SEL, VOL_ADC_VCOM_BUF_INC, VOL_ADC_VREF_BUF_INC,
	VOL_ADC_IBIAS_SEL, /*VOL_ADC_CFG1*/
	RLX_CUR_FILTER, TS_FUN, VOL_ADC_TSCUR_SEL,
	VOL_CALIB_UPD, CUR_CALIB_UPD, /*ADC_CONFIG1*/
	CUR_OUT_MOD, VOL_OUT_MOD, FRAME_SMP_INTERV,
	ADC_OFF_CAL_INTERV, RLX_SPT, /*GG_CON*/
	OCV_UPD, RELAX_STS, RELAX_VOL2_UPD, RELAX_VOL1_UPD, BAT_CON,
	QMAX_UPD_SOFT, TERM_UPD, OCV_STS, /*GG_STS*/
	RELAX_THRE_H, RELAX_THRE_L, /*RELAX_THRE*/
	RELAX_VOL1_H, RELAX_VOL1_L,
	RELAX_VOL2_H, RELAX_VOL2_L,
	RELAX_CUR1_H, RELAX_CUR1_L,
	RELAX_CUR2_H, RELAX_CUR2_L,
	OCV_THRE_VOL,
	OCV_VOL_H, OCV_VOL_L,
	OCV_VOL0_H, OCV_VOL0_L,
	OCV_CUR_H, OCV_CUR_L,
	OCV_CUR0_H, OCV_CUR0_L,
	PWRON_VOL_H, PWRON_VOL_L,
	PWRON_CUR_H, PWRON_CUR_L,
	OFF_CNT,
	Q_INIT_H3, Q_INIT_H2, Q_INIT_L1, Q_INIT_L0,
	Q_PRESS_H3, Q_PRESS_H2, Q_PRESS_L1, Q_PRESS_L0,
	BAT_VOL_H, BAT_VOL_L,
	BAT_CUR_H, BAT_CUR_L,
	BAT_TS_H, BAT_TS_L,
	USB_VOL_H, USB_VOL_L,
	SYS_VOL_H, SYS_VOL_L,
	Q_MAX_H3, Q_MAX_H2, Q_MAX_L1, Q_MAX_L0,
	Q_TERM_H3, Q_TERM_H2, Q_TERM_L1, Q_TERM_L0,
	Q_OCV_H3, Q_OCV_H2, Q_OCV_L1, Q_OCV_L0,
	OCV_CNT,
	SLEEP_CON_SAMP_CUR_H, SLEEP_CON_SAMP_CUR_L,
	CAL_OFFSET_H, CAL_OFFSET_L,
	VCALIB0_H, VCALIB0_L,
	VCALIB1_H, VCALIB1_L,
	IOFFSET_H, IOFFSET_L,
	BAT_R0, SOC_REG0, SOC_REG1, SOC_REG2,
	REMAIN_CAP_REG2, REMAIN_CAP_REG1, REMAIN_CAP_REG0,
	NEW_FCC_REG2, NEW_FCC_REG1, NEW_FCC_REG0,
	RESET_MODE,
	FG_INIT, HALT_CNT_REG, CALC_REST_REGL, UPDATE_LEVE_REG,
	VOL_ADC_B3, VOL_ADC_B2, VOL_ADC_B1, VOL_ADC_B0,
	VOL_ADC_K3, VOL_ADC_K2, VOL_ADC_K1, VOL_ADC_K0,
	BAT_EXS, CHG_STS, BAT_OVP_STS, CHRG_IN_CLAMP,
	CHIP_NAME_H, CHIP_NAME_L, CHRG_CUR_SEL, CHRG_VOL_SEL,
	PLUG_IN_STS, BAT_LTS_TS, USB_SYS_EN,
	F_MAX_FIELDS
};

static const struct reg_field rk817_battery_reg_fields[] = {
	[ADC_SLP_RATE] = REG_FIELD(0x50, 0, 0),
	[BAT_CUR_ADC_EN] = REG_FIELD(0x50, 2, 2),
	[BAT_VOL_ADC_EN] = REG_FIELD(0x50, 3, 3),
	[USB_VOL_ADC_EN] = REG_FIELD(0x50, 4, 4),
	[TS_ADC_EN] = REG_FIELD(0x50, 5, 5),
	[SYS_VOL_ADC_EN] = REG_FIELD(0x50, 6, 6),
	[GG_EN] = REG_FIELD(0x50, 7, 7),/*ADC_CONFIG0*/

	[CUR_ADC_DITH_SEL] = REG_FIELD(0x51, 1, 3),
	[CUR_ADC_DIH_EN] = REG_FIELD(0x51, 4, 4),
	[CUR_ADC_CHOP_EN] = REG_FIELD(0x51, 5, 5),
	[CUR_ADC_CHOP_SEL] = REG_FIELD(0x51, 6, 6),
	[CUR_ADC_CHOP_VREF_EN] = REG_FIELD(0x51, 7, 7), /*CUR_ADC_COFG0*/

	[CUR_ADC_VCOM_SEL] = REG_FIELD(0x52, 0, 1),
	[CUR_ADC_VCOM_BUF_INC] = REG_FIELD(0x52, 2, 2),
	[CUR_ADC_VREF_BUF_INC] = REG_FIELD(0x52, 3, 3),
	[CUR_ADC_BIAS_DEC] = REG_FIELD(0x52, 4, 4),
	[CUR_ADC_IBIAS_SEL] = REG_FIELD(0x52, 5, 6), /*CUR_ADC_COFG1*/

	[VOL_ADC_EXT_VREF_EN] = REG_FIELD(0x53, 0, 0),
	[VOL_ADC_DITH_SEL]  = REG_FIELD(0x53, 1, 3),
	[VOL_ADC_DITH_EN] = REG_FIELD(0x53, 4, 4),
	[VOL_ADC_CHOP_EN] = REG_FIELD(0x53, 5, 5),
	[VOL_ADC_CHOP_SEL] = REG_FIELD(0x53, 6, 6),
	[VOL_ADC_CHOP_VREF_EN] = REG_FIELD(0x53, 7, 7),/*VOL_ADC_COFG0*/

	[VOL_ADC_VCOM_SEL] = REG_FIELD(0x54, 0, 1),
	[VOL_ADC_VCOM_BUF_INC] = REG_FIELD(0x54, 2, 2),
	[VOL_ADC_VREF_BUF_INC] = REG_FIELD(0x54, 3, 3),
	[VOL_ADC_IBIAS_SEL] = REG_FIELD(0x54, 5, 6), /*VOL_ADC_COFG1*/

	[RLX_CUR_FILTER] = REG_FIELD(0x55, 0, 1),
	[TS_FUN] = REG_FIELD(0x55, 3, 3),
	[VOL_ADC_TSCUR_SEL] = REG_FIELD(0x55, 4, 5),
	[VOL_CALIB_UPD] = REG_FIELD(0x55, 6, 6),
	[CUR_CALIB_UPD] = REG_FIELD(0x55, 7, 7), /*ADC_CONFIG1*/

	[CUR_OUT_MOD] = REG_FIELD(0x56, 0, 0),
	[VOL_OUT_MOD] = REG_FIELD(0x56, 1, 1),
	[FRAME_SMP_INTERV] = REG_FIELD(0x56, 2, 3),
	[ADC_OFF_CAL_INTERV] = REG_FIELD(0x56, 4, 5),
	[RLX_SPT] = REG_FIELD(0x56, 6, 7), /*GG_CON*/

	[OCV_UPD] = REG_FIELD(0x57, 0, 0),
	[RELAX_STS] = REG_FIELD(0x57, 1, 1),
	[RELAX_VOL2_UPD] = REG_FIELD(0x57, 2, 2),
	[RELAX_VOL1_UPD] = REG_FIELD(0x57, 3, 3),
	[BAT_CON] = REG_FIELD(0x57, 4, 4),
	[QMAX_UPD_SOFT] = REG_FIELD(0x57, 5, 5),
	[TERM_UPD] = REG_FIELD(0x57, 6, 6),
	[OCV_STS] = REG_FIELD(0x57, 7, 7), /*GG_STS*/

	[RELAX_THRE_H] = REG_FIELD(0x58, 0, 7),
	[RELAX_THRE_L] = REG_FIELD(0x59, 0, 7),

	[RELAX_VOL1_H] = REG_FIELD(0x5A, 0, 7),
	[RELAX_VOL1_L] = REG_FIELD(0x5B, 0, 7),
	[RELAX_VOL2_H] = REG_FIELD(0x5C, 0, 7),
	[RELAX_VOL2_L] = REG_FIELD(0x5D, 0, 7),

	[RELAX_CUR1_H] = REG_FIELD(0x5E, 0, 7),
	[RELAX_CUR1_L] = REG_FIELD(0x5F, 0, 7),
	[RELAX_CUR2_H] = REG_FIELD(0x60, 0, 7),
	[RELAX_CUR2_L] = REG_FIELD(0x61, 0, 7),

	[OCV_THRE_VOL] = REG_FIELD(0x62, 0, 7),

	[OCV_VOL_H] = REG_FIELD(0x63, 0, 7),
	[OCV_VOL_L] = REG_FIELD(0x64, 0, 7),
	[OCV_VOL0_H] = REG_FIELD(0x65, 0, 7),
	[OCV_VOL0_L] = REG_FIELD(0x66, 0, 7),
	[OCV_CUR_H] = REG_FIELD(0x67, 0, 7),
	[OCV_CUR_L] = REG_FIELD(0x68, 0, 7),
	[OCV_CUR0_H] = REG_FIELD(0x69, 0, 7),
	[OCV_CUR0_L] = REG_FIELD(0x6A, 0, 7),
	[PWRON_VOL_H] = REG_FIELD(0x6B, 0, 7),
	[PWRON_VOL_L] = REG_FIELD(0x6C, 0, 7),
	[PWRON_CUR_H] = REG_FIELD(0x6D, 0, 7),
	[PWRON_CUR_L] = REG_FIELD(0x6E, 0, 7),
	[OFF_CNT] = REG_FIELD(0x6F, 0, 7),
	[Q_INIT_H3] = REG_FIELD(0x70, 0, 7),
	[Q_INIT_H2] = REG_FIELD(0x71, 0, 7),
	[Q_INIT_L1] = REG_FIELD(0x72, 0, 7),
	[Q_INIT_L0] = REG_FIELD(0x73, 0, 7),

	[Q_PRESS_H3] = REG_FIELD(0x74, 0, 7),
	[Q_PRESS_H2] = REG_FIELD(0x75, 0, 7),
	[Q_PRESS_L1] = REG_FIELD(0x76, 0, 7),
	[Q_PRESS_L0] = REG_FIELD(0x77, 0, 7),

	[BAT_VOL_H] = REG_FIELD(0x78, 0, 7),
	[BAT_VOL_L] = REG_FIELD(0x79, 0, 7),

	[BAT_CUR_H] = REG_FIELD(0x7A, 0, 7),
	[BAT_CUR_L] = REG_FIELD(0x7B, 0, 7),

	[BAT_TS_H] = REG_FIELD(0x7C, 0, 7),
	[BAT_TS_L] = REG_FIELD(0x7D, 0, 7),
	[USB_VOL_H] = REG_FIELD(0x7E, 0, 7),
	[USB_VOL_L] = REG_FIELD(0x7F, 0, 7),

	[SYS_VOL_H] = REG_FIELD(0x80, 0, 7),
	[SYS_VOL_L] = REG_FIELD(0x81, 0, 7),
	[Q_MAX_H3] = REG_FIELD(0x82, 0, 7),
	[Q_MAX_H2] = REG_FIELD(0x83, 0, 7),
	[Q_MAX_L1] = REG_FIELD(0x84, 0, 7),
	[Q_MAX_L0] = REG_FIELD(0x85, 0, 7),

	[Q_TERM_H3] = REG_FIELD(0x86, 0, 7),
	[Q_TERM_H2] = REG_FIELD(0x87, 0, 7),
	[Q_TERM_L1] = REG_FIELD(0x88, 0, 7),
	[Q_TERM_L0] = REG_FIELD(0x89, 0, 7),
	[Q_OCV_H3] = REG_FIELD(0x8A, 0, 7),
	[Q_OCV_H2] = REG_FIELD(0x8B, 0, 7),

	[Q_OCV_L1] = REG_FIELD(0x8C, 0, 7),
	[Q_OCV_L0] = REG_FIELD(0x8D, 0, 7),
	[OCV_CNT] = REG_FIELD(0x8E, 0, 7),
	[SLEEP_CON_SAMP_CUR_H] = REG_FIELD(0x8F, 0, 7),
	[SLEEP_CON_SAMP_CUR_L] = REG_FIELD(0x90, 0, 7),
	[CAL_OFFSET_H] = REG_FIELD(0x91, 0, 7),
	[CAL_OFFSET_L] = REG_FIELD(0x92, 0, 7),
	[VCALIB0_H] = REG_FIELD(0x93, 0, 7),
	[VCALIB0_L] = REG_FIELD(0x94, 0, 7),
	[VCALIB1_H] = REG_FIELD(0x95, 0, 7),
	[VCALIB1_L] = REG_FIELD(0x96, 0, 7),
	[IOFFSET_H] = REG_FIELD(0x97, 0, 7),
	[IOFFSET_L] = REG_FIELD(0x98, 0, 7),

	[BAT_R0] = REG_FIELD(0x99, 0, 7),
	[SOC_REG0] = REG_FIELD(0x9A, 0, 7),
	[SOC_REG1] = REG_FIELD(0x9B, 0, 7),
	[SOC_REG2] = REG_FIELD(0x9C, 0, 7),

	[REMAIN_CAP_REG0] = REG_FIELD(0x9D, 0, 7),
	[REMAIN_CAP_REG1] = REG_FIELD(0x9E, 0, 7),
	[REMAIN_CAP_REG2] = REG_FIELD(0x9F, 0, 7),
	[NEW_FCC_REG0] = REG_FIELD(0xA0, 0, 7),
	[NEW_FCC_REG1] = REG_FIELD(0xA1, 0, 7),
	[NEW_FCC_REG2] = REG_FIELD(0xA2, 0, 7),
	[RESET_MODE] = REG_FIELD(0xA3, 0, 3),
	[FG_INIT] = REG_FIELD(0xA5, 7, 7),

	[HALT_CNT_REG] = REG_FIELD(0xA6, 0, 7),
	[CALC_REST_REGL] = REG_FIELD(0xA7, 0, 7),
	[UPDATE_LEVE_REG] = REG_FIELD(0xA8, 0, 7),

	[VOL_ADC_B3] = REG_FIELD(0xA9, 0, 7),
	[VOL_ADC_B2] = REG_FIELD(0xAA, 0, 7),
	[VOL_ADC_B1] = REG_FIELD(0xAB, 0, 7),
	[VOL_ADC_B0] = REG_FIELD(0xAC, 0, 7),

	[VOL_ADC_K3] = REG_FIELD(0xAD, 0, 7),
	[VOL_ADC_K2] = REG_FIELD(0xAE, 0, 7),
	[VOL_ADC_K1] = REG_FIELD(0xAF, 0, 7),
	[VOL_ADC_K0] = REG_FIELD(0xB0, 0, 7),
	[CHRG_CUR_SEL] = REG_FIELD(0xE4, 0, 2),
	[CHRG_VOL_SEL] = REG_FIELD(0xE4, 4, 6),
	[USB_SYS_EN] = REG_FIELD(0xE6, 6, 6),
	[BAT_LTS_TS] = REG_FIELD(0xE9, 0, 7),
	[BAT_EXS] = REG_FIELD(0xEB, 7, 7),
	[CHG_STS] = REG_FIELD(0xEB, 4, 6),
	[BAT_OVP_STS] = REG_FIELD(0xEB, 3, 3),
	[CHRG_IN_CLAMP] = REG_FIELD(0xEB, 2, 2),
	[CHIP_NAME_H] = REG_FIELD(0xED, 0, 7),
	[CHIP_NAME_L] = REG_FIELD(0xEE, 0, 7),
	[PLUG_IN_STS] = REG_FIELD(0xF0, 6, 6),
};

struct temp_chrg_table {
	int temp_down;
	int temp_up;
	int chrg_current;
	int chrg_voltage;
	int chrg_current_index;
	int chrg_voltage_index;
};

struct battery_platform_data {
	u32 *ocv_table;
	u32 ocv_size;
	struct temp_chrg_table *tc_table;
	u32 tc_count;
	u32 *ntc_table;
	u32 ntc_size;
	int ntc_degree_from;
	u32 ntc_factor;
	u32 pwroff_vol;
	u32 monitor_sec;
	u32 bat_res;
	u32 design_capacity;
	u32 design_qmax;
	u32 sleep_enter_current;
	u32 sleep_exit_current;
	u32 sleep_filter_current;

	u32 bat_mode;
	u32 sample_res;
	u32 bat_res_up;
	u32 bat_res_down;
	u32 design_max_voltage;
	int fake_full_soc;
	int charge_stay_awake;
};

/* optional low-battery indicator */
#define RK817_BAT_LOW_DEFAULT_THRES   5   /* percent */
struct rk817_bat_low_gpio {
	struct gpio_desc *desc;     /* preferred descriptor path */
	int legacy_gpio;            /* fallback for legacy "bat_low_gpio" */
};

struct rk817_battery_device {
	struct platform_device		*pdev;
	struct device			*dev;
	struct i2c_client		*client;
	struct rk808			*rk817;
	struct power_supply		*bat;
	struct power_supply		*chg_psy;
	struct regmap_field		*rmap_fields[F_MAX_FIELDS];
	struct battery_platform_data	*pdata;
	struct workqueue_struct		*bat_monitor_wq;
	struct delayed_work		bat_delay_work;
	struct delayed_work		calib_delay_work;
	struct timer_list		caltimer;

	int				sample_res;
	int				bat_res;
	bool				is_first_power_on;
	int				chrg_status;
	int				current_avg;
	int				current_relax;
	int				current_sleep;
	int				voltage_usb;
	int				voltage_sys;
	int				voltage_avg;
	int				voltage_ocv;
	int				voltage_relax;
	int				voltage_k;/* VCALIB0 VCALIB1 */
	int				voltage_b;
	u32				remain_cap;
	int				design_cap;
	int				nac;
	int				fcc;
	int				qmax;
	int				dsoc;
	int				rsoc;
	int				fake_full_soc;
	int				pwron_voltage;
	int				powerpatch_res;
	time64_t			rtc_base;
	unsigned long			sleep_dischrg_sec;
	unsigned long			sleep_sum_sec;
	bool				sleep_chrg_online;
	u8				sleep_chrg_status;
	bool				active_awake;
	int				temperature;
	u32				monitor_ms;
	u32				pwroff_min;

	int				is_charging;
	u8				plugin_trigger;
	u8				plugout_trigger;
	int				chip_id;
	int				is_register_chg_psy;
	bool				fcc_update_done;	/* FCC aging update done */
	int				charge_index;

	/* low-battery gpio support */
	struct rk817_bat_low_gpio       bat_low;
	u32                             bat_low_threshold; /* percent */

	/* Hybrid mode (voltage + coulomb counting) */
	int				hybrid_v_full_chg;	/* learned charge-full voltage (mV) */
	int				hybrid_v_full_dis;	/* learned discharge-full voltage (mV) */
	int				hybrid_v_empty_chg;	/* charge-empty voltage (mV) */
	int				hybrid_v_empty_dis;	/* discharge-empty voltage (mV) */
	int				hybrid_voltage_ema;	/* EMA filtered voltage (mV) */
	int				hybrid_prev_voltage[2];	/* for median-of-3 */
	int				hybrid_visible_soc;	/* step-limited display SOC (0-100) */
	int				hybrid_internal_soc;	/* calculated SOC (0-100) */
	bool				hybrid_calibrated;	/* both V_FULL learned */
	bool				hybrid_tracking_peak;	/* tracking charge peak */
	int				hybrid_peak_mv;		/* peak charging voltage */
	int64_t				hybrid_peak_start_sec;	/* peak tracking start time */
	bool				hybrid_peak_stable;	/* peak within stability window */
	bool				hybrid_full_event;	/* full event active */
	int64_t				hybrid_last_loop_sec;	/* last loop boottime */
	bool				hybrid_first_run;	/* first iteration flag */
	int64_t				hybrid_99_start_sec;	/* time when SOC first hit 99% */
	bool				hybrid_map_loaded;	/* calib file load completed */
	u8				hybrid_map_tries;	/* calib file load attempts */

	/* Per-device state (formerly static locals) */
	u32				save_cap_old;		/* for rk817_bat_save_cap */
	int				save_dsoc_last;		/* for rk817_bat_save_dsoc */
	int				ps_changed_old_soc;	/* for power_supply_changed */
	int				ps_changed_status;	/* for power_supply_changed */
};

static u64 get_boot_sec(void)
{
	struct timespec ts;

	get_monotonic_boottime(&ts);

	return ts.tv_sec;
}

static u32 interpolate(int value, u32 *table, int size)
{
	u8 i;
	u16 d;

	if (size < 2)
		return 0;

	for (i = 0; i < size; i++) {
		if (value < table[i])
			break;
	}

	if ((i > 0) && (i < size)) {
		d = (value - table[i - 1]) * (MAX_INTERPOLATE / (size - 1));
		if (table[i] == table[i - 1])
			return i * ((MAX_INTERPOLATE + size / 2) / size);
		d /= table[i] - table[i - 1];
		d = d + (i - 1) * (MAX_INTERPOLATE / (size - 1));
	} else {
		d = i * ((MAX_INTERPOLATE + size / 2) / size);
	}

	if (d > 1000)
		d = 1000;

	return d;
}

/* (a * b) / c */
static int32_t ab_div_c(u32 a, u32 b, u32 c)
{
	bool sign;
	u32 ans = MAX_INT;
	u64 prod;
	int tmp;

	sign = ((((a ^ b) ^ c) & 0x80000000) != 0);
	if (c != 0) {
		if (sign)
			c = -c;
		prod = (u64)a * b + (c >> 1);
		if (prod > 0x7FFFFFFF) {
			/* Overflow, cap to MAX_INT */
			ans = MAX_INT;
		} else {
			tmp = (int)prod / c;
			if (tmp < MAX_INT)
				ans = tmp;
		}
	}

	if (sign)
		ans = -ans;

	return ans;
}

/*
 * ===== Hybrid Mode: Voltage + Coulomb Counting =====
 *
 * Strategy:
 * - Coulomb counting for daily SOC tracking (short-term accurate)
 * - Voltage for calibration at key moments (long-term stable)
 * - Charging: trust coulomb counting only (voltage is inflated)
 * - Discharging: coulomb counting primary, voltage calibrates periodically
 * - Sleep/wake: voltage calibration when discharging and stable
 * - Full detection: peak voltage dwelling for 15 minutes
 */

/* Forward declarations for hybrid mode */
static void rk817_bat_init_coulomb_cap(struct rk817_battery_device *battery,
				       u32 capacity);
static int rk817_bat_field_read(struct rk817_battery_device *battery,
				enum rk817_battery_fields field_id);
static int rk817_bat_field_write(struct rk817_battery_device *battery,
				 enum rk817_battery_fields field_id,
				 unsigned int val);

static int rk817_hybrid_median3(int a, int b, int c)
{
	if (a > b) { int t = a; a = b; b = t; }
	if (b > c) { int t = b; b = c; c = t; }
	if (a > b) { int t = a; a = b; b = t; }
	return b;
}

static int rk817_hybrid_ema_filter(int raw_mv, struct rk817_battery_device *battery)
{
	if (battery->hybrid_voltage_ema < 0)
		battery->hybrid_voltage_ema = raw_mv;
	else
		battery->hybrid_voltage_ema =
			(HYBRID_EMA_ALPHA_NUM * raw_mv +
			 (HYBRID_EMA_ALPHA_DEN - HYBRID_EMA_ALPHA_NUM) *
			 battery->hybrid_voltage_ema) / HYBRID_EMA_ALPHA_DEN;
	return battery->hybrid_voltage_ema;
}

/*
 * Charge curve: compressed at low SOC, linear at high SOC.
 * Exponent varies from 2.5 (at 0%) to 1.0 (at 100%).
 */
static int rk817_hybrid_voltage_to_soc_chg(int voltage_mv, int v_empty, int v_full)
{
	int range, idx;
	/* lookup table: 21 points, each 5% */
	static const int chg_curve[21] = {
		0, 12, 25, 37, 48, 58, 67, 75, 82, 88,
		92, 95, 97, 98, 99, 99, 99, 100, 100, 100, 100
	};

	if (v_full <= v_empty)
		return 1;

	range = v_full - v_empty;
	if (voltage_mv <= v_empty)
		return 0;
	if (voltage_mv >= v_full)
		return 100;

	idx = (voltage_mv - v_empty) * 20 / range;
	if (idx < 0) idx = 0;
	if (idx > 20) idx = 20;

	return chg_curve[idx];
}

/*
 * Discharge curve: S-shaped (smootherstep) for natural feel.
 * 100% plateau near V_FULL_DIS, steep drop at empty.
 */
static int rk817_hybrid_voltage_to_soc_dis(int voltage_mv, int v_empty, int v_full)
{
	int range, adj_full, idx;
	/* S-curve lookup table: 21 points */
	static const int dis_curve[21] = {
		0, 2, 5, 10, 18, 28, 39, 50, 61, 72,
		80, 86, 90, 93, 95, 97, 98, 99, 99, 100, 100
	};

	if (v_full <= v_empty)
		return 1;

	/* Create a small 100% plateau at the top (2% of range) */
	adj_full = v_full - (v_full - v_empty) * 2 / 100;
	if (adj_full < v_empty + HYBRID_MIN_RANGE_MV)
		adj_full = v_empty + HYBRID_MIN_RANGE_MV;

	if (voltage_mv >= v_full)
		return 100;
	if (voltage_mv >= adj_full)
		return 100;
	if (voltage_mv <= v_empty)
		return 0;

	range = adj_full - v_empty;
	idx = (voltage_mv - v_empty) * 20 / range;
	if (idx < 0) idx = 0;
	if (idx > 20) idx = 20;

	return dis_curve[idx];
}

static int rk817_hybrid_voltage_to_soc(int voltage_mv, bool charging,
					struct rk817_battery_device *battery)
{
	if (charging)
		return rk817_hybrid_voltage_to_soc_chg(voltage_mv,
			battery->hybrid_v_empty_chg, battery->hybrid_v_full_chg);
	else
		return rk817_hybrid_voltage_to_soc_dis(voltage_mv,
			battery->hybrid_v_empty_dis, battery->hybrid_v_full_dis);
}

static int rk817_hybrid_step_limit(int last, int target, bool charging)
{
	if (last < 0)
		return target;
	if (charging) {
		if (target <= last)
			return last;
		if (target <= last + 1)
			return target;
		return last + 1;
	} else {
		if (target >= last)
			return last;
		if (target >= last - 1)
			return target;
		return last - 1;
	}
}

static int64_t rk817_hybrid_boottime_sec(void)
{
	struct timespec ts;
	get_monotonic_boottime(&ts);
	return ts.tv_sec;
}

static int rk817_hybrid_file_write(struct rk817_battery_device *battery)
{
	struct file *fp;
	mm_segment_t oldfs;
	char buf[32];
	int len, ret;

	len = snprintf(buf, sizeof(buf), "%d %d\n",
		       battery->hybrid_v_full_chg,
		       battery->hybrid_v_full_dis);

	fp = filp_open(HYBRID_CALIB_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (IS_ERR(fp))
		return PTR_ERR(fp);

	oldfs = get_fs();
	set_fs(KERNEL_DS);
	ret = kernel_write(fp, buf, len, 0);
	set_fs(oldfs);

	filp_close(fp, NULL);

	return (ret == len) ? 0 : (ret < 0 ? (int)ret : -EIO);
}

static void rk817_hybrid_save_map(struct rk817_battery_device *battery)
{
	int ret;

	ret = rk817_hybrid_file_write(battery);
	if (ret) {
		pr_err("hybrid: failed to save calib to %s (%d)\n",
		       HYBRID_CALIB_PATH, ret);
		return;
	}

	battery->hybrid_map_loaded = true;

	DBG("hybrid: saved vfull_chg=%d vfull_dis=%d\n",
	    battery->hybrid_v_full_chg, battery->hybrid_v_full_dis);
}

static void rk817_hybrid_load_map(struct rk817_battery_device *battery)
{
	struct file *fp;
	mm_segment_t oldfs;
	char buf[32];
	ssize_t n;
	int vchg, vdis;

	if (battery->hybrid_map_loaded)
		return;

	if (++battery->hybrid_map_tries > HYBRID_CALIB_LOAD_TRIES) {
		battery->hybrid_map_loaded = true;
		return;
	}

	fp = filp_open(HYBRID_CALIB_PATH, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		/* ENOENT: rootfs not mounted yet or never learned */
		return;
	}

	oldfs = get_fs();
	set_fs(KERNEL_DS);
	n = kernel_read(fp, 0, buf, sizeof(buf) - 1);
	set_fs(oldfs);

	filp_close(fp, NULL);

	battery->hybrid_map_loaded = true;

	if (n <= 0)
		return;

	buf[n] = '\0';
	if (sscanf(buf, "%d %d", &vchg, &vdis) != 2)
		return;

	/* Validate and apply */
	if (vchg >= 3600 && vchg <= 4600) {
		battery->hybrid_v_full_chg = vchg;
		battery->hybrid_calibrated = true;
	}
	if (vdis >= 3600 && vdis <= 4600) {
		battery->hybrid_v_full_dis = vdis;
		battery->hybrid_calibrated = true;
	}

	if (battery->hybrid_calibrated)
		BAT_INFO("hybrid: loaded vfull_chg=%d vfull_dis=%d\n",
			 vchg, vdis);
}

static void rk817_hybrid_learn_vfull_chg(int voltage_ema_mv,
					 struct rk817_battery_device *battery)
{
	int quantized;

	if (voltage_ema_mv < 3600 || voltage_ema_mv > 4600)
		return;

	/* Quantize to 2mV precision */
	quantized = ((voltage_ema_mv + 1) / 2) * 2;
	if (abs(quantized - battery->hybrid_v_full_chg) >= 2) {
		battery->hybrid_v_full_chg = quantized;
		rk817_hybrid_save_map(battery);
		BAT_INFO("hybrid: learned vfull_chg=%d (ema=%d)\n",
			 quantized, voltage_ema_mv);
	}
}

static void rk817_hybrid_learn_vfull_dis(int voltage_ema_mv,
					 struct rk817_battery_device *battery)
{
	int quantized, max_dis;

	if (voltage_ema_mv < 3500 || voltage_ema_mv > 4600)
		return;

	/* Quantize to 2mV precision for better accuracy */
	quantized = ((voltage_ema_mv + 1) / 2) * 2;
	max_dis = battery->hybrid_v_full_chg - 15;
	if (max_dis < 3500)
		return;
	if (quantized > max_dis)
		quantized = max_dis;

	if (abs(quantized - battery->hybrid_v_full_dis) >= 2) {
		battery->hybrid_v_full_dis = quantized;
		rk817_hybrid_save_map(battery);
		battery->hybrid_calibrated = true;
		BAT_INFO("hybrid: learned vfull_dis=%d (ema=%d)\n",
			 quantized, voltage_ema_mv);
	}
}

static void rk817_hybrid_update_peak(struct rk817_battery_device *battery,
				     bool charging, int voltage_ema_mv)
{
	int64_t now_sec = rk817_hybrid_boottime_sec();

	if (!charging) {
		battery->hybrid_tracking_peak = false;
		return;
	}

	if (voltage_ema_mv >= HYBRID_PEAK_TRACK_START_MV) {
		if (!battery->hybrid_tracking_peak) {
			battery->hybrid_tracking_peak = true;
			battery->hybrid_peak_mv = voltage_ema_mv;
			battery->hybrid_peak_start_sec = now_sec;
			battery->hybrid_peak_stable = true;
		} else {
			if (voltage_ema_mv > battery->hybrid_peak_mv) {
				battery->hybrid_peak_mv = voltage_ema_mv;
				battery->hybrid_peak_start_sec = now_sec;
				battery->hybrid_peak_stable = true;
			} else if (voltage_ema_mv >=
				   (battery->hybrid_peak_mv - HYBRID_PEAK_STABILITY_MV)) {
				battery->hybrid_peak_stable = true;
			} else {
				/* dropped too far, abort */
				battery->hybrid_tracking_peak = false;
			}
		}
	}
}

static bool rk817_hybrid_peak_dwell_met(struct rk817_battery_device *battery)
{
	int64_t now_sec, elapsed;

	if (!battery->hybrid_tracking_peak || !battery->hybrid_peak_stable)
		return false;

	now_sec = rk817_hybrid_boottime_sec();
	elapsed = now_sec - battery->hybrid_peak_start_sec;
	return elapsed >= HYBRID_PEAK_DWELL_SEC;
}

static void rk817_hybrid_calculate(struct rk817_battery_device *battery)
{
	int raw_mv, ema_mv, soc;
	int raw_soc_from_coulomb;
	/*
	 * Only CC_OR_CV_CHRG is true charging. CHARGE_FINISH is done —
	 * coulomb counter should be at FCC, and voltage calibration
	 * should be allowed to correct any residual error.
	 */
	bool charging = (battery->chrg_status == CC_OR_CV_CHRG ||
			 battery->chrg_status == CHARGE_FINISH);
	bool status_full = (battery->chrg_status == CHARGE_FINISH);
	bool peak_dwell, full_event, allow_100;
	int64_t now_sec;
	long gap_sec;

	/* Step 1: Read raw voltage and apply EMA filter */
	raw_mv = battery->voltage_avg;
#if HYBRID_DEBUG
	{
		int gg_en = rk817_bat_field_read(battery, GG_EN);
		int cur_h = rk817_bat_field_read(battery, BAT_CUR_H);
		int cur_l = rk817_bat_field_read(battery, BAT_CUR_L);
		int qph3 = rk817_bat_field_read(battery, Q_PRESS_H3);
		int qph2 = rk817_bat_field_read(battery, Q_PRESS_H2);
		int qpl1 = rk817_bat_field_read(battery, Q_PRESS_L1);
		int qpl0 = rk817_bat_field_read(battery, Q_PRESS_L0);
		HYBRID_DBG("hybrid: chrg=%d status=%d first=%d v=%d rsoc=%d vis=%d int=%d dsoc=%d gg_en=%d cur=0x%02x%02x qpress=0x%02x%02x%02x%02x\n",
			 charging, battery->chrg_status, battery->hybrid_first_run,
			 raw_mv, battery->rsoc / 1000,
			 battery->hybrid_visible_soc, battery->hybrid_internal_soc,
			 battery->dsoc / 1000,
			 gg_en, cur_h, cur_l, qph3, qph2, qpl1, qpl0);
	}
#endif
	if (raw_mv <= 0) {
		/*
		 * Voltage read failure — don't fake it with v_full_dis
		 * (would report ~100%). Fall back to coulomb counter only.
		 */
		DBG("hybrid: voltage_avg=%d, skipping voltage calibration\n",
		    battery->voltage_avg);
		raw_soc_from_coulomb = battery->rsoc / 1000;
		battery->hybrid_internal_soc = raw_soc_from_coulomb;
		battery->hybrid_visible_soc = raw_soc_from_coulomb;
		battery->dsoc = battery->hybrid_visible_soc * 1000;
		return;
	}

	/* median-of-3 */
	if (battery->hybrid_prev_voltage[0] < 0)
		battery->hybrid_prev_voltage[0] = raw_mv;
	if (battery->hybrid_prev_voltage[1] < 0)
		battery->hybrid_prev_voltage[1] = battery->hybrid_prev_voltage[0];

	{
		int v_med = rk817_hybrid_median3(
			battery->hybrid_prev_voltage[1],
			battery->hybrid_prev_voltage[0],
			raw_mv);
		battery->hybrid_prev_voltage[1] = battery->hybrid_prev_voltage[0];
		battery->hybrid_prev_voltage[0] = raw_mv;
		ema_mv = rk817_hybrid_ema_filter(v_med, battery);
	}

	/* Step 2: Update peak tracking */
	rk817_hybrid_update_peak(battery, charging, ema_mv);
	peak_dwell = rk817_hybrid_peak_dwell_met(battery);
	full_event = status_full || peak_dwell;
	allow_100 = full_event || battery->hybrid_full_event;

	/* Step 3: Calculate SOC from coulomb counter */
	raw_soc_from_coulomb = battery->rsoc / 1000;
	HYBRID_DBG("hybrid: remain_cap=%d fcc=%d rsoc_hw=%d raw_soc=%d%%\n",
		 battery->remain_cap, battery->fcc,
		 battery->rsoc, raw_soc_from_coulomb);

	/* Step 4: Calculate SOC from voltage */
	soc = rk817_hybrid_voltage_to_soc(ema_mv, charging, battery);

	/* Step 5: Hybrid blending */
	now_sec = rk817_hybrid_boottime_sec();
	gap_sec = now_sec - battery->hybrid_last_loop_sec;
	battery->hybrid_last_loop_sec = now_sec;

	if (battery->hybrid_first_run) {
		battery->hybrid_internal_soc = raw_soc_from_coulomb;
		battery->hybrid_visible_soc = raw_soc_from_coulomb;
		rk817_bat_init_coulomb_cap(battery,
					    battery->remain_cap / 1000);
		battery->hybrid_first_run = false;
		HYBRID_DBG("hybrid: first run done, rsoc=%d%% vis=%d int=%d\n",
		    raw_soc_from_coulomb,
		    battery->hybrid_visible_soc,
		    battery->hybrid_internal_soc);
	} else if (charging) {
		battery->hybrid_internal_soc = raw_soc_from_coulomb;

		if (full_event && !battery->hybrid_full_event) {
			rk817_hybrid_learn_vfull_chg(ema_mv, battery);
			battery->hybrid_full_event = true;
		}
		HYBRID_DBG("hybrid: charge, coulomb=%d%% ema=%d int=%d peak=%d full=%d\n",
			 raw_soc_from_coulomb, ema_mv,
			 battery->hybrid_internal_soc,
			 battery->hybrid_peak_mv, full_event);
	} else {
		/*
		 * Discharging: coulomb counter primary.
		 * Periodically calibrate with voltage to correct drift.
		 *
		 * Adaptive calibration interval:
		 * - SOC 20-80%: 30min (voltage plateau, less reliable)
		 * - SOC <20% or >80%: 15min (voltage changes more, more reliable)
		 *
		 * Weighted blending (not direct overwrite):
		 * - SOC 20-80%: 30% voltage + 70% coulomb (voltage unreliable)
		 * - SOC <20%: 70% voltage + 30% coulomb (voltage reliable at low SOC)
		 * - SOC >80%: 60% voltage + 40% coulomb (voltage reliable near full)
		 */
		bool need_calibrate = false;
		int calib_interval;
		int weight;  /* weight for voltage SOC, out of 100 */
		int blended_soc;
		int volt_soc_raw;

		/* Adaptive interval based on SOC region */
		if (raw_soc_from_coulomb >= 20 && raw_soc_from_coulomb <= 80)
			calib_interval = 1800;  /* 30 minutes for plateau */
		else
			calib_interval = 900;   /* 15 minutes for edges */

		if (gap_sec >= calib_interval)
			need_calibrate = true;

		/* Raw voltage SOC before any calibration */
		volt_soc_raw = soc;

		if (need_calibrate && soc >= 0 && soc <= 100) {
			/*
			 * Weighted blending: smooth transition instead of abrupt reset
			 * This prevents SOC jumps while still correcting drift
			 */
			if (raw_soc_from_coulomb >= 20 && raw_soc_from_coulomb <= 80) {
				/* Plateau region: low voltage weight */
				weight = 30;
			} else if (raw_soc_from_coulomb < 20) {
				/* Low SOC: voltage is reliable */
				weight = 70;
			} else {
				/* High SOC: voltage is reliable */
				weight = 60;
			}

			/* Weighted average: new_soc = (weight * volt + (100-weight) * coulomb) / 100 */
			blended_soc = (weight * soc +
				       (100 - weight) * raw_soc_from_coulomb) / 100;

			/* Clamp */
			if (blended_soc < 0) blended_soc = 0;
			if (blended_soc > 100) blended_soc = 100;

			/* Apply to coulomb counter for future tracking */
			battery->rsoc = blended_soc * 1000;
			battery->remain_cap = blended_soc * battery->fcc * 1000 / 100;
			rk817_bat_init_coulomb_cap(battery,
				blended_soc * battery->fcc / 100);
			battery->hybrid_internal_soc = blended_soc;

			DBG("hybrid: calib coulomb=%d%% volt=%d%% "
			    "weight=%d blended=%d%% gap=%lds v=%d\n",
			    raw_soc_from_coulomb, soc, weight,
			    blended_soc, gap_sec, raw_mv);
		} else {
			/* Normal discharge: use coulomb counter */
			battery->hybrid_internal_soc = raw_soc_from_coulomb;
		}

		/*
		 * Learn V_FULL_DIS after unplugging from full.
		 * Relaxed threshold: 3800mV (was 4000mV).
		 * Battery still has high voltage shortly after unplug.
		 *
		 * Also reset full_event if voltage drops below learning
		 * threshold — we missed the window, prevent stuck flag.
		 */
		if (battery->hybrid_full_event && !battery->hybrid_tracking_peak) {
			if (ema_mv >= 3800) {
				rk817_hybrid_learn_vfull_dis(ema_mv, battery);
				battery->hybrid_full_event = false;
			} else if (ema_mv < 3600) {
				/* Voltage dropped too far, clear flag */
				battery->hybrid_full_event = false;
			}
		}
	}

	/* Step 6: Clamp */
	if (battery->hybrid_internal_soc < 0)
		battery->hybrid_internal_soc = 0;
	if (battery->hybrid_internal_soc > 100)
		battery->hybrid_internal_soc = 100;

	/* Step 7: Cap at 99% during charging until full event */
	if (charging && !allow_100 && battery->hybrid_internal_soc > 99) {
		/*
		 * Additional conditions to allow 100% without waiting 15 min:
		 * 1. Current is very small (CV phase, near termination)
		 *    AND voltage is high enough
		 * 2. Timeout: stuck at 99% for too long with small current
		 */
		bool curr_small = (battery->current_avg >= 0 &&
				   battery->current_avg < HYBRID_FULL_CURR_THRESH_MA);
		bool volt_high = (ema_mv >= HYBRID_FULL_VOLT_THRESH_MV);

		if (curr_small && volt_high) {
			/* Current is small and voltage is high - battery is full */
			allow_100 = true;
			HYBRID_DBG("hybrid: allow 100%%, curr=%d v=%d\n",
				   battery->current_avg, ema_mv);
		} else if (curr_small && battery->hybrid_internal_soc >= 99) {
			/* Track how long we've been at 99% with small current */
			int64_t now = rk817_hybrid_boottime_sec();

			if (battery->hybrid_99_start_sec == 0)
				battery->hybrid_99_start_sec = now;

			if ((now - battery->hybrid_99_start_sec) >= HYBRID_FULL_TIMEOUT_SEC) {
				allow_100 = true;
				HYBRID_DBG("hybrid: allow 100%%, timeout %llds curr=%d\n",
					   now - battery->hybrid_99_start_sec,
					   battery->current_avg);
			}
		}

		if (!allow_100)
			battery->hybrid_internal_soc = 99;
	}

	/* Reset 99% timer when not at 99% */
	if (battery->hybrid_internal_soc < 99)
		battery->hybrid_99_start_sec = 0;

	/* Step 8: Step-limit for display */
	battery->hybrid_visible_soc = rk817_hybrid_step_limit(
		battery->hybrid_visible_soc,
		battery->hybrid_internal_soc,
		charging);

	/* Step 9: Sync to existing dsoc for power supply reporting */
	battery->dsoc = battery->hybrid_visible_soc * 1000;

	HYBRID_DBG("hybrid: result int=%d vis=%d dsoc=%d\n",
		 battery->hybrid_internal_soc,
		 battery->hybrid_visible_soc,
		 battery->dsoc / 1000);
}

static int rk817_bat_field_read(struct rk817_battery_device *battery,
				enum rk817_battery_fields field_id)
{
	int val;
	int ret;

	ret = regmap_field_read(battery->rmap_fields[field_id], &val);
	if (ret < 0)
		return ret;

	return val;
}

static int rk817_bat_field_write(struct rk817_battery_device *battery,
				 enum rk817_battery_fields field_id,
				 unsigned int val)
{
	return regmap_field_write(battery->rmap_fields[field_id], val);
}

/*cal_offset: current offset value*/
static int rk817_bat_get_coffset(struct rk817_battery_device *battery)
{
	int  coffset_value = 0;

	coffset_value |= rk817_bat_field_read(battery, CAL_OFFSET_H) << 8;
	coffset_value |= rk817_bat_field_read(battery, CAL_OFFSET_L);

	return coffset_value;
}

static void rk817_bat_set_coffset(struct rk817_battery_device *battery, int val)
{
	u8  buf = 0;

	buf = (val >> 8) & 0xff;
	rk817_bat_field_write(battery, CAL_OFFSET_H, buf);
	buf = (val >> 0) & 0xff;
	rk817_bat_field_write(battery, CAL_OFFSET_L, buf);
}

/* current offset value calculated */
static int rk817_bat_get_ioffset(struct rk817_battery_device *battery)
{
	int  ioffset_value = 0;

	ioffset_value |= rk817_bat_field_read(battery, IOFFSET_H) << 8;
	ioffset_value |= rk817_bat_field_read(battery, IOFFSET_L);

	return ioffset_value;
}

static void rk817_bat_current_calibration(struct rk817_battery_device *battery)
{
	int pwron_value, ioffset, cal_offset;

	pwron_value = rk817_bat_field_read(battery, PWRON_CUR_H) << 8;
	pwron_value |= rk817_bat_field_read(battery, PWRON_CUR_L);

	ioffset = rk817_bat_get_ioffset(battery);

	DBG("Caloffset: 0x%x\n", rk817_bat_get_coffset(battery));
	DBG("IOFFSET: 0x%x\n", ioffset);
	cal_offset = ioffset;

	rk817_bat_set_coffset(battery, cal_offset);
	DBG("Caloffset: 0x%x\n", rk817_bat_get_coffset(battery));
	DBG("pwron_cur: 0x%x\n", pwron_value);
}

static int rk817_bat_get_vaclib0(struct rk817_battery_device *battery)
{
	int vcalib_value = 0;

	vcalib_value |= rk817_bat_field_read(battery, VCALIB0_H) << 8;
	vcalib_value |= rk817_bat_field_read(battery, VCALIB0_L);

	return vcalib_value;
}

static int rk817_bat_get_vaclib1(struct rk817_battery_device *battery)
{
	int vcalib_value = 0;

	vcalib_value |= rk817_bat_field_read(battery, VCALIB1_H) << 8;
	vcalib_value |= rk817_bat_field_read(battery, VCALIB1_L);

	return vcalib_value;
}

static void rk817_bat_init_voltage_kb(struct rk817_battery_device *battery)
{
	int vcalib0, vcalib1, delta;

	vcalib0 = rk817_bat_get_vaclib0(battery);
	vcalib1 = rk817_bat_get_vaclib1(battery);
	delta = vcalib1 - vcalib0;

	if (delta == 0) {
		dev_err(battery->dev,
			"bad voltage calibration: vcalib0=%d vcalib1=%d identical, using defaults\n",
			vcalib0, vcalib1);
		battery->voltage_k = 1000;
		battery->voltage_b = 0;
		return;
	}

	if (battery->chip_id == RK809_ID) {
		battery->voltage_k = (1050 - 600) * 1000 / delta;
		battery->voltage_b = 1050 - (battery->voltage_k * vcalib1) / 1000;
	} else {
		battery->voltage_k = (4025 - 2300) * 1000 / delta;
		battery->voltage_b = 4025 - (battery->voltage_k * vcalib1) / 1000;
	}
}

static void rk817_bat_restart_relax(struct rk817_battery_device *battery)
{
	rk817_bat_field_write(battery, RELAX_VOL1_UPD, 0x00);
	rk817_bat_field_write(battery, RELAX_VOL2_UPD, 0x00);
}

static bool is_rk817_bat_relax_mode(struct rk817_battery_device *battery)
{
	u8 relax_sts, relax_vol1_upd, relax_vol2_upd;

	relax_sts = rk817_bat_field_read(battery, RELAX_STS);
	relax_vol1_upd = rk817_bat_field_read(battery, RELAX_VOL1_UPD);
	relax_vol2_upd = rk817_bat_field_read(battery, RELAX_VOL2_UPD);

	DBG("RELAX_STS: %d\n", relax_sts);
	DBG("RELAX_VOL1_UPD: %d\n", relax_vol1_upd);
	DBG("RELAX_VOL2_UPD: %d\n", relax_vol2_upd);
	if (relax_sts && relax_vol1_upd && relax_vol2_upd)
		return true;
	else
		return false;
}

static u16 rk817_bat_get_relax_vol1(struct rk817_battery_device *battery)
{
	u16 vol, val = 0;

	val = rk817_bat_field_read(battery, RELAX_VOL1_H) << 8;
	val |= rk817_bat_field_read(battery, RELAX_VOL1_L);
	vol = battery->voltage_k * val / 1000 + battery->voltage_b;

	return vol;
}

static u16 rk817_bat_get_relax_vol2(struct rk817_battery_device *battery)
{
	u16 vol, val = 0;

	val = rk817_bat_field_read(battery, RELAX_VOL2_H) << 8;
	val |= rk817_bat_field_read(battery, RELAX_VOL2_L);
	vol = battery->voltage_k * val / 1000 + battery->voltage_b;

	return vol;
}

static u16 rk817_bat_get_relax_voltage(struct rk817_battery_device *battery)
{
	u16 relax_vol1, relax_vol2;

	if (!is_rk817_bat_relax_mode(battery))
		return 0;

	relax_vol1 = rk817_bat_get_relax_vol1(battery);
	relax_vol2 = rk817_bat_get_relax_vol2(battery);

	return relax_vol1 > relax_vol2 ? relax_vol1 : relax_vol2;
}

static void rk817_bat_set_relax_sample(struct rk817_battery_device *battery)
{
	u8 buf;
	int enter_thres, filter_thres;
	struct battery_platform_data *pdata = battery->pdata;

	enter_thres = CURRENT_TO_ADC(pdata->sleep_enter_current,
				     battery->sample_res);
	filter_thres = CURRENT_TO_ADC(pdata->sleep_filter_current,
				      battery->sample_res);

	/* set relax enter and exit threshold */
	buf = (enter_thres >> 8) & 0xff;
	rk817_bat_field_write(battery, RELAX_THRE_H, buf);
	buf = enter_thres & 0xff;
	rk817_bat_field_write(battery, RELAX_THRE_L, buf);
	/* set sample current threshold */
	buf = (filter_thres >> 8) & 0xff;
	rk817_bat_field_write(battery, SLEEP_CON_SAMP_CUR_H, buf);
	buf = filter_thres & 0xff;
	rk817_bat_field_write(battery, SLEEP_CON_SAMP_CUR_L, buf);

	/* reset relax update state */
	rk817_bat_restart_relax(battery);
	DBG("<%s>. sleep_enter_current = %d, sleep_exit_current = %d\n",
	    __func__, pdata->sleep_enter_current, pdata->sleep_exit_current);
}

static int rk817_bat_get_ocv_voltage(struct rk817_battery_device *battery)
{
	int vol, val = 0, vol_temp;

	val = rk817_bat_field_read(battery, OCV_VOL_H) << 8;
	val |= rk817_bat_field_read(battery, OCV_VOL_L);
	vol = battery->voltage_k * val / 1000 + battery->voltage_b;

	if (battery->chip_id == RK809_ID) {
		vol_temp = vol * battery->pdata->bat_res_up /
			   battery->pdata->bat_res_down + vol;
		vol = vol_temp;
	}

	return vol;
}

static int rk817_bat_get_ocv0_voltage0(struct rk817_battery_device *battery)
{
	int vol, val = 0, vol_temp;

	val = rk817_bat_field_read(battery, OCV_VOL0_H) << 8;
	val |= rk817_bat_field_read(battery, OCV_VOL0_L);
	vol = battery->voltage_k * val / 1000 + battery->voltage_b;
	if (battery->chip_id == RK809_ID) {
		vol_temp = vol * battery->pdata->bat_res_up /
			   battery->pdata->bat_res_down + vol;
		vol = vol_temp;
	}

	return vol;
}

/* power on battery voltage */
static int rk817_bat_get_pwron_voltage(struct rk817_battery_device *battery)
{
	int vol, val = 0, vol_temp;

	val = rk817_bat_field_read(battery, PWRON_VOL_H) << 8;
	val |= rk817_bat_field_read(battery, PWRON_VOL_L);
	vol = battery->voltage_k * val / 1000 + battery->voltage_b;
	if (battery->chip_id == RK809_ID) {
		vol_temp = vol * battery->pdata->bat_res_up /
			   battery->pdata->bat_res_down + vol;
		vol = vol_temp;
	}

	return vol;
}

static int rk817_bat_get_battery_voltage(struct rk817_battery_device *battery)
{
	int vol, val = 0, vol_temp;

	val = rk817_bat_field_read(battery, BAT_VOL_H) << 8;
	val |= rk817_bat_field_read(battery, BAT_VOL_L) << 0;

	vol = battery->voltage_k * val / 1000 + battery->voltage_b;

	if (battery->chip_id == RK809_ID) {
		vol_temp = vol * battery->pdata->bat_res_up /
			   battery->pdata->bat_res_down + vol;
		vol = vol_temp;
	}

	return vol;
}

static int rk817_bat_get_USB_voltage(struct rk817_battery_device *battery)
{
	int vol, val = 0, vol_temp;

	rk817_bat_field_write(battery, USB_VOL_ADC_EN, 0x01);

	val = rk817_bat_field_read(battery, USB_VOL_H) << 8;
	val |= rk817_bat_field_read(battery, USB_VOL_L) << 0;

	vol = (battery->voltage_k * val / 1000 + battery->voltage_b) * 60 / 46;

	if (battery->chip_id == RK809_ID) {
		vol_temp = vol * battery->pdata->bat_res_up /
			   battery->pdata->bat_res_down + vol;
		vol = vol_temp;
	}

	return vol;
}

static int rk817_bat_get_sys_voltage(struct rk817_battery_device *battery)
{
	int vol, val = 0, vol_temp;

	val = rk817_bat_field_read(battery, SYS_VOL_H) << 8;
	val |= rk817_bat_field_read(battery, SYS_VOL_L) << 0;

	vol = (battery->voltage_k * val / 1000 + battery->voltage_b) * 60 / 46;

	if (battery->chip_id == RK809_ID) {
		vol_temp = vol * battery->pdata->bat_res_up /
			   battery->pdata->bat_res_down + vol;
		vol = vol_temp;
	}

	return vol;
}

static int rk817_bat_get_avg_current(struct rk817_battery_device *battery)
{
	int cur, val = 0;

	val = rk817_bat_field_read(battery, BAT_CUR_H) << 8;
	val |= rk817_bat_field_read(battery, BAT_CUR_L);

	if (val & 0x8000)
		val -= 0x10000;

	cur = ADC_TO_CURRENT(val, battery->sample_res);

	return cur;
}

static int rk817_bat_get_relax_cur1(struct rk817_battery_device *battery)
{
	int cur, val = 0;

	val = rk817_bat_field_read(battery, RELAX_CUR1_H) << 8;
	val |= rk817_bat_field_read(battery, RELAX_CUR1_L);

	if (val & 0x8000)
		val -= 0x10000;

	cur = ADC_TO_CURRENT(val, battery->sample_res);

	return cur;
}

static int rk817_bat_get_relax_cur2(struct rk817_battery_device *battery)
{
	int cur, val = 0;

	val = rk817_bat_field_read(battery, RELAX_CUR2_H) << 8;
	val |= rk817_bat_field_read(battery, RELAX_CUR2_L);

	if (val & 0x8000)
		val -= 0x10000;

	cur = ADC_TO_CURRENT(val, battery->sample_res);

	return cur;
}

static int rk817_bat_get_relax_current(struct rk817_battery_device *battery)
{
	int relax_cur1, relax_cur2;

	if (!is_rk817_bat_relax_mode(battery))
		return 0;

	relax_cur1 = rk817_bat_get_relax_cur1(battery);
	relax_cur2 = rk817_bat_get_relax_cur2(battery);

	return (relax_cur1 < relax_cur2) ? relax_cur1 : relax_cur2;
}

static int rk817_bat_get_ocv_current(struct rk817_battery_device *battery)
{
	int cur, val = 0;

	val = rk817_bat_field_read(battery, OCV_CUR_H) << 8;
	val |= rk817_bat_field_read(battery, OCV_CUR_L);

	if (val & 0x8000)
		val -= 0x10000;

	cur = ADC_TO_CURRENT(val, battery->sample_res);

	return cur;
}

static int rk817_bat_get_ocv_current0(struct rk817_battery_device *battery)
{
	int cur, val = 0;

	val = rk817_bat_field_read(battery, OCV_CUR0_H) << 8;
	val |= rk817_bat_field_read(battery, OCV_CUR0_L);

	if (val & 0x8000)
		val -= 0x10000;

	cur = ADC_TO_CURRENT(val, battery->sample_res);

	return cur;
}

static int rk817_bat_get_pwron_current(struct rk817_battery_device *battery)
{
	int cur, val = 0;

	val = rk817_bat_field_read(battery, PWRON_CUR_H) << 8;
	val |= rk817_bat_field_read(battery, PWRON_CUR_L);

	if (val & 0x8000)
		val -= 0x10000;
	cur = ADC_TO_CURRENT(val, battery->sample_res);

	return cur;
}

static bool rk817_bat_remain_cap_is_valid(struct rk817_battery_device *battery)
{
	return !(rk817_bat_field_read(battery, Q_PRESS_H3) & CAP_INVALID);
}

static u32 rk817_bat_get_capacity_uah(struct rk817_battery_device *battery)
{
	u32 val = 0, capacity = 0;

	if (rk817_bat_remain_cap_is_valid(battery)) {
		val = rk817_bat_field_read(battery, Q_PRESS_H3) << 24;
		val |= rk817_bat_field_read(battery, Q_PRESS_H2) << 16;
		val |= rk817_bat_field_read(battery, Q_PRESS_L1) << 8;
		val |= rk817_bat_field_read(battery, Q_PRESS_L0) << 0;

		capacity = ADC_TO_CAPACITY_UAH(val, battery->sample_res);
	}

	return  capacity;
}

static u32 rk817_bat_get_capacity_mah(struct rk817_battery_device *battery)
{
	u32 val, capacity = 0;

	if (rk817_bat_remain_cap_is_valid(battery)) {
		val = rk817_bat_field_read(battery, Q_PRESS_H3) << 24;
		val |= rk817_bat_field_read(battery, Q_PRESS_H2) << 16;
		val |= rk817_bat_field_read(battery, Q_PRESS_L1) << 8;
		val |= rk817_bat_field_read(battery, Q_PRESS_L0) << 0;

		capacity = ADC_TO_CAPACITY(val, battery->sample_res);
	}
	DBG("Q_PRESS_H3 = 0x%x\n", rk817_bat_field_read(battery, Q_PRESS_H3));
	DBG("Q_PRESS_H2 = 0x%x\n", rk817_bat_field_read(battery, Q_PRESS_H2));
	DBG("Q_PRESS_L1 = 0x%x\n", rk817_bat_field_read(battery, Q_PRESS_L1));
	DBG("Q_PRESS_L0 = 0x%x\n", rk817_bat_field_read(battery, Q_PRESS_L0));

	DBG("capacity = %d\n", capacity);
	return  capacity;
}

static void  fuel_gauge_q_init_info(struct rk817_battery_device *battery)
{
	DBG("Q_INIT_H3 = 0x%x\n", rk817_bat_field_read(battery, Q_INIT_H3));
	DBG("Q_INIT_H2 = 0x%x\n", rk817_bat_field_read(battery, Q_INIT_H2));
	DBG("Q_INIT_L1 = 0x%x\n", rk817_bat_field_read(battery, Q_INIT_L1));
	DBG("Q_INIT_L0 = 0x%x\n", rk817_bat_field_read(battery, Q_INIT_L0));
}

static void rk817_bat_init_coulomb_cap(struct rk817_battery_device *battery,
				       u32 capacity)
{
	u8 buf;
	u32 cap;
	int val;

	fuel_gauge_q_init_info(battery);
	cap = CAPACITY_TO_ADC(capacity, battery->sample_res);
	DBG("new cap: 0x%x\n", cap);
	buf = (cap >> 24) & 0xff;
	rk817_bat_field_write(battery, Q_INIT_H3, buf);
	buf = (cap >> 16) & 0xff;
	rk817_bat_field_write(battery, Q_INIT_H2, buf);
	buf = (cap >> 8) & 0xff;
	rk817_bat_field_write(battery, Q_INIT_L1, buf);
	buf = (cap >> 0) & 0xff;

	/*
	 * Force hardware to detect Q_INIT change by toggling L0 byte.
	 * If current value matches desired value, write an alternate
	 * to ensure the register value actually changes.
	 */
	val = rk817_bat_field_read(battery, Q_INIT_L0);
	if (val == buf)
		rk817_bat_field_write(battery, Q_INIT_L0,
				      (buf == 0xFF) ? (buf - 1) : (buf + 1));
	else
		rk817_bat_field_write(battery, Q_INIT_L0, buf);

	battery->rsoc = capacity * 1000 * 100 / DIV(battery->fcc);
	battery->remain_cap = capacity * 1000;
	DBG("new remaincap: %d\n", battery->remain_cap);
	fuel_gauge_q_init_info(battery);
}

static void rk817_bat_save_cap(struct rk817_battery_device *battery,
			       int capacity)
{
	u8 buf;

	if (capacity >= battery->qmax)
		capacity = battery->qmax;
	if (capacity <= 0)
		capacity = 0;
	if (battery->save_cap_old == capacity)
		return;

	battery->save_cap_old = capacity;
	buf = (capacity >> 16) & 0xff;
	rk817_bat_field_write(battery, REMAIN_CAP_REG2, buf);
	buf = (capacity >> 8) & 0xff;
	rk817_bat_field_write(battery, REMAIN_CAP_REG1, buf);
	buf = (capacity >> 0) & 0xff;
	rk817_bat_field_write(battery, REMAIN_CAP_REG0, buf);
}

static void rk817_bat_update_qmax(struct rk817_battery_device *battery,
				  u32 capacity)
{
	u8 buf;
	u32 cap_adc;

	cap_adc = CAPACITY_TO_ADC(capacity, battery->sample_res);
	buf = (cap_adc >> 24) & 0xff;
	rk817_bat_field_write(battery, Q_MAX_H3, buf);
	buf = (cap_adc >> 16) & 0xff;
	rk817_bat_field_write(battery, Q_MAX_H2, buf);
	buf = (cap_adc >> 8) & 0xff;
	rk817_bat_field_write(battery, Q_MAX_L1, buf);
	buf = (cap_adc >> 0) & 0xff;
	rk817_bat_field_write(battery, Q_MAX_L0, buf);
	battery->qmax = capacity;
}

static int rk817_bat_get_qmax(struct rk817_battery_device *battery)
{
	u32 capacity;
	int val = 0;

	val = rk817_bat_field_read(battery, Q_MAX_H3) << 24;
	val |= rk817_bat_field_read(battery, Q_MAX_H2) << 16;
	val |= rk817_bat_field_read(battery, Q_MAX_L1) << 8;
	val |= rk817_bat_field_read(battery, Q_MAX_L0) << 0;
	capacity = ADC_TO_CAPACITY(val, battery->sample_res);
	battery->qmax = capacity;
	return capacity;
}

static void rk817_bat_save_fcc(struct rk817_battery_device *battery, int  fcc)
{
	u8 buf;

	buf = (fcc >> 16) & 0xff;
	rk817_bat_field_write(battery, NEW_FCC_REG2, buf);
	buf = (fcc >> 8) & 0xff;
	rk817_bat_field_write(battery, NEW_FCC_REG1, buf);
	buf = (fcc >> 0) & 0xff;
	rk817_bat_field_write(battery, NEW_FCC_REG0, buf);
}

static int rk817_bat_get_fcc(struct rk817_battery_device *battery)
{
	u32 fcc = 0;

	fcc |= rk817_bat_field_read(battery, NEW_FCC_REG2) << 16;
	fcc |= rk817_bat_field_read(battery, NEW_FCC_REG1) << 8;
	fcc |= rk817_bat_field_read(battery, NEW_FCC_REG0) << 0;

	if (fcc < MIN_FCC) {
		DBG("invalid fcc(%d), use design cap\n", fcc);
		fcc = battery->pdata->design_capacity;
		rk817_bat_save_fcc(battery, fcc);
	} else if (fcc > battery->pdata->design_qmax) {
		DBG("invalid fcc(%d), use qmax\n", fcc);
		fcc = battery->pdata->design_qmax;
		rk817_bat_save_fcc(battery, fcc);
	}

	return fcc;
}

static int rk817_bat_get_rsoc(struct rk817_battery_device *battery)
{
	int remain_cap;

	remain_cap = rk817_bat_get_capacity_uah(battery);

	return remain_cap * 100 / DIV(battery->fcc);
}

static int rk817_bat_get_off_count(struct rk817_battery_device *battery)
{
	return rk817_bat_field_read(battery, OFF_CNT);
}

static int rk817_bat_get_ocv_count(struct rk817_battery_device *battery)
{
	return rk817_bat_field_read(battery, OCV_CNT);
}

static int rk817_bat_vol2soc(struct rk817_battery_device *battery,
			     int voltage)
{
	u32 *ocv_table, temp;
	int ocv_size, ocv_soc;

	ocv_table = battery->pdata->ocv_table;
	ocv_size = battery->pdata->ocv_size;
	temp = interpolate(voltage, ocv_table, ocv_size);
	ocv_soc = ab_div_c(temp, MAX_PERCENTAGE, MAX_INTERPOLATE);

	return ocv_soc;
}

static int rk817_bat_vol2cap(struct rk817_battery_device *battery,
			     int voltage)
{
	u32 *ocv_table, temp;
	int ocv_size, capacity;

	ocv_table = battery->pdata->ocv_table;
	ocv_size = battery->pdata->ocv_size;
	temp = interpolate(voltage, ocv_table, ocv_size);
	capacity = ab_div_c(temp, battery->fcc, MAX_INTERPOLATE);

	return capacity;
}

static void rk817_bat_save_dsoc(struct rk817_battery_device *battery,
				int save_soc)
{
	if (battery->save_dsoc_last != save_soc) {
		rk817_bat_field_write(battery, SOC_REG0,
				      save_soc & 0xff);
		rk817_bat_field_write(battery, SOC_REG1,
				      (save_soc >> 8) & 0xff);
		rk817_bat_field_write(battery, SOC_REG2,
				      (save_soc >> 16) & 0xff);

		battery->save_dsoc_last = save_soc;
	}
}

static int rk817_bat_get_prev_dsoc(struct rk817_battery_device *battery)
{
	int soc_save;

	soc_save = rk817_bat_field_read(battery, SOC_REG0);
	soc_save |= (rk817_bat_field_read(battery, SOC_REG1) << 8);
	soc_save |= (rk817_bat_field_read(battery, SOC_REG2) << 16);

	return soc_save;
}

static bool is_rk817_bat_first_pwron(struct rk817_battery_device *battery)
{
	if (rk817_bat_field_read(battery, BAT_CON)) {
		rk817_bat_field_write(battery, BAT_CON, 0x00);
		return true;
	}

	return false;
}

static int rk817_bat_get_charge_status(struct rk817_battery_device *battery)
{
	int status;
	int raw_sts, plug_sts, bat_exs, chrg_cur_sel, chrg_vol_sel;

	if (battery->chip_id == RK809_ID) {
		if ((battery->voltage_avg > battery->pdata->design_max_voltage) &&
		    (battery->current_avg > 0) &&
		    ((battery->current_avg < 500) || (battery->rsoc / 1000 == 100)))
			return CHARGE_FINISH;

		if (battery->plugin_trigger)
			return CC_OR_CV_CHRG;
		else
			return CHRG_OFF;
	}
	raw_sts = rk817_bat_field_read(battery, CHG_STS);
	plug_sts = rk817_bat_field_read(battery, PLUG_IN_STS);
	bat_exs = rk817_bat_field_read(battery, BAT_EXS);
	chrg_cur_sel = rk817_bat_field_read(battery, CHRG_CUR_SEL);
	chrg_vol_sel = rk817_bat_field_read(battery, CHRG_VOL_SEL);
	status = raw_sts;

	/*
	 * Charger IC may not report status correctly (CHG_STS=0, PLUG_IN_STS=0)
	 * even when charging is actually happening. Fall back to current detection:
	 * positive current with charger configured (cur_sel/vol_sel != 0) = charging.
	 */
	if (status == CHRG_OFF && plug_sts == 0 &&
	    chrg_cur_sel != 0 && chrg_vol_sel != 0 &&
	    battery->current_avg > 100) {
		status = CC_OR_CV_CHRG;
	}

	/* Log only on state transition */
	{
		static int last_fallback;
		int now_fallback = (status == CC_OR_CV_CHRG && raw_sts == 0);
		if (now_fallback != last_fallback) {
			HYBRID_DBG("charge_sts: fallback %s, c=%d raw=%d\n",
				 now_fallback ? "enter" : "exit",
				 battery->current_avg, raw_sts);
			last_fallback = now_fallback;
		}
	}

	HYBRID_DBG("charge_sts: raw=%d plug=%d bat_exs=%d cur_sel=%d vol_sel=%d v=%d c=%d\n",
		 raw_sts, plug_sts, bat_exs, chrg_cur_sel, chrg_vol_sel,
		 battery->voltage_avg, battery->current_avg);

	if (status == CC_OR_CV_CHRG) {
		if (battery->rsoc >= 100) {
			DBG("charge to finish\n");
			status = CHARGE_FINISH;
		}
	}

	switch (status) {
	case CHRG_OFF:
		DBG("charge off...\n");
		break;
	case DEAD_CHRG:
		DBG("dead charge...\n");
		break;
	case TRICKLE_CHRG:
		DBG("trickle charge...\n");
		break;
	case CC_OR_CV_CHRG:
		DBG("CC or CV charge...\n");
		break;
	case CHARGE_FINISH:
		DBG("charge finish...\n");
		break;
	case USB_OVER_VOL:
		DBG("USB over voltage...\n");
		break;
	case BAT_TMP_ERR:
		DBG("battery temperature error...\n");
		break;
	case BAT_TIM_ERR:
		DBG("battery timer error..\n");
		break;
	default:
		break;
	}

	return status;
}

static int rk817_bat_get_charge_state(struct rk817_battery_device *battery)
{
	int status;

	status = rk817_bat_get_charge_status(battery);
	return (status == CC_OR_CV_CHRG || status == TRICKLE_CHRG);
}

static void rk817_bat_update_fcc(struct rk817_battery_device *battery)
{
	int temp_fcc = 0;

	if (battery->fcc_update_done)
		return;

	/* The conditions to update FCC are:
	 * during discharging state,
	 * with temperature above 18°C and
	 * displayed battery level below 1%
	 */
	if ((battery->chrg_status != CHRG_OFF) || (battery->dsoc > 1000) ||
		(battery->temperature < VIRTUAL_TEMPERATURE))
		return;

	/*
	 * Update the FCC to 99.5% of its original value when the aforementioned
	 * basic conditions are met ‌and‌ the loaded voltage falls below
	 * the DTS-configured shutdown voltage.
	 */
	if ((battery->voltage_avg <= battery->pdata->pwroff_vol) &&
	    (battery->rsoc > BATTERY_PERCENTAGE(1))) {
		temp_fcc = UPDATE_REDUCE_FCC(battery->fcc);
		if (temp_fcc > EFFECTIVE_FULL_MIN_CAP(battery->pdata->design_capacity)) {
			DBG("REDUCE: update fcc: design: %d, old: %d, new: %d\n",
			    battery->pdata->design_capacity, battery->fcc, temp_fcc);
			battery->qmax = temp_fcc;
			battery->fcc = temp_fcc;
			rk817_bat_update_qmax(battery, battery->qmax);
			rk817_bat_save_fcc(battery, battery->fcc);
			battery->fcc_update_done = true;
		}
	}

	/* Update the FCC to 100.5% of its original value when the following conditions are met:
	 *
	 * The aforementioned basic conditions are satisfied
	 * (e.g., discharging state, temperature >18°C).
	 * The loaded voltage exceeds the voltage corresponding to 5% SOC in the OCV table.
	 * The RSOC (Relative State of Charge) is below 5%.
	 */
	if ((battery->voltage_avg >= battery->pdata->ocv_table[1]) &&
	    (battery->rsoc < BATTERY_PERCENTAGE(5))) {
		temp_fcc = UPDATE_RAISE_FCC(battery->fcc);
		if (temp_fcc < EFFECTIVE_FULL_MAX_CAP(battery->pdata->design_capacity)) {
			DBG("RAISE fcc: design: %d, old: %d, new: %d\n",
			    battery->pdata->design_capacity, battery->fcc, temp_fcc);
			battery->qmax = temp_fcc;
			battery->fcc = temp_fcc;
			rk817_bat_update_qmax(battery, battery->qmax);
			rk817_bat_save_fcc(battery, battery->fcc);
			battery->fcc_update_done = true;
		}
	}
}

static void rk817_bat_enable_usb2vsys(struct rk817_battery_device *battery)
{
	DBG("enable usb2vsys!!!\n");
	rk817_bat_field_write(battery, USB_SYS_EN, 1);
}

static void rk817_bat_disable_usb2vsys(struct rk817_battery_device *battery)
{
	DBG("disable usb2vsys!!!\n");
	rk817_bat_field_write(battery, USB_SYS_EN, 0);
}

static void rk817_bat_enable_charge(struct rk817_battery_device *battery)
{
	DBG("enable charge by BAT_LTS_TS: 0xFA\n");
	rk817_bat_field_write(battery, BAT_LTS_TS, 0xFA);
}

static void rk817_bat_disable_charge(struct rk817_battery_device *battery)
{
	DBG("disable charge by BAT_LTS_TS: 0x05\n");
	rk817_bat_field_write(battery, BAT_LTS_TS, 0x05);
}

static void rk817_bat_init_ts_detect(struct rk817_battery_device *battery)
{
	if (!battery->pdata->ntc_size)
		return;

	/* the adc of ts1 controlled bit: enable */
	rk817_bat_field_write(battery, TS_ADC_EN, ENABLE);
	/* source current to TS pin */
	rk817_bat_field_write(battery, TS_FUN, TS_FUN_SOURCE_CURRENT);
	/* ts pin flow out current in active state */
	rk817_bat_field_write(battery, VOL_ADC_TSCUR_SEL, FLOW_OUT_20uA);

	battery->pdata->ntc_factor = (FLOW_OUT_20uA + 1) * 10;
	rk817_bat_enable_charge(battery);
}

static void rk817_bat_temperature_chrg(struct rk817_battery_device *battery, int temp)
{
	int i, up_temp, down_temp;
	int now_temp = temp;

	for (i = 0; i < battery->pdata->tc_count; i++) {
		up_temp = battery->pdata->tc_table[i].temp_up;
		down_temp = battery->pdata->tc_table[i].temp_down;

		if (now_temp >= down_temp && now_temp <= up_temp) {
			/* Temp range or charger are not update, return */
			if (battery->charge_index == i)
				return;

			if ((battery->pdata->tc_table[i].chrg_current != 0) &&
			    (battery->pdata->tc_table[i].chrg_current_index != 0xff)) {
				rk817_bat_field_write(battery,
						      CHRG_CUR_SEL,
						      battery->pdata->tc_table[i].chrg_current_index);
				DBG("T change: charger current: %d, index: %d\n",
				    battery->pdata->tc_table[i].chrg_current,
				    battery->pdata->tc_table[i].chrg_current_index);
			} else
				rk817_bat_disable_charge(battery);

			if ((battery->pdata->tc_table[i].chrg_voltage != 0) &&
				(battery->pdata->tc_table[i].chrg_voltage_index != 0xff)) {
				rk817_bat_disable_usb2vsys(battery);
				rk817_bat_field_write(battery,
						      CHRG_VOL_SEL,
						      battery->pdata->tc_table[i].chrg_voltage_index);
				rk817_bat_enable_usb2vsys(battery);
				DBG("T change: charger voltage: %d, index: %d\n",
				    battery->pdata->tc_table[i].chrg_voltage,
				    battery->pdata->tc_table[i].chrg_voltage_index);
			} else
				rk817_bat_enable_charge(battery);

			battery->charge_index = i;
		}
	}
}

static int rk817_bat_get_bat_ts(struct rk817_battery_device *battery)
{
	int temp_value = 0;

	temp_value = rk817_bat_field_read(battery, BAT_TS_H) << 8;
	temp_value |= rk817_bat_field_read(battery, BAT_TS_L);

	return temp_value;
}

static int rk817_bat_get_nts_res(struct rk817_battery_device *battery)
{
	int temp_value, res;
	int adc_to_vol;

	temp_value = rk817_bat_get_bat_ts(battery);
	adc_to_vol = temp_value * 1200 / 65536;

	res = adc_to_vol * 1000 / battery->pdata->ntc_factor;

	DBG("NTC: ADC: value: 0x%x, adc2vol:%d, res: %d\n",
	    temp_value, adc_to_vol, res);

	return res;
}

static void rk817_bat_update_temperature(struct rk817_battery_device *battery)
{
	u32 ntc_size, *ntc_table;
	int i, res;

	ntc_table = battery->pdata->ntc_table;
	ntc_size = battery->pdata->ntc_size;

	if (ntc_size) {
		res = rk817_bat_get_nts_res(battery);
		if (res == 0)
			return;

		if (res < ntc_table[ntc_size - 1]) {
			battery->temperature = (ntc_size + battery->pdata->ntc_degree_from) * 10;
			DBG("bat ntc upper max degree: R=%d\n", res);
		} else if (res > ntc_table[0]) {
			battery->temperature = battery->pdata->ntc_degree_from * 10;
			DBG("bat ntc lower min degree: R=%d\n", res);
		} else {
			for (i = 0; i < ntc_size; i++) {
				if (res >= ntc_table[i])
					break;
			}

			if (i <= 0)
				battery->temperature =
					(battery->pdata->ntc_degree_from) * 10;
			else
				battery->temperature =
					(i + battery->pdata->ntc_degree_from) * 10;
		}
		DBG("Temperature: %d\n", battery->temperature);
		rk817_bat_temperature_chrg(battery, battery->temperature / 10);
	}
}

/*
 * cccv and finish switch all the time will cause dsoc freeze,
 * if so, do finish chrg, 100ma is less than min finish_ma.
 */
static void rk817_bat_gas_gaugle_enable(struct rk817_battery_device *battery)
{
	rk817_bat_field_write(battery, GG_EN, ENABLE);
}

static void rk817_bat_gg_con_init(struct rk817_battery_device *battery)
{
	rk817_bat_field_write(battery, RLX_SPT, S_8_MIN);
	rk817_bat_field_write(battery, ADC_OFF_CAL_INTERV, S_8_MIN);
	rk817_bat_field_write(battery, VOL_OUT_MOD, AVERAGE_MODE);
	rk817_bat_field_write(battery, CUR_OUT_MOD, AVERAGE_MODE);
}

static void rk817_bat_adc_init(struct rk817_battery_device *battery)
{
	rk817_bat_field_write(battery, SYS_VOL_ADC_EN, ENABLE);
	rk817_bat_field_write(battery, TS_ADC_EN, ENABLE);
	rk817_bat_field_write(battery, USB_VOL_ADC_EN, ENABLE);
	rk817_bat_field_write(battery, BAT_VOL_ADC_EN, ENABLE);
	rk817_bat_field_write(battery, BAT_CUR_ADC_EN, ENABLE);
}

static void rk817_bat_init_info(struct rk817_battery_device *battery)
{
	battery->design_cap = battery->pdata->design_capacity;
	battery->qmax = battery->pdata->design_qmax;
	battery->bat_res = battery->pdata->bat_res;
	battery->monitor_ms = battery->pdata->monitor_sec * TIMER_MS_COUNTS;
	battery->sample_res = battery->pdata->sample_res;
	battery->fake_full_soc = battery->pdata->fake_full_soc * 1000;
	DBG("battery->qmax :%d\n", battery->qmax);
}

static int rk817_bat_get_prev_cap(struct rk817_battery_device *battery)
{
	int val = 0;

	val = rk817_bat_field_read(battery, REMAIN_CAP_REG2) << 16;
	val |= rk817_bat_field_read(battery, REMAIN_CAP_REG1) << 8;
	val |= rk817_bat_field_read(battery, REMAIN_CAP_REG0) << 0;

	return val;
}

static void rk817_bat_first_pwron(struct rk817_battery_device *battery)
{
	battery->rsoc =
		rk817_bat_vol2soc(battery,
				  battery->pwron_voltage);/* uAH */
	battery->dsoc = battery->rsoc;
	battery->fcc = battery->pdata->design_capacity;
	if (battery->fcc < MIN_FCC)
		battery->fcc = MIN_FCC;

	battery->nac = rk817_bat_vol2cap(battery, battery->pwron_voltage);

	rk817_bat_update_qmax(battery, battery->qmax);
	rk817_bat_save_fcc(battery, battery->fcc);
	DBG("%s, rsoc = %d, dsoc = %d, fcc = %d, nac = %d\n",
	    __func__, battery->rsoc, battery->dsoc, battery->fcc, battery->nac);
}

static void rk817_bat_not_first_pwron(struct rk817_battery_device *battery)
{
	int pre_soc, pre_cap;

	battery->fcc = rk817_bat_get_fcc(battery);
	pre_soc = rk817_bat_get_prev_dsoc(battery);
	pre_cap = rk817_bat_get_prev_cap(battery);
	battery->remain_cap = pre_cap * 1000;

	battery->dsoc = pre_soc;
	battery->nac = pre_cap;
	if (battery->nac < 0)
		battery->nac = 0;

	DBG("dsoc=%d cap=%d v=%d ov=%d rv=%d min=%d psoc=%d pcap=%d\n",
	    battery->dsoc, battery->nac, rk817_bat_get_battery_voltage(battery),
	    rk817_bat_get_ocv_voltage(battery),
	    rk817_bat_get_relax_voltage(battery),
	    battery->pwroff_min, rk817_bat_get_prev_dsoc(battery),
	    rk817_bat_get_prev_cap(battery));
}

static void rk817_bat_rsoc_init(struct rk817_battery_device *battery)
{
	battery->is_first_power_on = is_rk817_bat_first_pwron(battery);
	battery->pwroff_min = rk817_bat_get_off_count(battery);
	battery->pwron_voltage = rk817_bat_get_pwron_voltage(battery);

	DBG("%s, is_first_power_on = %d, pwroff_min = %d, pwron_voltage = %d\n",
	    __func__, battery->is_first_power_on,
	    battery->pwroff_min, battery->pwron_voltage);

	if (battery->is_first_power_on)
		rk817_bat_first_pwron(battery);
	else
		rk817_bat_not_first_pwron(battery);

	rk817_bat_save_dsoc(battery, battery->dsoc);
}

static void rk817_bat_caltimer_isr(struct timer_list *t)
{
	struct rk817_battery_device *battery =
		from_timer(battery, t, caltimer);

	mod_timer(&battery->caltimer, jiffies + MINUTE(8) * HZ);
	queue_delayed_work(battery->bat_monitor_wq,
			   &battery->calib_delay_work,
			   msecs_to_jiffies(10));
}

static void rk817_bat_internal_calib(struct work_struct *work)
{
	struct rk817_battery_device *battery = container_of(work,
			struct rk817_battery_device, calib_delay_work.work);

	rk817_bat_current_calibration(battery);
	/* calib voltage kb */
	rk817_bat_init_voltage_kb(battery);

	DBG("caltimer:coffset=0x%x\n", rk817_bat_get_coffset(battery));
}

static void rk817_bat_init_caltimer(struct rk817_battery_device *battery)
{
	timer_setup(&battery->caltimer,
		    rk817_bat_caltimer_isr,
		    0);
	battery->caltimer.expires = jiffies + MINUTE(8) * HZ;
	add_timer(&battery->caltimer);
	INIT_DELAYED_WORK(&battery->calib_delay_work, rk817_bat_internal_calib);
}

/* ---------- Low-battery GPIO helpers ---------- */
static bool rk817_bat_low_gpio_available(struct rk817_battery_device *battery)
{
	if (battery->bat_low.desc)
		return true;
	/* legacy path: valid gpio number requested below */
	return gpio_is_valid(battery->bat_low.legacy_gpio);
}

static void rk817_bat_low_gpio_set(struct rk817_battery_device *battery, int on)
{
	if (!rk817_bat_low_gpio_available(battery))
		return;

	if (battery->bat_low.desc) {
		gpiod_set_value_cansleep(battery->bat_low.desc, on ? 1 : 0);
	} else {
		/* legacy integer gpio path */
		gpio_direction_output(battery->bat_low.legacy_gpio, on ? 1 : 0);
	}
}

/*
 * Rule: turn ON when
 *   - dsoc <= threshold%  OR  voltage <= (pwroff_thresd + 50mV)
 * and NOT charging.
 * Turn OFF otherwise.
 * Note: When charging, do not control LED to avoid conflict with charger driver.
 */
static void rk817_bat_update_batlow_gpio(struct rk817_battery_device *battery)
{
	int dsoc_pct, on = 0;
	int is_charging = rk817_bat_get_charge_state(battery);
	int pwroff = battery->pdata->pwroff_vol;

	if (!rk817_bat_low_gpio_available(battery))
		return;


    /*
    * When charging, skip low-battery LED control to avoid conflict
    * with charger driver's chg_led_gpio. The charger driver will
    * control the LED to indicate charging status.
    */
	if (is_charging)
		return;

	dsoc_pct = (battery->dsoc + 500) / 1000;
	if ((dsoc_pct <= battery->bat_low_threshold) ||
	    (battery->voltage_avg <= pwroff + 50)) {
		on = 1;
	}
	rk817_bat_low_gpio_set(battery, on);
}

static void rk817_bat_init_batlow_from_dt(struct rk817_battery_device *battery)
{
	struct device *dev = battery->dev;
	struct device_node *np = dev->of_node;
	u32 thres = RK817_BAT_LOW_DEFAULT_THRES;
	int gpio;

	/* threshold in percent (0~100), default 5% */
	of_property_read_u32(np, "bat-low-threshold", &thres);
	if (thres > 100)
		thres = 100;
	battery->bat_low_threshold = thres;

	/* preferred: descriptor-based */
	battery->bat_low.desc = devm_gpiod_get_optional(dev, "bat-low", GPIOD_OUT_LOW);
	if (IS_ERR(battery->bat_low.desc)) {
		dev_warn(dev, "bat-low-gpios invalid, fallback to legacy\n");
		battery->bat_low.desc = NULL;
	}

	/* fallback: legacy "bat_low_gpio" */
	battery->bat_low.legacy_gpio = -EINVAL;
	if (!battery->bat_low.desc && of_find_property(np, "bat_low_gpio", NULL)) {
		gpio = of_get_named_gpio(np, "bat_low_gpio", 0);
		if (gpio_is_valid(gpio) && !devm_gpio_request_one(dev, gpio, GPIOF_OUT_INIT_LOW, "bat_low_gpio"))
			battery->bat_low.legacy_gpio = gpio;
	}
}

static void rk817_bat_init_fg(struct rk817_battery_device *battery)
{
	rk817_bat_adc_init(battery);
	rk817_bat_gas_gaugle_enable(battery);
	rk817_bat_gg_con_init(battery);
	rk817_bat_init_voltage_kb(battery);
	rk817_bat_set_relax_sample(battery);
	rk817_bat_init_caltimer(battery);
	rk817_bat_rsoc_init(battery);
	rk817_bat_init_coulomb_cap(battery, battery->nac);
	rk817_bat_init_ts_detect(battery);
	DBG("rsoc%d, fcc = %d\n", battery->rsoc, battery->fcc);
	battery->qmax = rk817_bat_get_qmax(battery);
	battery->voltage_avg = rk817_bat_get_battery_voltage(battery);
	battery->voltage_sys = rk817_bat_get_sys_voltage(battery);

	battery->voltage_ocv = rk817_bat_get_ocv_voltage(battery);
	battery->voltage_relax = rk817_bat_get_relax_voltage(battery);
	battery->current_avg = rk817_bat_get_avg_current(battery);
	battery->temperature = VIRTUAL_TEMPERATURE;

	DBG("probe init: battery->dsoc = %d, rsoc = %d, remain_cap = %d, bat_vol = %d, sys_vol = %d, qmax = %d\n",
	    battery->dsoc, battery->rsoc, battery->remain_cap,
	    battery->voltage_avg, battery->voltage_sys, battery->qmax);
}

static u8 rk817_bat_decode_chrg_voltage(u32 chrg_voltage)
{
	if (chrg_voltage == 0)
		return 0xff;

	if (chrg_voltage < 4150)
		return CHRG_VOL_4100MV;
	else if (chrg_voltage < 4200)
		return CHRG_VOL_4150MV;
	else if (chrg_voltage < 4250)
		return CHRG_VOL_4200MV;
	else if (chrg_voltage < 4300)
		return CHRG_VOL_4250MV;
	else if (chrg_voltage < 4350)
		return CHRG_VOL_4300MV;
	else if (chrg_voltage < 4400)
		return CHRG_VOL_4350MV;
	else if (chrg_voltage < 4450)
		return CHRG_VOL_4400MV;
	else
		return CHRG_VOL_4450MV;
}

static u8 rk817_bat_decode_chrg_current(struct rk817_battery_device *battery,
					u32 chrg_current)
{
	int val;

	if (chrg_current == 0)
		return 0xff;

	val = chrg_current * battery->pdata->sample_res / 10;
	if (val < 1000)
		return CHRG_CUR_500MA;
	else if (val < 1500)
		return CHRG_CUR_1000MA;
	else if (val < 2000)
		return CHRG_CUR_1500MA;
	else if (val < 2500)
		return CHRG_CUR_2000MA;
	else if (val < 2750)
		return CHRG_CUR_2500MA;
	else if (val < 3000)
		return CHRG_CUR_2750MA;
	else if (val < 3500)
		return CHRG_CUR_3000MA;
	else
		return CHRG_CUR_3500MA;
}

static int parse_temperature_chrg_table(struct rk817_battery_device *battery,
					struct device_node *np)
{
	int size, count;
	int i, chrg_current, chrg_voltage;
	const __be32 *list;

	if (!of_find_property(np, "temperature_chrg_table", &size))
		return 0;

	list = of_get_property(np, "temperature_chrg_table", &size);
	size /= sizeof(u32);
	if (!size || (size % 4)) {
		dev_err(battery->dev,
			"invalid temperature_chrg_table: size=%d (must be multiple of 4)\n", size);
		return -EINVAL;
	}

	count = size / 4;
	battery->pdata->tc_count = count;
	battery->pdata->tc_table = devm_kzalloc(battery->dev,
						count * sizeof(*battery->pdata->tc_table),
						GFP_KERNEL);
	if (!battery->pdata->tc_table)
		return -ENOMEM;

	for (i = 0; i < count; i++) {
		/* temperature */
		battery->pdata->tc_table[i].temp_down = be32_to_cpu(*list++);
		battery->pdata->tc_table[i].temp_up = be32_to_cpu(*list++);
		chrg_current = be32_to_cpu(*list++);
		chrg_voltage = be32_to_cpu(*list++);

		/*
		 * because charge current lowest level is 500mA:
		 * higher than or equal 1000ma, select charge current;
		 * lower than 500ma, must select input current.
		 */
		if (chrg_current >= 500) {
			battery->pdata->tc_table[i].chrg_current = chrg_current;
			battery->pdata->tc_table[i].chrg_current_index = rk817_bat_decode_chrg_current(battery, chrg_current);
		} else {
			battery->pdata->tc_table[i].chrg_current = 0;
		}

		if (chrg_voltage >= 4100) {
			battery->pdata->tc_table[i].chrg_voltage = chrg_voltage;
			battery->pdata->tc_table[i].chrg_voltage_index = rk817_bat_decode_chrg_voltage(chrg_voltage);
		} else {
			battery->pdata->tc_table[i].chrg_voltage = 0;
		}
		DBG("temp%d: [%d, %d], chrg_current=%d, current_index: %d, chrg_voltage: %d, voltage_index: %d\n",
		    i, battery->pdata->tc_table[i].temp_down,
		    battery->pdata->tc_table[i].temp_up,
		    battery->pdata->tc_table[i].chrg_current,
		    battery->pdata->tc_table[i].chrg_current_index,
		    battery->pdata->tc_table[i].chrg_voltage,
		    battery->pdata->tc_table[i].chrg_voltage_index);
	}

	return 0;
}

static int rk817_bat_parse_dt(struct rk817_battery_device *battery)
{
	u32 out_value;
	int length, ret;
	size_t size;
	struct battery_platform_data *pdata;
	struct device *dev = battery->dev;
	struct device_node *np = battery->dev->of_node;

	pdata = devm_kzalloc(battery->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	battery->pdata = pdata;
	/* init default param */
	pdata->bat_res = DEFAULT_BAT_RES;
	pdata->monitor_sec = DEFAULT_MONITOR_SEC;
	pdata->pwroff_vol = DEFAULT_PWROFF_VOL_THRESD;
	pdata->sleep_exit_current = DEFAULT_SLP_EXIT_CUR;
	pdata->sleep_enter_current = DEFAULT_SLP_ENTER_CUR;

	pdata->sleep_filter_current = DEFAULT_SLP_FILTER_CUR;
	pdata->bat_mode = MODE_BATTARY;
	pdata->fake_full_soc = 100;
	pdata->sample_res = DEFAULT_SAMPLE_RES;
	pdata->charge_stay_awake = 0;

	/* parse necessary param */
	if (!of_find_property(np, "ocv_table", &length)) {
		dev_err(dev, "ocv_table not found!\n");
		return -EINVAL;
	}

	pdata->ocv_size = length / sizeof(u32);
	if (pdata->ocv_size < 2) {
		dev_err(dev, "invalid ocv table\n");
		return -EINVAL;
	}

	size = sizeof(*pdata->ocv_table) * pdata->ocv_size;
	pdata->ocv_table = devm_kzalloc(battery->dev, size, GFP_KERNEL);
	if (!pdata->ocv_table)
		return -ENOMEM;

	ret = of_property_read_u32_array(np, "ocv_table", pdata->ocv_table,
					 pdata->ocv_size);
	if (ret < 0)
		return ret;

	ret = of_property_read_u32(np, "design_capacity", &out_value);
	if (ret < 0) {
		dev_err(dev, "design_capacity not found!\n");
		return ret;
	}
	pdata->design_capacity = out_value;

	ret = of_property_read_u32(np, "design_qmax", &out_value);
	if (ret < 0) {
		dev_err(dev, "design_qmax not found!\n");
		return ret;
	}
	pdata->design_qmax = out_value;

	/* parse unnecessary param */
	ret = of_property_read_u32(np, "sample_res", &pdata->sample_res);
	if (ret < 0)
		dev_err(dev, "sample_res missing!\n");

	ret = of_property_read_u32(np, "monitor_sec", &pdata->monitor_sec);
	if (ret < 0)
		dev_err(dev, "monitor_sec missing!\n");

	ret = of_property_read_u32(np, "virtual_power", &pdata->bat_mode);
	if (ret < 0)
		dev_err(dev, "virtual_power missing!\n");

	ret = of_property_read_u32(np, "bat_res", &pdata->bat_res);
	if (ret < 0)
		dev_err(dev, "bat_res missing!\n");

	ret = of_property_read_u32(np, "sleep_enter_current",
				   &pdata->sleep_enter_current);
	if (ret < 0)
		dev_err(dev, "sleep_enter_current missing!\n");

	ret = of_property_read_u32(np, "sleep_exit_current",
				   &pdata->sleep_exit_current);
	if (ret < 0)
		dev_err(dev, "sleep_exit_current missing!\n");

	ret = of_property_read_u32(np, "sleep_filter_current",
				   &pdata->sleep_filter_current);
	if (ret < 0)
		dev_err(dev, "sleep_filter_current missing!\n");

	ret = of_property_read_u32(np, "power_off_thresd", &pdata->pwroff_vol);
	if (ret < 0)
		dev_err(dev, "power_off_thresd missing!\n");

	ret = of_property_read_u32(np, "charge_stay_awake", &pdata->charge_stay_awake);
	if (ret < 0)
		dev_info(dev, "charge_stay_awake missing!\n");

	ret = of_property_read_u32(np, "fake_full_soc", &pdata->fake_full_soc);
	if (ret < 0)
		dev_info(dev, "fake_full_soc missing!\n");
	else {
		if (pdata->fake_full_soc > 100)
			pdata->fake_full_soc = 100;
	}

	if (battery->chip_id == RK809_ID) {
		ret = of_property_read_u32(np, "bat_res_up",
					   &pdata->bat_res_up);
		if (ret < 0) {
			dev_err(dev, "battery res_up missing\n");
			pdata->bat_res_up = 1;
		}

		ret = of_property_read_u32(np, "bat_res_down",
					   &pdata->bat_res_down);
		if (ret < 0) {
			dev_err(dev, "battery res_down missing!\n");
			pdata->bat_res_down = 1;
		}

		ret = of_property_read_u32(np, "design_max_voltage",
					   &pdata->design_max_voltage);
		if (ret < 0) {
			dev_err(dev, "battery design_max_voltage missing!\n");
			pdata->design_max_voltage = pdata->ocv_table[pdata->ocv_size - 1];
		}
		ret = of_property_read_u32(np, "register_chg_psy",
					   &battery->is_register_chg_psy);
		if (ret < 0 || !battery->is_register_chg_psy)
			dev_err(dev, "not have to register chg psy!\n");
	}

	if (!of_find_property(np, "ntc_table", &length)) {
		pdata->ntc_size = 0;
		battery->temperature = VIRTUAL_TEMPERATURE;
	} else {
		/* get ntc degree base value */
		ret = of_property_read_u32_index(np, "ntc_degree_from", 1,
				&pdata->ntc_degree_from);
		if (ret) {
			dev_err(dev, "invalid ntc_degree_from\n");
			return -EINVAL;
		}

		ret = of_property_read_u32_index(np, "ntc_degree_from", 0,
						 &out_value);
		if (ret) {
			dev_err(dev, "invalid ntc_degree_from\n");
			return -EINVAL;
		}

		if (out_value)
			pdata->ntc_degree_from = -pdata->ntc_degree_from;

		pdata->ntc_size = length / sizeof(u32);
	}

	if (pdata->ntc_size) {
		parse_temperature_chrg_table(battery, np);
		size = sizeof(*pdata->ntc_table) * pdata->ntc_size;
		pdata->ntc_table = devm_kzalloc(battery->dev, size, GFP_KERNEL);
		if (!pdata->ntc_table)
			return -ENOMEM;

		ret = of_property_read_u32_array(np, "ntc_table",
				pdata->ntc_table,
				pdata->ntc_size);
		if (ret < 0)
			return ret;
	}

	DBG("the battery dts info dump:\n"
	    "bat_res:%d\n"
	    "res_sample:%d\n"
	    "design_capacity:%d\n"
	    "design_qmax :%d\n"
	    "sleep_enter_current:%d\n"
	    "sleep_exit_current:%d\n"
	    "sleep_filter_current:%d\n"
	    "monitor_sec:%d\n"
	    "virtual_power:%d\n"
	    "pwroff_vol:%d\n",
	    pdata->bat_res,
	    pdata->sample_res,
	    pdata->design_capacity,
	    pdata->design_qmax,
	    pdata->sleep_enter_current,
	    pdata->sleep_exit_current,
	    pdata->sleep_filter_current,
	    pdata->monitor_sec,
	    pdata->bat_mode,
	    pdata->pwroff_vol);

	/* optional low-battery gpio & threshold */
	rk817_bat_init_batlow_from_dt(battery);

	/* Parse hybrid mode properties */
	ret = of_property_read_u32(np, "hybrid_v_full_chg", &out_value);
	if (ret >= 0)
		battery->hybrid_v_full_chg = out_value;

	ret = of_property_read_u32(np, "hybrid_v_full_dis", &out_value);
	if (ret >= 0)
		battery->hybrid_v_full_dis = out_value;

	ret = of_property_read_u32(np, "hybrid_v_empty_chg", &out_value);
	if (ret >= 0)
		battery->hybrid_v_empty_chg = out_value;

	ret = of_property_read_u32(np, "hybrid_v_empty_dis", &out_value);
	if (ret >= 0)
		battery->hybrid_v_empty_dis = out_value;

	DBG("hybrid: vfull_chg=%d vfull_dis=%d vempty_chg=%d vempty_dis=%d\n",
	    battery->hybrid_v_full_chg, battery->hybrid_v_full_dis,
	    battery->hybrid_v_empty_chg, battery->hybrid_v_empty_dis);

	return 0;
}

static enum power_supply_property rk817_bat_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
};

static int rk817_get_capacity_leve(struct rk817_battery_device *battery)
{
	int dsoc;

	if (battery->pdata->bat_mode == MODE_VIRTUAL)
		return POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;

	dsoc = (battery->dsoc + 500) / 1000;
	if (dsoc < 1)
		return POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
	else if (dsoc <= 20)
		return POWER_SUPPLY_CAPACITY_LEVEL_LOW;
	else if (dsoc <= 70)
		return POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
	else if (dsoc <= 90)
		return POWER_SUPPLY_CAPACITY_LEVEL_HIGH;
	else
		return POWER_SUPPLY_CAPACITY_LEVEL_FULL;
}

static int rk817_battery_time_to_full(struct rk817_battery_device *battery)
{
	int time_sec;
	int cap_temp;  /* remaining capacity in mAh */
	int current_ma;  /* charge current in mA */

	if (battery->pdata->bat_mode == MODE_VIRTUAL) {
		time_sec = 3600;
		return time_sec;
	}

	cap_temp = battery->remain_cap / 1000;  /* uAh -> mAh */
	if (cap_temp <= 0)
		return 0;

	/*
	 * time = remaining_capacity_mAh / charge_current_mA * 3600
	 * current_avg is positive when charging (this driver convention)
	 */
	current_ma = battery->current_avg;
	if (current_ma <= 0) {
		/* Not charging or no data */
		return 3600 * 24;  /* One day fallback */
	}

	time_sec = (cap_temp * 3600) / current_ma;
	return time_sec;
}

static int rk817_battery_get_property(struct power_supply *psy,
				      enum power_supply_property psp,
				      union power_supply_propval *val)
{
	struct rk817_battery_device *battery = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = battery->current_avg * 1000;/*uA*/
		if (battery->pdata->bat_mode == MODE_VIRTUAL)
			val->intval = VIRTUAL_CURRENT * 1000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = battery->voltage_avg * 1000;/*uV*/
		if (battery->pdata->bat_mode == MODE_VIRTUAL)
			val->intval = VIRTUAL_VOLTAGE * 1000;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = (battery->dsoc + 400) / 1000;
		if (battery->pdata->bat_mode == MODE_VIRTUAL)
			val->intval = VIRTUAL_SOC;
		break;
	case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
		val->intval = rk817_get_capacity_leve(battery);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = battery->temperature;
		if (battery->pdata->bat_mode == MODE_VIRTUAL)
			val->intval = VIRTUAL_TEMPERATURE;
		break;
	case POWER_SUPPLY_PROP_STATUS:
		if (battery->pdata->bat_mode == MODE_VIRTUAL)
			val->intval = VIRTUAL_STATUS;
		else if (battery->dsoc == MAX_PERCENTAGE)
			val->intval = POWER_SUPPLY_STATUS_FULL;
		else {
			if ((battery->chip_id != RK809_ID) &&
			    power_supply_is_system_supplied())
				val->intval = POWER_SUPPLY_STATUS_CHARGING;
			else if (battery->chip_id == RK809_ID &&
				 battery->plugin_trigger)
				val->intval = POWER_SUPPLY_STATUS_CHARGING;
			else
				val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		}
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		val->intval = battery->pdata->design_capacity * 1000;/* uAh */
		break;
	case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW:
		val->intval = rk817_battery_time_to_full(battery);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val->intval = 4500 * 1000;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = 5000 * 1000;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct power_supply_desc rk817_bat_desc = {
	.name		= "battery",
	.type		= POWER_SUPPLY_TYPE_BATTERY,
	.properties	= rk817_bat_props,
	.num_properties	= ARRAY_SIZE(rk817_bat_props),
	.get_property	= rk817_battery_get_property,
};

static int rk817_bat_init_power_supply(struct rk817_battery_device *battery)
{
	struct power_supply_config psy_cfg = { .drv_data = battery, };

	battery->bat = devm_power_supply_register(battery->dev,
						  &rk817_bat_desc,
						  &psy_cfg);
	if (IS_ERR(battery->bat)) {
		dev_err(battery->dev, "register bat power supply fail\n");
		return PTR_ERR(battery->bat);
	}

	return 0;
}

static enum power_supply_property rk809_chg_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_STATUS,
};

static int rk809_chg_get_property(struct power_supply *psy,
				  enum power_supply_property psp,
				  union power_supply_propval *val)
{
	struct rk817_battery_device *battery = power_supply_get_drvdata(psy);
	int online = 0;
	int ret = 0;

	if (battery->plugin_trigger)
		online = 1;
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = online;
		dev_dbg(battery->dev, "report online: %d\n", val->intval);
		break;
	case POWER_SUPPLY_PROP_STATUS:
		if (online)
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		dev_dbg(battery->dev, "report prop: %d\n", val->intval);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static const struct power_supply_desc rk809_chg_desc = {
	.name		= "charger",
	.type		= POWER_SUPPLY_TYPE_USB,
	.properties	= rk809_chg_props,
	.num_properties	= ARRAY_SIZE(rk809_chg_props),
	.get_property	= rk809_chg_get_property,
};

static int rk809_chg_init_power_supply(struct rk817_battery_device *battery)
{
	struct power_supply_config psy_cfg = { .drv_data = battery, };

	battery->chg_psy =
		devm_power_supply_register(battery->dev, &rk809_chg_desc,
					   &psy_cfg);
	if (IS_ERR(battery->chg_psy)) {
		dev_err(battery->dev, "register chg psy power supply fail\n");
		return PTR_ERR(battery->chg_psy);
	}

	return 0;
}

static void rk817_bat_power_supply_changed(struct rk817_battery_device *battery)
{
	if (battery->dsoc > MAX_PERCENTAGE)
		battery->dsoc = MAX_PERCENTAGE;
	else if (battery->dsoc < 0)
		battery->dsoc = 0;

	if ((battery->dsoc == battery->ps_changed_old_soc) &&
	    (battery->chrg_status == battery->ps_changed_status))
		return;

	/* Battery status change, report information */
	battery->ps_changed_status = battery->chrg_status;
	battery->ps_changed_old_soc = battery->dsoc;
	power_supply_changed(battery->bat);
	/* also refresh indicator when status flips */
	rk817_bat_update_batlow_gpio(battery);
	DBG("changed: dsoc=%d, rsoc=%d, v=%d, ov=%d c=%d, cap=%d, f=%d\n",
	    battery->dsoc, battery->rsoc, battery->voltage_avg,
	    battery->voltage_ocv, battery->current_avg,
	    battery->remain_cap, battery->fcc);
}

static void rk817_battery_debug_info(struct rk817_battery_device *battery)
{
	rk817_bat_get_battery_voltage(battery);
	rk817_bat_get_sys_voltage(battery);
	rk817_bat_get_USB_voltage(battery);
	rk817_bat_get_pwron_voltage(battery);
	rk817_bat_get_ocv_voltage(battery);
	rk817_bat_get_ocv0_voltage0(battery);

	rk817_bat_current_calibration(battery);
	rk817_bat_get_avg_current(battery);
	rk817_bat_get_relax_cur1(battery);
	rk817_bat_get_relax_cur2(battery);
	rk817_bat_get_relax_current(battery);
	rk817_bat_get_ocv_current(battery);
	rk817_bat_get_ocv_current0(battery);
	rk817_bat_get_pwron_current(battery);
	rk817_bat_get_ocv_count(battery);
	rk817_bat_save_dsoc(battery, battery->dsoc);
	DBG("capacity = %d\n", rk817_bat_get_capacity_mah(battery));
}

static void rk817_bat_update_fg_info(struct rk817_battery_device *battery)
{
	battery->voltage_avg = rk817_bat_get_battery_voltage(battery);
	battery->voltage_sys = rk817_bat_get_sys_voltage(battery);
	battery->current_avg = rk817_bat_get_avg_current(battery);
	battery->voltage_relax = rk817_bat_get_relax_voltage(battery);
	battery->rsoc = rk817_bat_get_rsoc(battery);
	battery->remain_cap = rk817_bat_get_capacity_uah(battery);
	battery->voltage_usb = rk817_bat_get_USB_voltage(battery);
	battery->chrg_status = rk817_bat_get_charge_status(battery);
	DBG("valtage usb: %d\n", battery->voltage_usb);
	DBG("UPDATE: voltage_avg = %d\n"
	    "voltage_sys = %d\n"
	    "current_avg = %d\n"
	    "rsoc = %d\n"
	    "chrg_status = %d\n"
	    "PWRON_CUR = %d\n"
	    "remain_cap = %d\n",
	    battery->voltage_avg,
	    battery->voltage_sys,
	    battery->current_avg,
	    battery->rsoc,
	    battery->chrg_status,
	    rk817_bat_get_pwron_current(battery),
	    battery->remain_cap);

	/* drive low-battery indicator after fresh readings */
	rk817_bat_update_batlow_gpio(battery);
}

static void rk817_bat_save_data(struct rk817_battery_device *battery)
{
	rk817_bat_save_dsoc(battery, battery->dsoc);
	rk817_bat_save_cap(battery, battery->remain_cap / 1000);
}

/* high load: current < 0 with charger in.
 * System will not shutdown while dsoc=0% with charging state(ac_in),
 * which will cause over discharge, so oppose status before report states.
 */
static void rk817_bat_stay_awake(struct rk817_battery_device *battery)
{
	bool status = false;

	if (!battery->pdata->charge_stay_awake)
		return;

	status = (battery->current_avg > 0) ||
		(battery->sleep_chrg_status == CC_OR_CV_CHRG) ||
		((battery->sleep_chrg_status == CHARGE_FINISH) &&
		(battery->dsoc / 1000 < 100));

	if (status && !battery->active_awake) {
		battery->active_awake = true;
		pm_stay_awake(battery->dev);
	} else {
		if (battery->active_awake && !status) {
			battery->active_awake = false;
			pm_relax(battery->dev);
		}
	}
}

static void rk817_bat_output_info(struct rk817_battery_device *battery)
{
	int index;

	DBG("info: dsoc=%d rsoc=%d v=%d c=%d cap=%d fcc=%d\n",
	    battery->dsoc, battery->rsoc, battery->voltage_avg,
	    battery->current_avg, battery->remain_cap, battery->fcc);
	if (battery->pdata->ntc_size && battery->pdata->tc_table) {
		index = battery->charge_index;
		if (index >= 0 && index < battery->pdata->tc_count) {
			DBG("Temperature: %d charger current: %dmA, index: %d, "
			    "charger voltage: %dmV, index: %d\n",
			    battery->temperature,
			    battery->pdata->tc_table[index].chrg_current,
			    battery->pdata->tc_table[index].chrg_current_index,
			    battery->pdata->tc_table[index].chrg_voltage,
			    battery->pdata->tc_table[index].chrg_voltage_index);
		}
	}
}

static void rk817_battery_work(struct work_struct *work)
{
	struct rk817_battery_device *battery =
		container_of(work,
			     struct rk817_battery_device,
			     bat_delay_work.work);

	rk817_bat_update_fg_info(battery);

	rk817_hybrid_load_map(battery);

	rk817_hybrid_calculate(battery);

	rk817_bat_update_fcc(battery);
	rk817_bat_power_supply_changed(battery);
	rk817_bat_save_data(battery);
	rk817_bat_stay_awake(battery);
	rk817_bat_update_temperature(battery);
	rk817_bat_output_info(battery);

	if (rk817_bat_field_read(battery, CUR_CALIB_UPD)) {
		rk817_bat_current_calibration(battery);
		rk817_bat_init_voltage_kb(battery);
		rk817_bat_field_write(battery, CUR_CALIB_UPD, 0x01);
	}

	queue_delayed_work(battery->bat_monitor_wq, &battery->bat_delay_work,
			   msecs_to_jiffies(battery->monitor_ms));
}

static irqreturn_t rk809_plug_in_isr(int irq, void *cg)
{
	struct rk817_battery_device *battery;

	battery = (struct rk817_battery_device *)cg;
	battery->plugin_trigger = 1;
	battery->plugout_trigger = 0;
	power_supply_changed(battery->bat);
	if (battery->is_register_chg_psy)
		power_supply_changed(battery->chg_psy);

	return IRQ_HANDLED;
}

static irqreturn_t rk809_plug_out_isr(int irq, void *cg)
{
	struct rk817_battery_device *battery;

	battery = (struct rk817_battery_device *)cg;
	battery->plugin_trigger = 0;
	battery->plugout_trigger = 1;
	power_supply_changed(battery->bat);
	if (battery->is_register_chg_psy)
		power_supply_changed(battery->chg_psy);

	return IRQ_HANDLED;
}

static int rk809_charge_init_irqs(struct rk817_battery_device *battery)
{
	struct rk808 *rk817 = battery->rk817;
	struct platform_device *pdev = battery->pdev;
	int ret, plug_in_irq, plug_out_irq;

	battery->plugin_trigger = 0;
	battery->plugout_trigger = 0;

	plug_in_irq = regmap_irq_get_virq(rk817->irq_data, RK817_IRQ_PLUG_IN);
	if (plug_in_irq < 0) {
		dev_err(battery->dev, "plug_in_irq request failed!\n");
		return plug_in_irq;
	}

	plug_out_irq = regmap_irq_get_virq(rk817->irq_data, RK817_IRQ_PLUG_OUT);
	if (plug_out_irq < 0) {
		dev_err(battery->dev, "plug_out_irq request failed!\n");
		return plug_out_irq;
	}

	ret = devm_request_threaded_irq(battery->dev, plug_in_irq, NULL,
					rk809_plug_in_isr,
					IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					"rk817_plug_in", battery);
	if (ret) {
		dev_err(&pdev->dev, "plug_in_irq request failed!\n");
		return ret;
	}

	ret = devm_request_threaded_irq(battery->dev, plug_out_irq, NULL,
					rk809_plug_out_isr,
					IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					"rk817_plug_out", battery);
	if (ret) {
		dev_err(&pdev->dev, "plug_out_irq request failed!\n");
		return ret;
	}

	if (rk817_bat_field_read(battery, PLUG_IN_STS)) {
		battery->plugin_trigger = 1;
		battery->plugout_trigger = 0;
	}

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id rk817_bat_of_match[] = {
	{ .compatible = "rk817,battery", },
	{ },
};
MODULE_DEVICE_TABLE(of, rk817_bat_of_match);
#else
static const struct of_device_id rk817_bat_of_match[] = {
	{ },
};
#endif

static int rk817_battery_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id =
			of_match_device(rk817_bat_of_match, &pdev->dev);
	struct rk808 *rk817 = dev_get_drvdata(pdev->dev.parent);
	struct i2c_client *client = rk817->i2c;
	struct rk817_battery_device *battery;
	int i, ret;

	if (!of_id) {
		dev_err(&pdev->dev, "Failed to find matching dt id\n");
		return -ENODEV;
	}

	battery = devm_kzalloc(&client->dev, sizeof(*battery), GFP_KERNEL);
	if (!battery)
		return -EINVAL;

	battery->rk817 = rk817;
	battery->client = client;
	battery->dev = &pdev->dev;
	platform_set_drvdata(pdev, battery);
	battery->chip_id = rk817->variant;

	if (IS_ERR(rk817->regmap)) {
		dev_err(battery->dev, "Failed to initialize regmap\n");
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(rk817_battery_reg_fields); i++) {
		const struct reg_field *reg_fields = rk817_battery_reg_fields;

		battery->rmap_fields[i] =
			devm_regmap_field_alloc(battery->dev,
						rk817->regmap,
						reg_fields[i]);
		if (IS_ERR(battery->rmap_fields[i])) {
			dev_err(battery->dev, "cannot allocate regmap field\n");
			return PTR_ERR(battery->rmap_fields[i]);
		}
	}

	ret = rk817_bat_parse_dt(battery);
	if (ret < 0) {
		dev_err(battery->dev, "battery parse dt failed!\n");
		return ret;
	}

	rk817_bat_init_info(battery);
	rk817_bat_init_fg(battery);

	/* Initialize hybrid mode */
	battery->hybrid_v_full_chg = HYBRID_V_FULL_CHG_DEFAULT;
	battery->hybrid_v_full_dis = HYBRID_V_FULL_DIS_DEFAULT;
	battery->hybrid_v_empty_chg = HYBRID_V_EMPTY_CHG_DEFAULT;
	battery->hybrid_v_empty_dis = HYBRID_V_EMPTY_DIS_DEFAULT;
	battery->hybrid_voltage_ema = -1;
	battery->hybrid_prev_voltage[0] = -1;
	battery->hybrid_prev_voltage[1] = -1;
	battery->hybrid_visible_soc = -1;
	battery->hybrid_internal_soc = -1;
	battery->hybrid_calibrated = false;
	battery->hybrid_tracking_peak = false;
	battery->hybrid_peak_mv = 0;
	battery->hybrid_peak_start_sec = 0;
	battery->hybrid_peak_stable = false;
	battery->hybrid_full_event = false;
	battery->hybrid_last_loop_sec = rk817_hybrid_boottime_sec();
	battery->hybrid_first_run = true;
	battery->hybrid_99_start_sec = 0;

	/* Calib file is lazy-loaded from rk817_battery_work once rootfs is up */
	battery->hybrid_map_loaded = false;
	battery->hybrid_map_tries = 0;

	BAT_INFO("hybrid mode: vfull_chg=%d vfull_dis=%d "
		 "vempty_chg=%d vempty_dis=%d\n",
		 battery->hybrid_v_full_chg, battery->hybrid_v_full_dis,
		 battery->hybrid_v_empty_chg, battery->hybrid_v_empty_dis);

	rk817_battery_debug_info(battery);
	rk817_bat_update_fg_info(battery);

	rk817_bat_output_info(battery);
	battery->bat_monitor_wq = alloc_ordered_workqueue("%s",
			WQ_MEM_RECLAIM | WQ_FREEZABLE, "rk817-bat-monitor-wq");
	INIT_DELAYED_WORK(&battery->bat_delay_work, rk817_battery_work);
	queue_delayed_work(battery->bat_monitor_wq, &battery->bat_delay_work,
			   msecs_to_jiffies(TIMER_MS_COUNTS * 5));

	ret = rk817_bat_init_power_supply(battery);
	if (ret) {
		dev_err(battery->dev, "rk817 power supply register failed!\n");
		return ret;
	}
	if (battery->is_register_chg_psy) {
		ret = rk809_chg_init_power_supply(battery);
		if (ret) {
			dev_err(battery->dev, "rk809 chg psy init failed!\n");
			return ret;
		}
	}

	if (battery->chip_id == RK809_ID)
		rk809_charge_init_irqs(battery);

	device_init_wakeup(battery->dev, true);

	DBG("name: 0x%x", rk817_bat_field_read(battery, CHIP_NAME_H));
	DBG("%x\n", rk817_bat_field_read(battery, CHIP_NAME_L));
	BAT_INFO("driver version %s\n", DRIVER_VERSION);

	return 0;
}

static void rk817_battery_shutdown(struct platform_device *dev)
{
	struct rk817_battery_device *battery = platform_get_drvdata(dev);

	if (!battery)
		return;

	/* Save final state before shutdown */
	rk817_bat_update_fg_info(battery);
	rk817_bat_save_data(battery);
	BAT_INFO("shutdown: dsoc=%d, rsoc=%d, cap=%d\n",
		 battery->dsoc / 1000, battery->rsoc / 1000,
		 battery->remain_cap / 1000);
}

static time64_t rk817_get_rtc_sec(void)
{
	int err;
	struct rtc_time tm;
	struct rtc_device *rtc;

	rtc = rtc_class_open(CONFIG_RTC_HCTOSYS_DEVICE);
	if (!rtc) {
		pr_warn("rk817-bat: cannot open rtc device\n");
		return 0;
	}

	err = rtc_read_time(rtc, &tm);
	if (err) {
		dev_err(rtc->dev.parent, "read hardware clk failed\n");
		rtc_class_close(rtc);
		return 0;
	}

	err = rtc_valid_tm(&tm);
	if (err) {
		dev_err(rtc->dev.parent, "invalid date time\n");
		rtc_class_close(rtc);
		return 0;
	}

	rtc_class_close(rtc);
	return rtc_tm_to_time64(&tm);
}

static int rk817_bat_pm_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct rk817_battery_device *battery = dev_get_drvdata(&pdev->dev);

	cancel_delayed_work_sync(&battery->bat_delay_work);
	del_timer_sync(&battery->caltimer);
	rk817_bat_update_fg_info(battery);

	battery->sleep_chrg_status = rk817_bat_get_charge_status(battery);
	battery->current_sleep = battery->current_avg;

	if (battery->current_avg > 0 ||
	    (battery->sleep_chrg_status == CC_OR_CV_CHRG) ||
	    (battery->sleep_chrg_status == CHARGE_FINISH))
		battery->sleep_chrg_online = 1;
	else
		battery->sleep_chrg_online = 0;

	battery->remain_cap = rk817_bat_get_capacity_uah(battery);
	battery->rsoc = rk817_bat_get_rsoc(battery);

	/* Record last loop time for gap detection on resume */
	battery->hybrid_last_loop_sec = rk817_hybrid_boottime_sec();

	battery->rtc_base = rk817_get_rtc_sec();
	rk817_bat_save_data(battery);

	if (is_rk817_bat_relax_mode(battery))
		rk817_bat_restart_relax(battery);

	DBG("suspend get_boot_sec: %llu\n", get_boot_sec());

	DBG("suspend: dl=%d rl=%d c=%d v=%d cap=%d ch=%d\n",
	    battery->dsoc, battery->rsoc, battery->current_avg,
	    rk817_bat_get_battery_voltage(battery),
	    rk817_bat_get_capacity_uah(battery),
	    battery->sleep_chrg_online);

	DBG("battery->sleep_chrg_status=%d\n", battery->sleep_chrg_status);

	return 0;
}

static int rk817_bat_rtc_sleep_sec(struct rk817_battery_device *battery)
{
	int interval_sec;

	interval_sec = rk817_get_rtc_sec() - battery->rtc_base;

	return (interval_sec > 0) ? interval_sec : 0;
}

static int rk817_bat_pm_resume(struct device *dev)
{
	struct rk817_battery_device *battery = dev_get_drvdata(dev);
	int interval_sec = 0;

	rk817_bat_update_fg_info(battery);

	interval_sec = rk817_bat_rtc_sleep_sec(battery);
	battery->sleep_sum_sec += interval_sec;

	/* Hybrid mode: resume后直接用coulomb counter的值，
	 * 不走step-limit，避免休眠期间充满电却显示99%
	 */
	battery->hybrid_first_run = true;
	BAT_INFO("resume: interval=%ds v=%d\n",
		 interval_sec, battery->voltage_avg);

	rk817_bat_save_data(battery);
	DBG("RESUME:");
	rk817_bat_output_info(battery);

	queue_delayed_work(battery->bat_monitor_wq, &battery->bat_delay_work,
			   msecs_to_jiffies(1000));

	/* Restart calibration timer (was suspended) */
	mod_timer(&battery->caltimer, jiffies + MINUTE(8) * HZ);

	/* ensure indicator is correct right after resume */
	rk817_bat_update_batlow_gpio(battery);
	return 0;
}

static const struct dev_pm_ops rk817_bat_pm_ops = {
	.suspend = rk817_bat_pm_suspend,
	.resume = rk817_bat_pm_resume,
};

static struct platform_driver rk817_battery_driver = {
	.probe = rk817_battery_probe,
	.shutdown = rk817_battery_shutdown,
	.driver = {
		.name = "rk817-battery",
		.pm = &rk817_bat_pm_ops,
		.of_match_table = of_match_ptr(rk817_bat_of_match),
	},
};

static int __init rk817_battery_init(void)
{
	return platform_driver_register(&rk817_battery_driver);
}
fs_initcall_sync(rk817_battery_init);

static void __exit rk817_battery_exit(void)
{
	platform_driver_unregister(&rk817_battery_driver);
}
module_exit(rk817_battery_exit);

MODULE_DESCRIPTION("RK817 Battery driver");
MODULE_LICENSE("GPL");
