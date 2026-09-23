// SPDX-License-Identifier: GPL-2.0
/*
 * HID driver for OpenSimHardware OSH PB Controller
 * Presents as odroidgo3-joypad compatible input device with tuning sysfs.
 *
 * The original device is a USB HID gamepad (VID:1209 PID:3100) with
 * analog sticks in 0-4095 range. This driver intercepts the raw HID
 * reports, remaps axes/buttons to match odroidgo3-joypad, and applies
 * runtime-adjustable tuning values, optional per-report axis spike filter,
 * runtime button swap (swap_ab/swap_xy) and PWM rumble.
 */

#include <linux/hid.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/pwm.h>
#include <linux/workqueue.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#define OSHPB_NAME		"GO-Super Gamepad"
#define OSHPB_PHYS		"odroidgo3_joypad/input0"
#define OSHPB_VENDOR		0x484B
#define OSHPB_PRODUCT		0x1100
#define OSHPB_REVISION		0x0100

/* analog stick center and range */
#define OSHPB_AXIS_CENTER	2048
#define OSHPB_AXIS_MAX		4095
#define OSHPB_AXIS_FUZZ		32
#define OSHPB_AXIS_FLAT		32

/* tuning default (percent) */
#define OSHPB_TUNING_DEFAULT	200

/* deadzone in raw ADC units */
#define OSHPB_DEADZONE_DEFAULT	64

#define CLAMP(x, low, high)  (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))

#define OSHPB_MAX_BUTTONS	64

struct oshpb_button {
	int hid_bit;		/* bit position in HID report bytes 1-8 */
	int linux_code;		/* evdev code (BTN_EAST, etc.) */
};

/* per-axis glitch filter state */
struct oshpb_axis {
	int reported;		/* last value passed to the input layer */
	int prev;		/* previous sample, accepted or dropped */
};

struct oshpb_device {
	struct hid_device *hdev;
	struct input_dev *input;

	/* tuning values (percent) */
	int tuning_x_p, tuning_x_n;
	int tuning_y_p, tuning_y_n;
	int tuning_rx_p, tuning_rx_n;
	int tuning_ry_p, tuning_ry_n;

	/* deadzone */
	int deadzone;

	/* scale factor */
	int scale;

	/* per-report axis glitch filter (0 = off) */
	int max_step;
	struct oshpb_axis ax_lx, ax_ly, ax_rx, ax_ry;

	/* button swap: 0 = as-is, 1 = swap */
	int swap_ab;			/* BTN_EAST <-> BTN_SOUTH */
	int swap_xy;			/* BTN_WEST <-> BTN_NORTH */
	struct mutex swap_lock;		/* serializes swap sysfs stores */

	/* debug: log first N reports */
	int debug_count;

	/* button mapping from DTS */
	struct oshpb_button buttons[OSHPB_MAX_BUTTONS];
	int button_count;

	/* stick-switch-key: when held, left stick mirrors to right stick */
	int fn_bit;		/* stick-switch-key HID bit, -1 = disabled */
	int l3_bit;		/* HID bit for L3 (left stick click) */
	int r3_bit;		/* HID bit for R3 (right stick click) */

	/* PWM rumble */
	struct pwm_device *pwm;
	struct work_struct play_work;
	u16 level;
	u16 boost_weak;
	u16 boost_strong;
	bool has_rumble;
};

/* ---------- Rumble helpers ---------- */

static int oshpb_pwm_start(struct oshpb_device *oshpb)
{
	struct pwm_state state;

	pwm_get_state(oshpb->pwm, &state);
	pwm_set_relative_duty_cycle(&state, oshpb->level, 0xffff);
	state.enabled = true;

	return pwm_apply_state(oshpb->pwm, &state);
}

static void oshpb_pwm_stop(struct oshpb_device *oshpb)
{
	pwm_disable(oshpb->pwm);
}

static void oshpb_play_work(struct work_struct *work)
{
	struct oshpb_device *oshpb = container_of(work,
					struct oshpb_device, play_work);

	if (oshpb->level)
		oshpb_pwm_start(oshpb);
	else
		oshpb_pwm_stop(oshpb);
}

static int oshpb_rumble_play(struct input_dev *dev, void *data,
			     struct ff_effect *effect)
{
	struct oshpb_device *oshpb = data;
	u32 boosted_level;

	if (effect->type != FF_RUMBLE)
		return 0;

	if (effect->u.rumble.strong_magnitude)
		boosted_level = effect->u.rumble.strong_magnitude + oshpb->boost_strong;
	else
		boosted_level = effect->u.rumble.weak_magnitude + oshpb->boost_weak;

	oshpb->level = (u16)CLAMP(boosted_level, 0, 0xffff);
	schedule_work(&oshpb->play_work);
	return 0;
}

static int oshpb_rumble_setup(struct hid_device *hdev, struct oshpb_device *oshpb,
			      struct device_node *joypad_np)
{
	struct pwm_state state;
	u32 boost_weak = 0, boost_strong = 0;
	int error;

	oshpb->pwm = of_pwm_get(joypad_np, "enable");
	if (IS_ERR(oshpb->pwm)) {
		hid_dbg(hdev, "PWM get failed: %ld\n", PTR_ERR(oshpb->pwm));
		oshpb->pwm = NULL;
		return PTR_ERR(oshpb->pwm);
	}

	INIT_WORK(&oshpb->play_work, oshpb_play_work);

	pwm_init_state(oshpb->pwm, &state);
	state.enabled = false;
	error = pwm_apply_state(oshpb->pwm, &state);
	if (error) {
		hid_err(hdev, "failed to apply initial PWM state: %d\n", error);
		return error;
	}

	hid_info(hdev, "rumble via PWM: period=%u\n", pwm_get_period(oshpb->pwm));

	/* read boost values from joypad node */
	of_property_read_u32(joypad_np, "rumble-boost-weak", &boost_weak);
	of_property_read_u32(joypad_np, "rumble-boost-strong", &boost_strong);
	oshpb->boost_weak = boost_weak;
	oshpb->boost_strong = boost_strong;

	return 0;
}

/* ---- Button swap and slew limit helpers ---- */

/*
 * swap_ab: A<->B (BTN_EAST <-> BTN_SOUTH), swap_xy: X<->Y (BTN_WEST <-> BTN_NORTH).
 * Unrelated codes pass through unchanged. Used for report remapping and
 * capability registration.
 */
static int oshpb_swap_ab_code(int code)
{
	switch (code) {
	case BTN_EAST:	return BTN_SOUTH;
	case BTN_SOUTH:	return BTN_EAST;
	default:	return code;
	}
}

static int oshpb_swap_xy_code(int code)
{
	switch (code) {
	case BTN_WEST:	return BTN_NORTH;
	case BTN_NORTH:	return BTN_WEST;
	default:	return code;
	}
}

/*
 * oshpb_remap_button() - remap a key code per the current swap settings.
 *
 * raw_event reads the flags lock-free, paired with WRITE_ONCE in the
 * sysfs store path.
 */
static int oshpb_remap_button(struct oshpb_device *oshpb, int code)
{
	if (READ_ONCE(oshpb->swap_ab))
		code = oshpb_swap_ab_code(code);
	if (READ_ONCE(oshpb->swap_xy))
		code = oshpb_swap_xy_code(code);
	return code;
}

/**
 * oshpb_axis_filter() - reject single-sample spikes larger than max_step.
 * @axis: per-axis filter state
 * @sample: new raw sample (output value units)
 * @max_step: maximum plausible change per report
 *
 * A sample deviating more than max_step from BOTH the last reported value
 * and the previous sample is an isolated glitch and is dropped: the last
 * reported value is held instead. A sample that stays far away on the next
 * report too (i.e. deviates from the previous sample by at most max_step)
 * is real stick movement and is accepted at once, so the stick can still
 * travel its full range at any speed.
 *
 * Return: the value to report this time.
 */
static int oshpb_axis_filter(struct oshpb_axis *axis, int sample, int max_step)
{
	int delta_rep = abs(sample - axis->reported);
	int delta_prev = abs(sample - axis->prev);

	axis->prev = sample;

	if (delta_rep > max_step && delta_prev > max_step)
		return axis->reported;	/* spike: drop, hold last value */

	axis->reported = sample;
	return sample;
}

/* ---- HID raw event handler ---- */

/*
 * Report ID 1 — Gamepad (from HID report descriptor):
 *
 *   Byte     0: Report ID (0x01)
 *   Bytes  1-8: 64 buttons (8 bytes, bit field)
 *   Bytes  9-10: Rx     (16-bit LE, 0-4095)
 *   Bytes 11-12: Ry     (16-bit LE, 0-4095)
 *   Bytes 13-14: Rz     (16-bit LE, 0-4095)
 *   Bytes 15-16: Slider (16-bit LE, 0-4095)
 *   Bytes 17-18: Slider (16-bit LE, 0-4095)
 *   Bytes 19-20: X      (16-bit LE, 0-4095)
 *   Byte     21: Hat switch 1 (0-7, 8=center)
 *   Bytes 22-24: Hat switches 2-4
 *
 * Physical stick mapping (confirmed via raw capture):
 *   X (19-20) = Left stick X
 *   Rx( 9-10) = Left stick Y
 *   Ry(11-12) = Right stick X
 *   Rz(13-14) = Right stick Y
 */

#define OSHPB_REPORT_ID		0x01
#define OSHPB_REPORT_SIZE	25

static int oshpb_raw_event(struct hid_device *hdev, struct hid_report *report,
			   u8 *data, int size)
{
	struct oshpb_device *oshpb = hid_get_drvdata(hdev);
	struct input_dev *input = oshpb->input;
	int lx, ly, rx, ry, hat, i;
	u64 btns;

	if (size < OSHPB_REPORT_SIZE)
		return 0;

	/* skip non-gamepad reports */
	if (data[0] != OSHPB_REPORT_ID)
		return 0;

	/*
	 * Axes: 16-bit LE, range 0-4095, center 2048.
	 * Convert to signed: val - 2048, range -2048..+2047
	 */
	lx = (data[19] | (data[20] << 8)) - OSHPB_AXIS_CENTER;	/* X   = left stick X  */
	ly = (data[9]  | (data[10] << 8)) - OSHPB_AXIS_CENTER;	/* Rx  = left stick Y  */
	rx = (data[11] | (data[12] << 8)) - OSHPB_AXIS_CENTER;	/* Ry  = right stick X */
	ry = (data[13] | (data[14] << 8)) - OSHPB_AXIS_CENTER;	/* Rz  = right stick Y */

	/* invert left stick (matches odroidgo3-joypad invert-absx/invert-absy) */
	lx = -lx;
	ly = -ly;

	/* debug: log first 10 reports (enable via: echo 'file hid-oshpb.c +p' > /sys/kernel/debug/dynamic_debug/control) */
	if (oshpb->debug_count < 10) {
		oshpb->debug_count++;
		dev_dbg(&hdev->dev,
			"raw[%d]: lx=%d ly=%d rx=%d ry=%d btn=%02x%02x%02x%02x%02x%02x%02x%02x hat=%d\n",
			oshpb->debug_count, lx + OSHPB_AXIS_CENTER, ly + OSHPB_AXIS_CENTER,
			rx + OSHPB_AXIS_CENTER, ry + OSHPB_AXIS_CENTER,
			data[1], data[2], data[3], data[4], data[5], data[6], data[7], data[8],
			data[21]);
	}

	/* apply deadzone */
	if (oshpb->deadzone) {
		if (abs(lx) < oshpb->deadzone) lx = 0;
		if (abs(ly) < oshpb->deadzone) ly = 0;
		if (abs(rx) < oshpb->deadzone) rx = 0;
		if (abs(ry) < oshpb->deadzone) ry = 0;
	}

	/* apply tuning */
	if (lx > 0 && oshpb->tuning_x_p)
		lx = (lx * oshpb->tuning_x_p) / 100;
	else if (lx < 0 && oshpb->tuning_x_n)
		lx = (lx * oshpb->tuning_x_n) / 100;

	if (ly > 0 && oshpb->tuning_y_p)
		ly = (ly * oshpb->tuning_y_p) / 100;
	else if (ly < 0 && oshpb->tuning_y_n)
		ly = (ly * oshpb->tuning_y_n) / 100;

	if (rx > 0 && oshpb->tuning_rx_p)
		rx = (rx * oshpb->tuning_rx_p) / 100;
	else if (rx < 0 && oshpb->tuning_rx_n)
		rx = (rx * oshpb->tuning_rx_n) / 100;

	if (ry > 0 && oshpb->tuning_ry_p)
		ry = (ry * oshpb->tuning_ry_p) / 100;
	else if (ry < 0 && oshpb->tuning_ry_n)
		ry = (ry * oshpb->tuning_ry_n) / 100;

	/* apply scale */
	if (oshpb->scale) {
		lx *= oshpb->scale;
		ly *= oshpb->scale;
		rx *= oshpb->scale;
		ry *= oshpb->scale;
	}

	/*
	 * Spike filter (optional, DTS: button-adc-max-step, output value
	 * units). Drops single samples jumping more than max_step away from
	 * both the last reported value and the previous sample — typical ADC
	 * noise glitches larger than half travel — and holds the last value
	 * instead. Sustained movement is accepted on the following report.
	 * Absent or 0 = no filtering. Applied before stick-switch emulation,
	 * so it tracks the physical axes.
	 */
	if (oshpb->max_step) {
		lx = oshpb_axis_filter(&oshpb->ax_lx, lx, oshpb->max_step);
		ly = oshpb_axis_filter(&oshpb->ax_ly, ly, oshpb->max_step);
		rx = oshpb_axis_filter(&oshpb->ax_rx, rx, oshpb->max_step);
		ry = oshpb_axis_filter(&oshpb->ax_ry, ry, oshpb->max_step);
	}

	/* buttons: 64 bits in bytes 1-8, mapped via DTS */
	btns = (u64)data[1]       | ((u64)data[2] << 8)  |
	       ((u64)data[3] << 16) | ((u64)data[4] << 24) |
	       ((u64)data[5] << 32) | ((u64)data[6] << 40) |
	       ((u64)data[7] << 48) | ((u64)data[8] << 56);

	/*
	 * stick-switch-key + left stick → right stick emulation
	 * When the switch key is held: duplicate left stick to right stick,
	 * and L3 (left stick click) acts as R3.
	 */
	if (oshpb->fn_bit >= 0 && (btns & BIT_ULL(oshpb->fn_bit))) {
		/* stick-switch held: left stick → right stick, left stick = 0 */
		rx = lx;
		ry = ly;
		lx = 0;
		ly = 0;
		/* L3 → R3 (suppress L3) */
		if (oshpb->l3_bit >= 0 && oshpb->r3_bit >= 0) {
			if (btns & BIT_ULL(oshpb->l3_bit)) {
				btns |= BIT_ULL(oshpb->r3_bit);
				btns &= ~BIT_ULL(oshpb->l3_bit);
			} else {
				btns &= ~BIT_ULL(oshpb->r3_bit);
			}
		}
	}

	/* left stick → ABS_X, ABS_Y */
	input_report_abs(input, ABS_X, lx);
	input_report_abs(input, ABS_Y, ly);

	/* right stick → ABS_RX, ABS_RY */
	input_report_abs(input, ABS_RX, rx);
	input_report_abs(input, ABS_RY, ry);

	/* hat → D-pad buttons (byte 21, 0-7=direction, 8=center) */
	hat = data[21];
	input_report_key(input, BTN_DPAD_UP,    hat == 0 || hat == 1 || hat == 7);
	input_report_key(input, BTN_DPAD_RIGHT, hat == 1 || hat == 2 || hat == 3);
	input_report_key(input, BTN_DPAD_DOWN,  hat == 3 || hat == 4 || hat == 5);
	input_report_key(input, BTN_DPAD_LEFT,  hat == 5 || hat == 6 || hat == 7);

	for (i = 0; i < oshpb->button_count; i++) {
		int bit = oshpb->buttons[i].hid_bit;

		/* swap_ab/swap_xy remap */
		input_report_key(input,
				 oshpb_remap_button(oshpb, oshpb->buttons[i].linux_code),
				 btns & BIT_ULL(bit));
	}

	input_sync(input);
	return 0;
}

/* ---- Button swap sysfs (swap_ab / swap_xy) ---- */

/*
 * Attributes on the HID device:
 *   /sys/bus/hid/devices/<id>/swap_ab [rw]
 *   /sys/bus/hid/devices/<id>/swap_xy [rw]
 *
 * Button swap: 0 = report as-is (default), 1 = swap.
 *   swap_ab: A<->B (BTN_EAST <-> BTN_SOUTH)
 *   swap_xy: X<->Y (BTN_WEST <-> BTN_NORTH)
 * Only 0/1 accepted, other input returns -EINVAL.
 */
static ssize_t oshpb_show_swap_ab(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct oshpb_device *oshpb = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", oshpb->swap_ab);
}

static ssize_t oshpb_show_swap_xy(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct oshpb_device *oshpb = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", oshpb->swap_xy);
}

/*
 * On any swap change force-release the four face buttons so a held key
 * cannot stay pressed under its old code. Returns 0 on success, negative
 * error code on failure.
 */
static int oshpb_store_swap(struct oshpb_device *oshpb, int *field,
			    const char *buf)
{
	unsigned int enable;
	int error;

	error = kstrtouint(buf, 10, &enable);
	if (error)
		return error;
	if (enable > 1)
		return -EINVAL;

	mutex_lock(&oshpb->swap_lock);
	if (enable != READ_ONCE(*field)) {
		WRITE_ONCE(*field, enable);
		if (oshpb->input) {
			input_report_key(oshpb->input, BTN_EAST, 0);
			input_report_key(oshpb->input, BTN_SOUTH, 0);
			input_report_key(oshpb->input, BTN_WEST, 0);
			input_report_key(oshpb->input, BTN_NORTH, 0);
			input_sync(oshpb->input);
		}
	}
	mutex_unlock(&oshpb->swap_lock);

	return 0;
}

static ssize_t oshpb_store_swap_ab(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct oshpb_device *oshpb = dev_get_drvdata(dev);
	int error;

	error = oshpb_store_swap(oshpb, &oshpb->swap_ab, buf);
	return error ? error : count;
}

static ssize_t oshpb_store_swap_xy(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct oshpb_device *oshpb = dev_get_drvdata(dev);
	int error;

	error = oshpb_store_swap(oshpb, &oshpb->swap_xy, buf);
	return error ? error : count;
}

static DEVICE_ATTR(swap_ab, S_IWUSR | S_IRUGO,
		   oshpb_show_swap_ab, oshpb_store_swap_ab);
static DEVICE_ATTR(swap_xy, S_IWUSR | S_IRUGO,
		   oshpb_show_swap_xy, oshpb_store_swap_xy);

static struct attribute *oshpb_attrs[] = {
	&dev_attr_swap_ab.attr,
	&dev_attr_swap_xy.attr,
	NULL,
};

static const struct attribute_group oshpb_attr_group = {
	.attrs = oshpb_attrs,
};

/* ---- probe / remove ---- */

static int oshpb_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct oshpb_device *oshpb;
	struct input_dev *input;
	struct device_node *joypad_np;
	struct device_node *child;
	u32 val, fuzz, flat;
	int error, idx;
	unsigned int connect_mask;

	joypad_np = of_find_compatible_node(NULL, NULL, "odroidgo3-hid-joypad");

	oshpb = devm_kzalloc(&hdev->dev, sizeof(*oshpb), GFP_KERNEL);
	if (!oshpb) {
		of_node_put(joypad_np);
		return -ENOMEM;
	}

	oshpb->hdev = hdev;
	hid_set_drvdata(hdev, oshpb);
	mutex_init(&oshpb->swap_lock);

	error = hid_parse(hdev);
	if (error) {
		hid_err(hdev, "parse failed: %d\n", error);
		of_node_put(joypad_np);
		return error;
	}

	/*
	 * Connect HID but NOT the input layer — we create our own input device
	 * so we can remap axes/buttons and apply tuning.
	 */
	connect_mask = HID_CONNECT_DEFAULT & ~HID_CONNECT_HIDINPUT;
	error = hid_hw_start(hdev, connect_mask);
	if (error) {
		hid_err(hdev, "hw start failed: %d\n", error);
		of_node_put(joypad_np);
		return error;
	}

	error = hid_hw_open(hdev);
	if (error) {
		hid_err(hdev, "hw open failed: %d\n", error);
		of_node_put(joypad_np);
		hid_hw_stop(hdev);
		return error;
	}

	/* button swap sysfs (swap_ab / swap_xy) */
	error = sysfs_create_group(&hdev->dev.kobj, &oshpb_attr_group);
	if (error) {
		hid_err(hdev, "sysfs group create failed: %d\n", error);
		of_node_put(joypad_np);
		hid_hw_close(hdev);
		hid_hw_stop(hdev);
		return error;
	}

	/* create input device matching odroidgo3-joypad identity */
	input = devm_input_allocate_device(&hdev->dev);
	if (!input) {
		error = -ENOMEM;
		goto err_close;
	}

	oshpb->input = input;
	input->name = OSHPB_NAME;
	input->phys = OSHPB_PHYS;
	input->id.bustype = BUS_HOST;
	input->id.vendor  = OSHPB_VENDOR;
	input->id.product = OSHPB_PRODUCT;
	input->id.version = OSHPB_REVISION;
	input->dev.parent = &hdev->dev;

	/* axes — fuzz/flat from DTS, or defaults */
	fuzz = OSHPB_AXIS_FUZZ;
	flat = OSHPB_AXIS_FLAT;
	if (joypad_np) {
		of_property_read_u32(joypad_np, "button-adc-fuzz", &fuzz);
		of_property_read_u32(joypad_np, "button-adc-flat", &flat);
	}
	input_set_abs_params(input, ABS_X,
		-OSHPB_AXIS_CENTER * 3, OSHPB_AXIS_CENTER * 3, fuzz, flat);
	input_set_abs_params(input, ABS_Y,
		-OSHPB_AXIS_CENTER * 3, OSHPB_AXIS_CENTER * 3, fuzz, flat);
	input_set_abs_params(input, ABS_RX,
		-OSHPB_AXIS_CENTER * 3, OSHPB_AXIS_CENTER * 3, fuzz, flat);
	input_set_abs_params(input, ABS_RY,
		-OSHPB_AXIS_CENTER * 3, OSHPB_AXIS_CENTER * 3, fuzz, flat);

	/* d-pad */
	input_set_capability(input, EV_KEY, BTN_DPAD_UP);
	input_set_capability(input, EV_KEY, BTN_DPAD_DOWN);
	input_set_capability(input, EV_KEY, BTN_DPAD_LEFT);
	input_set_capability(input, EV_KEY, BTN_DPAD_RIGHT);

	/* parse button mapping from DTS */
	oshpb->fn_bit = -1;
	oshpb->l3_bit = -1;
	oshpb->r3_bit = -1;
	idx = 0;
	if (joypad_np) {
		for_each_child_of_node(joypad_np, child) {
			u32 hid_bit, linux_code, swapped;

			if (of_property_read_u32(child, "hid-bit", &hid_bit))
				continue;
			if (of_property_read_u32(child, "linux,code", &linux_code))
				continue;
			if (idx >= OSHPB_MAX_BUTTONS)
				break;

			oshpb->buttons[idx].hid_bit = hid_bit;
			oshpb->buttons[idx].linux_code = linux_code;
			input_set_capability(input, EV_KEY, linux_code);
			/* swapped codes too (swap_ab/swap_xy), else the input
			 * core drops swapped reports */
			swapped = oshpb_swap_ab_code(linux_code);
			if (swapped != linux_code)
				input_set_capability(input, EV_KEY, swapped);
			swapped = oshpb_swap_xy_code(linux_code);
			if (swapped != linux_code)
				input_set_capability(input, EV_KEY, swapped);

			/* track L3/R3 for stick-switch emulation */
			if (linux_code == BTN_TRIGGER_HAPPY3)
				oshpb->l3_bit = hid_bit;
			else if (linux_code == BTN_TRIGGER_HAPPY4)
				oshpb->r3_bit = hid_bit;

			idx++;
		}
	}
	oshpb->button_count = idx;

	/* stick-switch-key: when held, left stick mirrors to right stick.
	 * Only works when skip-absr is also set (safety measure). */
	if (joypad_np && of_property_read_bool(joypad_np, "skip-absr")) {
		u32 switch_code;

		if (!of_property_read_u32(joypad_np, "stick-switch-key", &switch_code)) {
			for (idx = 0; idx < oshpb->button_count; idx++) {
				if (oshpb->buttons[idx].linux_code == switch_code) {
					oshpb->fn_bit = oshpb->buttons[idx].hid_bit;
					break;
				}
			}
			if (oshpb->fn_bit < 0)
				hid_warn(hdev, "stick-switch-key code %u not found in button mapping\n",
					 switch_code);
		}
	}

	/* rumble setup */
	error = oshpb_rumble_setup(hdev, oshpb, joypad_np);
	if (error) {
		hid_info(hdev, "rumble not available, continuing without\n");
		oshpb->has_rumble = false;
	} else {
		oshpb->has_rumble = true;
		input_set_capability(input, EV_FF, FF_RUMBLE);
		error = input_ff_create_memless(input, oshpb,
						oshpb_rumble_play);
		if (error) {
			hid_err(hdev, "FF create failed: %d\n", error);
			goto err_close;
		}
	}

	error = input_register_device(input);
	if (error) {
		hid_err(hdev, "input register failed: %d\n", error);
		goto err_close;
	}

	/* read tuning values from DTS, or use defaults */
	oshpb->tuning_x_p  = OSHPB_TUNING_DEFAULT;
	oshpb->tuning_x_n  = OSHPB_TUNING_DEFAULT;
	oshpb->tuning_y_p  = OSHPB_TUNING_DEFAULT;
	oshpb->tuning_y_n  = OSHPB_TUNING_DEFAULT;
	oshpb->tuning_rx_p = OSHPB_TUNING_DEFAULT;
	oshpb->tuning_rx_n = OSHPB_TUNING_DEFAULT;
	oshpb->tuning_ry_p = OSHPB_TUNING_DEFAULT;
	oshpb->tuning_ry_n = OSHPB_TUNING_DEFAULT;
	oshpb->deadzone    = OSHPB_DEADZONE_DEFAULT;
	oshpb->scale       = 2;

	if (joypad_np) {
		if (!of_property_read_u32(joypad_np, "abs_x-p-tuning", &val))
			oshpb->tuning_x_p = val;
		if (!of_property_read_u32(joypad_np, "abs_x-n-tuning", &val))
			oshpb->tuning_x_n = val;
		if (!of_property_read_u32(joypad_np, "abs_y-p-tuning", &val))
			oshpb->tuning_y_p = val;
		if (!of_property_read_u32(joypad_np, "abs_y-n-tuning", &val))
			oshpb->tuning_y_n = val;
		if (!of_property_read_u32(joypad_np, "abs_rx-p-tuning", &val))
			oshpb->tuning_rx_p = val;
		if (!of_property_read_u32(joypad_np, "abs_rx-n-tuning", &val))
			oshpb->tuning_rx_n = val;
		if (!of_property_read_u32(joypad_np, "abs_ry-p-tuning", &val))
			oshpb->tuning_ry_p = val;
		if (!of_property_read_u32(joypad_np, "abs_ry-n-tuning", &val))
			oshpb->tuning_ry_n = val;
		if (!of_property_read_u32(joypad_np, "button-adc-deadzone", &val))
			oshpb->deadzone = val;
		if (!of_property_read_u32(joypad_np, "button-adc-scale", &val))
			oshpb->scale = val;
		/*
		 * Spike filter threshold (optional): single samples jumping
		 * more than this many output units from both the last reported
		 * value and the previous sample are dropped as noise.
		 * Absent = 0 = no filtering.
		 */
		if (!of_property_read_u32(joypad_np, "button-adc-max-step", &val))
			oshpb->max_step = val;
	}

	of_node_put(joypad_np);
	joypad_np = NULL;

	/* startup rumble: vibrate 1 second on probe */
	if (oshpb->has_rumble) {
		oshpb->level = 0xFFFF;
		oshpb_pwm_start(oshpb);
		msleep(1000);
		oshpb_pwm_stop(oshpb);
		oshpb->level = 0;
	}

	hid_info(hdev, "OSH PB Controller registered as '%s'\n", OSHPB_NAME);
	return 0;

err_close:
	of_node_put(joypad_np);
	sysfs_remove_group(&hdev->dev.kobj, &oshpb_attr_group);
	hid_hw_close(hdev);
	hid_hw_stop(hdev);
	return error;
}

static void oshpb_remove(struct hid_device *hdev)
{
	struct oshpb_device *oshpb = hid_get_drvdata(hdev);

	sysfs_remove_group(&hdev->dev.kobj, &oshpb_attr_group);
	cancel_work_sync(&oshpb->play_work);
	hid_hw_close(hdev);
	hid_hw_stop(hdev);
}

/* ---- HID device table ---- */

static const struct hid_device_id oshpb_devices[] = {
	{ HID_USB_DEVICE(0x1209, 0x3100) },
	{ }
};
MODULE_DEVICE_TABLE(hid, oshpb_devices);

static struct hid_driver oshpb_driver = {
	.name		= "oshpb-controller",
	.id_table	= oshpb_devices,
	.probe		= oshpb_probe,
	.remove		= oshpb_remove,
	.raw_event	= oshpb_raw_event,
};
module_hid_driver(oshpb_driver);

MODULE_AUTHOR("lcdyk");
MODULE_DESCRIPTION("OpenSimHardware OSH PB Controller with odroidgo3-joypad compatible interface");
MODULE_LICENSE("GPL");
