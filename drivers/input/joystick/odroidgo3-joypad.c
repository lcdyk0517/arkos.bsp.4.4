/*
 * ODROID-GO3 游戏掌机 SARADC 摇杆 & GPIO 按键驱动
 *
 * 支持三种 ADC 模式：
 *   - legacy:     使用模拟多路复用器(AMUX)的单 ADC 通道
 *   - direct-adc: 每轴独立 IIO 通道，无需 AMUX
 *   - split-adc:  双 ADC 通道，左右摇杆分离
 *
 * 其他功能支持：
 *   - 可配置有效电平的 GPIO 按键
 *   - 基于 ADC 的按键（电阻梯）
 *   - 组合键上报
 *   - 基于 PWM 或 GPIO 的振动马达
 *   - 通过 sysfs 运行时校准
 *   - 可选的ADC毛刺滤波 (button-adc-max-step, 丢弃超过阈值的孤立跳变)
 *   - 按键交换 sysfs (swap_ab/swap_xy: 分别交换A/B和X/Y, 0=原样上报)
 *
 * Copyright (c) 2026 Hardkernel Co.,LTD
 * Copyright (c) 2026 lcdyk
 *
 * 本程序是自由软件；您可以根据自由软件基金会发布的 GNU 通用公共许可证
 * 第 2 版或（您可以选择的）更高版本的条款重新发布和/或修改它。
 */

/*----------------------------------------------------------------------------*/
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/input-polldev.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio_keys.h>
#include <linux/iio/consumer.h>
#include <linux/iio/types.h>
#include <linux/property.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/pwm.h>

/*----------------------------------------------------------------------------*/
#define DRV_NAME "odroidgo3_joypad"

/*----------------------------------------------------------------------------*/
/* 调试：通过 sysfs 暴露摇杆调谐值 */
#define JOYPAD_DEBUG_TUNING	1	/* 设为 1 启用 sysfs 调谐接口 */

/*----------------------------------------------------------------------------*/
#define	ADC_MAX_VOLTAGE		1800
#define	ADC_DATA_TUNING(x, p)	((x * p) / 100)
#define	ADC_TUNING_DEFAULT	180
#define	CLAMP(x, low, high)  (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))

/*----------------------------------------------------------------------------*/
/* 摇杆切换功能：L3/R3 按键码 */
#define STICK_SWITCH_L3_CODE	BTN_TRIGGER_HAPPY3
#define STICK_SWITCH_R3_CODE	BTN_TRIGGER_HAPPY4

/*----------------------------------------------------------------------------*/
/*
	AMUX 通道选择真值表：
	+--------------------------------+
	| IIO 通道 : ADC_IN1             |
	+--------------+-----------------+-----------------+--------+---------+
	| EN(GPIO3.B5) | SEL_A(GPIO3.B3) | SEL_B(GPIO3.B0) | 选择   |  事件   |
	+--------------+-----------------+-----------------+--------+---------+
	|      0       |        0        |         0       |  R-Y   | ABS_RY  |
	+--------------+-----------------+-----------------+--------+---------+
	|      0       |        0        |         1       |  R-X   | ABS_RX  |
	+--------------+-----------------+-----------------+--------+---------+
	|      0       |        1        |         0       |  L-Y   |  ABS_Y  |
	+--------------+-----------------+-----------------+--------+---------+
	|      0       |        1        |         1       |  L-X   |  ABS_X  |
	+--------------+-----------------+-----------------+--------+---------+
	|      1       |        X        |         X       |  XXXX  |
	+--------------+-----------------+-----------------+--------+
*/
/*----------------------------------------------------------------------------*/
/* ADC 按钮/摇杆轴结构 */
struct bt_adc {
	struct iio_channel *channel;	/* direct-adc: 每轴独立 IIO 通道 */
	int value;			/* 上报值 (mV) */
	int reported;			/* 上次上报值 (毛刺滤波后, 反转前) */
	int prev;			/* 上一个采样值 (含丢弃的毛刺) */
#if JOYPAD_DEBUG_TUNING
	int raw;			/* 校准前的原始 ADC 值 */
#endif
	int report_type;		/* 上报类型 (EV_ABS) */
	int max, min;			/* 输入设备范围 (mV) */
	int cal;			/* 校准后的 ADC 值 */
	int scale;			/* ADC 缩放值 */
	bool invert;			/* 是否反转上报值 */
	int amux_ch;			/* AMUX 通道号 */
	int tuning_p, tuning_n;		/* ADC 调谐值（百分比），p=正方向，n=负方向 */
};

/* 模拟多路复用器结构 */
struct analog_mux {
	struct iio_channel *iio_ch;	/* IIO ADC 通道 */
	struct iio_channel *iio_ch_r;	/* split-adc: 右摇杆通道 */
	int sel_a_gpio, sel_b_gpio;	/* 模拟多路复用器选择(a,b) GPIO */
	int en_gpio;			/* 模拟多路复用器使能 GPIO */
};

/* GPIO/ADC 按钮结构 */
struct bt_gpio {
	const char *label;		/* GPIO 请求标签 */
	int num;			/* GPIO 编号 */
	int report_type;		/* 上报类型 (EV_KEY) */
	int linux_code;			/* Linux 按键码 */
	bool old_value;			/* 上一次按钮值 */
	bool active_level;		/* 按下有效电平 */

	/* ADC 按键支持 */
	bool is_adc;			/* true = ADC 按键, false = GPIO 按键 */
	int adc_value;			/* 按下检测的目标 ADC 值 */
	int adc_fuzz;			/* ADC 值匹配容差 */
	int combo_codes[4];		/* 组合键码 (0 = 未使用) */
	int combo_count;		/* 组合键数量 */
};

/* 主 joypad 上下文结构 */
struct joypad {
	struct device *dev;
	int poll_interval;		/* 轮询间隔 (ms) */

	bool enable;			/* 上报使能/禁用 */

	struct analog_mux *amux;	/* 模拟多路复用器控制 */
	int amux_count;			/* 模拟多路复用器通道数 */
	struct bt_adc *adcs;		/* ADC 摇杆轴数组 */

	bool direct_adc_mode;		/* direct-adc: 绕过 AMUX 直接读取 joy_x/joy_y */
	bool split_adc_mode;		/* split-adc: 双 ADC 通道 (joy_left/joy_right) */

	bool skip_absr;			/* 跳过整个右摇杆对 (ABS_RX/ABS_RY) */
	bool skip_absl;			/* 跳过整个左摇杆对 (ABS_X/ABS_Y) */

	/* 上报参考点反转 */
	bool invert_absx;
	bool invert_absy;
	bool invert_absrx;
	bool invert_absry;

	int bt_gpio_count;		/* GPIO 按键数量 */
	struct bt_gpio *gpios;		/* GPIO 按键数组 */

	struct iio_channel *adc_key_channel;	/* ADC 按键共享通道 */
	bool has_adc_keys;			/* 是否有 ADC 按键 */

	int auto_repeat;		/* 按键自动重复 */

	int swap_ab;			/* 交换A/B: 0=原样, 1=BTN_EAST<->BTN_SOUTH */
	int swap_xy;			/* 交换X/Y: 0=原样, 1=BTN_WEST<->BTN_NORTH */

	int bt_adc_fuzz, bt_adc_flat;	/* 上报阈值 (mV) */
	int bt_adc_scale;		/* ADC 读值缩放 */
	int bt_adc_deadzone;		/* 摇杆死区控制 */
	int bt_adc_max_step;		/* 毛刺判定阈值 (0 = 关闭滤波) */

	struct mutex lock;

	int debug_ch;			/* AMUX 调试通道 */

	/* PWM 振动马达 */
	struct input_dev *input;
	struct pwm_device *pwm;
	struct work_struct play_work;
	u16 level;			/* 振动强度 */
	u16 boost_weak;			/* 弱振动增强 */
	u16 boost_strong;		/* 强振动增强 */

	/* 可选 GPIO 振动器 */
	int rumble_gpio;
	bool rumble_active_low;		/* GPIO 有效低电平 */
	bool has_rumble;		/* 是否有振动功能 */

	/* 摇杆切换功能（按住切换键时左摇杆→右摇杆） */
	int stick_switch_code;		/* 切换键的 linux code */
	bool stick_switch_active;	/* 切换键是否按下 */
};

/* ---------- 振动辅助函数 (GPIO + PWM 备选) ---------- */
/**
 * pwm_vibrator_start() - 启动 PWM 振动马达
 * @joypad: joypad 上下文
 *
 * 返回: 0 成功，负数失败
 */
static int pwm_vibrator_start(struct joypad *joypad)
{
	struct device *pdev = joypad->input->dev.parent;
	struct pwm_state state;
	int err;

	pwm_get_state(joypad->pwm, &state);
	pwm_set_relative_duty_cycle(&state, joypad->level, 0xffff);
	state.enabled = true;

	err = pwm_apply_state(joypad->pwm, &state);
	if (err) {
		dev_err(pdev, "PWM 状态应用失败: %d", err);
		return err;
	}

	return 0;
}

/**
 * pwm_vibrator_stop() - 停止 PWM 振动马达
 * @joypad: joypad 上下文
 */
static void pwm_vibrator_stop(struct joypad *joypad)
{
	pwm_disable(joypad->pwm);
}

/**
 * rumble_gpio_set() - 设置 GPIO 振动器状态
 * @joypad: joypad 上下文
 * @on:     true=开启, false=关闭
 */
static inline void rumble_gpio_set(struct joypad *joypad, bool on)
{
	int val = on ? 1 : 0;
	if (joypad->rumble_active_low)
		val = !val;
	gpio_set_value(joypad->rumble_gpio, val);
}

static int joypad_vibrator_start(struct joypad *joypad)
{
	if (gpio_is_valid(joypad->rumble_gpio)) {
		rumble_gpio_set(joypad, true);
		return 0;
	}
	return pwm_vibrator_start(joypad);
}

static void joypad_vibrator_stop(struct joypad *joypad)
{
	if (gpio_is_valid(joypad->rumble_gpio)) {
		rumble_gpio_set(joypad, false);
		return;
	}
	pwm_vibrator_stop(joypad);
}

static void pwm_vibrator_play_work(struct work_struct *work)
{
	struct joypad *joypad = container_of(work,
					struct joypad, play_work);

	if (joypad->level)
		joypad_vibrator_start(joypad);
	else
		joypad_vibrator_stop(joypad);
}


/*----------------------------------------------------------------------------*/
//
// 设置为 boot.ini 文件中的值（如果存在）
//
/*----------------------------------------------------------------------------*/
static unsigned int g_button_adc_fuzz = 0;
static unsigned int g_button_adc_flat = 0;
static unsigned int g_button_adc_scale = 0;
static unsigned int g_button_adc_deadzone = 0;

static int button_adc_fuzz(char *str)
{
	if (!str)
		return -EINVAL;
	g_button_adc_fuzz = simple_strtoul(str, NULL, 10);
	return 0;
}
__setup("button-adc-fuzz=", button_adc_fuzz);

static int button_adc_flat(char *str)
{
	if (!str)
		return -EINVAL;
	g_button_adc_flat = simple_strtoul(str, NULL, 10);
	return 0;
}
__setup("button-adc-flat=", button_adc_flat);

static int button_adc_scale(char *str)
{
	if (!str)
		return -EINVAL;
	g_button_adc_scale = simple_strtoul(str, NULL, 10);
	return 0;
}
__setup("button-adc-scale=", button_adc_scale);

static int button_adc_deadzone(char *str)
{
	if (!str)
		return -EINVAL;
	g_button_adc_deadzone = simple_strtoul(str, NULL, 10);
	return 0;
}
__setup("button-adc-deadzone=", button_adc_deadzone);

/*----------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
static int joypad_amux_select(struct analog_mux *amux, int channel, bool split_adc_mode)
{

    /* 选择多路复用器通道：仅当我们确实有使能 GPIO 时 */
    if (gpio_is_valid(amux->en_gpio)) {
        gpio_set_value(amux->en_gpio, 0);
	}

	if (split_adc_mode) {
		/* 分离ADC: ch0/ch2=Y, ch1/ch3=X */
		gpio_set_value(amux->sel_a_gpio, (channel == 1 || channel == 3) ? 1 : 0);
		gpio_set_value(amux->sel_b_gpio, (channel == 1 || channel == 3) ? 1 : 0);
	} else {
		switch(channel) {
			case 0:	gpio_set_value(amux->sel_a_gpio, 0);
				gpio_set_value(amux->sel_b_gpio, 0); break;
			case 1:	gpio_set_value(amux->sel_a_gpio, 0);
				gpio_set_value(amux->sel_b_gpio, 1); break;
			case 2:	gpio_set_value(amux->sel_a_gpio, 1);
				gpio_set_value(amux->sel_b_gpio, 0); break;
			case 3:	gpio_set_value(amux->sel_a_gpio, 1);
				gpio_set_value(amux->sel_b_gpio, 1); break;
			default:
				if (gpio_is_valid(amux->en_gpio))
					gpio_set_value(amux->en_gpio, 1);
				return -1;
		}
	}
	/* 多路复用器切换后等待信号稳定 (10-20μs) */
	usleep_range(10, 20);
	return 0;
}

/*----------------------------------------------------------------------------*/
/**
 * joypad_adc_read() - 读取ADC通道值.
 * @joypad:    joypad设备上下文
 * @adc:       ADC轴结构体
 * @out_value: 输出参数, 返回读取到的值 (已缩放)
 *
 * 支持三种ADC模式:
 *   - 直接ADC模式: 每轴一个通道 ("joy_x", "joy_y"), 无需AMUX切换
 *   - 分离ADC模式: 双ADC (joy_left/joy_right), AMUX用于X/Y选择
 *   - 传统模式: AMUX选择 + 从 "amux_adc" 读取
 *
 * 返回: 0 成功, 负数失败
 */
static int joypad_adc_read(struct joypad *joypad, struct bt_adc *adc, int *out_value)
{
	int value, ret;
	int samples[3];
	int i;

	for (i = 0; i < 3; i++) {
		if (joypad->direct_adc_mode) {
			if (!adc->channel)
				return -ENODEV;
			ret = iio_read_channel_processed(adc->channel, &samples[i]);
			if (ret)
				return ret;
		} else if (joypad->split_adc_mode) {
			if (!joypad->amux)
				return -ENODEV;
			ret = joypad_amux_select(joypad->amux, adc->amux_ch, true);
			if (ret)
				return ret;
			if (adc->amux_ch <= 1)
				ret = iio_read_channel_processed(joypad->amux->iio_ch_r, &samples[i]);
			else
				ret = iio_read_channel_processed(joypad->amux->iio_ch, &samples[i]);
			if (ret)
				return ret;
		} else {
			if (!joypad->amux)
				return -ENODEV;
			ret = joypad_amux_select(joypad->amux, adc->amux_ch, false);
			if (ret)
				return ret;
			ret = iio_read_channel_processed(joypad->amux->iio_ch, &samples[i]);
			if (ret)
				return ret;
		}
		if (i < 2)
			usleep_range(5, 10);
	}

	if (samples[0] > samples[1])
		swap(samples[0], samples[1]);
	if (samples[1] > samples[2])
		swap(samples[1], samples[2]);
	if (samples[0] > samples[1])
		swap(samples[0], samples[1]);

	value = samples[1];

	*out_value = value * adc->scale;
	return 0;
}

/*----------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
/*
 * 属性:
 *
 * /sys/devices/platform/odroidgo2_joypad/poll_interval [读写]
 */
/*----------------------------------------------------------------------------*/
static ssize_t joypad_store_poll_interval(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf,
				      size_t count)
{
	struct platform_device *pdev  = to_platform_device(dev);
	struct joypad *joypad = platform_get_drvdata(pdev);

	mutex_lock(&joypad->lock);
	joypad->poll_interval = simple_strtoul(buf, NULL, 10);
	mutex_unlock(&joypad->lock);

	return count;
}

/*----------------------------------------------------------------------------*/
static ssize_t joypad_show_poll_interval(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct platform_device *pdev  = to_platform_device(dev);
	struct joypad *joypad = platform_get_drvdata(pdev);

	return sprintf(buf, "%d\n", joypad->poll_interval);
}

/*----------------------------------------------------------------------------*/
static DEVICE_ATTR(poll_interval, S_IWUSR | S_IRUGO,
		   joypad_show_poll_interval,
		   joypad_store_poll_interval);

/*----------------------------------------------------------------------------*/
/*
 * 属性:
 *
 * /sys/devices/platform/odroidgo3_joypad/adc_fuzz [读写/只读]
 * 用于过滤事件流中的噪声值
 * JOYPAD_DEBUG_TUNING=1 时可写, 否则只读
 */
/*----------------------------------------------------------------------------*/
static ssize_t joypad_show_adc_fuzz(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct platform_device *pdev  = to_platform_device(dev);
	struct joypad *joypad = platform_get_drvdata(pdev);

	return sprintf(buf, "%d\n", joypad->bt_adc_fuzz);
}

/*----------------------------------------------------------------------------*/
#if JOYPAD_DEBUG_TUNING
static ssize_t joypad_store_adc_fuzz(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf,
				      size_t count)
{
	struct platform_device *pdev  = to_platform_device(dev);
	struct joypad *joypad = platform_get_drvdata(pdev);

	mutex_lock(&joypad->lock);
	joypad->bt_adc_fuzz = simple_strtoul(buf, NULL, 10);
	mutex_unlock(&joypad->lock);

	return count;
}
#endif

/*----------------------------------------------------------------------------*/
static DEVICE_ATTR(adc_fuzz, S_IWUSR | S_IRUGO,
		   joypad_show_adc_fuzz,
#if JOYPAD_DEBUG_TUNING
		   joypad_store_adc_fuzz);
#else
		   NULL);
#endif

/*----------------------------------------------------------------------------*/
/*
 * 属性:
 *
 * /sys/devices/platform/odroidgo3_joypad/adc_flat [读写/只读]
 * 在此值范围内的值将被丢弃并报告为0
 * JOYPAD_DEBUG_TUNING=1 时可写, 否则只读
 */
/*----------------------------------------------------------------------------*/
static ssize_t joypad_show_adc_flat(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct platform_device *pdev  = to_platform_device(dev);
	struct joypad *joypad = platform_get_drvdata(pdev);

	return sprintf(buf, "%d\n", joypad->bt_adc_flat);
}

/*----------------------------------------------------------------------------*/
#if JOYPAD_DEBUG_TUNING
static ssize_t joypad_store_adc_flat(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf,
				      size_t count)
{
	struct platform_device *pdev  = to_platform_device(dev);
	struct joypad *joypad = platform_get_drvdata(pdev);

	mutex_lock(&joypad->lock);
	joypad->bt_adc_flat = simple_strtoul(buf, NULL, 10);
	mutex_unlock(&joypad->lock);

	return count;
}
#endif

/*----------------------------------------------------------------------------*/
static DEVICE_ATTR(adc_flat, S_IWUSR | S_IRUGO,
		   joypad_show_adc_flat,
#if JOYPAD_DEBUG_TUNING
		   joypad_store_adc_flat);
#else
		   NULL);
#endif

/*----------------------------------------------------------------------------*/
/*
 * 属性:
 *
 * /sys/devices/platform/odroidgo3_joypad/adc_deadzone [读写/只读]
 * 摇杆死区值，中心附近的值将被归零
 * JOYPAD_DEBUG_TUNING=1 时可写, 否则只读
 */
/*----------------------------------------------------------------------------*/
static ssize_t joypad_show_adc_deadzone(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct platform_device *pdev  = to_platform_device(dev);
	struct joypad *joypad = platform_get_drvdata(pdev);

	return sprintf(buf, "%d\n", joypad->bt_adc_deadzone);
}

/*----------------------------------------------------------------------------*/
#if JOYPAD_DEBUG_TUNING
static ssize_t joypad_store_adc_deadzone(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf,
					  size_t count)
{
	struct platform_device *pdev  = to_platform_device(dev);
	struct joypad *joypad = platform_get_drvdata(pdev);

	mutex_lock(&joypad->lock);
	joypad->bt_adc_deadzone = simple_strtoul(buf, NULL, 10);
	mutex_unlock(&joypad->lock);

	return count;
}
#endif

/*----------------------------------------------------------------------------*/
static DEVICE_ATTR(adc_deadzone, S_IWUSR | S_IRUGO,
		   joypad_show_adc_deadzone,
#if JOYPAD_DEBUG_TUNING
		   joypad_store_adc_deadzone);
#else
		   NULL);
#endif

/*----------------------------------------------------------------------------*/
/*
 * 属性:
 *
 * /sys/devices/platform/odroidgo2_joypad/enable [读写]
 */
/*----------------------------------------------------------------------------*/
static ssize_t joypad_store_enable(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf,
				      size_t count)
{
	struct platform_device *pdev  = to_platform_device(dev);
	struct joypad *joypad = platform_get_drvdata(pdev);

	mutex_lock(&joypad->lock);
	joypad->enable = simple_strtoul(buf, NULL, 10);
	mutex_unlock(&joypad->lock);

	return count;
}

/*----------------------------------------------------------------------------*/
static ssize_t joypad_show_enable(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct platform_device *pdev  = to_platform_device(dev);
	struct joypad *joypad = platform_get_drvdata(pdev);

	return sprintf(buf, "%d\n", joypad->enable);
}

/*----------------------------------------------------------------------------*/
static DEVICE_ATTR(enable, S_IWUSR | S_IRUGO,
		   joypad_show_enable,
		   joypad_store_enable);

/*----------------------------------------------------------------------------*/
/*
 * 属性:
 *
 * /sys/devices/platform/odroidgo2_joypad/adc_cal [读写]
 */
/*----------------------------------------------------------------------------*/
static ssize_t joypad_store_adc_cal(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf,
				      size_t count)
{
	struct platform_device *pdev  = to_platform_device(dev);
	struct joypad *joypad = platform_get_drvdata(pdev);
	bool calibration;
	if (!joypad->amux_count)
		return count;

	calibration = simple_strtoul(buf, NULL, 10);

	if (calibration) {
		int nbtn;
		#define ADC_CAL_SAMPLES  50

		mutex_lock(&joypad->lock);
		for (nbtn = 0; nbtn < joypad->amux_count; nbtn++) {
			struct bt_adc *adc = &joypad->adcs[nbtn];
			int value, ret;
			long sum = 0;
			int samples = 0;
			int i;

			/*
			 * 直接ADC模式固定为2轴 (ABS_X/ABS_Y). 不应用 skip_absr/skip_absl.
			 */
			if (!joypad->direct_adc_mode) {
				/* 跳过整个右摇杆对: 索引 0 和 1 */
				if (joypad->skip_absr && (nbtn == 0 || nbtn == 1))
					continue;
				/* 跳过整个左摇杆对: 索引 2 和 3 */
				if (joypad->skip_absl && (nbtn == 2 || nbtn == 3))
					continue;
			}

			/* 多次采样取平均值 */
			for (i = 0; i < ADC_CAL_SAMPLES; i++) {
				ret = joypad_adc_read(joypad, adc, &value);
				if (ret) {
					dev_err(joypad->dev, "%s: adc read failed [%d], err=%d\n",
						__func__, nbtn, ret);
					continue;
				}
				sum += value;
				samples++;
				usleep_range(1000, 2000);  // 1ms间隔
			}

			if (samples == 0)
				continue;

			value = sum / samples;
			adc->value = value;
			adc->cal = value;
		}
		mutex_unlock(&joypad->lock);
		#undef ADC_CAL_SAMPLES
	}
	return count;
}

/*----------------------------------------------------------------------------*/
static ssize_t joypad_show_adc_cal(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct platform_device *pdev  = to_platform_device(dev);
	struct joypad *joypad = platform_get_drvdata(pdev);
	int nbtn;
	ssize_t pos;

	if (!joypad->amux_count)
		return sprintf(buf, "adc disabled\n");
	for (nbtn = 0, pos = 0; nbtn < joypad->amux_count; nbtn++) {
		struct bt_adc *adc = &joypad->adcs[nbtn];
		pos += sprintf(&buf[pos], "adc[%d]->cal = %d\n",
				nbtn, adc->cal);
	}
	pos += sprintf(&buf[pos], "adc scale = %d\n", joypad->bt_adc_scale);
	return pos;
}

/*----------------------------------------------------------------------------*/
static DEVICE_ATTR(adc_cal, S_IWUSR | S_IRUGO,
		   joypad_show_adc_cal,
		   joypad_store_adc_cal);

/*----------------------------------------------------------------------------*/
/*
 * ATTRIBUTES:
 *
 * /sys/devices/platform/odroidgo2_joypad/amux_debug [rw]
 *
 * echo [debug channel] > amux_debug
 * cat amux_debug : debug channel mux set & adc read
 */
/*----------------------------------------------------------------------------*/
static ssize_t joypad_store_amux_debug(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf,
				      size_t count)
{
	struct platform_device *pdev  = to_platform_device(dev);
	struct joypad *joypad = platform_get_drvdata(pdev);
	if (!joypad->amux_count)
		return count;

	joypad->debug_ch = simple_strtoul(buf, NULL, 10);

	/* 如果出错则使用默认设置(debug_ch = 0) */
	if (joypad->debug_ch > joypad->amux_count)
		joypad->debug_ch = 0;

	return count;
}

/*----------------------------------------------------------------------------*/
static ssize_t joypad_show_amux_debug(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct platform_device *pdev  = to_platform_device(dev);
	struct joypad *joypad = platform_get_drvdata(pdev);
	struct analog_mux *amux = joypad->amux;
	ssize_t pos;
	int value;

	if (joypad->direct_adc_mode)
		return sprintf(buf, "direct-adc: no amux\n");

	if (!joypad->amux_count)
		return sprintf(buf, "adc disabled\n");

	mutex_lock(&joypad->lock);

	/* 禁用轮询驱动 */
	if (joypad->enable)
		joypad->enable = false;

	if (joypad_amux_select(amux, joypad->debug_ch, joypad->split_adc_mode))
		goto err_out;

	if (joypad->split_adc_mode && joypad->debug_ch <= 1) {
		if (iio_read_channel_processed(amux->iio_ch_r, &value))
			goto err_out;
	} else {
		if (iio_read_channel_processed(amux->iio_ch, &value))
			goto err_out;
	}

	pos = sprintf(buf, "amux ch[%d], adc scale = %d, adc value = %d\n",
			joypad->debug_ch, joypad->bt_adc_scale,
			value * joypad->bt_adc_scale);
	goto out;

err_out:
	pos = sprintf(buf, "error : amux setup & adc read!\n");
out:
	mutex_unlock(&joypad->lock);
	return pos;
}

/*----------------------------------------------------------------------------*/
static DEVICE_ATTR(amux_debug, S_IWUSR | S_IRUGO,
		   joypad_show_amux_debug,
		   joypad_store_amux_debug);

/*----------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
/*
 * 属性:
 *
 * /sys/devices/platform/odroidgo2_joypad/rumble_period [读写]
 */
/*----------------------------------------------------------------------------*/
static ssize_t joypad_store_period(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf,
				      size_t count)
{
	struct platform_device *pdev  = to_platform_device(dev);
	struct joypad *joypad = platform_get_drvdata(pdev);

	mutex_lock(&joypad->lock);
	if (IS_ERR_OR_NULL(joypad->pwm)) {
		mutex_unlock(&joypad->lock);
		return -ENODEV;
	}
	pwm_set_period(joypad->pwm, simple_strtoul(buf, NULL, 10));
	mutex_unlock(&joypad->lock);

	return count;
}

/*----------------------------------------------------------------------------*/
static ssize_t joypad_show_period(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct platform_device *pdev  = to_platform_device(dev);
	struct joypad *joypad = platform_get_drvdata(pdev);

	if (IS_ERR_OR_NULL(joypad->pwm))
		return -ENODEV;

	return sprintf(buf, "%d\n", pwm_get_period(joypad->pwm));
}

/*----------------------------------------------------------------------------*/
static DEVICE_ATTR(rumble_period, S_IWUSR | S_IRUGO,
		   joypad_show_period,
		   joypad_store_period);


/*----------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
/*
 * 属性:
 *
 * /sys/devices/platform/odroidgo2_joypad/rumble_boost_strong [读写]
 */
/*----------------------------------------------------------------------------*/
static ssize_t joypad_store_boost_strong(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf,
				      size_t count)
{
	struct platform_device *pdev  = to_platform_device(dev);
	struct joypad *joypad = platform_get_drvdata(pdev);

	mutex_lock(&joypad->lock);
	joypad->boost_strong = simple_strtoul(buf, NULL, 10);
	mutex_unlock(&joypad->lock);

	return count;
}

/*----------------------------------------------------------------------------*/
static ssize_t joypad_show_boost_strong(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct platform_device *pdev  = to_platform_device(dev);
	struct joypad *joypad = platform_get_drvdata(pdev);

	return sprintf(buf, "%d\n", joypad->boost_strong);
}

/*----------------------------------------------------------------------------*/
static DEVICE_ATTR(rumble_boost_strong, S_IWUSR | S_IRUGO,
		   joypad_show_boost_strong,
		   joypad_store_boost_strong);

/*----------------------------------------------------------------------------*/
/*
 * 属性:
 *
 * /sys/devices/platform/odroidgo2_joypad/rumble_boost_weak [读写]
 */
/*----------------------------------------------------------------------------*/
static ssize_t joypad_store_boost_weak(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf,
				      size_t count)
{
	struct platform_device *pdev  = to_platform_device(dev);
	struct joypad *joypad = platform_get_drvdata(pdev);

	mutex_lock(&joypad->lock);
	joypad->boost_weak = simple_strtoul(buf, NULL, 10);
	mutex_unlock(&joypad->lock);

	return count;
}

/*----------------------------------------------------------------------------*/
static ssize_t joypad_show_boost_weak(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct platform_device *pdev  = to_platform_device(dev);
	struct joypad *joypad = platform_get_drvdata(pdev);

	return sprintf(buf, "%d\n", joypad->boost_weak);
}

/*----------------------------------------------------------------------------*/
static DEVICE_ATTR(rumble_boost_weak, S_IWUSR | S_IRUGO,
		   joypad_show_boost_weak,
		   joypad_store_boost_weak);

/*----------------------------------------------------------------------------*/
#if JOYPAD_DEBUG_TUNING
/*
 * 单个sysfs节点用于转储/设置所有调优值.
 *
 * 读取: cat /sys/devices/platform/odroidgo3_joypad/joypad_tuning
 *   每轴输出: 原始ADC, 校准值, 最终值, 调优正/负
 *
 * 写入: echo "x_p 200" > /sys/devices/platform/odroidgo3_joypad/joypad_tuning
 *   键: x_p, x_n, y_p, y_n, rx_p, rx_n, ry_p, ry_n
 */
static const char * const tuning_names[] = {
	"ry_p", "ry_n", "rx_p", "rx_n",
	"y_p",  "y_n",  "x_p",  "x_n",
};
static const int tuning_adc_idx[] = { 0, 0, 1, 1, 2, 2, 3, 3 };
static const bool tuning_is_p[] =   { 1, 0, 1, 0, 1, 0, 1, 0 };

static ssize_t joypad_show_tuning(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct joypad *joypad = platform_get_drvdata(pdev);
	int i, len = 0;

	if (!joypad->adcs)
		return sprintf(buf, "no adcs\n");

	for (i = 0; i < joypad->amux_count && i < 4; i++) {
		struct bt_adc *adc = &joypad->adcs[i];
		const char *name;

		switch (adc->report_type) {
		case ABS_X:  name = "x";  break;
		case ABS_Y:  name = "y";  break;
		case ABS_RX: name = "rx"; break;
		case ABS_RY: name = "ry"; break;
		default:     name = "?";  break;
		}
		len += scnprintf(buf + len, PAGE_SIZE - len,
			"%s: raw=%d cal=%d val=%d  tuning_p=%d tuning_n=%d\n",
			name, adc->raw, adc->cal, adc->value,
			adc->tuning_p, adc->tuning_n);
	}
	return len;
}

static ssize_t joypad_store_tuning(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct joypad *joypad = platform_get_drvdata(pdev);
	struct bt_adc *adc;
	char key[8];
	int val, i;

	if (sscanf(buf, "%7s %d", key, &val) != 2)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(tuning_names); i++) {
		if (!joypad->adcs || tuning_adc_idx[i] >= joypad->amux_count)
			continue;
		if (strcmp(key, tuning_names[i]))
			continue;

		adc = &joypad->adcs[tuning_adc_idx[i]];
		if (tuning_is_p[i])
			adc->tuning_p = val;
		else
			adc->tuning_n = val;
		return count;
	}
	return -EINVAL;
}

static DEVICE_ATTR(joypad_tuning, S_IWUSR | S_IRUGO,
		   joypad_show_tuning,
		   joypad_store_tuning);
#endif /* JOYPAD_DEBUG_TUNING */

/*----------------------------------------------------------------------------*/
/*
 * 属性:
 *
 * /sys/devices/platform/odroidgo3_joypad/stick_switch_key [读写]
 * 摇杆切换键的linux code (仅当DTS中配置了stick-switch-key时可写)
 */
/*----------------------------------------------------------------------------*/
static ssize_t joypad_show_stick_switch_key(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	struct platform_device *pdev  = to_platform_device(dev);
	struct joypad *joypad = platform_get_drvdata(pdev);

	return sprintf(buf, "%d\n", joypad->stick_switch_code);
}

/*----------------------------------------------------------------------------*/
static ssize_t joypad_store_stick_switch_key(struct device *dev,
					      struct device_attribute *attr,
					      const char *buf,
					      size_t count)
{
	struct platform_device *pdev  = to_platform_device(dev);
	struct joypad *joypad = platform_get_drvdata(pdev);

	/* 仅当DTS中配置了stick-switch-key时允许写入 */
	if (!joypad->stick_switch_code)
		return -EPERM;

	mutex_lock(&joypad->lock);
	joypad->stick_switch_code = simple_strtoul(buf, NULL, 10);
	mutex_unlock(&joypad->lock);

	return count;
}

/*----------------------------------------------------------------------------*/
static DEVICE_ATTR(stick_switch_key, S_IWUSR | S_IRUGO,
		   joypad_show_stick_switch_key,
		   joypad_store_stick_switch_key);

/*----------------------------------------------------------------------------*/
/**
 * joypad_swap_ab_code() - A/B交换: BTN_EAST <-> BTN_SOUTH.
 * joypad_swap_xy_code() - X/Y交换: BTN_WEST <-> BTN_NORTH.
 *
 * 无关的键码原样返回. 用于上报重映射和capability注册.
 */
static int joypad_swap_ab_code(int code)
{
	switch (code) {
	case BTN_EAST:	return BTN_SOUTH;
	case BTN_SOUTH:	return BTN_EAST;
	default:	return code;
	}
}

static int joypad_swap_xy_code(int code)
{
	switch (code) {
	case BTN_WEST:	return BTN_NORTH;
	case BTN_NORTH:	return BTN_WEST;
	default:	return code;
	}
}

/**
 * joypad_remap_button() - 按当前交换设置(swap_ab/swap_xy)重映射按键码.
 *
 * poll线程无锁读取标志, 与sysfs写入端通过 READ_ONCE/WRITE_ONCE 配对.
 */
static int joypad_remap_button(struct joypad *joypad, int code)
{
	if (READ_ONCE(joypad->swap_ab))
		code = joypad_swap_ab_code(code);
	if (READ_ONCE(joypad->swap_xy))
		code = joypad_swap_xy_code(code);
	return code;
}

/*----------------------------------------------------------------------------*/
/*
 * 属性:
 *
 * /sys/devices/platform/odroidgo3_joypad/swap_ab [读写]
 * /sys/devices/platform/odroidgo3_joypad/swap_xy [读写]
 * 按键交换: 0 = 原样上报(默认), 1 = 交换.
 *   swap_ab: A<->B (BTN_EAST <-> BTN_SOUTH)
 *   swap_xy: X<->Y (BTN_WEST <-> BTN_NORTH)
 * 仅接受 0/1, 非法输入返回 -EINVAL.
 */
/*----------------------------------------------------------------------------*/
static ssize_t joypad_show_swap_ab(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct joypad *joypad = platform_get_drvdata(pdev);

	return sprintf(buf, "%d\n", joypad->swap_ab);
}

static ssize_t joypad_show_swap_xy(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct joypad *joypad = platform_get_drvdata(pdev);

	return sprintf(buf, "%d\n", joypad->swap_xy);
}

/* 任一交换开关变化时, 强制释放四个功能键, 避免按住时切换导致按键卡住.
 * 返回 0 成功, 负数错误码. */
static int joypad_store_swap(struct joypad *joypad, int *field, const char *buf)
{
	unsigned int enable;
	int error;

	/* 严格解析并校验: 仅接受 0/1 */
	error = kstrtouint(buf, 10, &enable);
	if (error)
		return error;
	if (enable > 1)
		return -EINVAL;

	mutex_lock(&joypad->lock);
	if (enable != READ_ONCE(*field)) {
		WRITE_ONCE(*field, enable);
		if (joypad->input) {
			input_report_key(joypad->input, BTN_EAST, 0);
			input_report_key(joypad->input, BTN_SOUTH, 0);
			input_report_key(joypad->input, BTN_WEST, 0);
			input_report_key(joypad->input, BTN_NORTH, 0);
			input_sync(joypad->input);
		}
	}
	mutex_unlock(&joypad->lock);

	return 0;
}

static ssize_t joypad_store_swap_ab(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct joypad *joypad = platform_get_drvdata(pdev);
	int error;

	error = joypad_store_swap(joypad, &joypad->swap_ab, buf);
	return error ? error : count;
}

static ssize_t joypad_store_swap_xy(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct joypad *joypad = platform_get_drvdata(pdev);
	int error;

	error = joypad_store_swap(joypad, &joypad->swap_xy, buf);
	return error ? error : count;
}

/*----------------------------------------------------------------------------*/
static DEVICE_ATTR(swap_ab, S_IWUSR | S_IRUGO,
		   joypad_show_swap_ab, joypad_store_swap_ab);
static DEVICE_ATTR(swap_xy, S_IWUSR | S_IRUGO,
		   joypad_show_swap_xy, joypad_store_swap_xy);

static struct attribute *joypad_attrs[] = {
	&dev_attr_poll_interval.attr,
	&dev_attr_adc_fuzz.attr,
	&dev_attr_adc_flat.attr,
	&dev_attr_adc_deadzone.attr,
	&dev_attr_swap_ab.attr,
	&dev_attr_swap_xy.attr,
	&dev_attr_enable.attr,
	&dev_attr_adc_cal.attr,
	&dev_attr_amux_debug.attr,
	&dev_attr_rumble_period.attr,
	&dev_attr_rumble_boost_strong.attr,
	&dev_attr_rumble_boost_weak.attr,
	&dev_attr_stick_switch_key.attr,
#if JOYPAD_DEBUG_TUNING
	&dev_attr_joypad_tuning.attr,
#endif
	NULL,
};

static struct attribute_group joypad_attr_group = {
	.attrs = joypad_attrs,
};

/*----------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
/**
 * joypad_adc_key_check() - 检查基于ADC的按钮状态.
 * @joypad:   joypad设备上下文
 * @poll_dev: 轮询输入设备
 * @gpio:     按钮配置结构体
 *
 * 读取共享的ADC通道并将该值与此按钮的目标ADC值进行比较.
 * 如果在容差范围内(adc_fuzz), 则认为按钮被按下. 如果配置了组合键, 还会报告组合键.
 *
 * 如果摇杆切换键按下，L3（STICK_SWITCH_L3_CODE）会上报为 R3（STICK_SWITCH_R3_CODE）。
 */
static void joypad_adc_key_check(struct joypad *joypad,
				 struct input_polled_dev *poll_dev,
				 struct bt_gpio *gpio)
{
	int adc_val;
	bool pressed;
	int ret, i;
	int linux_code;

	if (!joypad->adc_key_channel)
		return;

	ret = iio_read_channel_raw(joypad->adc_key_channel, &adc_val);
	if (ret < 0) {
		dev_err_once(joypad->dev, "adc key read failed: %d\n", ret);
		return;
	}

	/* 检查ADC值是否在目标容差范围内 */
	pressed = (abs(adc_val - gpio->adc_value) <= gpio->adc_fuzz);

	/* 仅在状态变化时报告 */
	if (pressed == gpio->old_value)
		return;

	/* 根据切换键状态决定上报的按键码 */
	linux_code = gpio->linux_code;
	if (joypad->stick_switch_active) {
		if (linux_code == STICK_SWITCH_L3_CODE)
			linux_code = STICK_SWITCH_R3_CODE;
		else if (linux_code == STICK_SWITCH_R3_CODE)
			linux_code = STICK_SWITCH_L3_CODE;
	}

	/* 按键交换设置(swap_ab/swap_xy)重映射 */
	linux_code = joypad_remap_button(joypad, linux_code);

	/* 报告主键 */
	input_event(poll_dev->input,
		gpio->report_type, linux_code, pressed ? 1 : 0);

	/* 报告组合键 */
	for (i = 0; i < gpio->combo_count; i++) {
		int code = joypad_remap_button(joypad, gpio->combo_codes[i]);

		input_event(poll_dev->input,
			gpio->report_type, code, pressed ? 1 : 0);
	}

	gpio->old_value = pressed;
}

/*----------------------------------------------------------------------------*/
/**
 * joypad_gpio_key_check() - 检查基于GPIO的按钮状态.
 * @joypad:   joypad设备上下文
 * @poll_dev: 轮询输入设备
 * @gpio:     按钮配置结构体
 *
 * 读取GPIO引脚并根据配置的有效电平报告按钮按下/释放。
 * 如果摇杆切换键按下，L3（STICK_SWITCH_L3_CODE）会上报为 R3（STICK_SWITCH_R3_CODE）。
 */
static void joypad_gpio_key_check(struct joypad *joypad,
				 struct input_polled_dev *poll_dev,
				 struct bt_gpio *gpio)
{
	int value;
	int linux_code;

	value = gpio_get_value(gpio->num);
	if (value < 0) {
		dev_err(joypad->dev, "failed to get gpio %d state\n", gpio->num);
		return;
	}

	if (value != gpio->old_value) {
		/* 根据切换键状态决定上报的按键码 */
		linux_code = gpio->linux_code;
		if (joypad->stick_switch_active) {
			if (linux_code == STICK_SWITCH_L3_CODE)
				linux_code = STICK_SWITCH_R3_CODE;
			else if (linux_code == STICK_SWITCH_R3_CODE)
				linux_code = STICK_SWITCH_L3_CODE;
		}

		/* 按键交换设置(swap_ab/swap_xy)重映射 */
		linux_code = joypad_remap_button(joypad, linux_code);

		input_event(poll_dev->input,
			gpio->report_type, linux_code,
			(value == gpio->active_level) ? 1 : 0);
		gpio->old_value = value;
	}
}

/*----------------------------------------------------------------------------*/
/**
 * joypad_gpio_check() - 检查所有按钮状态(GPIO和ADC按钮).
 * @poll_dev: 轮询输入设备
 *
 * 遍历所有已配置的按钮并报告状态变化.
 * 支持GPIO按钮和基于ADC的按钮.
 *
 * 注意: input_sync() 不在此处调用; 调用者 joypad_poll()
 *       在ADC和GPIO处理完成后执行单次同步.
 */
static void joypad_gpio_check(struct input_polled_dev *poll_dev)
{
	struct joypad *joypad = poll_dev->private;
	int nbtn;

	for (nbtn = 0; nbtn < joypad->bt_gpio_count; nbtn++) {
		struct bt_gpio *gpio = &joypad->gpios[nbtn];

		if (gpio->is_adc)
			joypad_adc_key_check(joypad, poll_dev, gpio);
		else
			joypad_gpio_key_check(joypad, poll_dev, gpio);
	}
}

/*----------------------------------------------------------------------------*/
/**
 * joypad_axis_filter() - 丢弃单次采样的大跳变(毛刺).
 * @adc:      轴状态 (维护 reported/prev)
 * @sample:   本次采样值 (输出值单位)
 * @max_step: 单次轮询允许的最大变化量
 *
 * 与上次上报值和上一个采样值偏差都超过 max_step 的采样视为孤立毛刺,
 * 直接丢弃并保持上次上报值; 下一次采样仍偏离上一采样不超过 max_step
 * 的是真实移动, 立即放行, 摇杆仍可全速走完整个行程.
 *
 * 返回: 本次应上报的值
 */
static int joypad_axis_filter(struct bt_adc *adc, int sample, int max_step)
{
	int delta_rep = abs(sample - adc->reported);
	int delta_prev = abs(sample - adc->prev);

	adc->prev = sample;

	if (delta_rep > max_step && delta_prev > max_step)
		return adc->reported;	/* 毛刺: 丢弃, 保持上次上报值 */

	adc->reported = sample;
	return sample;
}

/*----------------------------------------------------------------------------*/
/**
 * joypad_adc_report_pair() - 读取、处理并报告一对ADC摇杆轴(X/Y).
 * @joypad:   joypad设备上下文
 * @poll_dev: 轮询输入设备
 * @adcx:     X轴ADC结构体
 * @adcy:     Y轴ADC结构体
 * @idx:      X轴索引 (仅用于错误日志)
 *
 * 处理流程: 读取 → 校准偏移 → 径向死区 → 方向调优 →
 *           钳位 → 摇杆切换映射 → 反转 → 上报.
 */
static void joypad_adc_report_pair(struct joypad *joypad,
				   struct input_polled_dev *poll_dev,
				   struct bt_adc *adcx, struct bt_adc *adcy,
				   int idx)
{
	int mag, deadzone = joypad->bt_adc_deadzone;
	int report_type_x, report_type_y;
	int value_x, value_y, ret;

	/* 读取X和Y */
	ret = joypad_adc_read(joypad, adcx, &value_x);
	if (ret) {
		dev_err(joypad->dev, "%s: adc read failed [%d], err=%d\n",
			__func__, idx, ret);
		return;
	}
	ret = joypad_adc_read(joypad, adcy, &value_y);
	if (ret) {
		dev_err(joypad->dev, "%s: adc read failed [%d], err=%d\n",
			__func__, idx + 1, ret);
		return;
	}

	adcx->value = value_x;
	adcy->value = value_y;

#if JOYPAD_DEBUG_TUNING
	/* 存储校准前的原始值用于调试 */
	adcx->raw = value_x;
	adcy->raw = value_y;
#endif

	/* 校准 */
	adcx->value -= adcx->cal;
	adcy->value -= adcy->cal;

	/* 径向死区 */
	mag = int_sqrt((adcx->value * adcx->value) + (adcy->value * adcy->value));
	if (deadzone && mag <= deadzone) {
		adcx->value = 0;
		adcy->value = 0;
	}

	/* 应用调优 */
	if (adcx->tuning_n && adcx->value < 0)
		adcx->value = ADC_DATA_TUNING(adcx->value, adcx->tuning_n);
	if (adcx->tuning_p && adcx->value > 0)
		adcx->value = ADC_DATA_TUNING(adcx->value, adcx->tuning_p);
	if (adcy->tuning_n && adcy->value < 0)
		adcy->value = ADC_DATA_TUNING(adcy->value, adcy->tuning_n);
	if (adcy->tuning_p && adcy->value > 0)
		adcy->value = ADC_DATA_TUNING(adcy->value, adcy->tuning_p);

	/* 限制范围 */
	adcx->value = CLAMP(adcx->value, adcx->min, adcx->max);
	adcy->value = CLAMP(adcy->value, adcy->min, adcy->max);

	/*
	 * 毛刺滤波 (可选, DTS: button-adc-max-step, 输出值单位).
	 * 丢弃同时偏离上次上报值和上一采样超过 max_step 的孤立大跳变
	 * (如超过半行程的ADC干扰), 保持上次上报值; 持续偏离上一采样
	 * 不超过 max_step 的是真实移动, 立即放行. 属性不存在时为0,
	 * 保持原有直接上报行为.
	 */
	if (joypad->bt_adc_max_step) {
		adcx->value = joypad_axis_filter(adcx, adcx->value,
						joypad->bt_adc_max_step);
		adcy->value = joypad_axis_filter(adcy, adcy->value,
						joypad->bt_adc_max_step);
	}

	/* 处理摇杆切换 */
	report_type_x = adcx->report_type;
	report_type_y = adcy->report_type;
	if (joypad->stick_switch_active) {
		switch (report_type_x) {
		case ABS_X:  report_type_x = ABS_RX; break;
		case ABS_Y:  report_type_x = ABS_RY; break;
		case ABS_RX: report_type_x = ABS_X;  break;
		case ABS_RY: report_type_x = ABS_Y;  break;
		}
		switch (report_type_y) {
		case ABS_X:  report_type_y = ABS_RX; break;
		case ABS_Y:  report_type_y = ABS_RY; break;
		case ABS_RX: report_type_y = ABS_X;  break;
		case ABS_RY: report_type_y = ABS_Y;  break;
		}
	}

	/* 反转处理 */
	value_x = adcx->invert ? -adcx->value : adcx->value;
	value_y = adcy->invert ? -adcy->value : adcy->value;

	input_report_abs(poll_dev->input, report_type_x, value_x);
	input_report_abs(poll_dev->input, report_type_y, value_y);
}

/*----------------------------------------------------------------------------*/
/**
 * joypad_adc_check() - 读取并报告所有ADC摇杆轴.
 * @poll_dev: 轮询输入设备
 *
 * 读取所有已配置轴的ADC值并报告给输入子系统. 支持三种ADC模式:
 *   - 直接ADC: 直接读取 joy_x/joy_y, 无AMUX
 *   - 分离ADC: 双ADC通道, AMUX用于X/Y选择
 *   - 传统模式: 单ADC, AMUX用于通道切换
 *
 * 注意: input_sync() 不在此处调用; 调用者 joypad_poll()
 *       在ADC和GPIO处理完成后执行单次同步.
 */
static void joypad_adc_check(struct input_polled_dev *poll_dev)
{
	struct joypad *joypad = poll_dev->private;
	int nbtn;

	if (!joypad->amux_count)
		return;

	/* 直接ADC模式: 固定2轴 (ABS_X/ABS_Y) */
	if (joypad->direct_adc_mode) {
		joypad_adc_report_pair(joypad, poll_dev,
				       &joypad->adcs[0], &joypad->adcs[1], 0);
		return;
	}

	/*
	 * 传统/分离ADC模式: 4轴成对 (右摇杆 0-1, 左摇杆 2-3).
	 * 可通过 skip_absr/skip_absl 标志跳过轴对.
	 */
	for (nbtn = 0; nbtn + 1 < joypad->amux_count; nbtn += 2) {
		if ((nbtn == 0 && joypad->skip_absr) ||
		    (nbtn == 2 && joypad->skip_absl))
			continue;

		joypad_adc_report_pair(joypad, poll_dev,
				       &joypad->adcs[nbtn],
				       &joypad->adcs[nbtn + 1], nbtn);
	}
}

/*----------------------------------------------------------------------------*/
/**
 * joypad_stick_switch_update() - 更新摇杆切换键状态.
 * @joypad:   joypad上下文
 * @poll_dev: 轮询输入设备
 *
 * 检测 stick-switch-key 的按键状态并保存到 joypad->stick_switch_active。
 * 不进行任何事件上报，只更新状态标志。
 *
 * 上报逻辑在 joypad_adc_check() 和 joypad_gpio_key_check() 中处理。
 */
static void joypad_stick_switch_update(struct joypad *joypad,
				       struct input_polled_dev *poll_dev)
{
	struct input_dev *input = poll_dev->input;

	/* 未配置切换键 */
	if (!joypad->stick_switch_code)
		return;

	/* 超出有效按键码范围（如用户态的 999 哨兵值）视为未绑定 */
	if (joypad->stick_switch_code >= KEY_CNT) {
		joypad->stick_switch_active = false;
		return;
	}

	/* 检测按键状态并保存 */
	joypad->stick_switch_active = test_bit(joypad->stick_switch_code, input->key);
}

/*----------------------------------------------------------------------------*/
/**
 * joypad_poll() - 主轮询函数, 按固定间隔调用.
 * @poll_dev: 轮询输入设备
 *
 * 这是注册到输入轮询子系统的核心轮询回调. 它执行以下操作:
 *   1. 更新摇杆切换键状态
 *   2. 检查并报告ADC摇杆轴 (如果已配置)
 *   3. 检查并报告所有按钮状态 (GPIO和ADC)
 *   4. 发送单次 input_sync() 以刷新所有待处理事件
 *   5. 如果通过sysfs更改了轮询间隔则更新
 */
static void joypad_poll(struct input_polled_dev *poll_dev)
{
	struct joypad *joypad = poll_dev->private;

	if (joypad->enable) {
		/* 1. 更新摇杆切换键状态（必须在上报之前） */
		joypad_stick_switch_update(joypad, poll_dev);

		/* 2. 报告ADC摇杆轴 */
		if (joypad->amux_count)
			joypad_adc_check(poll_dev);

		/* 3. 报告所有按钮状态 */
		joypad_gpio_check(poll_dev);

		/* 4. 对所有报告的事件进行单次同步 */
		input_sync(poll_dev->input);
	}

	/* 如果通过sysfs更改了轮询间隔则更新 */
	if (poll_dev->poll_interval != joypad->poll_interval) {
		mutex_lock(&joypad->lock);
		poll_dev->poll_interval = joypad->poll_interval;
		mutex_unlock(&joypad->lock);
	}
}

/*----------------------------------------------------------------------------*/
/**
 * joypad_open() - 当输入设备被打开时调用.
 * @poll_dev: 轮询输入设备
 *
 * 初始化按钮状态并执行初始ADC校准.
 * 校准值同时作为初始位置上报给输入子系统，避免重复读取ADC.
 */
static void joypad_open(struct input_polled_dev *poll_dev)
{
	struct joypad *joypad = poll_dev->private;
	int nbtn;

	/* 初始化GPIO按钮状态为释放 */
	for (nbtn = 0; nbtn < joypad->bt_gpio_count; nbtn++) {
		struct bt_gpio *gpio = &joypad->gpios[nbtn];
		gpio->old_value = gpio->active_level ? 0 : 1;
	}

	/* 通过读取初始中心值校准ADC轴，并直接上报初始位置 */
	/* 使用成对处理，支持径向死区 */
	/* 使用多次采样取平均值，避免单次读数的偶然性 */
	#define CALIBRATION_SAMPLES  50
	for (nbtn = 0; nbtn + 1 < joypad->amux_count; nbtn += 2) {
		struct bt_adc *adcx = &joypad->adcs[nbtn];
		struct bt_adc *adcy = &joypad->adcs[nbtn + 1];
		int value_x, value_y, ret;
		long sum_x = 0, sum_y = 0;
		int samples = 0;
		int i;

		/* 跳过未为此设备配置的轴 */
		if (!joypad->direct_adc_mode) {
			if (joypad->skip_absr && (nbtn == 0))
				continue;
			if (joypad->skip_absl && (nbtn == 2))
				continue;
		}

		/* 多次采样取平均值 */
		for (i = 0; i < CALIBRATION_SAMPLES; i++) {
			ret = joypad_adc_read(joypad, adcx, &value_x);
			if (ret) {
				dev_err(joypad->dev, "%s: adc read failed [%d], err=%d\n",
					__func__, nbtn, ret);
				continue;
			}
			ret = joypad_adc_read(joypad, adcy, &value_y);
			if (ret) {
				dev_err(joypad->dev, "%s: adc read failed [%d], err=%d\n",
					__func__, nbtn + 1, ret);
				continue;
			}
			sum_x += value_x;
			sum_y += value_y;
			samples++;
			usleep_range(1000, 2000);  // 1ms间隔
		}

		if (samples == 0)
			continue;

		/* 计算平均值作为中心点 */
		value_x = sum_x / samples;
		value_y = sum_y / samples;

		/* 校准：以当前位置作为中心点 */
		adcx->cal = value_x;
		adcy->cal = value_y;

		/* 上报初始位置（校准后偏移为0） */
		adcx->value = 0;
		adcy->value = 0;
		input_report_abs(poll_dev->input, adcx->report_type, 0);
		input_report_abs(poll_dev->input, adcy->report_type, 0);
		adcx->reported = 0;
		adcy->reported = 0;
		adcx->prev = 0;
		adcy->prev = 0;

		dev_dbg(joypad->dev, "%s: adc[%d] calibrated = %d, adc[%d] calibrated = %d\n",
			__func__, nbtn, adcx->cal, nbtn + 1, adcy->cal);
	}
	#undef CALIBRATION_SAMPLES

	/* 上报按钮初始状态 */
	joypad_gpio_check(poll_dev);
	input_sync(poll_dev->input);

	/* 启用轮询 */
	mutex_lock(&joypad->lock);
	joypad->enable = true;
	mutex_unlock(&joypad->lock);

	dev_info(joypad->dev, "%s: opened\n", __func__);
}

/*----------------------------------------------------------------------------*/
/**
 * joypad_close() - 当输入设备被关闭时调用.
 * @poll_dev: 轮询输入设备
 *
 * 禁用轮询并停止任何活动的振动.
 */
static void joypad_close(struct input_polled_dev *poll_dev)
{
	struct joypad *joypad = poll_dev->private;

	/* 禁用轮询 */
	mutex_lock(&joypad->lock);
	joypad->enable = false;
	mutex_unlock(&joypad->lock);

	/* 如果正在振动则停止 */
	cancel_work_sync(&joypad->play_work);
	joypad_vibrator_stop(joypad);

	dev_info(joypad->dev, "%s: closed\n", __func__);
}

/*----------------------------------------------------------------------------*/
/**
 * joypad_amux_setup() - 初始化模拟多路复用器(AMUX)硬件.
 * @dev:    设备指针
 * @joypad: joypad上下文
 *
 * 设置用于传统和分离ADC模式中通道切换的模拟多路复用器. 配置:
 *   - IIO ADC通道 (单通道或分离模式的双通道)
 *   - 用于通道切换的选择A/B GPIO
 *   - 可选的使能GPIO
 *
 * 返回: 成功返回0, 失败返回负错误码
 */
static int joypad_amux_setup(struct device *dev, struct joypad *joypad)
{
	struct analog_mux *amux;
	enum iio_chan_type type;
	enum of_gpio_flags flags;
	int ret;

	/* 分配模拟多路复用器控制结构体 */
	joypad->amux = devm_kzalloc(dev, sizeof(struct analog_mux), GFP_KERNEL);
	if (!joypad->amux) {
		dev_err(dev, "%s: amux allocation failed\n", __func__);
		return -ENOMEM;
	}
	amux = joypad->amux;

	/* 根据模式获取IIO ADC通道 */
	if (joypad->split_adc_mode) {
		/* 分离ADC: 左右摇杆使用独立通道 */
		amux->iio_ch = devm_iio_channel_get(dev, "joy_left");
		amux->iio_ch_r = devm_iio_channel_get(dev, "joy_right");
		if (IS_ERR(amux->iio_ch) || IS_ERR(amux->iio_ch_r)) {
			dev_err(dev, "split-adc: iio channel get error\n");
			return -EINVAL;
		}
	} else {
		/* 传统模式: 通过AMUX的单ADC通道 */
		amux->iio_ch = devm_iio_channel_get(dev, "amux_adc");
		if (IS_ERR(amux->iio_ch)) {
			dev_err(dev, "iio channel get error\n");
			return -EINVAL;
		}
	}

	/* 验证IIO通道 */
	if (!amux->iio_ch->indio_dev)
		return -ENXIO;

	if (iio_get_channel_type(amux->iio_ch, &type))
		return -EINVAL;

	if (type != IIO_VOLTAGE) {
		dev_err(dev, "incompatible channel type %d\n", type);
		return -EINVAL;
	}

	/* 设置选择A GPIO (AMUX地址位0) */
	amux->sel_a_gpio = of_get_named_gpio_flags(dev->of_node,
				"amux-a-gpios", 0, &flags);
	if (gpio_is_valid(amux->sel_a_gpio)) {
		ret = devm_gpio_request(dev, amux->sel_a_gpio, "amux-sel-a");
		if (ret < 0) {
			dev_err(dev, "%s: failed to request amux-sel-a %d\n",
				__func__, amux->sel_a_gpio);
			return ret;
		}
		ret = gpio_direction_output(amux->sel_a_gpio, 0);
		if (ret < 0)
			return ret;
	}

	/* 设置选择B GPIO (AMUX地址位1) */
	amux->sel_b_gpio = of_get_named_gpio_flags(dev->of_node,
				"amux-b-gpios", 0, &flags);
	if (gpio_is_valid(amux->sel_b_gpio)) {
		ret = devm_gpio_request(dev, amux->sel_b_gpio, "amux-sel-b");
		if (ret < 0) {
			dev_err(dev, "%s: failed to request amux-sel-b %d\n",
				__func__, amux->sel_b_gpio);
			return ret;
		}
		ret = gpio_direction_output(amux->sel_b_gpio, 0);
		if (ret < 0)
			return ret;
	}

	/* 设置使能GPIO (可选, 低电平有效) */
	amux->en_gpio = of_get_named_gpio_flags(dev->of_node,
			"amux-en-gpios", 0, &flags);
	if (gpio_is_valid(amux->en_gpio)) {
		ret = devm_gpio_request(dev, amux->en_gpio, "amux-en");
		if (ret < 0) {
			dev_err(dev, "%s: failed to request amux-en %d\n",
				__func__, amux->en_gpio);
			return ret;
		}
		ret = gpio_direction_output(amux->en_gpio, 0);
		if (ret < 0)
			return ret;
	} else {
		dev_info(dev, "amux-en-gpios not configured; leaving MUX EN unchanged\n");
	}

	return 0;
}

/*----------------------------------------------------------------------------*/
/**
 * joypad_adc_setup_direct() - 为直接ADC模式设置ADC.
 * @dev:    设备指针
 * @joypad: joypad上下文
 *
 * 配置2轴 (ABS_X/ABS_Y), 直接访问IIO通道.
 * 每个轴有自己的IIO通道 (joy_x/joy_y), 无需AMUX.
 *
 * 返回: 成功返回0, 失败返回负错误码
 */
static int joypad_adc_setup_direct(struct device *dev, struct joypad *joypad)
{
	enum iio_chan_type type;
	struct bt_adc *adc;

	/* 轴0: X */
	adc = &joypad->adcs[0];
	adc->channel = devm_iio_channel_get(dev, "joy_x");
	if (IS_ERR(adc->channel)) {
		dev_err(dev, "direct-adc: iio channel 'joy_x' get error\n");
		return -EINVAL;
	}
	if (!adc->channel->indio_dev)
		return -ENXIO;
	if (iio_get_channel_type(adc->channel, &type))
		return -EINVAL;
	if (type != IIO_VOLTAGE) {
		dev_err(dev, "direct-adc: incompatible joy_x channel type %d\n", type);
		return -EINVAL;
	}

	adc->scale = joypad->bt_adc_scale;
	adc->max = (ADC_MAX_VOLTAGE / 2);
	adc->min = (ADC_MAX_VOLTAGE / 2) * (-1);
	if (adc->scale) {
		adc->max *= adc->scale;
		adc->min *= adc->scale;
	}
	adc->amux_ch = 0; /* 直接ADC模式中未使用 */
	adc->invert = joypad->invert_absx;
	adc->report_type = ABS_X;
	if (device_property_read_u32(dev, "abs_x-p-tuning", &adc->tuning_p))
		adc->tuning_p = ADC_TUNING_DEFAULT;
	if (device_property_read_u32(dev, "abs_x-n-tuning", &adc->tuning_n))
		adc->tuning_n = ADC_TUNING_DEFAULT;

	/* 轴1: Y */
	adc = &joypad->adcs[1];
	adc->channel = devm_iio_channel_get(dev, "joy_y");
	if (IS_ERR(adc->channel)) {
		dev_err(dev, "direct-adc: iio channel 'joy_y' get error\n");
		return -EINVAL;
	}
	if (!adc->channel->indio_dev)
		return -ENXIO;
	if (iio_get_channel_type(adc->channel, &type))
		return -EINVAL;
	if (type != IIO_VOLTAGE) {
		dev_err(dev, "direct-adc: incompatible joy_y channel type %d\n", type);
		return -EINVAL;
	}

	adc->scale = joypad->bt_adc_scale;
	adc->max = (ADC_MAX_VOLTAGE / 2);
	adc->min = (ADC_MAX_VOLTAGE / 2) * (-1);
	if (adc->scale) {
		adc->max *= adc->scale;
		adc->min *= adc->scale;
	}
	adc->amux_ch = 1; /* 直接ADC模式中未使用 */
	adc->invert = joypad->invert_absy;
	adc->report_type = ABS_Y;
	if (device_property_read_u32(dev, "abs_y-p-tuning", &adc->tuning_p))
		adc->tuning_p = ADC_TUNING_DEFAULT;
	if (device_property_read_u32(dev, "abs_y-n-tuning", &adc->tuning_n))
		adc->tuning_n = ADC_TUNING_DEFAULT;

	return 0;
}

/*----------------------------------------------------------------------------*/
/**
 * joypad_adc_setup_legacy() - 为传统/分离ADC模式设置ADC.
 * @dev:    设备指针
 * @joypad: joypad上下文
 *
 * 使用AMUX通道切换配置4轴.
 * 默认映射: ch0=RY, ch1=RX, ch2=Y, ch3=X
 * 可通过 "amux-channel-mapping" DTS属性覆盖.
 *
 * 返回: 成功返回0, 失败返回负错误码
 */
static int joypad_adc_setup_legacy(struct device *dev, struct joypad *joypad)
{
	int nbtn;

	for (nbtn = 0; nbtn < joypad->amux_count; nbtn++) {
		struct bt_adc *adc = &joypad->adcs[nbtn];
		bool invert = false;
		int report_type;

	/* 设置缩放和范围 */
		adc->scale = joypad->bt_adc_scale;
		adc->max = (ADC_MAX_VOLTAGE / 2);
		adc->min = (ADC_MAX_VOLTAGE / 2) * (-1);
		if (adc->scale) {
			adc->max *= adc->scale;
			adc->min *= adc->scale;
		}

		/* 默认AMUX通道 (可能在下面被覆盖) */
		adc->amux_ch = nbtn;

		/* 根据索引配置轴 */
		switch (nbtn) {
		case 0: /* 右摇杆 Y */
			invert = joypad->invert_absry;
			report_type = ABS_RY;
			break;
		case 1: /* 右摇杆 X */
			invert = joypad->invert_absrx;
			report_type = ABS_RX;
			break;
		case 2: /* 左摇杆 Y */
			invert = joypad->invert_absy;
			report_type = ABS_Y;
			break;
		case 3: /* 左摇杆 X */
			invert = joypad->invert_absx;
			report_type = ABS_X;
			break;
		default:
			dev_err(dev, "%s: invalid amux count %d\n", __func__, nbtn);
			return -EINVAL;
		}

		adc->report_type = report_type;
		adc->invert = invert;

		/* 解析调优值 */
		switch (report_type) {
		case ABS_RY:
			if (device_property_read_u32(dev, "abs_ry-p-tuning", &adc->tuning_p))
				adc->tuning_p = ADC_TUNING_DEFAULT;
			if (device_property_read_u32(dev, "abs_ry-n-tuning", &adc->tuning_n))
				adc->tuning_n = ADC_TUNING_DEFAULT;
			break;
		case ABS_RX:
			if (device_property_read_u32(dev, "abs_rx-p-tuning", &adc->tuning_p))
				adc->tuning_p = ADC_TUNING_DEFAULT;
			if (device_property_read_u32(dev, "abs_rx-n-tuning", &adc->tuning_n))
				adc->tuning_n = ADC_TUNING_DEFAULT;
			break;
		case ABS_Y:
			if (device_property_read_u32(dev, "abs_y-p-tuning", &adc->tuning_p))
				adc->tuning_p = ADC_TUNING_DEFAULT;
			if (device_property_read_u32(dev, "abs_y-n-tuning", &adc->tuning_n))
				adc->tuning_n = ADC_TUNING_DEFAULT;
			break;
		case ABS_X:
			if (device_property_read_u32(dev, "abs_x-p-tuning", &adc->tuning_p))
				adc->tuning_p = ADC_TUNING_DEFAULT;
			if (device_property_read_u32(dev, "abs_x-n-tuning", &adc->tuning_n))
				adc->tuning_n = ADC_TUNING_DEFAULT;
			break;
		}

		/*
		 * 如果指定了 "amux-channel-mapping", 则覆盖物理AMUX通道.
		 * DTS示例: amux-channel-mapping = <2 3 1 0>;
		 */
		if (of_property_read_u32_index(dev->of_node,
					       "amux-channel-mapping",
					       nbtn, &adc->amux_ch))
			adc->amux_ch = nbtn; /* 回退到默认值 */
	}

	return 0;
}

/*----------------------------------------------------------------------------*/
/**
 * joypad_adc_setup() - 初始化ADC摇杆轴.
 * @dev:    设备指针
 * @joypad: joypad上下文
 *
 * 主ADC设置函数. 分配ADC结构体并根据检测到的ADC模式
 * 委托给特定模式的设置函数:
 *   - 直接ADC: joypad_adc_setup_direct()
 *   - 传统/分离: joypad_adc_setup_legacy()
 *
 * 返回: 成功返回0, 失败返回负错误码
 */
static int joypad_adc_setup(struct device *dev, struct joypad *joypad)
{
	/* 分配ADC轴结构体 */
	joypad->adcs = devm_kzalloc(dev, joypad->amux_count *
				sizeof(struct bt_adc), GFP_KERNEL);
	if (!joypad->adcs) {
		dev_err(dev, "%s: adcs allocation failed\n", __func__);
		return -ENOMEM;
	}

	/* 委托给特定模式的设置 */
	if (joypad->direct_adc_mode)
		return joypad_adc_setup_direct(dev, joypad);
	else
		return joypad_adc_setup_legacy(dev, joypad);
}

/*----------------------------------------------------------------------------*/
static int joypad_gpio_setup(struct device *dev, struct joypad *joypad)
{
	struct device_node *node, *pp;
	int nbtn;
	bool has_adc_key = false;

	node = dev->of_node;
	if (!node)
		return -ENODEV;

	joypad->gpios = devm_kzalloc(dev, joypad->bt_gpio_count *
				sizeof(struct bt_gpio), GFP_KERNEL);

	if (!joypad->gpios) {
		dev_err(dev, "%s devm_kzmalloc error!", __func__);
		return -ENOMEM;
	}

	/* 第一遍: 检查是否存在ADC按键 */
	for_each_child_of_node(node, pp) {
		if (of_find_property(pp, "adc-key", NULL)) {
			has_adc_key = true;
			break;
		}
	}

	/* 如果需要, 获取ADC按键的共享ADC通道 */
	if (has_adc_key) {
		joypad->adc_key_channel = devm_iio_channel_get(dev, "adc-key");
		if (IS_ERR(joypad->adc_key_channel)) {
			dev_err(dev, "Failed to get adc-key io-channel\n");
			return -EINVAL;
		}
		joypad->has_adc_keys = true;
		dev_info(dev, "ADC key channel initialized\n");
	}

	/* 第二遍: 设置每个按钮 */
	nbtn = 0;
	for_each_child_of_node(node, pp) {
		struct bt_gpio *gpio;
		int error;

		if (!of_find_property(pp, "linux,code", NULL) &&
		    !of_find_property(pp, "adc-key", NULL))
			continue;

		gpio = &joypad->gpios[nbtn++];
		gpio->label = of_get_property(pp, "label", NULL);

		/* 检查这是否是ADC按键 */
		gpio->is_adc = of_find_property(pp, "adc-key", NULL);

		if (gpio->is_adc) {
			/* ADC按键设置 */
			if (of_property_read_u32(pp, "adc_value", &gpio->adc_value)) {
				dev_err(dev, "ADC key without adc_value\n");
				return -EINVAL;
			}
			/* 如果未指定则默认 fuzz = 20 */
			if (of_property_read_u32(pp, "adc_fuzz", &gpio->adc_fuzz))
				gpio->adc_fuzz = 20;

			/* 可选的组合键码 */
			gpio->combo_count = of_property_count_u32_elems(pp, "linux,code-combo");
			if (gpio->combo_count > 0 && gpio->combo_count <= 4) {
				of_property_read_u32_array(pp, "linux,code-combo",
					gpio->combo_codes, gpio->combo_count);
			} else {
				gpio->combo_count = 0;
			}

			gpio->num = -1;  /* ADC按键无GPIO */
			gpio->active_level = 0;
			gpio->old_value = false;

			dev_info(dev, "ADC key: label=%s, adc_value=%d, fuzz=%d, combo=%d\n",
				gpio->label ? gpio->label : "unnamed",
				gpio->adc_value, gpio->adc_fuzz, gpio->combo_count);
		} else {
			/* GPIO按键设置 - 原始逻辑 */
			enum of_gpio_flags flags;

			gpio->num = of_get_gpio_flags(pp, 0, &flags);
			if (gpio->num < 0) {
				error = gpio->num;
				dev_err(dev, "Failed to get gpio flags, error: %d\n",
					error);
				return error;
			}

			/* gpio有效电平(按键按下电平) */
			gpio->active_level = (flags & OF_GPIO_ACTIVE_LOW) ? 0 : 1;

			if (gpio_is_valid(gpio->num)) {
				error = devm_gpio_request_one(dev, gpio->num,
							      GPIOF_IN, gpio->label);
				if (error < 0) {
					dev_err(dev,
						"Failed to request GPIO %d, error %d\n",
						gpio->num, error);
					return error;
				}
			}
		}

		if (of_property_read_u32(pp, "linux,code", &gpio->linux_code)) {
			if (gpio->is_adc && gpio->combo_count > 0) {
				/* 使用第一个组合键码作为主键 */
				gpio->linux_code = gpio->combo_codes[0];
				gpio->combo_count--;
				memmove(gpio->combo_codes, gpio->combo_codes + 1,
					gpio->combo_count * sizeof(int));
			} else {
				dev_err(dev, "Button without keycode: 0x%x\n",
					gpio->num);
				return -EINVAL;
			}
		}
		if (of_property_read_u32(pp, "linux,input-type",
				&gpio->report_type))
			gpio->report_type = EV_KEY;
	}
	if (nbtn == 0)
		return -EINVAL;

	return 0;
}

/*----------------------------------------------------------------------------*/
/**
 * rumble_play_effect() - 力反馈震动效果的回调.
 * @dev:    输入设备
 * @data:   joypad上下文 (在 input_ff_create_memless 时传入)
 * @effect: 力反馈效果结构体
 *
 * 当用户空间触发震动效果时调用. 应用增益值并调度振动工作队列.
 *
 * 返回: 始终返回0
 */
static int rumble_play_effect(struct input_dev *dev, void *data, struct ff_effect *effect)
{
	struct joypad *joypad = data;
	u32 boosted_level;

	if (effect->type != FF_RUMBLE)
		return 0;

	/* 根据强/弱幅值应用增益 */
	if (effect->u.rumble.strong_magnitude)
		boosted_level = effect->u.rumble.strong_magnitude + joypad->boost_strong;
	else
		boosted_level = effect->u.rumble.weak_magnitude + joypad->boost_weak;

	joypad->level = (u16)CLAMP(boosted_level, 0, 0xffff);

	dev_dbg(joypad->dev, "rumble level = %d\n", joypad->level);
	schedule_work(&joypad->play_work);

	return 0;
}

/*----------------------------------------------------------------------------*/
/**
 * joypad_rumble_setup() - 初始化基于PWM的震动马达.
 * @dev:    设备指针
 * @joypad: joypad上下文
 *
 * 获取PWM设备并将其初始化为禁用状态.
 * 实际的振动通过力反馈接口控制.
 *
 * 注意: INIT_WORK() 由调用者 joypad_probe() 统一初始化.
 *
 * 返回: 成功返回0, 失败返回负错误码
 */
static int joypad_rumble_setup(struct device *dev, struct joypad *joypad)
{
	int err;
	struct pwm_state state;

	joypad->pwm = devm_pwm_get(dev, "enable");
	if (IS_ERR(joypad->pwm))
	{
		dev_err(dev, "rumble get error\n");
		return -EINVAL;
	}

	/* 同步PWM状态并确保其关闭. */
	pwm_init_state(joypad->pwm, &state);
	state.enabled = false;
	err = pwm_apply_state(joypad->pwm, &state);
	if (err) {
		dev_err(dev, "failed to apply initial PWM state: %d",
			err);
		return err;
	}
	dev_info(dev, "rumble setup success!\n");
	return 0;
}
static int joypad_input_setup(struct device *dev, struct joypad *joypad)
{
	struct input_polled_dev *poll_dev;
	struct input_dev *input;
	int nbtn, error;
	u32 joypad_revision = 0;
	u32 joypad_product = 0;
	u32 boost_weak = 0;
	u32 boost_strong = 0;
	poll_dev = devm_input_allocate_polled_device(dev);
	if (!poll_dev) {
		dev_err(dev, "no memory for polled device\n");
		return -ENOMEM;
	}

	poll_dev->private	= joypad;
	poll_dev->poll		= joypad_poll;
	poll_dev->poll_interval	= joypad->poll_interval;
	poll_dev->open		= joypad_open;
	poll_dev->close		= joypad_close;

	input = poll_dev->input;
	joypad->input = poll_dev->input;
	
	device_property_read_string(dev, "joypad-name", &input->name);
	input->phys = DRV_NAME"/input0";

	device_property_read_u32(dev, "joypad-revision", &joypad_revision);
	device_property_read_u32(dev, "joypad-product", &joypad_product);
	input->id.bustype = BUS_HOST;
	input->id.vendor  = 0x484B;
	input->id.product = (u16)joypad_product;
	input->id.version = (u16)joypad_revision;

	/*
	 * IIO ADC按键设置 (0 mv ~ 1800 mv) * adc->scale
	 * 在纯GPIO模式下, joypad->amux_count 为0, 不注册ABS轴.
	 */
	if (joypad->amux_count) {
		__set_bit(EV_ABS, input->evbit);
		for (nbtn = 0; nbtn < joypad->amux_count; nbtn++) {
			struct bt_adc *adc = &joypad->adcs[nbtn];
			/* 不为跳过的右摇杆对注册ABS能力 */
			if (joypad->skip_absr && (nbtn == 0 || nbtn == 1))
				continue;
			/* 不为跳过的左摇杆对注册ABS能力 */
			if (joypad->skip_absl && (nbtn == 2 || nbtn == 3))
				continue;

			input_set_abs_params(input, adc->report_type,
					adc->min, adc->max,
					joypad->bt_adc_fuzz,
					joypad->bt_adc_flat);
			dev_info(dev,
				"%s : SCALE = %d, ABS min = %d, max = %d,"
				" fuzz = %d, flat = %d, deadzone = %d\n",
				__func__, adc->scale, adc->min, adc->max,
				joypad->bt_adc_fuzz, joypad->bt_adc_flat,
				joypad->bt_adc_deadzone);
			dev_info(dev,
				"%s : adc tuning_p = %d, adc_tuning_n = %d\n\n",
				__func__, adc->tuning_p, adc->tuning_n);
		}
	}

	/*
	 * 如果配置了摇杆切换键，注册右摇杆能力
	 * R3 (BTN_THUMBR) 由 DTS 中的 GPIO 按钮节点注册
	 */
	if (joypad->stick_switch_code) {
		int abs_max = (ADC_MAX_VOLTAGE / 2);
		int abs_min = abs_max * (-1);

		if (joypad->bt_adc_scale) {
			abs_max *= joypad->bt_adc_scale;
			abs_min *= joypad->bt_adc_scale;
		}

		__set_bit(EV_ABS, input->evbit);
		input_set_abs_params(input, ABS_RX, abs_min, abs_max,
			joypad->bt_adc_fuzz, joypad->bt_adc_flat);
		input_set_abs_params(input, ABS_RY, abs_min, abs_max,
			joypad->bt_adc_fuzz, joypad->bt_adc_flat);

		dev_info(dev, "%s: stick-switch-key enabled, RX/RY registered (min=%d, max=%d)\n",
			__func__, abs_min, abs_max);
	}

	/* 震动设置 - 仅在震动设备可用时注册 */
	device_property_read_u32(dev, "rumble-boost-weak", &boost_weak);
	device_property_read_u32(dev, "rumble-boost-strong", &boost_strong);
	joypad->boost_weak = boost_weak;
	joypad->boost_strong = boost_strong;
	dev_info(dev, "Boost = %d, %d",boost_weak, boost_strong);
	
	if (joypad->has_rumble) {
		input_set_capability(input, EV_FF, FF_RUMBLE);
		error = input_ff_create_memless(input, joypad, rumble_play_effect);
		if (error) {
			dev_err(dev, "unable to register rumble, err=%d\n",
				error);
			return error;
		}
	}
	

	/* GPIO按键设置 */
	__set_bit(EV_KEY, input->evbit);
	for(nbtn = 0; nbtn < joypad->bt_gpio_count; nbtn++) {
		struct bt_gpio *gpio = &joypad->gpios[nbtn];
		int i, swapped;

		input_set_capability(input, gpio->report_type,
				gpio->linux_code);
		/* 注册交换后的键码(swap_ab/swap_xy), 否则交换上报会被input core丢弃 */
		swapped = joypad_swap_ab_code(gpio->linux_code);
		if (swapped != gpio->linux_code)
			input_set_capability(input, gpio->report_type, swapped);
		swapped = joypad_swap_xy_code(gpio->linux_code);
		if (swapped != gpio->linux_code)
			input_set_capability(input, gpio->report_type, swapped);
		/* 注册组合键能力 */
		for (i = 0; i < gpio->combo_count; i++) {
			input_set_capability(input, gpio->report_type,
					gpio->combo_codes[i]);
			swapped = joypad_swap_ab_code(gpio->combo_codes[i]);
			if (swapped != gpio->combo_codes[i])
				input_set_capability(input, gpio->report_type, swapped);
			swapped = joypad_swap_xy_code(gpio->combo_codes[i]);
			if (swapped != gpio->combo_codes[i])
				input_set_capability(input, gpio->report_type, swapped);
		}
	}

	if (joypad->auto_repeat)
		__set_bit(EV_REP, input->evbit);

	joypad->dev = dev;

	error = input_register_polled_device(poll_dev);
	if (error) {
		dev_err(dev, "unable to register polled device, err=%d\n",
			error);
		return error;
	}
	return 0;
}

/*----------------------------------------------------------------------------*/
static void joypad_setup_value_check(struct device *dev, struct joypad *joypad)
{
	/*
		fuzz: 指定用于从事件流中过滤噪声的模糊值.
	*/
	if (g_button_adc_fuzz)
		joypad->bt_adc_fuzz = g_button_adc_fuzz;
	else
		device_property_read_u32(dev, "button-adc-fuzz",
					&joypad->bt_adc_fuzz);
	/*
		flat: 在此值范围内的值将被joydev接口丢弃并报告为0.
	*/
	if (g_button_adc_flat)
		joypad->bt_adc_flat = g_button_adc_flat;
	else
		device_property_read_u32(dev, "button-adc-flat",
					&joypad->bt_adc_flat);

	/* 摇杆报告值控制 */
	if (g_button_adc_scale)
		joypad->bt_adc_scale = g_button_adc_scale;
	else
		device_property_read_u32(dev, "button-adc-scale",
					&joypad->bt_adc_scale);

	/* 摇杆死区值控制 */
	if (g_button_adc_deadzone)
		joypad->bt_adc_deadzone = g_button_adc_deadzone;
	else
		device_property_read_u32(dev, "button-adc-deadzone",
					&joypad->bt_adc_deadzone);

	/*
		毛刺滤波阈值(可选):
		与上次上报值和上一采样偏差都超过该值的采样视为孤立毛刺
		(如超过半行程的ADC干扰), 直接丢弃并保持上次上报值.
		属性不存在时保持为0, 不做任何过滤.
	*/
	device_property_read_u32(dev, "button-adc-max-step",
				&joypad->bt_adc_max_step);
}

/*----------------------------------------------------------------------------*/
/**
 * joypad_gpio_child_count() - 统计设备树中的按钮子节点.
 * @node: 父设备节点
 *
 * 统计具有 "linux,code" 或 "adc-key" 属性的子节点,
 * 表明它们是按钮定义.
 *
 * 返回: 按钮子节点的数量
 */
static int joypad_gpio_child_count(struct device_node *node)
{
	struct device_node *pp;
	int count = 0;

	for_each_child_of_node(node, pp) {
		if (of_find_property(pp, "linux,code", NULL) ||
		    of_find_property(pp, "adc-key", NULL))
			count++;
	}
	return count;
}

/*----------------------------------------------------------------------------*/
/**
 * joypad_parse_adc_mode() - 从设备树解析ADC模式配置.
 * @dev:    设备指针
 * @joypad: joypad上下文
 *
 * 确定ADC操作模式并设置amux_count:
 *   - 直接ADC: 2轴 (ABS_X/ABS_Y), 直接IIO通道
 *   - 分离ADC: 4轴, 双ADC通道, AMUX
 *   - 传统模式: 可通过amux-count属性配置
 *
 * 还解析轮询间隔和自动重复设置.
 */
static void joypad_parse_adc_mode(struct device *dev, struct joypad *joypad)
{
	/* 确定ADC模式 */
	joypad->direct_adc_mode = device_property_present(dev, "direct-adc");
	joypad->split_adc_mode = device_property_present(dev, "split-adc");

	/* 根据模式设置轴数 */
	if (joypad->direct_adc_mode) {
		/* 直接ADC: 固定2轴 (ABS_X/ABS_Y) */
		joypad->amux_count = 2;
	} else if (joypad->split_adc_mode) {
		/* 分离ADC: 固定4轴, 双ADC */
		joypad->amux_count = 4;
	} else {
		/* 传统模式: 从设备树获取数量 */
		device_property_read_u32(dev, "amux-count", &joypad->amux_count);
	}

	/* 解析轮询间隔 */
	device_property_read_u32(dev, "poll-interval", &joypad->poll_interval);

	/* 解析自动重复标志 */
	joypad->auto_repeat = device_property_present(dev, "autorepeat");
}

/*----------------------------------------------------------------------------*/
/**
 * joypad_parse_axes_config() - 解析轴反转和跳过配置.
 * @dev:    设备指针
 * @joypad: joypad上下文
 *
 * 解析以下设备树属性:
 *   - skip-absr: 跳过右摇杆对 (ABS_RX/ABS_RY)
 *   - skip-absl: 跳过左摇杆对 (ABS_X/ABS_Y)
 *   - invert-absx/absy/absrx/absry: 反转轴方向
 *   - stick-switch-key: 摇杆切换键 (按住时左摇杆→右摇杆)
 */
static void joypad_parse_axes_config(struct device *dev, struct joypad *joypad)
{
	/* 解析轴对跳过标志 */
	joypad->skip_absr = device_property_present(dev, "skip-absr");
	joypad->skip_absl = device_property_present(dev, "skip-absl");

	/* 解析轴反转标志 */
	joypad->invert_absx = device_property_present(dev, "invert-absx");
	joypad->invert_absy = device_property_present(dev, "invert-absy");
	joypad->invert_absrx = device_property_present(dev, "invert-absrx");
	joypad->invert_absry = device_property_present(dev, "invert-absry");

	/* 解析摇杆切换键 */
	if (device_property_read_u32(dev, "stick-switch-key", &joypad->stick_switch_code))
		joypad->stick_switch_code = 0;

	dev_info(dev,
		"%s: invert-absx=%d, invert-absy=%d, invert-absrx=%d, invert-absry=%d, skip-absr=%d, skip-absl=%d, stick-switch=%d\n",
		__func__, joypad->invert_absx, joypad->invert_absy,
		joypad->invert_absrx, joypad->invert_absry,
		joypad->skip_absr, joypad->skip_absl,
		joypad->stick_switch_code);
}

/*----------------------------------------------------------------------------*/
/**
 * joypad_parse_rumble_gpio() - 解析震动GPIO配置.
 * @dev:    设备指针
 * @joypad: joypad上下文
 *
 * 解析可选的 "rumble-gpio" 属性, 用于基于GPIO的振动.
 * 如果已配置, 则请求GPIO并将其设置为非活动状态.
 *
 * 返回: 成功返回0, 失败返回负错误码
 */
static int joypad_parse_rumble_gpio(struct device *dev, struct joypad *joypad)
{
	enum of_gpio_flags gflags;
	int gpio;
	int err;

	gpio = of_get_named_gpio_flags(dev->of_node, "rumble-gpio", 0, &gflags);
	if (!gpio_is_valid(gpio)) {
		joypad->rumble_gpio = -EINVAL;
		dev_dbg(dev, "no rumble-gpio; will try PWM\n");
		return 0;
	}

	joypad->rumble_gpio = gpio;
	joypad->rumble_active_low = !!(gflags & OF_GPIO_ACTIVE_LOW);

	err = devm_gpio_request(dev, joypad->rumble_gpio, "rumble-gpio");
	if (err) {
		dev_err(dev, "failed to request rumble gpio %d\n", joypad->rumble_gpio);
		return err;
	}

	/* 将GPIO设置为非活动状态 (马达关闭) */
	err = gpio_direction_output(joypad->rumble_gpio,
		joypad->rumble_active_low ? 1 : 0);
	if (err)
		return err;

	dev_info(dev, "rumble via GPIO: gpio=%d active_low=%d\n",
		 joypad->rumble_gpio, joypad->rumble_active_low);

	return 0;
}

/*----------------------------------------------------------------------------*/
/**
 * joypad_dt_parse() - 解析所有设备树配置.
 * @dev:    设备指针
 * @joypad: joypad上下文
 *
 * 主设备树解析函数. 协调解析:
 *   - ADC模式和轴配置
 *   - GPIO按钮定义
 *   - ADC/AMUX硬件设置
 *   - 震动马达配置
 *
 * 返回: 成功返回0, 失败返回负错误码
 */
static int joypad_dt_parse(struct device *dev, struct joypad *joypad)
{
	int error = 0;

	/* 首先应用boot.ini覆盖 */
	joypad_setup_value_check(dev, joypad);

	/* 解析ADC模式 (直接/分离/传统) 和基本设置 */
	joypad_parse_adc_mode(dev, joypad);

	/* 解析轴反转和跳过配置 */
	joypad_parse_axes_config(dev, joypad);

	/* 统计GPIO按钮 */
	joypad->bt_gpio_count = joypad_gpio_child_count(dev->of_node);
	if (joypad->bt_gpio_count == 0) {
		dev_err(dev, "no button child nodes found\n");
		return -EINVAL;
	}

	/*
	 * ADC/AMUX初始化 (仅当 amux_count > 0 时).
	 * 当 amux_count = 0 时, 以纯GPIO模式运行.
	 */
	if (joypad->amux_count) {
		error = joypad_adc_setup(dev, joypad);
		if (error)
			return error;

			/* 直接ADC无AMUX硬件, 跳过amux设置 */
		if (!joypad->direct_adc_mode) {
			error = joypad_amux_setup(dev, joypad);
			if (error)
				return error;
		} else {
			dev_info(dev, "%s: direct-adc enabled (joy_x/joy_y direct)\n",
				__func__);
		}
	} else {
		dev_info(dev, "%s: amux-count=0, ADC joystick disabled (GPIO-only mode)\n",
			__func__);
	}

	/* 设置GPIO按钮 */
	error = joypad_gpio_setup(dev, joypad);
	if (error)
		return error;

	dev_info(dev, "%s: adc axis cnt=%d, gpio button cnt=%d\n",
		__func__, joypad->amux_count, joypad->bt_gpio_count);

	/* 设置震动GPIO (可选) */
	error = joypad_parse_rumble_gpio(dev, joypad);
	if (error)
		return error;

	return 0;
}

static int __maybe_unused joypad_suspend(struct device *dev)
{
	struct platform_device *pdev  = to_platform_device(dev);
	struct joypad *joypad = platform_get_drvdata(pdev);

	cancel_work_sync(&joypad->play_work);
	if (joypad->level)
		joypad_vibrator_stop(joypad);

	return 0;
}

static int __maybe_unused joypad_resume(struct device *dev)
{
	struct platform_device *pdev  = to_platform_device(dev);
	struct joypad *joypad = platform_get_drvdata(pdev);

	if (joypad->level)
		joypad_vibrator_start(joypad);

	return 0;
}

static SIMPLE_DEV_PM_OPS(joypad_pm_ops,
			 joypad_suspend, joypad_resume);
/*----------------------------------------------------------------------------*/
static int joypad_probe(struct platform_device *pdev)
{
	struct joypad *joypad;
	struct device *dev = &pdev->dev;
	int error;

	joypad = devm_kzalloc(dev, sizeof(struct joypad), GFP_KERNEL);
	if (!joypad) {
		dev_err(dev, "joypad devm_kzmalloc error!");
		return -ENOMEM;
	}

	/* 设备树数据解析 */
	error = joypad_dt_parse(dev, joypad);
	if (error) {
		dev_err(dev, "dt parse error!(err = %d)\n", error);
		return error;
	}

	mutex_init(&joypad->lock);
	platform_set_drvdata(pdev, joypad);

	error = sysfs_create_group(&pdev->dev.kobj, &joypad_attr_group);
	if (error) {
		dev_err(dev, "create sysfs group fail, error: %d\n",
			error);
		return error;
	}

	/* 震动设置 (可选) - 必须在 input_setup 之前完成
	 *  - 如果 rumble-gpio 有效: 使用GPIO路径.
	 *  - 否则: 尝试PWM路径, 但如果不可用不要使探测失败.
	 */
	INIT_WORK(&joypad->play_work, pwm_vibrator_play_work);
	if (gpio_is_valid(joypad->rumble_gpio)) {
		joypad->has_rumble = true;
	} else {
		error = joypad_rumble_setup(dev, joypad);
		if (error) {
			dev_info(dev, "rumble not available, continuing without rumble support\n");
			joypad->has_rumble = false;
		} else {
			joypad->has_rumble = true;
		}
	}

	/* 轮询输入设备设置 */
	error = joypad_input_setup(dev, joypad);
	if (error) {
		dev_err(dev, "input setup failed!(err = %d)\n", error);
		return error;
	}

	/* 启动震动: 探测时振动1秒 */
	if (joypad->has_rumble) {
		joypad->level = 0xFFFF;
		joypad_vibrator_start(joypad);
		msleep(1000);
		joypad_vibrator_stop(joypad);
		joypad->level = 0;
	}

	dev_info(dev, "%s : probe success\n", __func__);
	return 0;
}

/*----------------------------------------------------------------------------*/
static const struct of_device_id joypad_of_match[] = {
	{ .compatible = "odroidgo3-joypad", },
	{},
};

MODULE_DEVICE_TABLE(of, joypad_of_match);

/*----------------------------------------------------------------------------*/
static struct platform_driver joypad_driver = {
	.probe = joypad_probe,
	.driver = {
		.name = DRV_NAME,
		.pm = &joypad_pm_ops,
		.of_match_table = of_match_ptr(joypad_of_match),
	},
};

/*----------------------------------------------------------------------------*/
static int __init joypad_init(void)
{
	return platform_driver_register(&joypad_driver);
}

/*----------------------------------------------------------------------------*/
static void __exit joypad_exit(void)
{
	platform_driver_unregister(&joypad_driver);
}

/*----------------------------------------------------------------------------*/
late_initcall(joypad_init);
module_exit(joypad_exit);

/*----------------------------------------------------------------------------*/
MODULE_AUTHOR("Hardkernel Co.,LTD");
MODULE_DESCRIPTION("SARADC joystick & GPIO buttons driver for ODROID-GO3");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);

/*----------------------------------------------------------------------------*/