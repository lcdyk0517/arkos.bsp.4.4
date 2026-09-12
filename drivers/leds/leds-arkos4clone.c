/*
 * Arkos4Clone LED 驱动程序
 *
 * =====================================================================
 * 驱动概述
 * =====================================================================
 *
 * 本驱动支持 Arkos4Clone 设备上的所有 LED 灯，共 3 种类型、最多 11 个 LED：
 *
 *   第一类：电源灯（充电指示灯）
 *     - arkos4clone-led  : 双色 LED，单个 GPIO 控制两种颜色
 *     - led-red          : 独立红色 LED
 *     - led-blue         : 独立蓝色 LED
 *     - led-green        : 独立绿色 LED
 *
 *   第二类：摇杆灯（用户可自由控制）
 *     - joy-green        : 摇杆 RGB 绿色
 *     - joy-red          : 摇杆 RGB 红色
 *     - joy-blue         : 摇杆 RGB 蓝色
 *     - joy-left         : 左侧 LED
 *     - joy-right        : 右侧 LED
 *     - joy-led          : 摇杆 LED
 *
 *   第三类：脉冲 RGB LED（单线脉冲协议）
 *     - joyled           : 通过脉冲数量控制 10 种颜色/效果
 *
 * =====================================================================
 * LED 分组与共存关系
 * =====================================================================
 *
 *   - 所有 LED 类型独立初始化，GPIO 申请失败时自动跳过，互不影响
 *   - v1/v2 硬件共用同一设备树，通过 GPIO 是否有效自动兼容
 *   - 电源灯（bicolor + led-red + led-blue）受充电监控联动控制
 *   - 摇杆灯（joy-*）始终由用户自由控制，无自动逻辑
 *   - 脉冲灯（joyled）始终由用户自由控制，无自动逻辑
 *
 * =====================================================================
 * 充电指示逻辑（仅电源灯）
 * =====================================================================
 *
 *   充电监控每 2 秒轮询一次 power_supply（battery → charger → usb → dc → mains）
 *
 *   优先级从高到低：
 *
 *   1. 充电/充满（charging/full）
 *      → 强制走充电逻辑，忽略阈值和用户输入
 *      - 充电中：arkos4clone-led=高电平颜色, led-red=亮, led-blue=灭
 *      - 充满：  arkos4clone-led=低电平颜色, led-red=灭, led-blue=亮
 *
 *   2. 未充电 + 阈值>0（threshold mode）
 *      → 按电量与阈值比较，忽略用户输入
 *      - 电量≥阈值：arkos4clone-led=低电平颜色, led-red=灭, led-blue=亮
 *      - 电量<阈值：arkos4clone-led=高电平颜色, led-red=亮, led-blue=灭
 *
 *   3. 未充电 + 阈值=0（user mode）
 *      → 用户通过 sysfs 自由控制，驱动不干预
 *
 * =====================================================================
 * 电池阈值
 * =====================================================================
 *
 *   位置：/sys/class/leds/<电源灯>/battery_threshold
 *         /sys/devices/platform/arkos4clone-led/battery_threshold
 *   值：0 = 关闭阈值模式（用户控制）
 *       10/20/30/.../90 = 电量百分比阈值
 *
 * =====================================================================
 * 休眠/唤醒逻辑
 * =====================================================================
 *
 *   suspend（休眠）：
 *     - 脉冲 LED：停止定时刷新，发送 OFF 信号
 *     - 电源灯（充电/充满）：走充电逻辑
 *     - 电源灯（非充电）：全部亮（作为待机指示）
 *     - 注意：不修改 cdev->brightness，保留用户原始设置
 *
 *   resume（唤醒）：
 *     - 脉冲 LED：重新初始化 GPIO，恢复之前的模式，重启定时刷新
 *     - 电源灯（阈值>0 或 充电/充满）：走 update_charge_leds
 *     - 电源灯（非充电 + 阈值=0）：根据 cdev->brightness 恢复用户设置
 *
 * =====================================================================
 * 设备树示例
 * =====================================================================
 *
 *   leds: arkos4clone-leds {
 *       compatible = "arkos4clone-led";
 *       // 电源灯
 *       led-gpio = <&gpio2 RK_PB5 GPIO_ACTIVE_HIGH>;
 *       led-high-color = "red";
 *       led-low-color = "blue";
 *       led-red = <&gpio0 RK_PC1 GPIO_ACTIVE_HIGH>;
 *       led-blue = <&gpio0 RK_PA0 GPIO_ACTIVE_HIGH>;
 *       led-green = <&gpio0 RK_PA1 GPIO_ACTIVE_HIGH>;
 *       // 摇杆灯 (v1 和 v2 共用)
 *       joy-green = <&gpio2 RK_PA1 GPIO_ACTIVE_HIGH>;
 *       joy-red = <&gpio2 RK_PA2 GPIO_ACTIVE_HIGH>;
 *       joy-blue = <&gpio2 RK_PA0 GPIO_ACTIVE_HIGH>;
 *       joy-left = <&gpio2 RK_PA3 GPIO_ACTIVE_HIGH>;
 *       joy-right = <&gpio2 RK_PA4 GPIO_ACTIVE_HIGH>;
 *       joy-led = <&gpio2 RK_PA5 GPIO_ACTIVE_HIGH>;
 *       // 脉冲 RGB LED
 *       pulse-gpios = <&gpio0 RK_PB3 GPIO_ACTIVE_HIGH>;
 *       irq-gpios = <&gpio0 RK_PB4 GPIO_ACTIVE_HIGH>;
 *   };
 *
 * =====================================================================
 * Sysfs 接口一览
 * =====================================================================
 *
 *   平台设备 /sys/devices/platform/arkos4clone-led/：
 *     - status            (RO) : 查看所有 LED 状态 + 充电状态
 *     - gpio              (RW) : <名称> <0/1> 设置任意 LED
 *     - colors            (RO) : 查看各 LED 可用颜色
 *     - battery_threshold (RW) : 电量阈值 (0/10-90)
 *
 *   LED class /sys/class/leds/：
 *     - arkos4clone-led/brightness       (RW) : 双色 LED (0/1/2)
 *     - arkos4clone-led/battery_threshold (RW) : 电量阈值
 *     - led-red/brightness               (RW) : 独立电源灯 (0/1)
 *     - led-red/battery_threshold        (RW) : 电量阈值
 *     - led-blue/brightness              (RW) : 独立电源灯 (0/1)
 *     - led-blue/battery_threshold       (RW) : 电量阈值
 *     - led-green/brightness             (RW) : 独立电源灯 (0/1)
 *     - led-green/battery_threshold      (RW) : 电量阈值
 *     - joy-green/brightness             (RW) : 摇杆灯 (0/1)
 *     - joy-red/brightness               (RW) : 摇杆灯 (0/1)
 *     - joy-blue/brightness              (RW) : 摇杆灯 (0/1)
 *     - joy-left/brightness              (RW) : 摇杆灯 (0/1)
 *     - joy-right/brightness             (RW) : 摇杆灯 (0/1)
 *     - joy-led/brightness               (RW) : 摇杆灯 (0/1)
 *     - joyled/brightness                (RW) : 脉冲 LED (0-255)
 *     - joyled/mode                      (RW) : 脉冲模式 (off/red/green/...)
 *
 * Copyright (C) 2024 lcdyk0517 <lcdyk0517@qq.com>
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/of_gpio.h>
#include <linux/of.h>
#include <linux/leds.h>
#include <linux/gpio.h>
#include <linux/workqueue.h>
#include <linux/power_supply.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/interrupt.h>

/* 最大独立 LED 数量 */
#define MAX_LEDS		9

/* 充电状态轮询间隔（毫秒） */
#define CHARGE_POLL_INTERVAL	2000

#define REFRESH_INTERVAL	(HZ * 2)

/* ===== 脉冲 LED (pulse-gpio) 定义 ===== */
/* 脉冲时序参数 */
#define PULSE_DELAY_US		1000
#define PULSE_GAP_US		5000

/* 脉冲 LED 模式 (脉冲数量编码) */
enum pulse_led_mode {
	PULSE_MODE_OFF		= 10,
	PULSE_MODE_RED		= 1,
	PULSE_MODE_RED_GREEN	= 2,
	PULSE_MODE_GREEN	= 3,
	PULSE_MODE_GREEN_BLUE	= 4,
	PULSE_MODE_BLUE		= 5,
	PULSE_MODE_BLUE_RED	= 6,
	PULSE_MODE_RED_GREEN_BLUE = 7,
	PULSE_MODE_BREATHING	= 8,
	PULSE_MODE_SCROLLING	= 9,
};

/* 独立 LED 索引 */
enum {
	LED_RED = 0,
	LED_BLUE,
	LED_GREEN,
	LED_JOY_GREEN,
	LED_JOY_RED,
	LED_JOY_BLUE,
	LED_JOY_LEFT,
	LED_JOY_RIGHT,
	LED_JOY_LED,
};

/* 独立 LED 结构体 */
struct arkos4clone_led {
	struct led_classdev cdev;
	struct gpio_desc *gpiod;
	bool active_low;
	bool valid;
	struct work_struct work;
	int new_level;
	int index;
	int battery_threshold;
	struct arkos4clone_led_priv *priv;
};

/* 前向声明 */
static int arkos4clone_pulse_led_init(struct arkos4clone_led_priv *priv);
static void arkos4clone_led_cleanup(struct arkos4clone_led *led);
static int arkos4clone_charge_monitor_init(struct arkos4clone_led_priv *priv);

/* LED classdev mode 属性前向声明 */
static ssize_t mode_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t mode_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
static DEVICE_ATTR_RW(mode);

/* LED classdev battery_threshold 属性前向声明 */
static ssize_t bicolor_threshold_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t bicolor_threshold_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);

static struct device_attribute dev_attr_bicolor_battery_threshold = {
	.attr = { .name = "battery_threshold", .mode = 0644 },
	.show = bicolor_threshold_show,
	.store = bicolor_threshold_store,
};

static ssize_t ind_threshold_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t ind_threshold_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);

static struct device_attribute dev_attr_ind_battery_threshold = {
	.attr = { .name = "battery_threshold", .mode = 0644 },
	.show = ind_threshold_show,
	.store = ind_threshold_store,
};

/* LED classdev 专用属性组 */
static struct attribute *bicolor_led_attrs[] = {
	&dev_attr_bicolor_battery_threshold.attr,
	NULL,
};
ATTRIBUTE_GROUPS(bicolor_led);

static struct attribute *ind_led_attrs[] = {
	&dev_attr_ind_battery_threshold.attr,
	NULL,
};
ATTRIBUTE_GROUPS(ind_led);

/* LED classdev 专用属性组 (显示在 /sys/class/leds/joyled/ 下) */
static struct attribute *joyled_attrs[] = {
	&dev_attr_mode.attr,
	NULL,
};
ATTRIBUTE_GROUPS(joyled);

/* LED 名称数组，用于设备树属性匹配 */
static const char *led_names[MAX_LEDS] = {
	"led-red",
	"led-blue",
	"led-green",
	"joy-green",
	"joy-red",
	"joy-blue",
	"joy-left",
	"joy-right",
	"joy-led",
};

/**
 * struct arkos4clone_led_priv - 驱动私有数据
 */
struct arkos4clone_led_priv {
	struct device *dev;
	struct arkos4clone_led leds[MAX_LEDS];

	/* 双色 LED */
	bool has_bicolor;
	int bicolor_gpio;
	bool bicolor_active_low;
	char bicolor_high_color[16];
	char bicolor_low_color[16];
	struct led_classdev bicolor_cdev;
	int bicolor_battery_threshold;

	/* 脉冲 LED */
	bool has_pulse_led;
	int pulse_gpio;
	int irq_gpio;
	int irq_num;
	struct led_classdev pulse_cdev;
	int pulse_mode;
	struct timer_list refresh_timer;

	/* 充电监控 */
	struct power_supply *psy;
	struct delayed_work charge_work;
	bool charging;
	bool full;
	bool charge_monitoring;

	/* 电量阈值控制 */
	int battery_threshold;
	int battery_capacity;
};

/* ===== 脉冲 LED 函数实现 ===== */

/**
 * arkos4clone_led_irq_handler - LED 控制器中断处理函数
 * @irq: 中断号
 * @dev_id: 设备 ID（私有数据指针）
 *
 * 当 LED 控制器通过 irq-gpios 发送信号时触发。
 * 可用于检测 LED 控制器状态变化。
 */
static irqreturn_t arkos4clone_led_irq_handler(int irq, void *dev_id)
{
	struct arkos4clone_led_priv *priv = dev_id;

	if (!priv)
		return IRQ_NONE;

	dev_dbg(priv->dev, "LED 控制器中断触发\n");
	return IRQ_HANDLED;
}

/**
 * send_pulse_count - 通过单线脉冲协议发送控制信号
 * @pulse_gpio: 脉冲输出 GPIO 编号
 * @pulse_count: 脉冲数量，决定 LED 显示模式（1-10）
 *
 * 协议时序（基于硬件逆向分析）：
 *   第一步：拉高 GPIO，保持 7 个延时周期（7ms）作为起始信号
 *   第二步：拉低 GPIO，保持 1 个延时周期（1ms）
 *   第三步：发送 pulse_count 个数据脉冲，每个脉冲高→1ms→低→1ms
 *   第四步：结束延时 5ms，确保低电平
 *
 * 脉冲数与模式对应关系：
 *   1=红, 2=黄, 3=绿, 4=青, 5=蓝, 6=紫, 7=白, 8=呼吸, 9=彩虹, 10/11=关闭
 */
static void send_pulse_count(int pulse_gpio, int pulse_count)
{
	int i;

	/* 第一步: 起始信号 - 拉高并保持 7 个延时周期 */
	gpio_set_value(pulse_gpio, 1);
	for (i = 0; i < 7; i++)
		udelay(PULSE_DELAY_US);

	/* 第二步: 拉低 */
	gpio_set_value(pulse_gpio, 0);
	udelay(PULSE_DELAY_US);

	/* 第三步: 发送数据脉冲 */
	for (i = 0; i < pulse_count; i++) {
		gpio_set_value(pulse_gpio, 1);
		udelay(PULSE_DELAY_US);
		gpio_set_value(pulse_gpio, 0);
		udelay(PULSE_DELAY_US);
	}

	/* 第四步: 结束延时并确保低电平 */
	udelay(PULSE_GAP_US);
	gpio_set_value(pulse_gpio, 0);
}

/**
 * arkos4clone_pulse_led_set - 设置脉冲 LED 颜色/效果
 * @led_cdev: LED 类设备指针
 * @brightness: 亮度值（0-255），映射到 10 种模式
 *
 * 亮度值分段映射：
 *   0       → 关闭（脉冲数 10）
 *   1-28    → 红色（脉冲数 1）
 *   29-56   → 黄色（脉冲数 2）
 *   57-84   → 绿色（脉冲数 3）
 *   85-112  → 青色（脉冲数 4）
 *   113-140 → 蓝色（脉冲数 5）
 *   141-168 → 紫色（脉冲数 6）
 *   169-196 → 白色（脉冲数 7）
 *   197-224 → 呼吸灯（脉冲数 8）
 *   225-255 → 彩虹（脉冲数 9）
 */
static void arkos4clone_pulse_led_set(struct led_classdev *led_cdev,
				      enum led_brightness brightness)
{
	struct arkos4clone_led_priv *priv =
		container_of(led_cdev, struct arkos4clone_led_priv, pulse_cdev);
	int target_mode;

	if (!gpio_is_valid(priv->pulse_gpio))
		return;

	if (brightness == LED_OFF) {
		target_mode = PULSE_MODE_OFF;
	} else if (brightness <= 28) {
		target_mode = PULSE_MODE_RED;
	} else if (brightness <= 56) {
		target_mode = PULSE_MODE_RED_GREEN;
	} else if (brightness <= 84) {
		target_mode = PULSE_MODE_GREEN;
	} else if (brightness <= 112) {
		target_mode = PULSE_MODE_GREEN_BLUE;
	} else if (brightness <= 140) {
		target_mode = PULSE_MODE_BLUE;
	} else if (brightness <= 168) {
		target_mode = PULSE_MODE_BLUE_RED;
	} else if (brightness <= 196) {
		target_mode = PULSE_MODE_RED_GREEN_BLUE;
	} else if (brightness <= 224) {
		target_mode = PULSE_MODE_BREATHING;
	} else {
		target_mode = PULSE_MODE_SCROLLING;
	}

	send_pulse_count(priv->pulse_gpio, target_mode);
	priv->pulse_mode = target_mode;
}

/**
 * arkos4clone_pulse_led_get - 获取脉冲 LED 当前模式
 * @led_cdev: LED 类设备指针
 *
 * 返回：LED_FULL（模式>0）或 LED_OFF（模式=0/10/11）
 */
static enum led_brightness arkos4clone_pulse_led_get(struct led_classdev *led_cdev)
{
	struct arkos4clone_led_priv *priv =
		container_of(led_cdev, struct arkos4clone_led_priv, pulse_cdev);
	return priv->pulse_mode ? LED_FULL : LED_OFF;
}

/**
 * arkos4clone_pulse_refresh_timer - 脉冲 LED 定时刷新回调
 *
 * LED 控制器为非锁存型，需要每隔 2 秒重发一次脉冲以维持当前显示。
 * 定时器在 suspend 时停止，resume 时重启。
 */
static void arkos4clone_pulse_refresh_timer(unsigned long data)
{
	struct arkos4clone_led_priv *priv = (struct arkos4clone_led_priv *)data;

	if (priv->pulse_mode > 0)
		send_pulse_count(priv->pulse_gpio, priv->pulse_mode);

	mod_timer(&priv->refresh_timer, jiffies + REFRESH_INTERVAL);
}

/* ===== 独立 GPIO LED 函数实现 ===== */

/**
 * arkos4clone_led_work - LED GPIO 工作队列处理函数
 * @work: 工作队列结构体指针
 *
 * 当 GPIO 可能睡眠时（如 I2C GPIO 扩展器），通过工作队列在进程上下文中
 * 执行 GPIO 操作。对于普通 GPIO，直接设置即可，不会走到这里。
 */
static void arkos4clone_led_work(struct work_struct *work)
{
	struct arkos4clone_led *led =
		container_of(work, struct arkos4clone_led, work);

	if (led->gpiod)
		gpiod_set_value_cansleep(led->gpiod, led->new_level);
}

/**
 * arkos4clone_led_set_raw - 直接设置独立 LED GPIO 电平
 * @led: LED 结构体指针
 * @level: 电平值（0=灭，1=亮）
 *
 * 根据 GPIO 是否可能睡眠选择设置方式：
 *   - 可睡眠：通过工作队列异步设置
 *   - 不可睡眠：直接设置
 */
static void arkos4clone_led_set_raw(struct arkos4clone_led *led, int level)
{
	if (!led->gpiod)
		return;

	led->new_level = level;

	if (gpiod_cansleep(led->gpiod))
		schedule_work(&led->work);
	else
		gpiod_set_value(led->gpiod, level);
}

/* ===== 双色 LED 函数实现 ===== */

/**
 * arkos4clone_bicolor_set - 设置双色 LED 颜色
 * @priv: 驱动私有数据结构指针
 * @color: 颜色值
 *   0 = 低电平颜色（如蓝色）
 *   1 = 高电平颜色（如红色）
 *   2 = 高阻态（灯灭）
 *
 * 双色 LED 使用单个 GPIO 控制两种颜色：
 *   - active_high：输出 1=高电平颜色亮，输出 0=低电平颜色亮
 *   - active_low：输出 0=高电平颜色亮，输出 1=低电平颜色亮
 *   - 高阻态：切换为输入模式，两个颜色都灭
 */
static void arkos4clone_bicolor_set(struct arkos4clone_led_priv *priv, int color)
{
	int level;

	if (!gpio_is_valid(priv->bicolor_gpio))
		return;

	if (color == 2) {
		/* 高阻态：切换为输入模式 */
		gpio_direction_input(priv->bicolor_gpio);
		return;
	}

	/* 根据 active_low 标志计算实际 GPIO 电平 */
	if (priv->bicolor_active_low)
		level = color ? 0 : 1;
	else
		level = color ? 1 : 0;

	gpio_direction_output(priv->bicolor_gpio, level);
}

/**
 * arkos4clone_bicolor_get - 获取双色 LED 当前颜色
 * @priv: 驱动私有数据结构指针
 *
 * 返回：0=低电平颜色亮，1=高电平颜色亮，2=高阻态（灭）
 */
static int arkos4clone_bicolor_get(struct arkos4clone_led_priv *priv)
{
	int value;
	struct gpio_desc *gpiod;

	if (!gpio_is_valid(priv->bicolor_gpio))
		return 0;

	/* 检查是否为输入模式（高阻态） */
	gpiod = gpio_to_desc(priv->bicolor_gpio);
	if (gpiod && gpiod_get_direction(gpiod) == GPIOF_DIR_IN)
		return 2;

	value = gpio_get_value(priv->bicolor_gpio);

	/* 根据 active_low 标志转换 */
	if (priv->bicolor_active_low)
		return value ? 0 : 1;
	else
		return value ? 1 : 0;
}

/**
 * arkos4clone_led_set - 独立 LED 亮度设置回调（sysfs 写 brightness 时调用）
 * @led_cdev: LED 类设备指针
 * @brightness: 亮度值（LED_OFF=0 或 LED_FULL=1）
 *
 * 对于 led-red 和 led-blue（电源灯），在充电监控开启且满足以下条件时
 * 阻止用户控制：
 *   - 电池阈值 > 0（阈值模式）
 *   - 正在充电（charging）
 *   - 已充满（full）
 *
 * 摇杆灯（joy-*）始终允许用户控制。
 */
static void arkos4clone_led_set(struct led_classdev *led_cdev,
				enum led_brightness brightness)
{
	struct arkos4clone_led *led =
		container_of(led_cdev, struct arkos4clone_led, cdev);
	struct arkos4clone_led_priv *priv = led->priv;
	int level;

	if (!led->gpiod || !priv)
		return;

	/* 阈值模式或充电/充满时，阻止 led-red 和 led-blue 的用户控制 */
	if ((led->index == LED_RED || led->index == LED_BLUE)) {
		if (priv->charge_monitoring && (led->battery_threshold > 0 || priv->charging || priv->full)) {
			dev_dbg(priv->dev, "%s control blocked\n", led_cdev->name);
			return;
		}
	}

	level = (brightness == LED_OFF) ? 0 : 1;
	arkos4clone_led_set_raw(led, level);
}

/**
 * arkos4clone_led_get - 独立 LED 亮度获取回调（sysfs 读 brightness 时调用）
 * @led_cdev: LED 类设备指针
 *
 * 返回：LED_FULL（GPIO 高电平）或 LED_OFF（GPIO 低电平）
 */
static enum led_brightness arkos4clone_led_get(struct led_classdev *led_cdev)
{
	struct arkos4clone_led *led =
		container_of(led_cdev, struct arkos4clone_led, cdev);

	if (!led->gpiod)
		return LED_OFF;

	return gpiod_get_value_cansleep(led->gpiod) ? LED_FULL : LED_OFF;
}

/**
 * arkos4clone_bicolor_cdev_set - 双色 LED 亮度设置回调（sysfs 写 brightness 时调用）
 * @led_cdev: LED 类设备指针
 * @brightness: 颜色值（0=低电平颜色亮，1=高电平颜色亮，2=高阻态灭）
 *
 * 在充电监控开启且满足以下条件时阻止用户控制：
 *   - 电池阈值 > 0（阈值模式）
 *   - 正在充电（charging）
 *   - 已充满（full）
 */
static void arkos4clone_bicolor_cdev_set(struct led_classdev *led_cdev,
					 enum led_brightness brightness)
{
	struct arkos4clone_led_priv *priv =
		container_of(led_cdev, struct arkos4clone_led_priv, bicolor_cdev);

	/* 阈值模式或充电/充满时阻止用户控制 */
	if (priv->charge_monitoring && (priv->bicolor_battery_threshold > 0 || priv->charging || priv->full)) {
		dev_dbg(priv->dev, "led control blocked\n");
		return;
	}

	arkos4clone_bicolor_set(priv, brightness);
}

/**
 * arkos4clone_bicolor_cdev_get - 双色 LED 亮度获取回调（sysfs 读 brightness 时调用）
 * @led_cdev: LED 类设备指针
 *
 * 返回：当前颜色值（0=低电平颜色亮，1=高电平颜色亮，2=高阻态灭）
 */
static enum led_brightness arkos4clone_bicolor_cdev_get(struct led_classdev *led_cdev)
{
	struct arkos4clone_led_priv *priv =
		container_of(led_cdev, struct arkos4clone_led_priv, bicolor_cdev);

	return arkos4clone_bicolor_get(priv);
}

/* ===== LED 初始化函数 ===== */

/**
 * arkos4clone_led_init - 初始化单个独立 GPIO LED
 * @dev: 设备指针
 * @led: LED 结构体指针（输出）
 * @name: LED 名称（用于 sysfs 和日志）
 * @gpio: GPIO 编号
 * @active_low: 是否低电平有效
 * @index: LED 索引（用于区分电源灯和摇杆灯）
 * @priv: 驱动私有数据指针
 *
 * 流程：申请 GPIO → 转换为 gpio_desc → 初始化工作队列 → 注册 LED class 设备
 * GPIO 申请失败时返回错误，不会导致整个 probe 失败。
 *
 * 返回：0=成功，负数=错误码
 */
static int arkos4clone_led_init(struct device *dev,
				struct arkos4clone_led *led,
				const char *name,
				int gpio,
				bool active_low,
				int index,
				struct arkos4clone_led_priv *priv)
{
	unsigned long flags;
	int ret;

	led->valid = false;
	led->active_low = active_low;
	led->index = index;
	led->priv = priv;

	if (!gpio_is_valid(gpio)) {
		dev_dbg(dev, "LED %s: GPIO not configured\n", name);
		return 0;
	}

	/* 所有 LED 初始状态为灭 */
	flags = active_low ? GPIOF_OUT_INIT_HIGH : GPIOF_OUT_INIT_LOW;

	if (active_low)
		flags |= GPIOF_ACTIVE_LOW;

	ret = devm_gpio_request_one(dev, gpio, flags, name);
	if (ret) {
		dev_warn(dev, "LED %s: failed to request GPIO %d: %d\n",
			 name, gpio, ret);
		return ret;
	}

	led->gpiod = gpio_to_desc(gpio);
	if (!led->gpiod) {
		dev_err(dev, "LED %s: failed to get GPIO descriptor\n", name);
		return -EINVAL;
	}

	INIT_WORK(&led->work, arkos4clone_led_work);

	led->cdev.name = name;
	led->cdev.brightness_set = arkos4clone_led_set;
	led->cdev.brightness_get = arkos4clone_led_get;
	led->cdev.max_brightness = 1;
	led->cdev.brightness = LED_OFF;
	led->cdev.flags = 0;
	led->cdev.groups = ind_led_groups;

	ret = led_classdev_register(dev, &led->cdev);
	if (ret) {
		dev_err(dev, "LED %s: failed to register: %d\n", name, ret);
		return ret;
	}

	led->valid = true;
	dev_info(dev, "LED %s: GPIO %d (active-%s)\n",
		 name, gpio, active_low ? "low" : "high");

	return 0;
}

/**
 * arkos4clone_bicolor_init - 初始化双色 LED（led-gpio）
 * @priv: 驱动私有数据结构指针
 *
 * 从设备树读取以下属性：
 *   - led-gpio        : GPIO 编号
 *   - led-high-color  : 高电平颜色名（默认 "red"）
 *   - led-low-color   : 低电平颜色名（默认 "blue"）
 *   - GPIO_ACTIVE_LOW  : 是否低电平有效
 *
 * 注册名为 "arkos4clone-led" 的 LED class 设备，max_brightness=2。
 *
 * 返回：0=成功，负数=错误码
 */
static int arkos4clone_bicolor_init(struct arkos4clone_led_priv *priv)
{
	struct device *dev = priv->dev;
	struct device_node *np = dev->of_node;
	enum of_gpio_flags flags;
	const char *color;
	int gpio, ret;

	gpio = of_get_named_gpio_flags(np, "led-gpio", 0, &flags);
	if (!gpio_is_valid(gpio)) {
		dev_dbg(dev, "No bicolor LED (led-gpio) configured\n");
		return 0;
	}

	priv->bicolor_gpio = gpio;
	priv->bicolor_active_low = (flags & OF_GPIO_ACTIVE_LOW) != 0;

	/* 获取颜色名称 */
	color = of_get_property(np, "led-high-color", NULL);
	if (color)
		strlcpy(priv->bicolor_high_color, color, sizeof(priv->bicolor_high_color));
	else
		strcpy(priv->bicolor_high_color, "red");

	color = of_get_property(np, "led-low-color", NULL);
	if (color)
		strlcpy(priv->bicolor_low_color, color, sizeof(priv->bicolor_low_color));
	else
		strcpy(priv->bicolor_low_color, "blue");

	/* 申请 GPIO */
	ret = devm_gpio_request_one(dev, gpio,
				    GPIOF_OUT_INIT_LOW | (priv->bicolor_active_low ? GPIOF_ACTIVE_LOW : 0),
				    "arkos4clone-led");
	if (ret) {
		dev_err(dev, "Failed to request LED GPIO %d: %d\n", gpio, ret);
		return ret;
	}

	/* 注册 LED 类设备 */
	priv->bicolor_cdev.name = "arkos4clone-led";
	priv->bicolor_cdev.max_brightness = 2;	/* 0=低电平颜色，1=高电平颜色，2=高阻态 */
	priv->bicolor_cdev.brightness_set = arkos4clone_bicolor_cdev_set;
	priv->bicolor_cdev.brightness_get = arkos4clone_bicolor_cdev_get;
	priv->bicolor_cdev.brightness = 0;	/* 默认：低电平颜色 */
	priv->bicolor_cdev.flags = 0;
	priv->bicolor_cdev.groups = bicolor_led_groups;

	ret = led_classdev_register(dev, &priv->bicolor_cdev);
	if (ret) {
		dev_err(dev, "Failed to register LED: %d\n", ret);
		return ret;
	}

	priv->has_bicolor = true;
	dev_info(dev, "LED (bicolor): GPIO %d, high=%s, low=%s, active-%s\n",
		 gpio, priv->bicolor_high_color, priv->bicolor_low_color,
		 priv->bicolor_active_low ? "low" : "high");

	return 0;
}

/**
 * arkos4clone_pulse_led_init - 初始化脉冲 RGB LED（pulse-gpio）
 * @priv: 驱动私有数据结构指针
 *
 * 从设备树读取 pulse-gpios 属性，申请 GPIO，注册名为 "joyled" 的
 * LED class 设备（max_brightness=255）。
 *
 * 初始化操作：
 *   - 发送脉冲数 11（关闭 LED）作为初始状态
 *   - 启动 2 秒定时刷新（维持非锁存控制器的显示）
 *
 * 返回：0=成功，负数=错误码
 */
static int arkos4clone_pulse_led_init(struct arkos4clone_led_priv *priv)
{
	struct device *dev = priv->dev;
	struct device_node *np = dev->of_node;
	int gpio, ret;

	/* 获取脉冲 GPIO (pulse-gpios) */
	gpio = of_get_named_gpio(np, "pulse-gpios", 0);
	if (!gpio_is_valid(gpio)) {
		dev_dbg(dev, "No pulse LED (pulse-gpios) configured\n");
		return 0;
	}

	priv->pulse_gpio = gpio;

	/* 获取中断 GPIO (irq-gpios)，用于接收 LED 控制器反馈 */
	priv->irq_gpio = -EINVAL;
	priv->irq_num = -EINVAL;

	gpio = of_get_named_gpio(np, "irq-gpios", 0);
	if (gpio_is_valid(gpio)) {
		priv->irq_gpio = gpio;
		ret = devm_gpio_request_one(dev, gpio, GPIOF_OUT_INIT_LOW,
					    "arkos4clone-irq");
		if (ret) {
			dev_warn(dev, "Failed to request IRQ GPIO %d: %d\n",
				 gpio, ret);
			priv->irq_gpio = -EINVAL;
		} else {
			/* 复位延时后改为输入模式 */
			udelay(100);
			gpio_direction_input(priv->irq_gpio);
			priv->irq_num = gpio_to_irq(priv->irq_gpio);
			if (priv->irq_num >= 0) {
				ret = devm_request_irq(dev, priv->irq_num,
						       arkos4clone_led_irq_handler,
						       IRQF_TRIGGER_RISING,
						       "arkos4clone-led-irq",
						       priv);
				if (ret)
					priv->irq_num = -EINVAL;
				else
					dev_dbg(dev, "IRQ %d registered for GPIO %d\n",
						priv->irq_num, gpio);
			}
		}
	} else {
		dev_dbg(dev, "No IRQ GPIO (irq-gpios) configured\n");
	}

	/* 申请 pulse GPIO */
	ret = devm_gpio_request_one(dev, priv->pulse_gpio, GPIOF_OUT_INIT_LOW,
				    "arkos4clone-pulse");
	if (ret) {
		dev_err(dev, "Failed to request pulse GPIO %d: %d\n",
			priv->pulse_gpio, ret);
		return ret;
	}

	/* 注册 joyled LED 类设备 */
	priv->pulse_cdev.name = "joyled";
	priv->pulse_cdev.brightness_set = arkos4clone_pulse_led_set;
	priv->pulse_cdev.brightness_get = arkos4clone_pulse_led_get;
	priv->pulse_cdev.max_brightness = 255;
	priv->pulse_cdev.flags = 0;
	priv->pulse_cdev.groups = joyled_groups;

	ret = devm_led_classdev_register(dev, &priv->pulse_cdev);
	if (ret) {
		dev_err(dev, "Failed to register joyled: %d\n", ret);
		return ret;
	}

	/* 发送初始化脉冲 (11脉冲 = 关闭) */
	send_pulse_count(priv->pulse_gpio, 11);
	priv->pulse_mode = 11;

	/* 启动定时刷新 */
	setup_timer(&priv->refresh_timer, arkos4clone_pulse_refresh_timer,
		    (unsigned long)priv);
	mod_timer(&priv->refresh_timer, jiffies + REFRESH_INTERVAL);

	priv->has_pulse_led = true;

	dev_info(dev, "Pulse LED: GPIO %d, IRQ GPIO %d (IRQ %d)\n",
		 priv->pulse_gpio,
		 gpio_is_valid(priv->irq_gpio) ? priv->irq_gpio : -1,
		 priv->irq_num >= 0 ? priv->irq_num : -1);

	return 0;
}

/**
 * arkos4clone_led_cleanup - 清理独立 LED 资源
 * @led: LED 结构体指针
 */
static void arkos4clone_led_cleanup(struct arkos4clone_led *led)
{
	if (led->valid) {
		led_classdev_unregister(&led->cdev);
		cancel_work_sync(&led->work);
		led->valid = false;
	}
}

/**
 * arkos4clone_update_charge_leds - 根据充电状态更新电源灯
 * @priv: 私有数据结构指针
 *
 * 三级优先级逻辑：
 *
 * 1. 充电/充满（最高优先级）：
 *    - 无视阈值，强制走充电逻辑
 *    - 充电中：bicolor=高电平颜色(1), led-red=亮(1), led-blue=灭(0)
 *    - 充满：  bicolor=低电平颜色(0), led-red=灭(0), led-blue=亮(1)
 *
 * 2. 非充电 + 阈值>0（threshold mode）：
 *    - 按电量与阈值比较
 *    - 电量≥阈值：bicolor=低电平颜色(0), led-red=灭(0), led-blue=亮(1)
 *    - 电量<阈值：bicolor=高电平颜色(1), led-red=亮(1), led-blue=灭(0)
 *    - battery_capacity=-1 时视为未就绪，按"低于阈值"处理
 *
 * 3. 非充电 + 阈值=0（user mode）：
 *    - 不做任何事，允许用户通过 sysfs 自由控制
 */
static void arkos4clone_update_charge_leds(struct arkos4clone_led_priv *priv)
{
	/* 充电/充满优先：无视阈值，走充电逻辑 */
	if (priv->charging || priv->full) {
		if (priv->has_bicolor)
			arkos4clone_bicolor_set(priv, priv->full ? 0 : 1);

		if (priv->leds[LED_RED].valid || priv->leds[LED_BLUE].valid) {
			if (priv->leds[LED_RED].valid)
				arkos4clone_led_set_raw(&priv->leds[LED_RED],
						       priv->charging ? 1 : 0);
			if (priv->leds[LED_BLUE].valid)
				arkos4clone_led_set_raw(&priv->leds[LED_BLUE],
						       priv->charging ? 0 : 1);
		}
		return;
	}

	/* 非充电 + 阈值模式：每个 LED 按自己的阈值独立判断 */
	if (priv->has_bicolor && priv->bicolor_battery_threshold > 0) {
		bool above = (priv->battery_capacity >= priv->bicolor_battery_threshold &&
			      priv->battery_capacity >= 0);
		arkos4clone_bicolor_set(priv, above ? 0 : 1);
	}

	if (priv->leds[LED_RED].valid && priv->leds[LED_RED].battery_threshold > 0) {
		bool above = (priv->battery_capacity >= priv->leds[LED_RED].battery_threshold &&
			      priv->battery_capacity >= 0);
		arkos4clone_led_set_raw(&priv->leds[LED_RED], above ? 0 : 1);
	}

	if (priv->leds[LED_BLUE].valid && priv->leds[LED_BLUE].battery_threshold > 0) {
		bool above = (priv->battery_capacity >= priv->leds[LED_BLUE].battery_threshold &&
			      priv->battery_capacity >= 0);
		arkos4clone_led_set_raw(&priv->leds[LED_BLUE], above ? 1 : 0);
	}

	/* 非充电 + 无阈值：用户控制，不做任何事 */
}

/**
 * arkos4clone_charge_work - 充电状态检测工作队列
 * @work: 工作队列结构体指针
 *
 * 每 2 秒轮询一次电源状态，检测充电/充满状态变化。
 *
 * 执行流程：
 *   1. 读取电池容量（POWER_SUPPLY_PROP_CAPACITY）
 *   2. 读取充电状态（POWER_SUPPLY_PROP_STATUS）
 *   3. 根据状态值判断 charging/full 标志
 *   4. 有自动控制逻辑时更新 LED（充电/充满/阈值模式）
 *   5. 重新调度下一次轮询
 *
 * 检测的电源类型优先级：
 *   battery → charger → usb → dc → mains
 */
static void arkos4clone_charge_work(struct work_struct *work)
{
	struct arkos4clone_led_priv *priv =
		container_of(to_delayed_work(work), struct arkos4clone_led_priv, charge_work);
	union power_supply_propval val_status;
	bool charging = false;
	bool full = false;
	int ret;

	if (!priv->psy)
		goto reschedule;

	/* 读取电池容量 */
	{
		union power_supply_propval val_cap;
		int ret_cap;
		ret_cap = power_supply_get_property(priv->psy, POWER_SUPPLY_PROP_CAPACITY, &val_cap);
		if (ret_cap == 0)
			priv->battery_capacity = val_cap.intval;
	}

	/* 从电池电源供应获取充电状态 */
	ret = power_supply_get_property(priv->psy, POWER_SUPPLY_PROP_STATUS, &val_status);
	if (ret) {
		dev_err(priv->dev, "Failed to get STATUS property: %d\n", ret);
		goto reschedule;
	}

	/* 根据状态判断充电和充满 */
	switch (val_status.intval) {
	case POWER_SUPPLY_STATUS_CHARGING:
		charging = true;
		full = false;
		break;
	case POWER_SUPPLY_STATUS_FULL:
		charging = false;
		full = true;
		break;
	case POWER_SUPPLY_STATUS_DISCHARGING:
	case POWER_SUPPLY_STATUS_NOT_CHARGING:
	case POWER_SUPPLY_STATUS_UNKNOWN:
	default:
		charging = false;
		full = false;
		break;
	}

	/* 始终更新充电状态 */
	if (priv->charging != charging || priv->full != full) {
		dev_dbg(priv->dev, "Power: status=%d (%s), charging=%d, full=%d\n",
			val_status.intval,
			val_status.intval == POWER_SUPPLY_STATUS_CHARGING ? "CHARGING" :
			val_status.intval == POWER_SUPPLY_STATUS_FULL ? "FULL" :
			val_status.intval == POWER_SUPPLY_STATUS_DISCHARGING ? "DISCHARGING" : "OTHER",
			charging, full);

		priv->charging = charging;
		priv->full = full;
	}

	/* 有自动控制逻辑时更新 LED */
	if (priv->charging || priv->full ||
	    priv->bicolor_battery_threshold > 0 ||
	    priv->leds[LED_RED].battery_threshold > 0 ||
	    priv->leds[LED_BLUE].battery_threshold > 0)
		arkos4clone_update_charge_leds(priv);

reschedule:
	schedule_delayed_work(&priv->charge_work,
			      msecs_to_jiffies(CHARGE_POLL_INTERVAL));
}

/**
 * arkos4clone_charge_monitor_init - 初始化充电监控
 * @priv: 私有数据结构指针
 *
 * 检查是否有可用于充电指示的 LED（bicolor、led-red、led-blue）。
 * 如果存在，尝试获取电源供应对象（battery → charger → usb → dc → mains），
 * 并启动 2 秒周期的充电状态轮询工作队列。
 *
 * 首次轮询延迟 500ms（给 power_supply 子系统时间注册）。
 *
 * 返回：0=成功
 */
static int arkos4clone_charge_monitor_init(struct arkos4clone_led_priv *priv)
{
	bool has_charge_led = false;

	/* 检查是否有可用于充电指示的 LED */
	if (priv->has_bicolor)
		has_charge_led = true;

	/* 如果配置了 led-red 或 led-blue，启用充电监控 */
	if (priv->leds[LED_RED].valid || priv->leds[LED_BLUE].valid)
		has_charge_led = true;

	if (!has_charge_led) {
		dev_info(priv->dev, "Charge monitoring disabled (no charge LED configured)\n");
		return 0;
	}

	/* 尝试获取电源供应对象 */
	priv->psy = power_supply_get_by_name("battery");
	if (priv->psy) {
		dev_dbg(priv->dev, "Found power supply: battery\n");
	} else {
		priv->psy = power_supply_get_by_name("charger");
		if (priv->psy)
			dev_dbg(priv->dev, "Found power supply: charger\n");
	}
	if (!priv->psy) {
		priv->psy = power_supply_get_by_name("usb");
		if (priv->psy)
			dev_dbg(priv->dev, "Found power supply: usb\n");
	}
	if (!priv->psy) {
		priv->psy = power_supply_get_by_name("dc");
		if (priv->psy)
			dev_dbg(priv->dev, "Found power supply: dc\n");
	}
	if (!priv->psy) {
		priv->psy = power_supply_get_by_name("mains");
		if (priv->psy)
			dev_dbg(priv->dev, "Found power supply: mains\n");
	}

	if (!priv->psy) {
		dev_info(priv->dev, "No power supply found, charge monitoring disabled\n");
		return 0;
	}

	priv->charging = false;
	priv->full = false;
	priv->charge_monitoring = true;

	INIT_DELAYED_WORK(&priv->charge_work, arkos4clone_charge_work);
	schedule_delayed_work(&priv->charge_work, msecs_to_jiffies(500));

	dev_info(priv->dev, "Charge monitoring enabled\n");
	return 0;
}

/**
 * arkos4clone_charge_monitor_exit - 退出充电监控
 * @priv: 私有数据结构指针
 *
 * 停止充电状态轮询工作队列，释放电源供应对象引用。
 */
static void arkos4clone_charge_monitor_exit(struct arkos4clone_led_priv *priv)
{
	if (priv->charge_monitoring) {
		cancel_delayed_work_sync(&priv->charge_work);
	}
	if (priv->psy) {
		power_supply_put(priv->psy);
		priv->psy = NULL;
	}
}

/**
 * status_show - 显示所有 LED 状态
 * @dev: 设备指针
 * @attr: 设备属性指针
 * @buf: 输出缓冲区
 *
 * Sysfs 接口：/sys/devices/platform/arkos4clone-led/status
 *
 * 返回：写入缓冲区的字节数
 */
static ssize_t status_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct arkos4clone_led_priv *priv = dev_get_drvdata(dev);
	int i, count = 0;

	if (priv->charge_monitoring) {
		count += scnprintf(buf + count, PAGE_SIZE - count, "charging: %s\n",
				   priv->full ? "full" : priv->charging ? "yes" : "no");
		count += scnprintf(buf + count, PAGE_SIZE - count, "capacity: %d%%\n",
				   priv->battery_capacity);
	}

	if (priv->has_bicolor) {
		int color = arkos4clone_bicolor_get(priv);
		const char *color_name;
		if (color == 2)
			color_name = "off";
		else if (color == 1)
			color_name = priv->bicolor_high_color;
		else
			color_name = priv->bicolor_low_color;
		count += scnprintf(buf + count, PAGE_SIZE - count, "arkos4clone-led: %d (%s)\n",
				   color, color_name);
	}

	for (i = 0; i < MAX_LEDS; i++) {
		struct arkos4clone_led *led = &priv->leds[i];

		if (led->valid) {
			int state = gpiod_get_value_cansleep(led->gpiod);
			count += scnprintf(buf + count, PAGE_SIZE - count, "%s: %d\n",
					   led_names[i], state);
		}
	}

	return count;
}

static DEVICE_ATTR_RO(status);

/**
 * battery_threshold_show - 显示平台设备电池阈值
 * @dev: 设备指针
 * @attr: 设备属性指针
 * @buf: 输出缓冲区
 *
 * Sysfs 接口：/sys/devices/platform/arkos4clone-led/battery_threshold
 *
 * 返回：写入缓冲区的字节数
 */
static ssize_t battery_threshold_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct arkos4clone_led_priv *priv = dev_get_drvdata(dev);
	int count = 0;

	if (priv->has_bicolor)
		count += scnprintf(buf + count, PAGE_SIZE - count,
				   "arkos4clone-led: %d\n", priv->bicolor_battery_threshold);
	if (priv->leds[LED_RED].valid)
		count += scnprintf(buf + count, PAGE_SIZE - count,
				   "led-red: %d\n", priv->leds[LED_RED].battery_threshold);
	if (priv->leds[LED_BLUE].valid)
		count += scnprintf(buf + count, PAGE_SIZE - count,
				   "led-blue: %d\n", priv->leds[LED_BLUE].battery_threshold);
	if (count == 0)
		count = scnprintf(buf, PAGE_SIZE, "0\n");
	return count;
}

/**
 * battery_threshold_store - 设置平台设备电池阈值
 * @dev: 设备指针
 * @attr: 设备属性指针
 * @buf: 输入缓冲区
 * @count: 输入数据长度
 *
 * Sysfs 接口：/sys/devices/platform/arkos4clone-led/battery_threshold
 * 格式：echo 30 > battery_threshold
 *       echo 0 > battery_threshold
 *
 * 有效值：0=关闭阈值模式，10-90=电量百分比阈值（必须是10的倍数）
 * 设置后立即触发充电 LED 更新。
 *
 * 返回：处理的字节数或负错误码
 */
static ssize_t battery_threshold_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct arkos4clone_led_priv *priv = dev_get_drvdata(dev);
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;

	if (val != 0 && (val % 10 != 0 || val < 10 || val > 90))
		return -EINVAL;

	/* 同时设置所有电源 LED 的阈值 */
	if (priv->has_bicolor)
		priv->bicolor_battery_threshold = val;
	if (priv->leds[LED_RED].valid)
		priv->leds[LED_RED].battery_threshold = val;
	if (priv->leds[LED_BLUE].valid)
		priv->leds[LED_BLUE].battery_threshold = val;

	if (priv->charge_monitoring)
		arkos4clone_update_charge_leds(priv);

	return count;
}
static DEVICE_ATTR_RW(battery_threshold);

/**
 * gpio_store - 设置 LED 状态
 * @dev: 设备指针
 * @attr: 设备属性指针
 * @buf: 输入缓冲区
 * @count: 输入数据长度
 *
 * Sysfs 接口：/sys/devices/platform/arkos4clone-led/gpio
 * 格式：<led名称> <值>
 * 例如：echo "arkos4clone-led 1" > gpio
 *       echo "joy-green 1" > gpio
 *
 * 返回：处理的字节数或负错误码
 */
static ssize_t gpio_store(struct device *dev,
			  struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct arkos4clone_led_priv *priv = dev_get_drvdata(dev);
	char name[32];
	int value, i;

	if (sscanf(buf, "%31s %d", name, &value) != 2)
		return -EINVAL;

	/* 双色 LED 控制 (led-gpio) */
	if (priv->has_bicolor && (!strcmp(name, "arkos4clone-led") || !strcmp(name, "led"))) {
		if (priv->charge_monitoring && (priv->bicolor_battery_threshold > 0 || priv->charging || priv->full)) {
			dev_dbg(dev, "led control blocked\n");
			return -EBUSY;
		}
		if (value == 0 || value == 1)
			arkos4clone_bicolor_set(priv, value);
		return count;
	}

	/* 独立 LED 控制 */
	for (i = 0; i < MAX_LEDS; i++) {
		if (!strcmp(name, led_names[i]) && priv->leds[i].valid) {
			/* 阈值模式或充电/充满时，阻止 led-red 和 led-blue 的控制 */
			if ((i == LED_RED || i == LED_BLUE) &&
			    priv->charge_monitoring && (priv->leds[i].battery_threshold > 0 || priv->charging || priv->full)) {
				dev_dbg(dev, "%s control blocked\n", name);
				return -EBUSY;
			}
			arkos4clone_led_set(&priv->leds[i].cdev,
					    value ? LED_FULL : LED_OFF);
			return count;
		}
	}

	return -ENODEV;
}

/**
 * gpio_show - 显示 LED 控制帮助和当前状态
 * @dev: 设备指针
 * @attr: 设备属性指针
 * @buf: 输出缓冲区
 *
 * Sysfs 接口：/sys/devices/platform/arkos4clone-led/gpio
 *
 * 返回：写入缓冲区的字节数
 */
static ssize_t gpio_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct arkos4clone_led_priv *priv = dev_get_drvdata(dev);
	int i, count = 0;

	if (priv->has_bicolor) {
		int color = arkos4clone_bicolor_get(priv);
		const char *color_name;
		if (color == 2)
			color_name = "off";
		else if (color == 1)
			color_name = priv->bicolor_high_color;
		else
			color_name = priv->bicolor_low_color;
		count += scnprintf(buf + count, PAGE_SIZE - count,
				   "arkos4clone-led: %d (%s)\n"
				   "  0 = %s\n"
				   "  1 = %s\n"
				   "  2 = off (high-Z)\n",
				   color, color_name,
				   priv->bicolor_low_color,
				   priv->bicolor_high_color);
	}

	for (i = 0; i < MAX_LEDS; i++) {
		if (priv->leds[i].valid) {
			int gpio_val = gpiod_get_value_cansleep(priv->leds[i].gpiod);
			count += scnprintf(buf + count, PAGE_SIZE - count, "%s: %d\n",
					   led_names[i], gpio_val);
		}
	}

	if (count == 0)
		count = scnprintf(buf, PAGE_SIZE, "No LEDs configured\n");

	return count;
}

static DEVICE_ATTR_RW(gpio);

/**
 * colors_show - 显示可用颜色
 * @dev: 设备指针
 * @attr: 设备属性指针
 * @buf: 输出缓冲区
 *
 * Sysfs 接口：/sys/devices/platform/arkos4clone-led/colors
 *
 * 返回：写入缓冲区的字节数
 */
static ssize_t colors_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct arkos4clone_led_priv *priv = dev_get_drvdata(dev);
	int count = 0;

	if (priv->has_bicolor) {
		count += scnprintf(buf + count, PAGE_SIZE - count, "arkos4clone-led:\n");
		count += scnprintf(buf + count, PAGE_SIZE - count, "  0: %s\n",
				   priv->bicolor_low_color);
		count += scnprintf(buf + count, PAGE_SIZE - count, "  1: %s\n",
				   priv->bicolor_high_color);
		count += scnprintf(buf + count, PAGE_SIZE - count, "  2: off (high-Z)\n");
	}

	/* 显示独立 LED */
	if (priv->leds[LED_RED].valid)
		count += scnprintf(buf + count, PAGE_SIZE - count, "led-red: 0/1\n");
	if (priv->leds[LED_BLUE].valid)
		count += scnprintf(buf + count, PAGE_SIZE - count, "led-blue: 0/1\n");
	if (priv->leds[LED_GREEN].valid)
		count += scnprintf(buf + count, PAGE_SIZE - count, "led-green: 0/1\n");
	if (priv->leds[LED_JOY_GREEN].valid)
		count += scnprintf(buf + count, PAGE_SIZE - count, "joy-green: 0/1\n");
	if (priv->leds[LED_JOY_RED].valid)
		count += scnprintf(buf + count, PAGE_SIZE - count, "joy-red: 0/1\n");
	if (priv->leds[LED_JOY_BLUE].valid)
		count += scnprintf(buf + count, PAGE_SIZE - count, "joy-blue: 0/1\n");
	if (priv->leds[LED_JOY_LEFT].valid)
		count += scnprintf(buf + count, PAGE_SIZE - count, "joy-left: 0/1\n");
	if (priv->leds[LED_JOY_RIGHT].valid)
		count += scnprintf(buf + count, PAGE_SIZE - count, "joy-right: 0/1\n");
	if (priv->leds[LED_JOY_LED].valid)
		count += scnprintf(buf + count, PAGE_SIZE - count, "joy-led: 0/1\n");

	if (count == 0)
		count = scnprintf(buf, PAGE_SIZE, "No LEDs configured\n");

	return count;
}

static DEVICE_ATTR_RO(colors);

/* ===== LED classdev battery_threshold 属性实现 ===== */

/**
 * bicolor_threshold_show - 显示双色 LED 类设备电池阈值
 * @dev: 设备指针
 * @attr: 设备属性指针
 * @buf: 输出缓冲区
 *
 * Sysfs 接口：/sys/class/leds/arkos4clone-led/battery_threshold
 *
 * 返回：写入缓冲区的字节数
 */
static ssize_t bicolor_threshold_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct arkos4clone_led_priv *priv =
		container_of(dev_get_drvdata(dev), struct arkos4clone_led_priv, bicolor_cdev);
	return scnprintf(buf, PAGE_SIZE, "%d\n", priv->bicolor_battery_threshold);
}

/**
 * bicolor_threshold_store - 设置双色 LED 类设备电池阈值
 * @dev: 设备指针
 * @attr: 设备属性指针
 * @buf: 输入缓冲区
 * @count: 输入数据长度
 *
 * Sysfs 接口：/sys/class/leds/arkos4clone-led/battery_threshold
 * 格式：echo 30 > battery_threshold
 *       echo 0 > battery_threshold
 *
 * 有效值：0=关闭阈值模式，10-90=电量百分比阈值（必须是10的倍数）
 * 与平台设备 battery_threshold 共享同一个值。
 *
 * 返回：处理的字节数或负错误码
 */
static ssize_t bicolor_threshold_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct arkos4clone_led_priv *priv =
		container_of(dev_get_drvdata(dev), struct arkos4clone_led_priv, bicolor_cdev);
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;
	if (val != 0 && (val % 10 != 0 || val < 10 || val > 90))
		return -EINVAL;

	priv->bicolor_battery_threshold = val;
	if (priv->charge_monitoring)
		arkos4clone_update_charge_leds(priv);
	return count;
}

/**
 * ind_threshold_show - 显示独立电源 LED 类设备电池阈值
 * @dev: 设备指针
 * @attr: 设备属性指针
 * @buf: 输出缓冲区
 *
 * Sysfs 接口：/sys/class/leds/led-red/battery_threshold
 *         或：/sys/class/leds/led-blue/battery_threshold
 *
 * 返回：写入缓冲区的字节数
 */
static ssize_t ind_threshold_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct arkos4clone_led *led =
		container_of(dev_get_drvdata(dev), struct arkos4clone_led, cdev);
	return scnprintf(buf, PAGE_SIZE, "%d\n", led->battery_threshold);
}

/**
 * ind_threshold_store - 设置独立电源 LED 类设备电池阈值
 * @dev: 设备指针
 * @attr: 设备属性指针
 * @buf: 输入缓冲区
 * @count: 输入数据长度
 *
 * Sysfs 接口：/sys/class/leds/led-red/battery_threshold
 *         或：/sys/class/leds/led-blue/battery_threshold
 * 格式：echo 30 > battery_threshold
 *       echo 0 > battery_threshold
 *
 * 有效值：0=关闭阈值模式，10-90=电量百分比阈值（必须是10的倍数）
 * 与平台设备 battery_threshold 共享同一个值。
 *
 * 返回：处理的字节数或负错误码
 */
static ssize_t ind_threshold_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct arkos4clone_led *led =
		container_of(dev_get_drvdata(dev), struct arkos4clone_led, cdev);
	struct arkos4clone_led_priv *priv = led->priv;
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;
	if (val != 0 && (val % 10 != 0 || val < 10 || val > 90))
		return -EINVAL;

	led->battery_threshold = val;
	if (priv->charge_monitoring)
		arkos4clone_update_charge_leds(priv);
	return count;
}

/* ===== 脉冲 LED sysfs 属性 ===== */

/* 模式字符串映射 */
static const struct {
	const char *name;
	int mode;
} pulse_mode_map[] = {
	{ "off",		PULSE_MODE_OFF },
	{ "red",		PULSE_MODE_RED },
	{ "red_green",		PULSE_MODE_RED_GREEN },
	{ "green",		PULSE_MODE_GREEN },
	{ "green_blue",		PULSE_MODE_GREEN_BLUE },
	{ "blue",		PULSE_MODE_BLUE },
	{ "blue_red",		PULSE_MODE_BLUE_RED },
	{ "red_green_blue",	PULSE_MODE_RED_GREEN_BLUE },
	{ "breathing",		PULSE_MODE_BREATHING },
	{ "scrolling",		PULSE_MODE_SCROLLING },
};

/**
 * mode_store - 设置脉冲 LED 模式（字符串）
 * @dev: 设备指针
 * @attr: 设备属性指针
 * @buf: 输入缓冲区
 * @count: 输入数据长度
 *
 * Sysfs 接口：/sys/class/leds/joyled/mode
 * 用法: echo red > mode
 *       echo breathing > mode
 *       echo red_green_blue > mode
 *
 * 支持的模式:
 *   off, red, red_green, green, green_blue, blue, blue_red,
 *   red_green_blue, breathing, scrolling
 */
static ssize_t mode_store(struct device *dev,
			  struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct arkos4clone_led_priv *priv;
	char mode_str[32];
	int i;

	if (!led_cdev)
		return -ENODEV;

	priv = container_of(led_cdev, struct arkos4clone_led_priv, pulse_cdev);

	if (!priv->has_pulse_led)
		return -ENODEV;

	/* 复制并去除换行符 */
	if (count >= sizeof(mode_str))
		return -EINVAL;
	strncpy(mode_str, buf, count);
	mode_str[count] = '\0';
	if (mode_str[count - 1] == '\n')
		mode_str[count - 1] = '\0';

	/* 查找模式 */
	for (i = 0; i < ARRAY_SIZE(pulse_mode_map); i++) {
		if (strcasecmp(mode_str, pulse_mode_map[i].name) == 0) {
			int brightness_val;

			dev_dbg(dev, "设置模式: %s (脉冲数: %d)\n",
				pulse_mode_map[i].name, pulse_mode_map[i].mode);
			send_pulse_count(priv->pulse_gpio, pulse_mode_map[i].mode);
			priv->pulse_mode = pulse_mode_map[i].mode;

			/* 同步 cdev->brightness，确保 LED 核心 suspend/resume 一致 */
			switch (pulse_mode_map[i].mode) {
			case PULSE_MODE_OFF:          brightness_val = 0;   break;
			case PULSE_MODE_RED:          brightness_val = 14;  break;
			case PULSE_MODE_RED_GREEN:    brightness_val = 42;  break;
			case PULSE_MODE_GREEN:        brightness_val = 70;  break;
			case PULSE_MODE_GREEN_BLUE:   brightness_val = 98;  break;
			case PULSE_MODE_BLUE:         brightness_val = 126; break;
			case PULSE_MODE_BLUE_RED:     brightness_val = 154; break;
			case PULSE_MODE_RED_GREEN_BLUE: brightness_val = 182; break;
			case PULSE_MODE_BREATHING:    brightness_val = 210; break;
			case PULSE_MODE_SCROLLING:    brightness_val = 240; break;
			default:                      brightness_val = 0;   break;
			}
			led_cdev->brightness = brightness_val;
			return count;
		}
	}

	dev_err(dev, "未知模式: %s\n", mode_str);
	return -EINVAL;
}

/**
 * mode_show - 显示当前模式和可用模式
 * @dev: 设备指针
 * @attr: 设备属性指针
 * @buf: 输出缓冲区
 *
 * Sysfs 接口：/sys/class/leds/joyled/mode
 */
static ssize_t mode_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct arkos4clone_led_priv *priv;
	int i, count = 0;
	const char *current_mode = "unknown";

	if (!led_cdev)
		return scnprintf(buf, PAGE_SIZE, "pulse LED not available\n");

	priv = container_of(led_cdev, struct arkos4clone_led_priv, pulse_cdev);

	if (!priv->has_pulse_led)
		return scnprintf(buf, PAGE_SIZE, "pulse LED not available\n");

	/* 查找当前模式名称 */
	for (i = 0; i < ARRAY_SIZE(pulse_mode_map); i++) {
		if (priv->pulse_mode == pulse_mode_map[i].mode) {
			current_mode = pulse_mode_map[i].name;
			break;
		}
	}

	count += scnprintf(buf + count, PAGE_SIZE - count, "current: %s\n\n", current_mode);
	count += scnprintf(buf + count, PAGE_SIZE - count, "available modes:\n");
	for (i = 0; i < ARRAY_SIZE(pulse_mode_map); i++) {
		count += scnprintf(buf + count, PAGE_SIZE - count, "  %s\n",
				   pulse_mode_map[i].name);
	}

	return count;
}



/* Sysfs 属性数组 */
static struct attribute *arkos4clone_led_attrs[] = {
	&dev_attr_status.attr,
	&dev_attr_gpio.attr,
	&dev_attr_colors.attr,
	&dev_attr_battery_threshold.attr,
	NULL,
};

/* Sysfs 属性组 */
static const struct attribute_group arkos4clone_led_attr_group = {
	.attrs = arkos4clone_led_attrs,
};

/* ===== 电源管理 ===== */

/**
 * arkos4clone_led_suspend - 设备休眠回调
 * @dev: 设备指针
 *
 * 休眠时的 LED 处理策略：
 *
 * 1. 脉冲 LED：
 *    - 停止 2 秒定时刷新（del_timer_sync）
 *    - 发送 OFF 信号关闭 LED
 *
 * 2. 电源灯（bicolor + led-red + led-blue）：
 *    - 充电/充满：走充电逻辑（update_charge_leds）
 *    - 非充电：全部亮（作为待机指示）
 *    - 注意：不修改 cdev->brightness，保留用户原始设置用于 resume 恢复
 *
 * 返回：0=成功
 */
static int arkos4clone_led_suspend(struct device *dev)
{
	struct arkos4clone_led_priv *priv = dev_get_drvdata(dev);

	if (!priv)
		return 0;

	/* 脉冲 LED：停止定时刷新，发送关闭信号 */
	if (priv->has_pulse_led && gpio_is_valid(priv->pulse_gpio)) {
		del_timer_sync(&priv->refresh_timer);
		send_pulse_count(priv->pulse_gpio, PULSE_MODE_OFF);
	}

	/* 电源灯：充电/充满走充电逻辑，非充电全部亮 */
	if (priv->has_bicolor || priv->leds[LED_RED].valid || priv->leds[LED_BLUE].valid) {
		if (priv->charging || priv->full) {
			/* 充电/充满：走充电逻辑 */
			arkos4clone_update_charge_leds(priv);
		} else {
			/* 非充电：全部亮（红蓝同时亮作为待机指示） */
			if (priv->has_bicolor)
				arkos4clone_bicolor_set(priv, 1);
			if (priv->leds[LED_RED].valid)
				arkos4clone_led_set_raw(&priv->leds[LED_RED], 1);
			if (priv->leds[LED_BLUE].valid)
				arkos4clone_led_set_raw(&priv->leds[LED_BLUE], 1);
		}
		/* 注意：不修改 cdev->brightness，保留用户原始设置用于 resume 恢复 */
	}

	return 0;
}

/**
 * arkos4clone_led_resume - 设备唤醒回调
 * @dev: 设备指针
 *
 * 唤醒时的 LED 恢复策略：
 *
 * 1. 脉冲 LED：
 *    - 重新初始化 GPIO 状态（拉低 → 等待 50ms）
 *    - 发送初始化脉冲（11=关闭） → 等待 100ms
 *    - 恢复之前的模式
 *    - 重启 2 秒定时刷新
 *
 * 2. 电源灯（bicolor + led-red + led-blue）：
 *    - 阈值>0 或 充电/充满：走 update_charge_leds
 *    - 非充电 + 阈值=0：根据 cdev->brightness 恢复用户之前手动设置的状态
 *    - 注意：不修改 cdev->brightness，由 sysfs 写操作恢复
 *
 * 返回：0=成功
 */
static int arkos4clone_led_resume(struct device *dev)
{
	struct arkos4clone_led_priv *priv = dev_get_drvdata(dev);
	int i;

	if (!priv)
		return 0;

	/* 恢复脉冲 LED */
	if (priv->has_pulse_led && gpio_is_valid(priv->pulse_gpio)) {
		/* 重新初始化 GPIO 状态 */
		gpio_direction_output(priv->pulse_gpio, 0);
		gpio_set_value(priv->pulse_gpio, 0);
		msleep(50);

		/* 发送初始化脉冲（11=关闭） */
		send_pulse_count(priv->pulse_gpio, 11);
		msleep(100);

		/* 恢复之前的模式 */
		if (priv->pulse_mode > 0 && priv->pulse_mode != PULSE_MODE_OFF)
			send_pulse_count(priv->pulse_gpio, priv->pulse_mode);

		/* 重启定时刷新 */
		mod_timer(&priv->refresh_timer, jiffies + REFRESH_INTERVAL);
	}

	/* 恢复电源灯 */
	if (priv->has_bicolor || priv->leds[LED_RED].valid || priv->leds[LED_BLUE].valid) {
		if (priv->bicolor_battery_threshold > 0 ||
		    priv->leds[LED_RED].battery_threshold > 0 ||
		    priv->leds[LED_BLUE].battery_threshold > 0 ||
		    priv->charging || priv->full) {
			/* 阈值模式或充电/充满：走充电逻辑 */
			arkos4clone_update_charge_leds(priv);

			/* 恢复没有阈值的用户控制 LED（suspend 时被全部点亮）
			 * 仅在非充电+非充满时恢复，充电/充满时充电逻辑优先 */
			if (!priv->charging && !priv->full) {
				if (priv->has_bicolor && priv->bicolor_battery_threshold == 0)
					arkos4clone_bicolor_set(priv, priv->bicolor_cdev.brightness);
				for (i = 0; i < MAX_LEDS; i++) {
					if (priv->leds[i].valid && priv->leds[i].battery_threshold == 0)
						arkos4clone_led_set_raw(&priv->leds[i],
									 priv->leds[i].cdev.brightness);
				}
			}
		} else {
			/* 非充电 + 阈值=0：恢复用户之前手动设置的状态
			 * cdev->brightness 在 suspend 时未被修改，仍保留用户设置 */
			if (priv->has_bicolor)
				arkos4clone_bicolor_set(priv, priv->bicolor_cdev.brightness);
			for (i = 0; i < MAX_LEDS; i++) {
				if (priv->leds[i].valid)
					arkos4clone_led_set_raw(&priv->leds[i],
								 priv->leds[i].cdev.brightness);
			}
		}
	}

	return 0;
}

static SIMPLE_DEV_PM_OPS(arkos4clone_led_pm_ops,
			 arkos4clone_led_suspend,
			 arkos4clone_led_resume);

/**
 * arkos4clone_led_remove - 平台设备移除函数
 * @pdev: 平台设备指针
 *
 * 逆序清理所有资源：
 *   1. 停止充电监控（cancel_delayed_work_sync）
 *   2. 移除 sysfs 属性组
 *   3. 关闭脉冲 LED（del_timer_sync + 发送 OFF 信号）
 *   4. 注销双色 LED class 设备
 *   5. 清理独立 GPIO LED（注销 class 设备 + cancel_work_sync）
 *
 * 返回：0
 */
static int arkos4clone_led_remove(struct platform_device *pdev)
{
	struct arkos4clone_led_priv *priv = platform_get_drvdata(pdev);
	int i;

	arkos4clone_charge_monitor_exit(priv);
	sysfs_remove_group(&pdev->dev.kobj, &arkos4clone_led_attr_group);

	/* 关闭脉冲 LED */
	if (priv->has_pulse_led) {
		del_timer_sync(&priv->refresh_timer);
		if (gpio_is_valid(priv->pulse_gpio))
			send_pulse_count(priv->pulse_gpio, PULSE_MODE_OFF);
	}

	if (priv->has_bicolor)
		led_classdev_unregister(&priv->bicolor_cdev);

	for (i = 0; i < MAX_LEDS; i++)
		arkos4clone_led_cleanup(&priv->leds[i]);

	return 0;
}

/**
 * arkos4clone_led_shutdown - 平台设备关机函数
 * @pdev: 平台设备指针
 *
 * 系统关机时关闭所有 LED：
 *   - 停止脉冲 LED 定时刷新
 *   - 发送 OFF 信号关闭脉冲 LED
 */
static void arkos4clone_led_shutdown(struct platform_device *pdev)
{
	struct arkos4clone_led_priv *priv = platform_get_drvdata(pdev);

	if (priv && priv->has_pulse_led) {
		if (gpio_is_valid(priv->pulse_gpio)) {
			del_timer_sync(&priv->refresh_timer);
			send_pulse_count(priv->pulse_gpio, PULSE_MODE_OFF);
		}
	}
}

/**
 * arkos4clone_led_probe - 平台设备探测函数
 * @pdev: 平台设备指针
 *
 * 设备初始化流程：
 *
 *   1. 分配并初始化驱动私有数据
 *   2. 初始化双色 LED（led-gpio，如果设备树已配置）
 *   3. 初始化脉冲 LED（pulse-gpio，如果设备树已配置且硬件存在）
 *   4. 初始化独立 GPIO LED（led-red, led-blue, joy-*）
 *   5. 创建 sysfs 属性组
 *   6. 启动充电监控工作队列
 *
 * 错误处理策略：
 *   - 双色 LED 和脉冲 LED 初始化失败不会导致 probe 失败
 *   - 独立 LED GPIO 申请失败不影响其他 LED
 *   - 无任何 LED 配置时返回 -ENODEV
 *
 * 返回：成功返回 0，失败返回负错误码
 */
static int arkos4clone_led_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct arkos4clone_led_priv *priv;
	int i, ret, count = 0;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	priv->charge_monitoring = false;
	priv->charging = false;
	priv->full = false;
	priv->battery_capacity = -1;
	priv->has_bicolor = false;
	priv->has_pulse_led = false;
	priv->bicolor_gpio = -EINVAL;
	priv->pulse_gpio = -EINVAL;
	platform_set_drvdata(pdev, priv);

	/* 初始化双色 LED（led-gpio，如果已配置） */
	ret = arkos4clone_bicolor_init(priv);
	if (ret)
		return ret;

	if (priv->has_bicolor)
		count++;

	/* 初始化脉冲 LED（pulse-gpio，如果已配置且硬件存在） */
	ret = arkos4clone_pulse_led_init(priv);
	if (ret) {
		dev_info(dev, "pulse LED init skipped or failed: %d\n", ret);
		/* 不返回错误，继续初始化其他 LED */
	}

	if (priv->has_pulse_led)
		count++;

	/* 初始化独立 LED */
	for (i = 0; i < MAX_LEDS; i++) {
		enum of_gpio_flags flags;
		int gpio;

		gpio = of_get_named_gpio_flags(np, led_names[i], 0, &flags);
		if (gpio_is_valid(gpio)) {
			bool active_low = (flags & OF_GPIO_ACTIVE_LOW) != 0;
			ret = arkos4clone_led_init(dev, &priv->leds[i],
						   led_names[i], gpio, active_low,
						   i, priv);
			if (ret == 0 && priv->leds[i].valid)
				count++;
		}
	}

	if (count == 0) {
		dev_warn(dev, "No LEDs configured\n");
		return -ENODEV;
	}

	/* 创建 Sysfs 接口 */
	ret = sysfs_create_group(&dev->kobj, &arkos4clone_led_attr_group);
	if (ret) {
		dev_err(dev, "Failed to create sysfs group: %d\n", ret);
		if (priv->has_pulse_led)
			del_timer_sync(&priv->refresh_timer);
		if (priv->has_bicolor)
			led_classdev_unregister(&priv->bicolor_cdev);
		for (i = 0; i < MAX_LEDS; i++)
			arkos4clone_led_cleanup(&priv->leds[i]);
		return ret;
	}

	/* 初始化充电监控 */
	arkos4clone_charge_monitor_init(priv);

	dev_info(dev, "Arkos4Clone LED driver loaded (%d LED%s%s%s)\n",
		 count, count > 1 ? "s" : "",
		 priv->has_bicolor ? ", bicolor" : "",
		 priv->has_pulse_led ? ", pulse" : "");
	return 0;
}

/* 设备树匹配表，compatible 属性必须与设备树中的节点一致 */
static const struct of_device_id arkos4clone_led_of_match[] = {
	{ .compatible = "arkos4clone-led", },
	{ },
};
MODULE_DEVICE_TABLE(of, arkos4clone_led_of_match);

/* 平台驱动结构体 */
static struct platform_driver arkos4clone_led_driver = {
	.probe = arkos4clone_led_probe,
	.remove = arkos4clone_led_remove,
	.shutdown = arkos4clone_led_shutdown,
	.driver = {
		.name = "arkos4clone-led",
		.of_match_table = arkos4clone_led_of_match,
		.pm = &arkos4clone_led_pm_ops,
	},
};

module_platform_driver(arkos4clone_led_driver);

MODULE_AUTHOR("lcdyk0517 <lcdyk0517@qq.com>");
MODULE_DESCRIPTION("Arkos4Clone LED 驱动：电源灯(充电指示) + 摇杆灯(三选一互斥)");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:arkos4clone-led");