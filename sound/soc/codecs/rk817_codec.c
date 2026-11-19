/*
 * Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/mfd/rk808.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <sound/core.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include "rk817_codec.h"

static int dbg_enable;
module_param_named(dbg_level, dbg_enable, int, 0644);

#define DBG(args...) \
	do { \
		if (dbg_enable) { \
			pr_info(args); \
		} \
	} while (0)

/* For route */
#define RK817_CODEC_PLAYBACK	1
#define RK817_CODEC_CAPTURE	2
#define RK817_CODEC_INCALL	4
#define RK817_CODEC_ALL	(RK817_CODEC_PLAYBACK |\
	RK817_CODEC_CAPTURE | RK817_CODEC_INCALL)

/*
 * DDAC L/R volume setting
 * 0db~-95db,0.375db/step,for example:
 * 0: 0dB
 * 0x0a: -3.75dB
 * 0x7d: -46dB
 * 0xff: -95dB
 */
#define OUT_VOLUME	(0x03)

#ifdef CONFIG_ARCH_ROCKCHIP_ODROIDGOA
/*
 * 动态音量范围控制
 * RK817音量寄存器: 0=0dB, 每步-0.375dB, 最大255=-95dB
 * 通过DTS的 volume-min-db 限制最小音量
 * 最大输出固定为-6.75dB（寄存器0x12），与上游保持一致
 * 例如：volume-min-db = <72> 表示范围 -27dB ~ -6.75dB
 */

/* 最大允许输出: -6.75dB (寄存器值 0x12) */
#define RK817_VOL_MAX_REG 0x12

/* TLV用于显示dB值（会在probe中更新） */
static DECLARE_TLV_DB_SCALE(rk817_vol_tlv, -9500, 375, 0);

/* 前向声明 */
static int rk817_vol_get(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol);
static int rk817_vol_put(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol);

static const struct snd_kcontrol_new rk817_dac_controls[] = {
	SOC_DOUBLE_R_EXT_TLV("Playback Volume",
		RK817_CODEC_DDAC_VOLL, RK817_CODEC_DDAC_VOLR,
		0, 255, 1,
		rk817_vol_get, rk817_vol_put, rk817_vol_tlv),
	SOC_DOUBLE_R("Record Volume", RK817_CODEC_DADC_VOLL,
		RK817_CODEC_DADC_VOLR, 0, 0xFF, 1),
};
#endif

/*
 * DADC L/R volume setting
 * 0db~-95db,0.375db/step,for example:
 * 0: 0dB
 * 0x0a: -3.75dB
 * 0x7d: -46dB
 * 0xff: -95dB
 */
#define CAPTURE_VOLUME	(0x0)

#define CODEC_SET_SPK 1
#define CODEC_SET_HP 2


struct rk817_codec_priv {
	struct snd_soc_codec *codec;
	struct regmap *regmap;
	struct rk808 *rk817;
	struct clk *mclk;

	unsigned int stereo_sysclk;
	unsigned int rate;

	unsigned int spk_volume;
	unsigned int hp_volume;
	unsigned int capture_volume;

	bool mic_in_differential;
	bool pdmdata_out_enable;
	bool use_ext_amplifier;
	int chip_ver;
	bool adc_for_loopback;

	long int playback_path;
	long int capture_path;

	struct gpio_desc *spk_ctl_gpio;
	struct gpio_desc *hp_ctl_gpio;
	int spk_mute_delay;
	int hp_mute_delay;

	/* 动态音量最小值配置（单位：0.01dB），最大值固定为-6.75dB */
	int volume_min_db;

	int user_vol;
	/* True when the DAC is at its minimum register (effective volume 0) */
	bool vol_at_floor;
	/* Previous floor state; the amplifier is only re-enabled on the edge */
	bool last_vol_at_floor;
};

#ifdef CONFIG_ARCH_ROCKCHIP_ODROIDGOA
/*
 * 自定义音量get/put回调
 * 将用户空间0-255映射到实际寄存器范围
 * 寄存器范围: [RK817_VOL_MAX_REG(0x12=-6.75dB), vol_max_reg(由DTS决定)]
 * 用户0% -> 寄存器vol_max_reg(最安静)
 * 用户100% -> 寄存器RK817_VOL_MAX_REG(-6.75dB，最大允许输出)
 */

/*
 * Re-apply the volume registers and mute state from the last user
 * volume. At volume 0 the DAC hardware mute bit (DACMT) is also set
 * and the external amplifier(s) are disabled, so the output is truly
 * silent (no amplifier hiss).
 */
static void rk817_codec_amp_mute(struct snd_soc_codec *codec, int mute)
{
	struct rk817_codec_priv *rk817 = snd_soc_codec_get_drvdata(codec);

	if (mute) {
		if (rk817->spk_ctl_gpio)
			gpiod_set_value(rk817->spk_ctl_gpio, 0);
		if (rk817->hp_ctl_gpio)
			gpiod_set_value(rk817->hp_ctl_gpio, 0);
	} else {
		switch (rk817->playback_path) {
		case SPK_PATH:
			if (rk817->spk_ctl_gpio)
				gpiod_set_value(rk817->spk_ctl_gpio, 1);
			break;
		case HP_PATH:
			if (rk817->hp_ctl_gpio)
				gpiod_set_value(rk817->hp_ctl_gpio, 1);
			break;
		case SPK_HP:
			if (rk817->spk_ctl_gpio)
				gpiod_set_value(rk817->spk_ctl_gpio, 1);
			if (rk817->hp_ctl_gpio)
				gpiod_set_value(rk817->hp_ctl_gpio, 1);
			break;
		default:
			break;
		}
	}
}

static void rk817_codec_apply_volume(struct snd_soc_codec *codec)
{
	struct rk817_codec_priv *rk817 = snd_soc_codec_get_drvdata(codec);
	int vol_max_reg, reg_val, reg_range;

	vol_max_reg = (-rk817->volume_min_db * 8) / (3 * 100);
	vol_max_reg = clamp(vol_max_reg, RK817_VOL_MAX_REG + 1, 255);

	reg_range = vol_max_reg - RK817_VOL_MAX_REG;
	reg_val = vol_max_reg - (rk817->user_vol * reg_range) / 255;
	reg_val = clamp(reg_val, RK817_VOL_MAX_REG, vol_max_reg);

	snd_soc_write(codec, RK817_CODEC_DDAC_VOLL, reg_val);
	snd_soc_write(codec, RK817_CODEC_DDAC_VOLR, reg_val);

	rk817->vol_at_floor = (reg_val == vol_max_reg);

	if (rk817->vol_at_floor) {
		snd_soc_update_bits(codec, RK817_CODEC_DDAC_MUTE_MIXCTL,
				    DACMT_ENABLE, DACMT_ENABLE);
		rk817_codec_amp_mute(codec, 1);
	} else {
		if (rk817->last_vol_at_floor)
			rk817_codec_amp_mute(codec, 0);
		snd_soc_update_bits(codec, RK817_CODEC_DDAC_MUTE_MIXCTL,
				    DACMT_ENABLE, DACMT_DISABLE);
	}

	rk817->last_vol_at_floor = rk817->vol_at_floor;
}

static int rk817_vol_get(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct rk817_codec_priv *rk817 = snd_soc_codec_get_drvdata(codec);
	unsigned int reg_val;
	int vol_max_reg, user_val, reg_range;

	reg_val = snd_soc_read(codec, RK817_CODEC_DDAC_VOLL);

	/* 计算寄存器最大值：reg_max = -min_db / 0.375 */
	vol_max_reg = (-rk817->volume_min_db * 8) / (3 * 100);
	vol_max_reg = clamp(vol_max_reg, RK817_VOL_MAX_REG + 1, 255);

	/* 寄存器值 -> 用户空间值 */
	reg_range = vol_max_reg - RK817_VOL_MAX_REG;
	user_val = ((vol_max_reg - reg_val) * 255) / reg_range;
	user_val = clamp(user_val, 0, 255);

	ucontrol->value.integer.value[0] = user_val;
	ucontrol->value.integer.value[1] = user_val;
	return 0;
}

static int rk817_vol_put(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct rk817_codec_priv *rk817 = snd_soc_codec_get_drvdata(codec);

	rk817->user_vol = ucontrol->value.integer.value[0];
	rk817_codec_apply_volume(codec);
	return 1;
}
#endif

static const struct reg_default rk817_reg_defaults[] = {
	{ RK817_CODEC_DTOP_VUCTL, 0x003 },
	{ RK817_CODEC_DTOP_VUCTIME, 0x00 },
	{ RK817_CODEC_DTOP_LPT_SRST, 0x00 },
	{ RK817_CODEC_DTOP_DIGEN_CLKE, 0x00 },
	{ RK817_CODEC_AREF_RTCFG0, 0x00 },
	{ RK817_CODEC_AREF_RTCFG1, 0x06 },
	{ RK817_CODEC_AADC_CFG0, 0xc8 },
	{ RK817_CODEC_AADC_CFG1, 0x00 },
	{ RK817_CODEC_DADC_VOLL, 0x00 },
	{ RK817_CODEC_DADC_VOLR, 0x00 },
	{ RK817_CODEC_DADC_SR_ACL0, 0x00 },
	{ RK817_CODEC_DADC_ALC1, 0x00 },
	{ RK817_CODEC_DADC_ALC2, 0x00 },
	{ RK817_CODEC_DADC_NG, 0x00 },
	{ RK817_CODEC_DADC_HPF, 0x00 },
	{ RK817_CODEC_DADC_RVOLL, 0xff },
	{ RK817_CODEC_DADC_RVOLR, 0xff },
	{ RK817_CODEC_AMIC_CFG0, 0x70 },
	{ RK817_CODEC_AMIC_CFG1, 0x00 },
	{ RK817_CODEC_DMIC_PGA_GAIN, 0x66 },
	{ RK817_CODEC_DMIC_LMT1, 0x00 },
	{ RK817_CODEC_DMIC_LMT2, 0x00 },
	{ RK817_CODEC_DMIC_NG1, 0x00 },
	{ RK817_CODEC_DMIC_NG2, 0x00 },
	{ RK817_CODEC_ADAC_CFG0, 0x00 },
	{ RK817_CODEC_ADAC_CFG1, 0x07 },
	{ RK817_CODEC_DDAC_POPD_DACST, 0x82 },
	{ RK817_CODEC_DDAC_VOLL, 0x00 },
	{ RK817_CODEC_DDAC_VOLR, 0x00 },
	{ RK817_CODEC_DDAC_SR_LMT0, 0x00 },
	{ RK817_CODEC_DDAC_LMT1, 0x00 },
	{ RK817_CODEC_DDAC_LMT2, 0x00 },
	{ RK817_CODEC_DDAC_MUTE_MIXCTL, 0xa0 },
	{ RK817_CODEC_DDAC_RVOLL, 0xff },
	{ RK817_CODEC_DDAC_RVOLR, 0xff },
	{ RK817_CODEC_AHP_ANTI0, 0x00 },
	{ RK817_CODEC_AHP_ANTI1, 0x00 },
	{ RK817_CODEC_AHP_CFG0, 0xe0 },
	{ RK817_CODEC_AHP_CFG1, 0x1f },
	{ RK817_CODEC_AHP_CP, 0x09 },
	{ RK817_CODEC_ACLASSD_CFG1, 0x69 },
	{ RK817_CODEC_ACLASSD_CFG2, 0x44 },
	{ RK817_CODEC_APLL_CFG0, 0x04 },
	{ RK817_CODEC_APLL_CFG1, 0x00 },
	{ RK817_CODEC_APLL_CFG2, 0x30 },
	{ RK817_CODEC_APLL_CFG3, 0x19 },
	{ RK817_CODEC_APLL_CFG4, 0x65 },
	{ RK817_CODEC_APLL_CFG5, 0x01 },
	{ RK817_CODEC_DI2S_CKM, 0x01 },
	{ RK817_CODEC_DI2S_RSD, 0x00 },
	{ RK817_CODEC_DI2S_RXCR1, 0x00 },
	{ RK817_CODEC_DI2S_RXCR2, 0x17 },
	{ RK817_CODEC_DI2S_RXCMD_TSD, 0x00 },
	{ RK817_CODEC_DI2S_TXCR1, 0x00 },
	{ RK817_CODEC_DI2S_TXCR2, 0x17 },
	{ RK817_CODEC_DI2S_TXCR3_TXCMD, 0x00 },
};

static bool rk817_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RK817_CODEC_DTOP_LPT_SRST:
		return true;
	default:
		return false;
	}
}

static bool rk817_codec_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RK817_CODEC_DTOP_VUCTL:
	case RK817_CODEC_DTOP_VUCTIME:
	case RK817_CODEC_DTOP_LPT_SRST:
	case RK817_CODEC_DTOP_DIGEN_CLKE:
	case RK817_CODEC_AREF_RTCFG0:
	case RK817_CODEC_AREF_RTCFG1:
	case RK817_CODEC_AADC_CFG0:
	case RK817_CODEC_AADC_CFG1:
	case RK817_CODEC_DADC_VOLL:
	case RK817_CODEC_DADC_VOLR:
	case RK817_CODEC_DADC_SR_ACL0:
	case RK817_CODEC_DADC_ALC1:
	case RK817_CODEC_DADC_ALC2:
	case RK817_CODEC_DADC_NG:
	case RK817_CODEC_DADC_HPF:
	case RK817_CODEC_DADC_RVOLL:
	case RK817_CODEC_DADC_RVOLR:
	case RK817_CODEC_AMIC_CFG0:
	case RK817_CODEC_AMIC_CFG1:
	case RK817_CODEC_DMIC_PGA_GAIN:
	case RK817_CODEC_DMIC_LMT1:
	case RK817_CODEC_DMIC_LMT2:
	case RK817_CODEC_DMIC_NG1:
	case RK817_CODEC_DMIC_NG2:
	case RK817_CODEC_ADAC_CFG0:
	case RK817_CODEC_ADAC_CFG1:
	case RK817_CODEC_DDAC_POPD_DACST:
	case RK817_CODEC_DDAC_VOLL:
	case RK817_CODEC_DDAC_VOLR:
	case RK817_CODEC_DDAC_SR_LMT0:
	case RK817_CODEC_DDAC_LMT1:
	case RK817_CODEC_DDAC_LMT2:
	case RK817_CODEC_DDAC_MUTE_MIXCTL:
	case RK817_CODEC_DDAC_RVOLL:
	case RK817_CODEC_DDAC_RVOLR:
	case RK817_CODEC_AHP_ANTI0:
	case RK817_CODEC_AHP_ANTI1:
	case RK817_CODEC_AHP_CFG0:
	case RK817_CODEC_AHP_CFG1:
	case RK817_CODEC_AHP_CP:
	case RK817_CODEC_ACLASSD_CFG1:
	case RK817_CODEC_ACLASSD_CFG2:
	case RK817_CODEC_APLL_CFG0:
	case RK817_CODEC_APLL_CFG1:
	case RK817_CODEC_APLL_CFG2:
	case RK817_CODEC_APLL_CFG3:
	case RK817_CODEC_APLL_CFG4:
	case RK817_CODEC_APLL_CFG5:
	case RK817_CODEC_DI2S_CKM:
	case RK817_CODEC_DI2S_RSD:
	case RK817_CODEC_DI2S_RXCR1:
	case RK817_CODEC_DI2S_RXCR2:
	case RK817_CODEC_DI2S_RXCMD_TSD:
	case RK817_CODEC_DI2S_TXCR1:
	case RK817_CODEC_DI2S_TXCR2:
	case RK817_CODEC_DI2S_TXCR3_TXCMD:
		return true;
	default:
		return false;
	}
}

static int rk817_codec_ctl_gpio(struct rk817_codec_priv *rk817,
				int gpio, int level)
{
	if ((gpio & CODEC_SET_SPK) &&
	    rk817->spk_ctl_gpio) {
		gpiod_set_value(rk817->spk_ctl_gpio, level);
		DBG("%s set spk clt %d\n", __func__, level);
		if (rk817->spk_mute_delay)
			msleep(rk817->spk_mute_delay);
	}

	if ((gpio & CODEC_SET_HP) &&
	    rk817->hp_ctl_gpio) {
		gpiod_set_value(rk817->hp_ctl_gpio, level);
		DBG("%s set hp clt %d\n", __func__, level);
		if (rk817->hp_mute_delay)
			msleep(rk817->hp_mute_delay);
	}

	return 0;
}

static int rk817_reset(struct snd_soc_codec *codec)
{
	struct rk817_codec_priv *rk817 = snd_soc_codec_get_drvdata(codec);

	snd_soc_write(codec, RK817_CODEC_DTOP_LPT_SRST, 0x40);
	snd_soc_write(codec, RK817_CODEC_DDAC_POPD_DACST, 0x02);
	snd_soc_write(codec, RK817_CODEC_DI2S_CKM, 0x00);
	snd_soc_write(codec, RK817_CODEC_DTOP_DIGEN_CLKE, 0xff);
	snd_soc_write(codec, RK817_CODEC_APLL_CFG1, 0x58);
	snd_soc_write(codec, RK817_CODEC_APLL_CFG2, 0x2d);
	snd_soc_write(codec, RK817_CODEC_APLL_CFG3, 0x0c);
	snd_soc_write(codec, RK817_CODEC_APLL_CFG5, 0x00);
	snd_soc_write(codec, RK817_CODEC_DTOP_DIGEN_CLKE, 0x00);

	/* 根据芯片版本配置 APLL，chip_ver=0 视为异常使用上游默认值 */
	if (rk817->chip_ver > 0 && rk817->chip_ver <= 0x4) {
		DBG("%s (%d): 0x4 and previous versions\n",
		    __func__, __LINE__);
		snd_soc_write(codec, RK817_CODEC_APLL_CFG0, 0x0c);
		snd_soc_write(codec, RK817_CODEC_APLL_CFG4, 0x95);
	} else {
		DBG("%s (%d): using default APLL config (ver=0x%x)\n",
		    __func__, __LINE__, rk817->chip_ver);
		snd_soc_write(codec, RK817_CODEC_APLL_CFG0, 0x04);
		snd_soc_write(codec, RK817_CODEC_APLL_CFG4, 0xa5);
	}

	return 0;
}

static struct rk817_reg_val_typ playback_power_up_list[] = {
	{RK817_CODEC_AREF_RTCFG1, 0x40},
	{RK817_CODEC_DDAC_POPD_DACST, 0x02},
	/* 设置默认采样率为 48kHz，hw_params 会根据实际采样率重新配置 */
	{RK817_CODEC_DDAC_SR_LMT0, 0x02},
	/* APLL 默认配置，hw_params 会根据芯片版本和采样率重新配置 */
	{RK817_CODEC_APLL_CFG0, 0x04},
	{RK817_CODEC_APLL_CFG1, 0x58},
	{RK817_CODEC_APLL_CFG2, 0x2d},
	{RK817_CODEC_APLL_CFG3, 0x0c},
	{RK817_CODEC_APLL_CFG4, 0xa5},
	{RK817_CODEC_APLL_CFG5, 0x00},

	{RK817_CODEC_DI2S_RXCMD_TSD, 0x00},
	{RK817_CODEC_DI2S_RSD, 0x00},
	/* {RK817_CODEC_DI2S_CKM, 0x00}, */
	{RK817_CODEC_DI2S_RXCR1, 0x00},
	{RK817_CODEC_DI2S_RXCMD_TSD, 0x20},
	{RK817_CODEC_DTOP_VUCTIME, 0xf4},
	{RK817_CODEC_DDAC_MUTE_MIXCTL, 0x00},

	{RK817_CODEC_DDAC_VOLL, RK817_VOL_MAX_REG},
	{RK817_CODEC_DDAC_VOLR, RK817_VOL_MAX_REG},
};

#define RK817_CODEC_PLAYBACK_POWER_UP_LIST_LEN \
	ARRAY_SIZE(playback_power_up_list)

static struct rk817_reg_val_typ playback_power_down_list[] = {
	{RK817_CODEC_DDAC_MUTE_MIXCTL, 0x01},
	{RK817_CODEC_ADAC_CFG1, 0x0f},
	/* HP */
	{RK817_CODEC_AHP_CFG0, 0xe0},
	{RK817_CODEC_AHP_CP, 0x09},
	/* SPK */
	{RK817_CODEC_ACLASSD_CFG1, 0x69},
};

#define RK817_CODEC_PLAYBACK_POWER_DOWN_LIST_LEN \
	ARRAY_SIZE(playback_power_down_list)

static struct rk817_reg_val_typ capture_power_up_list[] = {
	{RK817_CODEC_AREF_RTCFG1, 0x40},
	{RK817_CODEC_DADC_SR_ACL0, 0x02},
	/* {RK817_CODEC_DTOP_DIGEN_CLKE, 0xff}, */
	/* {RK817_CODEC_APLL_CFG0, 0x04}, */
	{RK817_CODEC_APLL_CFG1, 0x58},
	{RK817_CODEC_APLL_CFG2, 0x2d},
	/* {RK817_CODEC_APLL_CFG4, 0xa5}, */
	{RK817_CODEC_APLL_CFG5, 0x00},

	/*{RK817_CODEC_DI2S_RXCMD_TSD, 0x00},*/
	{RK817_CODEC_DI2S_RSD, 0x00},
	/* {RK817_CODEC_DI2S_CKM, 0x00}, */
	{RK817_CODEC_DI2S_RXCR1, 0x00},
	{RK817_CODEC_DI2S_RXCMD_TSD, 0x20},
	{RK817_CODEC_DTOP_VUCTIME, 0xf4},

	{RK817_CODEC_DDAC_MUTE_MIXCTL, 0x00},
	{RK817_CODEC_AADC_CFG0, 0x08},
	{RK817_CODEC_AMIC_CFG0, 0x0f},
	{RK817_CODEC_DI2S_TXCR3_TXCMD, 0x88},
	{RK817_CODEC_DDAC_POPD_DACST, 0x02},
	/* 0x29: -18db to 27db */
	{RK817_CODEC_DMIC_PGA_GAIN, 0x99},
};

#define RK817_CODEC_CAPTURE_POWER_UP_LIST_LEN \
	ARRAY_SIZE(capture_power_up_list)

static struct rk817_reg_val_typ capture_power_down_list[] = {
	{RK817_CODEC_AADC_CFG0, 0xc8},
	{RK817_CODEC_AMIC_CFG0, 0x70},
};

#define RK817_CODEC_CAPTURE_POWER_DOWN_LIST_LEN \
	ARRAY_SIZE(capture_power_down_list)

static int rk817_restart_dac_digital_clk_and_apll(struct snd_soc_codec *codec)
{
	snd_soc_update_bits(codec, RK817_CODEC_ADAC_CFG1,
			    PWD_DACBIAS_MASK, PWD_DACBIAS_DOWN);
	usleep_range(500, 600);
	snd_soc_update_bits(codec, RK817_CODEC_DTOP_DIGEN_CLKE,
			    DAC_DIG_CLK_MASK, DAC_DIG_CLK_DIS);
	usleep_range(500, 600);
	snd_soc_update_bits(codec, RK817_CODEC_DTOP_DIGEN_CLKE,
			    DAC_DIG_CLK_MASK, DAC_DIG_CLK_EN);
	DBG("%s: %d - Playback DIG CLK OPS\n", __func__, __LINE__);
	snd_soc_update_bits(codec, RK817_CODEC_APLL_CFG5,
			    PLL_PW_DOWN, PLL_PW_DOWN);
	usleep_range(50, 60);
	snd_soc_update_bits(codec, RK817_CODEC_APLL_CFG5,
			    PLL_PW_DOWN, PLL_PW_UP);
	usleep_range(500, 600);
	snd_soc_update_bits(codec, RK817_CODEC_ADAC_CFG1,
			    PWD_DACBIAS_MASK, PWD_DACBIAS_ON);

	return 0;
}

static int rk817_restart_adc_digital_clk_and_apll(struct snd_soc_codec *codec)
{
	snd_soc_update_bits(codec, RK817_CODEC_DTOP_DIGEN_CLKE,
			    ADC_DIG_CLK_MASK, ADC_DIG_CLK_DIS);
	usleep_range(500, 600);
	snd_soc_update_bits(codec, RK817_CODEC_DTOP_DIGEN_CLKE,
			    ADC_DIG_CLK_MASK, ADC_DIG_CLK_EN);
	DBG("%s: %d - Capture DIG CLK OPS\n", __func__, __LINE__);
	snd_soc_update_bits(codec, RK817_CODEC_APLL_CFG5,
			    PLL_PW_DOWN, PLL_PW_DOWN);
	usleep_range(50, 60);
	snd_soc_update_bits(codec, RK817_CODEC_APLL_CFG5,
			    PLL_PW_DOWN, PLL_PW_UP);
	usleep_range(500, 600);

	return 0;
}

static int rk817_codec_power_up(struct snd_soc_codec *codec, int type)
{
	struct rk817_codec_priv *rk817 = snd_soc_codec_get_drvdata(codec);
	int i;

	DBG("%s : power up %s %s %s\n", __func__,
	    type & RK817_CODEC_PLAYBACK ? "playback" : "",
	    type & RK817_CODEC_CAPTURE ? "capture" : "",
	    type & RK817_CODEC_INCALL ? "incall" : "");

	if (type & RK817_CODEC_PLAYBACK) {
		snd_soc_update_bits(codec, RK817_CODEC_DTOP_DIGEN_CLKE,
				    DAC_DIG_CLK_MASK, DAC_DIG_CLK_EN);
		for (i = 0; i < RK817_CODEC_PLAYBACK_POWER_UP_LIST_LEN; i++) {
			snd_soc_write(codec, playback_power_up_list[i].reg,
				      playback_power_up_list[i].value);
		}
	}

	if (type & RK817_CODEC_CAPTURE) {
		snd_soc_update_bits(codec, RK817_CODEC_DTOP_DIGEN_CLKE,
				    ADC_DIG_CLK_MASK, ADC_DIG_CLK_EN);
		for (i = 0; i < RK817_CODEC_CAPTURE_POWER_UP_LIST_LEN; i++) {
			snd_soc_write(codec, capture_power_up_list[i].reg,
				      capture_power_up_list[i].value);
		}

		if (rk817->mic_in_differential)
			snd_soc_update_bits(codec, RK817_CODEC_AMIC_CFG0,
					    MIC_DIFF_MASK, MIC_DIFF_EN);
		else
			snd_soc_update_bits(codec, RK817_CODEC_AMIC_CFG0,
					    MIC_DIFF_MASK, MIC_DIFF_DIS);

		if (rk817->pdmdata_out_enable)
			snd_soc_update_bits(codec, RK817_CODEC_DI2S_CKM,
					    PDM_EN_MASK, PDM_EN_ENABLE);

		snd_soc_write(codec, RK817_CODEC_DADC_VOLL,
			      rk817->capture_volume);
		snd_soc_write(codec, RK817_CODEC_DADC_VOLR,
			      rk817->capture_volume);
	}

	return 0;
}

static int rk817_codec_power_down(struct snd_soc_codec *codec, int type)
{
	int i;

	DBG("%s : power down %s %s %s\n", __func__,
	    type & RK817_CODEC_PLAYBACK ? "playback" : "",
	    type & RK817_CODEC_CAPTURE ? "capture" : "",
	    type & RK817_CODEC_INCALL ? "incall" : "");

	/* mute output for pop noise */
	if ((type & RK817_CODEC_PLAYBACK) ||
	    (type & RK817_CODEC_INCALL)) {
		snd_soc_update_bits(codec, RK817_CODEC_DDAC_MUTE_MIXCTL,
				    DACMT_ENABLE, DACMT_ENABLE);
	}

	if (type & RK817_CODEC_CAPTURE) {
		for (i = 0; i < RK817_CODEC_CAPTURE_POWER_DOWN_LIST_LEN; i++) {
			snd_soc_write(codec, capture_power_down_list[i].reg,
				      capture_power_down_list[i].value);
		}
		snd_soc_update_bits(codec, RK817_CODEC_DTOP_DIGEN_CLKE,
				    ADC_DIG_CLK_MASK, ADC_DIG_CLK_DIS);
	}

	if (type & RK817_CODEC_PLAYBACK) {
		for (i = 0; i < RK817_CODEC_PLAYBACK_POWER_DOWN_LIST_LEN; i++) {
			snd_soc_write(codec, playback_power_down_list[i].reg,
				      playback_power_down_list[i].value);
		}
		snd_soc_update_bits(codec, RK817_CODEC_DTOP_DIGEN_CLKE,
				    DAC_DIG_CLK_MASK, DAC_DIG_CLK_DIS);
	}

	if (type == RK817_CODEC_ALL) {
		for (i = 0; i < RK817_CODEC_PLAYBACK_POWER_DOWN_LIST_LEN; i++) {
			snd_soc_write(codec, playback_power_down_list[i].reg,
				      playback_power_down_list[i].value);
		}
		for (i = 0; i < RK817_CODEC_CAPTURE_POWER_DOWN_LIST_LEN; i++) {
			snd_soc_write(codec, capture_power_down_list[i].reg,
				      capture_power_down_list[i].value);
		}
		snd_soc_write(codec, RK817_CODEC_DTOP_DIGEN_CLKE, 0x00);
		snd_soc_write(codec, RK817_CODEC_APLL_CFG5, 0x01);
		snd_soc_write(codec, RK817_CODEC_AREF_RTCFG1, 0x06);
	}

	return 0;
}

/* For tiny alsa playback/capture/voice call path */
static const char * const rk817_playback_path_mode[] = {
#ifndef CONFIG_ARCH_ROCKCHIP_ODROIDGOA
	"OFF", "RCV", "SPK", "HP", "HP_NO_MIC", "BT", "SPK_HP", /* 0-6 */
	"RING_SPK", "RING_HP", "RING_HP_NO_MIC", "RING_SPK_HP"}; /* 7-10 */
#else
	"OFF", "SPK", "HP", "SPK_HP"};
#endif

static const char * const rk817_capture_path_mode[] = {
#ifndef CONFIG_ARCH_ROCKCHIP_ODROIDGOA
	"MIC OFF", "Main Mic", "Hands Free Mic", "BT Sco Mic"};

static const char * const rk817_call_path_mode[] = {
	"OFF", "RCV", "SPK", "HP", "HP_NO_MIC", "BT"}; /* 0-5 */

static const char * const rk817_modem_input_mode[] = {"OFF", "ON"};
#else
	"MIC OFF", "Main Mic"};
#endif

static SOC_ENUM_SINGLE_DECL(rk817_playback_path_type,
	0, 0, rk817_playback_path_mode);

static SOC_ENUM_SINGLE_DECL(rk817_capture_path_type,
	0, 0, rk817_capture_path_mode);

#ifndef CONFIG_ARCH_ROCKCHIP_ODROIDGOA
static SOC_ENUM_SINGLE_DECL(rk817_call_path_type,
	0, 0, rk817_call_path_mode);

static SOC_ENUM_SINGLE_DECL(rk817_modem_input_type,
	0, 0, rk817_modem_input_mode);
#endif

static int rk817_playback_path_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct rk817_codec_priv *rk817 = snd_soc_codec_get_drvdata(codec);

	DBG("%s : playback_path %ld\n", __func__, rk817->playback_path);

	ucontrol->value.integer.value[0] = rk817->playback_path;

	return 0;
}

static int rk817_playback_path_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct rk817_codec_priv *rk817 = snd_soc_codec_get_drvdata(codec);
	long int pre_path;

	if (rk817->playback_path == ucontrol->value.integer.value[0]) {
		DBG("%s : playback_path is not changed!\n",
		    __func__);
		return 0;
	}

	pre_path = rk817->playback_path;
	rk817->playback_path = ucontrol->value.integer.value[0];

	DBG("%s : set playback_path %ld, pre_path %ld\n",
	    __func__, rk817->playback_path, pre_path);

	// 在切换路径时先关闭所有GPIO避免冲突
	rk817_codec_ctl_gpio(rk817, CODEC_SET_SPK | CODEC_SET_HP, 0);

	if (rk817->playback_path != OFF)
		clk_prepare_enable(rk817->mclk);
	else
		clk_disable_unprepare(rk817->mclk);

	switch (rk817->playback_path) {
	case OFF:
#ifndef CONFIG_ARCH_ROCKCHIP_ODROIDGOA
		if (pre_path != OFF && (pre_path != HP_PATH &&
			pre_path != HP_NO_MIC && pre_path != RING_HP &&
			pre_path != RING_HP_NO_MIC)) {
			rk817_codec_power_down(codec, RK817_CODEC_PLAYBACK);
			if (rk817->capture_path == 0)
				rk817_codec_power_down(codec, RK817_CODEC_ALL);
		}
		break;
	case RCV:
	case SPK_PATH:
	case RING_SPK:
#else
		if (pre_path != OFF && (pre_path != HP_PATH)) {
			rk817_codec_power_down(codec, RK817_CODEC_PLAYBACK);
			if (rk817->capture_path == 0)
				rk817_codec_power_down(codec, RK817_CODEC_ALL);
		}
		break;
	case SPK_PATH:
#endif
		if (pre_path == OFF)
			rk817_codec_power_up(codec, RK817_CODEC_PLAYBACK);
		if (!rk817->use_ext_amplifier) {
			/* power on dac ibias/l/r */
			snd_soc_write(codec, RK817_CODEC_ADAC_CFG1,
				      PWD_DACBIAS_ON | PWD_DACD_ON |
				      PWD_DACL_ON | PWD_DACR_ON);
			/* CLASS D mode */
			snd_soc_write(codec, RK817_CODEC_DDAC_MUTE_MIXCTL, 0x10);
			/* CLASS D enable */
			snd_soc_write(codec, RK817_CODEC_ACLASSD_CFG1, 0xa5);
			/* restart CLASS D, OCPP/N */
			snd_soc_write(codec, RK817_CODEC_ACLASSD_CFG2, 0xc4);
		} else {
			/* HP_CP_EN , CP 2.3V */
			snd_soc_write(codec, RK817_CODEC_AHP_CP, 0x11);
			/* power on HP two stage opamp ,HP amplitude 0db */
			snd_soc_write(codec, RK817_CODEC_AHP_CFG0, 0x80);
			/* power on dac ibias/l/r */
			snd_soc_write(codec, RK817_CODEC_ADAC_CFG1,
					  PWD_DACBIAS_ON | PWD_DACD_DOWN |
					  PWD_DACL_ON | PWD_DACR_ON);
			snd_soc_update_bits(codec, RK817_CODEC_DDAC_MUTE_MIXCTL,
						DACMT_ENABLE, DACMT_DISABLE);
		}
#ifndef CONFIG_ARCH_ROCKCHIP_ODROIDGOA
		snd_soc_write(codec, RK817_CODEC_DDAC_VOLL, rk817->spk_volume);
		snd_soc_write(codec, RK817_CODEC_DDAC_VOLR, rk817->spk_volume);
		break;
	case HP_PATH:
	case HP_NO_MIC:
	case RING_HP:
	case RING_HP_NO_MIC:
#else
		break;
	case HP_PATH:
#endif
		if (pre_path == OFF)
			rk817_codec_power_up(codec, RK817_CODEC_PLAYBACK);
		/* HP_CP_EN , CP 2.3V */
		snd_soc_write(codec, RK817_CODEC_AHP_CP, 0x11);
		/* power on HP two stage opamp ,HP amplitude 0db */
		snd_soc_write(codec, RK817_CODEC_AHP_CFG0, 0x80);
		/* power on dac ibias/l/r */
		snd_soc_write(codec, RK817_CODEC_ADAC_CFG1,
			      PWD_DACBIAS_ON | PWD_DACD_DOWN |
			      PWD_DACL_ON | PWD_DACR_ON);
		snd_soc_update_bits(codec, RK817_CODEC_DDAC_MUTE_MIXCTL,
				    DACMT_ENABLE, DACMT_DISABLE);

#ifndef CONFIG_ARCH_ROCKCHIP_ODROIDGOA
		snd_soc_write(codec, RK817_CODEC_DDAC_VOLL, rk817->hp_volume);
		snd_soc_write(codec, RK817_CODEC_DDAC_VOLR, rk817->hp_volume);
		break;
	case BT:
		break;
	case SPK_HP:
	case RING_SPK_HP:
#else
		break;
	case SPK_HP:
#endif
		if (pre_path == OFF)
			rk817_codec_power_up(codec, RK817_CODEC_PLAYBACK);

		/* HP_CP_EN , CP 2.3V  */
		snd_soc_write(codec, RK817_CODEC_AHP_CP, 0x11);
		/* power on HP two stage opamp ,HP amplitude 0db */
		snd_soc_write(codec, RK817_CODEC_AHP_CFG0, 0x80);

		/* power on dac ibias/l/r */
		snd_soc_write(codec, RK817_CODEC_ADAC_CFG1,
			      PWD_DACBIAS_ON | PWD_DACD_ON |
			      PWD_DACL_ON | PWD_DACR_ON);

		if (!rk817->use_ext_amplifier) {
			/* CLASS D mode */
			snd_soc_write(codec, RK817_CODEC_DDAC_MUTE_MIXCTL, 0x10);
			/* CLASS D enable */
			snd_soc_write(codec, RK817_CODEC_ACLASSD_CFG1, 0xa5);
			/* restart CLASS D, OCPP/N */
			snd_soc_write(codec, RK817_CODEC_ACLASSD_CFG2, 0xc4);
		}

#ifndef CONFIG_ARCH_ROCKCHIP_ODROIDGOA
		snd_soc_write(codec, RK817_CODEC_DDAC_VOLL, rk817->hp_volume);
		snd_soc_write(codec, RK817_CODEC_DDAC_VOLR, rk817->hp_volume);
#endif
		break;
	default:
		return -EINVAL;
	}

	//根据新路径设置GPIO状态
	switch (rk817->playback_path) {
	case SPK_PATH:
#ifndef CONFIG_ARCH_ROCKCHIP_ODROIDGOA
	case RING_SPK:
#endif
		rk817_codec_ctl_gpio(rk817, CODEC_SET_SPK, 1);
		break;
	case HP_PATH:
#ifndef CONFIG_ARCH_ROCKCHIP_ODROIDGOA
	case HP_NO_MIC:
	case RING_HP:
	case RING_HP_NO_MIC:
#endif
		rk817_codec_ctl_gpio(rk817, CODEC_SET_HP, 1);
		break;
	case SPK_HP:
#ifndef CONFIG_ARCH_ROCKCHIP_ODROIDGOA
	case RING_SPK_HP:
#endif
		rk817_codec_ctl_gpio(rk817, CODEC_SET_SPK | CODEC_SET_HP, 1);
		break;
	default:
		break;
	}

#ifdef CONFIG_ARCH_ROCKCHIP_ODROIDGOA
	/* A path switch resets the volume/mute registers; re-apply the user state */
	rk817_codec_apply_volume(codec);
#endif

	return 0;
}

static int rk817_capture_path_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct rk817_codec_priv *rk817 = snd_soc_codec_get_drvdata(codec);

	DBG("%s : capture_path %ld\n", __func__,
	    rk817->capture_path);

	ucontrol->value.integer.value[0] = rk817->capture_path;

	return 0;
}

static int rk817_capture_path_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct rk817_codec_priv *rk817 = snd_soc_codec_get_drvdata(codec);
	long int pre_path;

	if (rk817->capture_path == ucontrol->value.integer.value[0]) {
		DBG("%s : capture_path is not changed!\n",
		    __func__);
		return 0;
	}

	pre_path = rk817->capture_path;
	rk817->capture_path = ucontrol->value.integer.value[0];

	DBG("%s : set capture_path %ld, pre_path %ld\n", __func__,
	    rk817->capture_path, pre_path);

	if (rk817->capture_path != MIC_OFF)
		clk_prepare_enable(rk817->mclk);
	else
		clk_disable_unprepare(rk817->mclk);

	switch (rk817->capture_path) {
	case MIC_OFF:
		if (pre_path != MIC_OFF)
			rk817_codec_power_down(codec, RK817_CODEC_CAPTURE);
		break;
	case MAIN_MIC:
		if (pre_path == MIC_OFF)
			rk817_codec_power_up(codec, RK817_CODEC_CAPTURE);

		if (rk817->adc_for_loopback) {
			/* don't need to gain when adc use for loopback */
			snd_soc_update_bits(codec, RK817_CODEC_AMIC_CFG0, 0xf, 0x0);
			snd_soc_write(codec, RK817_CODEC_DMIC_PGA_GAIN, 0x66);
			snd_soc_write(codec, RK817_CODEC_DADC_VOLL, 0x00);
			snd_soc_write(codec, RK817_CODEC_DADC_VOLR, 0x00);
			break;
		}
		if (!rk817->mic_in_differential) {
			snd_soc_write(codec, RK817_CODEC_DADC_VOLL, 0xff);
			snd_soc_update_bits(codec, RK817_CODEC_AADC_CFG0,
					    ADC_L_PWD_MASK, ADC_L_PWD_EN);
			snd_soc_update_bits(codec, RK817_CODEC_AMIC_CFG0,
					    PWD_PGA_L_MASK, PWD_PGA_L_EN);
		}
		break;
#ifndef CONFIG_ARCH_ROCKCHIP_ODROIDGOA
	case HANDS_FREE_MIC:
		if (pre_path == MIC_OFF)
			rk817_codec_power_up(codec, RK817_CODEC_CAPTURE);

		if (rk817->adc_for_loopback) {
			/* don't need to gain when adc use for loopback */
			snd_soc_update_bits(codec, RK817_CODEC_AMIC_CFG0, 0xf, 0x0);
			snd_soc_write(codec, RK817_CODEC_DMIC_PGA_GAIN, 0x66);
			snd_soc_write(codec, RK817_CODEC_DADC_VOLL, 0x00);
			snd_soc_write(codec, RK817_CODEC_DADC_VOLR, 0x00);
			break;
		}
		if (!rk817->mic_in_differential) {
			snd_soc_write(codec, RK817_CODEC_DADC_VOLL, 0xff);
			snd_soc_update_bits(codec, RK817_CODEC_AADC_CFG0,
	 				    ADC_L_PWD_MASK, ADC_L_PWD_EN);
	 		snd_soc_update_bits(codec, RK817_CODEC_AMIC_CFG0,
	 				    PWD_PGA_L_MASK, PWD_PGA_L_EN);
	 	}
	 	break;
	case BT_SCO_MIC:
		break;
#endif
	default:
		return -EINVAL;
	}

	return 0;
}

static struct snd_kcontrol_new rk817_snd_path_controls[] = {
	SOC_ENUM_EXT("Playback Path", rk817_playback_path_type,
		     rk817_playback_path_get, rk817_playback_path_put),

	SOC_ENUM_EXT("Capture MIC Path", rk817_capture_path_type,
		     rk817_capture_path_get, rk817_capture_path_put),
};

static int rk817_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct rk817_codec_priv *rk817 = snd_soc_codec_get_drvdata(codec);

	rk817->stereo_sysclk = freq;

	DBG("%s : MCLK = %dHz\n", __func__, rk817->stereo_sysclk);

	return 0;
}

static int rk817_set_dai_fmt(struct snd_soc_dai *codec_dai,
			     unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	unsigned int i2s_mst = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		i2s_mst |= RK817_I2S_MODE_SLV;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		i2s_mst |= RK817_I2S_MODE_MST;
		break;
	default:
		dev_err(codec->dev, "%s : set master mask failed!\n",
			__func__);
		return -EINVAL;
	}
	DBG("%s : i2s %s mode\n", __func__,
	    i2s_mst ? "master" : "slave");

	snd_soc_update_bits(codec, RK817_CODEC_DI2S_CKM,
			    RK817_I2S_MODE_MASK, i2s_mst);

	return 0;
}

static int rk817_hw_params(struct snd_pcm_substream *substream,
			   struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	struct rk817_codec_priv *rk817 = snd_soc_codec_get_drvdata(codec);
	unsigned int rate = params_rate(params);
	unsigned char apll_cfg3_val;
	unsigned char dtop_digen_sr_lmt0;

	DBG("%s : MCLK = %dHz, sample rate = %dHz\n",
	    __func__, rk817->stereo_sysclk, rate);

	/* chip_ver=0 视为异常，使用上游默认配置 */
	if (rk817->chip_ver > 0 && rk817->chip_ver <= 0x4) {
		DBG("%s: 0x4 and previous versions\n", __func__);
		snd_soc_write(codec, RK817_CODEC_APLL_CFG0, 0x0c);
		snd_soc_write(codec, RK817_CODEC_APLL_CFG4, 0x95);
	} else {
		DBG("%s: using default APLL config (ver=0x%x)\n",
		    __func__, rk817->chip_ver);
		snd_soc_write(codec, RK817_CODEC_APLL_CFG0, 0x04);
		snd_soc_write(codec, RK817_CODEC_APLL_CFG4, 0xa5);
	}

	switch (rate) {
	case 8000:
		apll_cfg3_val = 0x03;
		dtop_digen_sr_lmt0 = 0x00;
		break;
	case 16000:
		apll_cfg3_val = 0x06;
		dtop_digen_sr_lmt0 = 0x01;
		break;
	case 96000:
		apll_cfg3_val = 0x18;
		dtop_digen_sr_lmt0 = 0x03;
		break;
	case 32000:
	case 44100:
	case 48000:
		apll_cfg3_val = 0x0c;
		dtop_digen_sr_lmt0 = 0x02;
		break;
	default:
		pr_err("Unsupported rate: %d\n", rate);
		return -EINVAL;
	}

	if (!((substream->stream == SNDRV_PCM_STREAM_CAPTURE) && rk817->pdmdata_out_enable)) {
		/* 根据芯片版本更新 APLL 配置，chip_ver=0 视为异常使用上游默认值 */
		if (rk817->chip_ver > 0 && rk817->chip_ver <= 0x4) {
			DBG("%s: 0x4 and previous versions, update APLL\n", __func__);
			snd_soc_write(codec, RK817_CODEC_APLL_CFG0, 0x0c);
			snd_soc_write(codec, RK817_CODEC_APLL_CFG4, 0x95);
		} else {
			DBG("%s: using default APLL config (ver=0x%x)\n",
			    __func__, rk817->chip_ver);
			snd_soc_write(codec, RK817_CODEC_APLL_CFG0, 0x04);
			snd_soc_write(codec, RK817_CODEC_APLL_CFG4, 0xa5);
		}

		/* 更新采样率相关配置 */
		snd_soc_write(codec, RK817_CODEC_APLL_CFG3, apll_cfg3_val);
		snd_soc_update_bits(codec, RK817_CODEC_DDAC_SR_LMT0,
				    DACSRT_MASK, dtop_digen_sr_lmt0);

		/* 重启前先静音，防止 DACBIAS 断开产生 pop 噪声 */
		snd_soc_update_bits(codec, RK817_CODEC_DDAC_MUTE_MIXCTL,
				    DACMT_ENABLE, DACMT_ENABLE);

		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			rk817_restart_dac_digital_clk_and_apll(codec);
		else
			rk817_restart_adc_digital_clk_and_apll(codec);

		/* 重启后恢复静音状态 */
		snd_soc_update_bits(codec, RK817_CODEC_DDAC_MUTE_MIXCTL,
				    DACMT_ENABLE, DACMT_DISABLE);
	}

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		snd_soc_write(codec, RK817_CODEC_DI2S_RXCR2, VDW_RX_16BITS);
		snd_soc_write(codec, RK817_CODEC_DI2S_TXCR2, VDW_TX_16BITS);
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
	case SNDRV_PCM_FORMAT_S32_LE:
		snd_soc_write(codec, RK817_CODEC_DI2S_RXCR2, VDW_RX_24BITS);
		snd_soc_write(codec, RK817_CODEC_DI2S_TXCR2, VDW_TX_24BITS);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int rk817_digital_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rk817_codec_priv *rk817 = snd_soc_codec_get_drvdata(codec);

	DBG("%s %d, playback_path %ld\n", __func__, mute, rk817->playback_path);
	
#ifdef CONFIG_ARCH_ROCKCHIP_ODROIDGOA
	/* Keep the DAC muted at minimum volume; stream start/stop must not unmute it */
	if (rk817->vol_at_floor) {
		snd_soc_update_bits(codec, RK817_CODEC_DDAC_MUTE_MIXCTL,
				    DACMT_ENABLE, DACMT_ENABLE);
		return 0;
	}
#endif

	if (mute)
		snd_soc_update_bits(codec, RK817_CODEC_DDAC_MUTE_MIXCTL,
				    DACMT_ENABLE, DACMT_ENABLE);
	else
		snd_soc_update_bits(codec, RK817_CODEC_DDAC_MUTE_MIXCTL,
				    DACMT_ENABLE, DACMT_DISABLE);

	//完整的GPIO控制逻辑
	if (mute) {
		/* 静音时关闭所有GPIO */
		rk817_codec_ctl_gpio(rk817, CODEC_SET_SPK, 0);
		rk817_codec_ctl_gpio(rk817, CODEC_SET_HP, 0);
	} else {
		/* 取消静音时根据播放路径设置GPIO */
		switch (rk817->playback_path) {
		case SPK_PATH:
#ifndef CONFIG_ARCH_ROCKCHIP_ODROIDGOA
		case RING_SPK:
#endif
			rk817_codec_ctl_gpio(rk817, CODEC_SET_SPK, 1);
			rk817_codec_ctl_gpio(rk817, CODEC_SET_HP, 0);
			break;
		case HP_PATH:
#ifndef CONFIG_ARCH_ROCKCHIP_ODROIDGOA
		case HP_NO_MIC:
		case RING_HP:
		case RING_HP_NO_MIC:
#endif
			rk817_codec_ctl_gpio(rk817, CODEC_SET_SPK, 0);
			rk817_codec_ctl_gpio(rk817, CODEC_SET_HP, 1);
			break;
		case SPK_HP:
#ifndef CONFIG_ARCH_ROCKCHIP_ODROIDGOA
		case RING_SPK_HP:
#endif
			rk817_codec_ctl_gpio(rk817, CODEC_SET_SPK, 1);
			rk817_codec_ctl_gpio(rk817, CODEC_SET_HP, 1);
			break;
		default:
			rk817_codec_ctl_gpio(rk817, CODEC_SET_SPK, 0);
			rk817_codec_ctl_gpio(rk817, CODEC_SET_HP, 0);
			break;
		}
	}

	return 0;
}

#define RK817_PLAYBACK_RATES (SNDRV_PCM_RATE_8000 |\
			      SNDRV_PCM_RATE_16000 |	\
			      SNDRV_PCM_RATE_32000 |	\
			      SNDRV_PCM_RATE_44100 |	\
			      SNDRV_PCM_RATE_48000 |	\
			      SNDRV_PCM_RATE_96000)

#define RK817_CAPTURE_RATES (SNDRV_PCM_RATE_8000 |\
			      SNDRV_PCM_RATE_16000 |	\
			      SNDRV_PCM_RATE_32000 |	\
			      SNDRV_PCM_RATE_44100 |	\
			      SNDRV_PCM_RATE_48000 |	\
			      SNDRV_PCM_RATE_96000)

#define RK817_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
			SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S24_LE |\
			SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_ops rk817_dai_ops = {
	.hw_params	= rk817_hw_params,
	.set_fmt	= rk817_set_dai_fmt,
	.set_sysclk	= rk817_set_dai_sysclk,
	.digital_mute	= rk817_digital_mute,
};

static struct snd_soc_dai_driver rk817_dai[] = {
	{
		.name = "rk817-hifi",
		.id = RK817_HIFI,
		.playback = {
			.stream_name = "HiFi Playback",
			.channels_min = 2,
			.channels_max = 8,
			.rates = RK817_PLAYBACK_RATES,
			.formats = RK817_FORMATS,
		},
		.capture = {
			.stream_name = "HiFi Capture",
			.channels_min = 2,
			.channels_max = 8,
			.rates = RK817_CAPTURE_RATES,
			.formats = RK817_FORMATS,
		},
		.ops = &rk817_dai_ops,
	},
	{
		.name = "rk817-voice",
		.id = RK817_VOICE,
		.playback = {
			.stream_name = "Voice Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RK817_PLAYBACK_RATES,
			.formats = RK817_FORMATS,
		},
		.capture = {
			.stream_name = "Voice Capture",
			.channels_min = 2,
			.channels_max = 8,
			.rates = RK817_CAPTURE_RATES,
			.formats = RK817_FORMATS,
		},
		.ops = &rk817_dai_ops,
	},

};

static int rk817_suspend(struct snd_soc_codec *codec)
{
	rk817_codec_power_down(codec, RK817_CODEC_ALL);
	return 0;
}

static int rk817_resume(struct snd_soc_codec *codec)
{
	struct rk817_codec_priv *rk817 = snd_soc_codec_get_drvdata(codec);

	rk817_codec_power_up(codec, RK817_CODEC_ALL);

	DBG("[%s] playback path %ld, spk %d, hp %d\n", __func__,
		rk817->playback_path,
		rk817->spk_volume, rk817->hp_volume);

	switch (rk817->playback_path) {
	case OFF:
		break;
	case SPK_PATH:
		if (!rk817->use_ext_amplifier) {
			/* power on dac ibias/l/r */
			snd_soc_write(codec, RK817_CODEC_ADAC_CFG1,
				      PWD_DACBIAS_ON | PWD_DACD_ON |
				      PWD_DACL_ON | PWD_DACR_ON);
			/* CLASS D mode */
			snd_soc_write(codec, RK817_CODEC_DDAC_MUTE_MIXCTL, 0x10);
			/* CLASS D enable */
			snd_soc_write(codec, RK817_CODEC_ACLASSD_CFG1, 0xa5);
			/* restart CLASS D, OCPP/N */
			snd_soc_write(codec, RK817_CODEC_ACLASSD_CFG2, 0xc4);
		} else {
			/* HP_CP_EN , CP 2.3V */
			snd_soc_write(codec, RK817_CODEC_AHP_CP, 0x11);
			/* power on HP two stage opamp ,HP amplitude 0db */
			snd_soc_write(codec, RK817_CODEC_AHP_CFG0, 0x80);
			/* power on dac ibias/l/r */
			snd_soc_write(codec, RK817_CODEC_ADAC_CFG1,
					  PWD_DACBIAS_ON | PWD_DACD_DOWN |
					  PWD_DACL_ON | PWD_DACR_ON);
			snd_soc_update_bits(codec, RK817_CODEC_DDAC_MUTE_MIXCTL,
						DACMT_ENABLE, DACMT_DISABLE);
		}
		break;
	case HP_PATH:
		/* HP_CP_EN , CP 2.3V */
		snd_soc_write(codec, RK817_CODEC_AHP_CP, 0x11);
		/* power on HP two stage opamp ,HP amplitude 0db */
		snd_soc_write(codec, RK817_CODEC_AHP_CFG0, 0x80);
		/* power on dac ibias/l/r */
		snd_soc_write(codec, RK817_CODEC_ADAC_CFG1,
			      PWD_DACBIAS_ON | PWD_DACD_DOWN |
			      PWD_DACL_ON | PWD_DACR_ON);
		snd_soc_update_bits(codec, RK817_CODEC_DDAC_MUTE_MIXCTL,
				    DACMT_ENABLE, DACMT_DISABLE);
		break;
	case SPK_HP:
		/* HP_CP_EN , CP 2.3V  */
		snd_soc_write(codec, RK817_CODEC_AHP_CP, 0x11);
		/* power on HP two stage opamp ,HP amplitude 0db */
		snd_soc_write(codec, RK817_CODEC_AHP_CFG0, 0x80);

		/* power on dac ibias/l/r */
		snd_soc_write(codec, RK817_CODEC_ADAC_CFG1,
			      PWD_DACBIAS_ON | PWD_DACD_ON |
			      PWD_DACL_ON | PWD_DACR_ON);

		if (!rk817->use_ext_amplifier) {
			/* CLASS D mode */
			snd_soc_write(codec, RK817_CODEC_DDAC_MUTE_MIXCTL, 0x10);
			/* CLASS D enable */
			snd_soc_write(codec, RK817_CODEC_ACLASSD_CFG1, 0xa5);
			/* restart CLASS D, OCPP/N */
			snd_soc_write(codec, RK817_CODEC_ACLASSD_CFG2, 0xc4);
		}
		break;
	default:
		return -EINVAL;
	}

#ifdef CONFIG_ARCH_ROCKCHIP_ODROIDGOA
	/* Resume power-up resets the volume/mute registers; restore user state */
	rk817_codec_apply_volume(codec);
#endif

	return 0;
}

static int rk817_probe(struct snd_soc_codec *codec)
{
	struct rk817_codec_priv *rk817 = snd_soc_codec_get_drvdata(codec);

	DBG("%s\n", __func__);

	if (!rk817) {
		dev_err(codec->dev, "%s : rk817 priv is NULL!\n",
			__func__);
		return -EINVAL;
	}
	rk817->codec = codec;
	rk817->playback_path = OFF;
	rk817->capture_path = MIC_OFF;

	/* 始终启用 mclk，在 rk817_remove 中禁用 */
	/* 读取芯片版本 */
	{
		int chip_name = snd_soc_read(codec, RK817_PMIC_CHIP_NAME);
		int chip_ver = snd_soc_read(codec, RK817_PMIC_CHIP_VER);
		rk817->chip_ver = (chip_ver & 0x0f);
		dev_info(codec->dev, "%s: chip_name:0x%x, chip_ver:0x%x\n",
			 __func__, chip_name, chip_ver);
	}

	rk817_reset(codec);

#ifdef CONFIG_ARCH_ROCKCHIP_ODROIDGOA
	/*
	 * 更新TLV显示范围
	 * DECLARE_TLV_DB_SCALE创建的数组格式: [类型, 长度, min_dB, step]
	 * rk817_vol_tlv[2] = min_dB
	 */
	rk817_vol_tlv[2] = rk817->volume_min_db;
	dev_info(codec->dev, "%s: volume range %d dB to -6 dB\n",
		 __func__, rk817->volume_min_db / 100);

	rk817->user_vol = 0;
	rk817->vol_at_floor = true;
	rk817->last_vol_at_floor = true;
#endif

	snd_soc_add_codec_controls(codec, rk817_snd_path_controls,
				   ARRAY_SIZE(rk817_snd_path_controls));
	return 0;
}

/* power down chip */
static int rk817_remove(struct snd_soc_codec *codec)
{
	struct rk817_codec_priv *rk817 = snd_soc_codec_get_drvdata(codec);

	DBG("%s\n", __func__);

	if (!rk817) {
		dev_err(codec->dev, "%s : rk817 is NULL\n", __func__);
		return 0;
	}

	rk817_codec_power_down(codec, RK817_CODEC_ALL);
	mdelay(10);
	clk_disable_unprepare(rk817->mclk);

	return 0;
}

static struct regmap *rk817_get_regmap(struct device *dev)
{
	struct rk817_codec_priv *rk817 = dev_get_drvdata(dev);

	return rk817->regmap;
}

static struct snd_soc_codec_driver soc_codec_dev_rk817 = {
	.probe = rk817_probe,
	.remove = rk817_remove,
	.get_regmap = rk817_get_regmap,
	.suspend = rk817_suspend,
	.resume = rk817_resume,
#ifdef CONFIG_ARCH_ROCKCHIP_ODROIDGOA
	.controls = rk817_dac_controls,
	.num_controls = ARRAY_SIZE(rk817_dac_controls),
#endif
};

static int rk817_codec_parse_dt_property(struct device *dev,
					 struct rk817_codec_priv *rk817)
{
	struct device_node *node = dev->parent->of_node;
	int ret;

	DBG("%s()\n", __func__);

	if (!node) {
		dev_err(dev, "%s() dev->parent->of_node is NULL\n",
			__func__);
		return -ENODEV;
	}

	node = of_get_child_by_name(dev->parent->of_node, "codec");
	if (!node) {
		dev_err(dev, "%s() Can not get child: codec\n",
			__func__);
		return -ENODEV;
	}

	//支持spk-ctl-gpios和hp-ctl-gpios属性
	// 首先尝试获取 hp-ctl-gpios，如果失败再尝试 hp-ctl
	rk817->hp_ctl_gpio = devm_gpiod_get_optional(dev, "hp-ctl-gpios",
						  GPIOD_OUT_LOW);
	if (IS_ERR_OR_NULL(rk817->hp_ctl_gpio)) {
		rk817->hp_ctl_gpio = devm_gpiod_get_optional(dev, "hp-ctl",
						  GPIOD_OUT_LOW);
	}
	if (!IS_ERR_OR_NULL(rk817->hp_ctl_gpio)) {
		DBG("%s : hp-ctl-gpio %d\n", __func__,
		    desc_to_gpio(rk817->hp_ctl_gpio));
	}

	// 首先尝试获取 spk-ctl-gpios，如果失败再尝试 spk-ctl
	rk817->spk_ctl_gpio = devm_gpiod_get_optional(dev, "spk-ctl-gpios",
						  GPIOD_OUT_LOW);
	if (IS_ERR_OR_NULL(rk817->spk_ctl_gpio)) {
		rk817->spk_ctl_gpio = devm_gpiod_get_optional(dev, "spk-ctl",
						  GPIOD_OUT_LOW);
	}
	if (!IS_ERR_OR_NULL(rk817->spk_ctl_gpio)) {
		DBG("%s : spk-ctl-gpio %d\n", __func__,
		    desc_to_gpio(rk817->spk_ctl_gpio));
	}

	ret = of_property_read_u32(node, "spk-mute-delay-ms",
				   &rk817->spk_mute_delay);
	if (ret < 0) {
		DBG("%s() Can not read property spk-mute-delay-ms\n",
			__func__);
		rk817->spk_mute_delay = 0;
	}

	ret = of_property_read_u32(node, "hp-mute-delay-ms",
				   &rk817->hp_mute_delay);
	if (ret < 0) {
		DBG("%s() Can not read property hp-mute-delay-ms\n",
		    __func__);
		rk817->hp_mute_delay = 0;
	}
	DBG("spk mute delay %dms --- hp mute delay %dms\n",
	    rk817->spk_mute_delay, rk817->hp_mute_delay);

	ret = of_property_read_u32(node, "spk-volume", &rk817->spk_volume);
	if (ret < 0) {
		DBG("%s() Can not read property spk-volume\n", __func__);
		rk817->spk_volume = OUT_VOLUME;
	}
	if (rk817->spk_volume < 3)
		rk817->spk_volume = 3;

	ret = of_property_read_u32(node, "hp-volume",
				   &rk817->hp_volume);
	if (ret < 0) {
		DBG("%s() Can not read property hp-volume\n",
		    __func__);
		rk817->hp_volume = OUT_VOLUME;
	}
	if (rk817->hp_volume < 3)
		rk817->hp_volume = 3;

	ret = of_property_read_u32(node, "capture-volume",
				   &rk817->capture_volume);
	if (ret < 0) {
		DBG("%s() Can not read property capture-volume\n",
		    __func__);
		rk817->capture_volume = CAPTURE_VOLUME;
	}

	rk817->mic_in_differential =
			of_property_read_bool(node, "mic-in-differential");

	rk817->pdmdata_out_enable =
			of_property_read_bool(node, "pdmdata-out-enable");

	rk817->use_ext_amplifier =
			of_property_read_bool(node, "use-ext-amplifier");

	rk817->adc_for_loopback =
			of_property_read_bool(node, "adc-for-loopback");

	/*
	 * 解析动态音量最小值
	 * DTS中使用正整数表示dB绝对值，如 volume-min-db = <72>; 表示 -27dB
	 * 驱动内部转换为负值并使用0.01dB单位
	 * 最大音量固定为-6.75dB（寄存器0x12），与上游保持一致
	 */
	ret = of_property_read_u32(node, "volume-min-db", &rk817->volume_min_db);
	if (ret < 0) {
		DBG("%s() Can not read property volume-min-db, use default 95dB\n",
		    __func__);
		rk817->volume_min_db = 95;  /* 默认-95dB */
	}
	/* 转换为负值并以0.01dB单位存储 */
	rk817->volume_min_db = -rk817->volume_min_db * 100;

	DBG("volume range: %d dB to -6 dB\n", rk817->volume_min_db / 100);

	return 0;
}

static const struct regmap_config rk817_codec_regmap_config = {
	.name = "rk817-codec",
	.reg_bits = 8,
	.val_bits = 8,
	.reg_stride = 1,
	.max_register = 0x4f,
	.cache_type = REGCACHE_NONE,
	.volatile_reg = rk817_volatile_register,
	.writeable_reg = rk817_codec_register,
	.readable_reg = rk817_codec_register,
	.reg_defaults = rk817_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(rk817_reg_defaults),
};

static int rk817_platform_probe(struct platform_device *pdev)
{
	struct rk808 *rk817 = dev_get_drvdata(pdev->dev.parent);
	struct rk817_codec_priv *rk817_codec_data;
	int ret;

	DBG("%s\n", __func__);

	if (!rk817) {
		dev_err(&pdev->dev, "%s : rk817 is NULL\n", __func__);
		return -EINVAL;
	}

	rk817_codec_data = devm_kzalloc(&pdev->dev,
					sizeof(struct rk817_codec_priv),
					GFP_KERNEL);
	if (!rk817_codec_data)
		return -ENOMEM;

	platform_set_drvdata(pdev, rk817_codec_data);

	ret = rk817_codec_parse_dt_property(&pdev->dev, rk817_codec_data);
	if (ret < 0) {
		dev_err(&pdev->dev, "%s() parse device tree property error %d\n",
			__func__, ret);
		goto err_;
	}

	rk817_codec_data->regmap = devm_regmap_init_i2c(rk817->i2c,
					    &rk817_codec_regmap_config);
	if (IS_ERR(rk817_codec_data->regmap)) {
		ret = PTR_ERR(rk817_codec_data->regmap);
		dev_err(&pdev->dev, "failed to allocate register map: %d\n",
			ret);
		goto err_;
	}

	rk817_codec_data->mclk = devm_clk_get(&pdev->dev, "mclk");
	if (IS_ERR(rk817_codec_data->mclk)) {
		dev_err(&pdev->dev, "Unable to get mclk\n");
		ret = -ENXIO;
		goto err_;
	}

	ret = snd_soc_register_codec(&pdev->dev, &soc_codec_dev_rk817,
				     rk817_dai, ARRAY_SIZE(rk817_dai));
	if (ret < 0) {
		dev_err(&pdev->dev, "%s() register codec error %d\n",
			__func__, ret);
		goto err_;
	}

	return 0;
err_:

	return ret;
}

static int rk817_platform_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);

	return 0;
}

static void rk817_platform_shutdown(struct platform_device *pdev)
{
	struct rk817_codec_priv *rk817 = dev_get_drvdata(&pdev->dev);

	DBG("%s\n", __func__);

	rk817_codec_power_down(rk817->codec, RK817_CODEC_ALL);

}

static const struct of_device_id rk817_codec_dt_ids[] = {
	{ .compatible = "rockchip,rk817-codec" },
	{},
};
MODULE_DEVICE_TABLE(of, rk817_codec_dt_ids);

static struct platform_driver rk817_codec_driver = {
	.driver = {
		   .name = "rk817-codec",
		   .of_match_table = rk817_codec_dt_ids,
		   },
	.probe = rk817_platform_probe,
	.remove = rk817_platform_remove,
	.shutdown = rk817_platform_shutdown,
};

module_platform_driver(rk817_codec_driver);

MODULE_DESCRIPTION("ASoC RK817 codec driver");
MODULE_AUTHOR("binyuan <kevan.lan@rock-chips.com>");
MODULE_LICENSE("GPL v2");
