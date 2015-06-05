/* linux/drivers/video/samsung/mdnie.c
 *
 * Register interface file for Samsung mDNIe driver
 *
 * Copyright (c) 2009 Samsung Electronics
 * http://www.samsungsemi.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <mach/gpio.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/mm.h>
#include <linux/device.h>
#include <linux/backlight.h>
#include <linux/platform_device.h>
#include <linux/mdnie.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <linux/delay.h>
#include <linux/lcd.h>
#include <linux/rtc.h>

#include "s3cfb.h"
#include "s3cfb_mdnie.h"

#if defined(CONFIG_CPU_EXYNOS4210)
#if defined(CONFIG_FB_S5P_LD9040) || defined(CONFIG_FB_S5P_NT35560)
#include "mdnie_table_u1.h"
#elif defined(CONFIG_FB_S5P_S6E8AA0)
#include "mdnie_table_q1.h"
#elif defined(CONFIG_FB_S5P_S6E8AB0)
#include "mdnie_table_p8.h"
#elif defined(CONFIG_FB_S5P_S6F1202A)
#include "mdnie_table.h"
#include "mdnie_table_p2_boe.h"
#include "mdnie_table_p2_hydis.h"
#include "mdnie_table_p2_sec.h"
#elif defined(CONFIG_FB_S5P_S6C1372)
#include "mdnie_table_p4.h"
#endif
#include "mdnie_color_tone_4210.h"
#else	/* CONFIG_CPU_EXYNOS4210 */
#if defined(CONFIG_FB_S5P_S6E8AA0)
#include "mdnie_table_c1m0.h"
#elif defined(CONFIG_FB_S5P_EA8061) || defined(CONFIG_FB_S5P_S6EVR02)
#include "mdnie_table_t0.h"
#elif defined(CONFIG_FB_S5P_S6E63M0)
#include "mdnie_table_c1m0.h"
#elif defined(CONFIG_FB_S5P_S6C1372)
#include "mdnie_table_p4note.h"
#elif defined(CONFIG_FB_S5P_S6D6AA1)
#include "mdnie_table_gc1.h"
#elif defined(CONFIG_FB_S5P_LMS501XX)
#include "mdnie_table_baffin.h"
#else
#include "mdnie_table_4412.h"
#endif
#include "mdnie_color_tone.h"	/* sholud be added for 4212, 4412 */
#endif

#if defined(CONFIG_TDMB) || defined(CONFIG_TARGET_LOCALE_NTT)
#include "mdnie_dmb.h"
#endif

#if defined(CONFIG_FB_MDNIE_PWM)
#define MIN_BRIGHTNESS		0
#define DEFAULT_BRIGHTNESS		150
#if defined(CONFIG_FB_S5P_S6F1202A)
#define MAX_BACKLIGHT_VALUE		1424	/* 1504(94%) -> 1424(89%) */
#define MID_BACKLIGHT_VALUE		544	/*  784(49%) ->  544(34%) */
#define LOW_BACKLIGHT_VALUE		16
#define DIM_BACKLIGHT_VALUE		16
#define CABC_CUTOFF_BACKLIGHT_VALUE	40	/* 2.5% */
#elif defined(CONFIG_FB_S5P_S6C1372)
#define MAX_BACKLIGHT_VALUE		1441	/* 90% */
#define MID_BACKLIGHT_VALUE		784
#define LOW_BACKLIGHT_VALUE		16
#define DIM_BACKLIGHT_VALUE		16
#define CABC_CUTOFF_BACKLIGHT_VALUE	34
#endif
#define MAX_BRIGHTNESS_LEVEL		255
#define MID_BRIGHTNESS_LEVEL		150
#define LOW_BRIGHTNESS_LEVEL		30
#define DIM_BRIGHTNESS_LEVEL		20
#endif

#define SCENARIO_IS_COLOR(scenario)	\
	((scenario >= COLOR_TONE_1) && (scenario < COLOR_TONE_MAX))

#if defined(CONFIG_TDMB) || defined(CONFIG_TARGET_LOCALE_NTT)
#define SCENARIO_IS_DMB(scenario)	\
	((scenario >= DMB_NORMAL_MODE) && (scenario < DMB_MODE_MAX))
#define SCENARIO_IS_VALID(scenario)	\
	((SCENARIO_IS_COLOR(scenario)) || SCENARIO_IS_DMB(scenario) || \
	(scenario < SCENARIO_MAX))
#else
#define SCENARIO_IS_VALID(scenario)	\
	((SCENARIO_IS_COLOR(scenario)) || (scenario < SCENARIO_MAX))
#endif

#ifdef CONFIG_FB_S5P_MDNIE_HIJACK
// Yank555.lu : Hijack variables
int hijack = HIJACK_DISABLED;
int black = 0;
int black_r = 0;
int black_g = 0;
int black_b = 0;
#endif

static char tuning_file_name[50];

struct class *mdnie_class;

struct mdnie_info *g_mdnie;

#ifdef CONFIG_MACH_P4NOTE
static struct mdnie_backlight_value b_value;
#endif

struct sysfs_debug_info {
	u8 enable;
	pid_t pid;
	char comm[TASK_COMM_LEN];
	char time[128];
};

static struct sysfs_debug_info negative[5];
static u8 negative_idx;

int mdnie_send_sequence(struct mdnie_info *mdnie, const unsigned short *seq)
{
	int ret = 0, i = 0;
	const unsigned short *wbuf;

	if (IS_ERR_OR_NULL(seq)) {
		dev_err(mdnie->dev, "mdnie sequence is null\n");
		return -EPERM;
	}

	mutex_lock(&mdnie->dev_lock);
#ifdef CONFIG_FB_S5P_MDNIE_HIJACK
	// Yank555.lu : use hijack profile if hijack is enabled
	if (hijack == HIJACK_ENABLED)
		wbuf = &tune_hijack;
	else
#endif

	wbuf = seq;

	s3c_mdnie_mask();

	while (wbuf[i] != END_SEQ) {
		mdnie_write(wbuf[i], wbuf[i+1]);
		i += 2;
	}

	s3c_mdnie_unmask();

	mutex_unlock(&mdnie->dev_lock);

	return ret;
}

void set_mdnie_value(struct mdnie_info *mdnie, u8 force)
{
	u8 idx;

	if ((!mdnie->enable) && (!force)) {
		dev_err(mdnie->dev, "mdnie states is off\n");
		return;
	}

	if (mdnie->scenario == VIDEO_WARM_MODE)
		mdnie->tone = TONE_WARM;
	else if (mdnie->scenario == VIDEO_COLD_MODE)
		mdnie->tone = TONE_COLD;
	else
		mdnie->tone = TONE_NORMAL;

	if (mdnie->tunning) {
		dev_info(mdnie->dev, "mdnie tunning mode is enabled\n");
		return;
	}

	mutex_lock(&mdnie->lock);

	if (mdnie->negative == NEGATIVE_ON) {
		dev_info(mdnie->dev, "NEGATIVE_ON\n");
		mdnie_send_sequence(mdnie, negative_table[mdnie->cabc].seq);
		goto exit;
	}

#if defined(CONFIG_TDMB) || defined(CONFIG_TARGET_LOCALE_NTT)
	if (SCENARIO_IS_DMB(mdnie->scenario)) {
		idx = mdnie->scenario - DMB_NORMAL_MODE;
		mdnie->tone = idx;

		mdnie_send_sequence(mdnie, tune_dmb[mdnie->mode].seq);
		dev_info(mdnie->dev, "mode=%d, scenario=%d, outdoor=%d, cabc=%d, %s\n",
			mdnie->mode, mdnie->scenario, mdnie->outdoor,
			mdnie->cabc, tune_dmb[mdnie->mode].name);
		goto etc;
	}
#endif

	if (SCENARIO_IS_COLOR(mdnie->scenario)) {
		idx = mdnie->scenario - COLOR_TONE_1;
		mdnie_send_sequence(mdnie, color_tone_table[idx].seq);
		dev_info(mdnie->dev, "mode=%d, scenario=%d, outdoor=%d, cabc=%d, %s\n",
			mdnie->mode, mdnie->scenario, mdnie->outdoor, mdnie->cabc,
			color_tone_table[idx].name);

		goto exit;
	} else if (mdnie->scenario == CAMERA_MODE) {
		mdnie_send_sequence(mdnie, camera_table[mdnie->outdoor].seq);
		dev_info(mdnie->dev, "%s\n", camera_table[mdnie->outdoor].name);

		goto exit;
	} else {
		mdnie_send_sequence(mdnie, tunning_table[mdnie->cabc][mdnie->mode][mdnie->scenario].seq);
		dev_info(mdnie->dev, "mode=%d, scenario=%d, outdoor=%d, cabc=%d, %s\n",
			mdnie->mode, mdnie->scenario, mdnie->outdoor, mdnie->cabc,
			tunning_table[mdnie->cabc][mdnie->mode][mdnie->scenario].name);
	}

#if defined(CONFIG_TDMB) || defined(CONFIG_TARGET_LOCALE_NTT)
etc:
#endif
	if (!IS_ERR_OR_NULL(etc_table[mdnie->cabc][mdnie->outdoor][mdnie->tone].seq)) {
		mdnie_send_sequence(mdnie, etc_table[mdnie->cabc][mdnie->outdoor][mdnie->tone].seq);
		dev_info(mdnie->dev, "%s\n", etc_table[mdnie->cabc][mdnie->outdoor][mdnie->tone].name);
	}

exit:
	mutex_unlock(&mdnie->lock);

	return;
}

#if defined(CONFIG_FB_MDNIE_PWM)
#ifdef CONFIG_MACH_P4NOTE
static int get_backlight_level_from_brightness(unsigned int brightness)
{
	unsigned int value;

	/* brightness tuning*/
	if (brightness >= MID_BRIGHTNESS_LEVEL)
		value = (brightness - MID_BRIGHTNESS_LEVEL) * (b_value.max-b_value.mid) / (MAX_BRIGHTNESS_LEVEL-MID_BRIGHTNESS_LEVEL) + b_value.mid;
	else if (brightness >= LOW_BRIGHTNESS_LEVEL)
		value = (brightness - LOW_BRIGHTNESS_LEVEL) * (b_value.mid-b_value.low) / (MID_BRIGHTNESS_LEVEL-LOW_BRIGHTNESS_LEVEL) + b_value.low;
	else if (brightness >= DIM_BRIGHTNESS_LEVEL)
		value = (brightness - DIM_BRIGHTNESS_LEVEL) * (b_value.low-b_value.dim) / (LOW_BRIGHTNESS_LEVEL-DIM_BRIGHTNESS_LEVEL) + b_value.dim;
	else if (brightness > 0)
		value = b_value.dim;
	else
		return 0;

	if (value > 1600)
		value = 1600;

	if (value < 16)
		value = 1;
	else
		value = value >> 4;

	return value;
}
#else
static int get_backlight_level_from_brightness(unsigned int brightness)
{
	unsigned int value;

	/* brightness tuning*/
	if (brightness >= MID_BRIGHTNESS_LEVEL)
		value = (brightness - MID_BRIGHTNESS_LEVEL) * (MAX_BACKLIGHT_VALUE-MID_BACKLIGHT_VALUE) / (MAX_BRIGHTNESS_LEVEL-MID_BRIGHTNESS_LEVEL) + MID_BACKLIGHT_VALUE;
	else if (brightness >= LOW_BRIGHTNESS_LEVEL)
		value = (brightness - LOW_BRIGHTNESS_LEVEL) * (MID_BACKLIGHT_VALUE-LOW_BACKLIGHT_VALUE) / (MID_BRIGHTNESS_LEVEL-LOW_BRIGHTNESS_LEVEL) + LOW_BACKLIGHT_VALUE;
	else if (brightness >= DIM_BRIGHTNESS_LEVEL)
		value = (brightness - DIM_BRIGHTNESS_LEVEL) * (LOW_BACKLIGHT_VALUE-DIM_BACKLIGHT_VALUE) / (LOW_BRIGHTNESS_LEVEL-DIM_BRIGHTNESS_LEVEL) + DIM_BACKLIGHT_VALUE;
	else if (brightness > 0)
		value = DIM_BACKLIGHT_VALUE;
	else
		return 0;

	if (value > 1600)
		value = 1600;

	if (value < 16)
		value = 1;
	else
		value = value >> 4;

	return value;
}
#endif

#if defined(CONFIG_CPU_EXYNOS4210)
static void mdnie_pwm_control(struct mdnie_info *mdnie, int value)
{
	mutex_lock(&mdnie->dev_lock);
	mdnie_write(0x00, 0x0000);
	mdnie_write(0xB4, 0xC000 | value);
	mdnie_write(0x28, 0x0000);
	mutex_unlock(&mdnie->dev_lock);
}

static void mdnie_pwm_control_cabc(struct mdnie_info *mdnie, int value)
{
	int reg;
	const unsigned char *p_plut;
	u16 min_duty;
	unsigned idx;

	mutex_lock(&mdnie->dev_lock);

	idx = tunning_table[mdnie->cabc][mdnie->mode][mdnie->scenario].idx_lut;
	p_plut = power_lut[mdnie->power_lut_idx][idx];
	min_duty = p_plut[7] * value / 100;

	mdnie_write(0x00, 0x0000);

	if (min_duty < 4)
		reg = 0xC000 | (max(1, (value * p_plut[3] / 100)));
	else {
		/*PowerLUT*/
		mdnie_write(0x76, (p_plut[0] * value / 100) << 8 | (p_plut[1] * value / 100));
		mdnie_write(0x77, (p_plut[2] * value / 100) << 8 | (p_plut[3] * value / 100));
		mdnie_write(0x78, (p_plut[4] * value / 100) << 8 | (p_plut[5] * value / 100));
		mdnie_write(0x79, (p_plut[6] * value / 100) << 8	| (p_plut[7] * value / 100));
		mdnie_write(0x7a, (p_plut[8] * value / 100) << 8);

		reg = 0x5000 | (value << 4);
	}

	mdnie_write(0xB4, reg);
	mdnie_write(0x28, 0x0000);

	mutex_unlock(&mdnie->dev_lock);
}
#elif defined(CONFIG_CPU_EXYNOS4212) || defined(CONFIG_CPU_EXYNOS4412)
static void mdnie_pwm_control(struct mdnie_info *mdnie, int value)
{
	mutex_lock(&mdnie->dev_lock);
	mdnie_write(0x00, 0x0001);
	mdnie_write(0xB6, 0xC000 | value);
	mdnie_write(0xff, 0x0000);
	mutex_unlock(&mdnie->dev_lock);
}

static void mdnie_pwm_control_cabc(struct mdnie_info *mdnie, int value)
{
	int reg;
	const unsigned char *p_plut;
	u16 min_duty;
	unsigned idx;

	mutex_lock(&mdnie->dev_lock);

	idx = tunning_table[mdnie->cabc][mdnie->mode][mdnie->scenario].idx_lut;
	p_plut = power_lut[mdnie->power_lut_idx][idx];
	min_duty = p_plut[7] * value / 100;

	mdnie_write(0x00, 0x0001);

	if (min_duty < 4)
		reg = 0xC000 | (max(1, (value * p_plut[3] / 100)));
	else {
		/*PowerLUT*/
		mdnie_write(0x79, (p_plut[0] * value / 100) << 8 | (p_plut[1] * value / 100));
		mdnie_write(0x7a, (p_plut[2] * value / 100) << 8 | (p_plut[3] * value / 100));
		mdnie_write(0x7b, (p_plut[4] * value / 100) << 8 | (p_plut[5] * value / 100));
		mdnie_write(0x7c, (p_plut[6] * value / 100) << 8	| (p_plut[7] * value / 100));
		mdnie_write(0x7d, (p_plut[8] * value / 100) << 8);

		reg = 0x5000 | (value << 4);
	}

	mdnie_write(0xB6, reg);
	mdnie_write(0xff, 0x0000);

	mutex_unlock(&mdnie->dev_lock);
}
#endif

void set_mdnie_pwm_value(struct mdnie_info *mdnie, int value)
{
	mdnie_pwm_control(mdnie, value);
}

static int update_brightness(struct mdnie_info *mdnie)
{
	unsigned int value;
	unsigned int brightness = mdnie->bd->props.brightness;

	value = get_backlight_level_from_brightness(brightness);

	if (!mdnie->enable) {
		dev_err(mdnie->dev, "mdnie states is off\n");
		return 0;
	}

	if (brightness <= CABC_CUTOFF_BACKLIGHT_VALUE) {
		mdnie_pwm_control(mdnie, value);
	} else {
		if ((mdnie->cabc) && (mdnie->scenario != CAMERA_MODE) && !(mdnie->tunning))
			mdnie_pwm_control_cabc(mdnie, value);
		else
			mdnie_pwm_control(mdnie, value);
	}
	return 0;
}

static int mdnie_set_brightness(struct backlight_device *bd)
{
	struct mdnie_info *mdnie = bl_get_data(bd);
	int ret = 0;
	unsigned int brightness = bd->props.brightness;

	if (brightness < MIN_BRIGHTNESS ||
		brightness > bd->props.max_brightness) {
		dev_err(&bd->dev, "lcd brightness should be %d to %d. now %d\n",
			MIN_BRIGHTNESS, bd->props.max_brightness, brightness);
		brightness = bd->props.max_brightness;
	}

	if ((mdnie->enable) && (mdnie->bd_enable)) {
		ret = update_brightness(mdnie);
		dev_info(&bd->dev, "brightness=%d\n", bd->props.brightness);
		if (ret < 0)
			return -EINVAL;
	}

	return ret;
}

static int mdnie_get_brightness(struct backlight_device *bd)
{
	return bd->props.brightness;
}

static const struct backlight_ops mdnie_backlight_ops  = {
	.get_brightness = mdnie_get_brightness,
	.update_status = mdnie_set_brightness,
};
#endif

static ssize_t mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mdnie_info *mdnie = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", mdnie->mode);
}

static ssize_t mode_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct mdnie_info *mdnie = dev_get_drvdata(dev);
	unsigned int value;
	int ret;

	ret = strict_strtoul(buf, 0, (unsigned long *)&value);

	dev_info(dev, "%s :: value=%d\n", __func__, value);

	if (value >= MODE_MAX) {
		value = STANDARD;
		return -EINVAL;
	}

	mutex_lock(&mdnie->lock);
	mdnie->mode = value;
	mutex_unlock(&mdnie->lock);

	set_mdnie_value(mdnie, 0);
#if defined(CONFIG_FB_MDNIE_PWM)
	if ((mdnie->enable) && (mdnie->bd_enable))
		update_brightness(mdnie);
#endif

	return count;
}


static ssize_t scenario_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mdnie_info *mdnie = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", mdnie->scenario);
}

static ssize_t scenario_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct mdnie_info *mdnie = dev_get_drvdata(dev);
	unsigned int value;
	int ret;

	ret = strict_strtoul(buf, 0, (unsigned long *)&value);

	dev_info(dev, "%s :: value=%d\n", __func__, value);

	if (!SCENARIO_IS_VALID(value))
		value = CYANOGENMOD_MODE;

#if defined(CONFIG_FB_MDNIE_PWM)
	if (value >= SCENARIO_MAX)
		value = CYANOGENMOD_MODE;
#endif

	mutex_lock(&mdnie->lock);
	mdnie->scenario = value;
	mutex_unlock(&mdnie->lock);

	set_mdnie_value(mdnie, 0);
#if defined(CONFIG_FB_MDNIE_PWM)
	if ((mdnie->enable) && (mdnie->bd_enable))
		update_brightness(mdnie);
#endif

	return count;
}


static ssize_t outdoor_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mdnie_info *mdnie = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", mdnie->outdoor);
}

static ssize_t outdoor_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct mdnie_info *mdnie = dev_get_drvdata(dev);
	unsigned int value;
	int ret;

	ret = strict_strtoul(buf, 0, (unsigned long *)&value);

	dev_info(dev, "%s :: value=%d\n", __func__, value);

	if (value >= OUTDOOR_MAX)
		value = OUTDOOR_OFF;

	value = (value) ? OUTDOOR_ON : OUTDOOR_OFF;

	mutex_lock(&mdnie->lock);
	mdnie->outdoor = value;
	mutex_unlock(&mdnie->lock);

	set_mdnie_value(mdnie, 0);

	return count;
}


#if defined(CONFIG_FB_MDNIE_PWM)
static ssize_t cabc_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mdnie_info *mdnie = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", mdnie->cabc);
}

static ssize_t cabc_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct mdnie_info *mdnie = dev_get_drvdata(dev);
	unsigned int value;
	int ret;

	ret = strict_strtoul(buf, 0, (unsigned long *)&value);

	dev_info(dev, "%s :: value=%d\n", __func__, value);

	if (value >= CABC_MAX)
		value = CABC_OFF;

	value = (value) ? CABC_ON : CABC_OFF;

	mutex_lock(&mdnie->lock);
	mdnie->cabc = value;
	mutex_unlock(&mdnie->lock);

	set_mdnie_value(mdnie, 0);
	if ((mdnie->enable) && (mdnie->bd_enable))
		update_brightness(mdnie);

	return count;
}

static ssize_t auto_brightness_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct mdnie_info *mdnie = dev_get_drvdata(dev);
	char *pos = buf;
	int i;

	pos += sprintf(pos, "%d, %d, ", mdnie->auto_brightness, mdnie->power_lut_idx);

	for (i = 0; i < 5; i++)
		pos += sprintf(pos, "0x%02x, ", power_lut[mdnie->power_lut_idx][0][i]);

	pos += sprintf(pos, "\n");

	return pos - buf;
}

static ssize_t auto_brightness_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct mdnie_info *mdnie = dev_get_drvdata(dev);
	int value;
	int rc;

	rc = strict_strtoul(buf, (unsigned int)0, (unsigned long *)&value);
	if (rc < 0)
		return rc;
	else {
		if (mdnie->auto_brightness != value) {
			dev_info(dev, "%s - %d -> %d\n", __func__, mdnie->auto_brightness, value);
			mutex_lock(&mdnie->dev_lock);
			mdnie->auto_brightness = value;
#if defined(CONFIG_FB_S5P_S6C1372)
			mutex_lock(&mdnie->lock);
			mdnie->cabc = (value) ? CABC_ON : CABC_OFF;
			mutex_unlock(&mdnie->lock);
#endif
			if (mdnie->auto_brightness >= 5)
				mdnie->power_lut_idx = LUT_LEVEL_OUTDOOR_2;
			else if (mdnie->auto_brightness == 4)
				mdnie->power_lut_idx = LUT_LEVEL_OUTDOOR_1;
			else
				mdnie->power_lut_idx = LUT_LEVEL_MANUAL_AND_INDOOR;
			mutex_unlock(&mdnie->dev_lock);
			set_mdnie_value(mdnie, 0);
			if (mdnie->bd_enable)
				update_brightness(mdnie);
		}
	}
	return size;
}

static DEVICE_ATTR(auto_brightness, 0644, auto_brightness_show, auto_brightness_store);
#endif

static ssize_t tunning_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	char temp[128];

	sprintf(temp, "%s\n", tuning_file_name);
	strcat(buf, temp);

	return strlen(buf);
}

static ssize_t tunning_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct mdnie_info *mdnie = dev_get_drvdata(dev);

	if (!strncmp(buf, "0", 1)) {
		mdnie->tunning = FALSE;
		dev_info(dev, "%s :: tunning is disabled.\n", __func__);
	} else if (!strncmp(buf, "1", 1)) {
		mdnie->tunning = TRUE;
		dev_info(dev, "%s :: tunning is enabled.\n", __func__);
	} else {
		if (!mdnie->tunning)
			return count;
		memset(tuning_file_name, 0, sizeof(tuning_file_name));
		strcpy(tuning_file_name, "/sdcard/mdnie/");
		strncat(tuning_file_name, buf, count-1);

		mdnie_txtbuf_to_parsing(tuning_file_name);

		dev_info(dev, "%s :: %s\n", __func__, tuning_file_name);
	}

	return count;
}

static ssize_t negative_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mdnie_info *mdnie = dev_get_drvdata(dev);
	char *pos = buf;
	u32 i;

	pos += sprintf(pos, "%d\n", mdnie->negative);

	for (i = 0; i < 5; i++) {
		if (negative[i].enable) {
			pos += sprintf(pos, "pid=%d, ", negative[i].pid);
			pos += sprintf(pos, "%s, ", negative[i].comm);
			pos += sprintf(pos, "%s\n", negative[i].time);
		}
	}

	return pos - buf;
}

static ssize_t negative_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct mdnie_info *mdnie = dev_get_drvdata(dev);
	unsigned int value;
	int ret;
	struct timespec ts;
	struct rtc_time tm;

	ret = strict_strtoul(buf, 0, (unsigned long *)&value);
	dev_info(dev, "%s :: value=%d, by %s\n", __func__, value, current->comm);

	if (ret < 0)
		return ret;
	else {
		if (mdnie->negative == value)
			return count;

		if (value >= NEGATIVE_MAX)
			value = NEGATIVE_OFF;

		value = (value) ? NEGATIVE_ON : NEGATIVE_OFF;

		mutex_lock(&mdnie->lock);
		mdnie->negative = value;
		if (value) {
			getnstimeofday(&ts);
			rtc_time_to_tm(ts.tv_sec, &tm);
			negative[negative_idx].enable = value;
			negative[negative_idx].pid = current->pid;
			strcpy(negative[negative_idx].comm, current->comm);
			sprintf(negative[negative_idx].time, "%d-%02d-%02d %02d:%02d:%02d",
				tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
			negative_idx++;
			negative_idx %= 5;
		}
		mutex_unlock(&mdnie->lock);

		set_mdnie_value(mdnie, 0);
	}
	return count;
}

#ifdef CONFIG_FB_S5P_MDNIE_HIJACK
/* Yank555.lu : Hijack sysfs interface (Thanx to WhatHub for the base work on n9005)
 *
 * SysFs interface :
 * -----------------
 *
 * /sys/class/mdnie/mdnie/hijack : 0 = disbaled / 1 = enabled
 *
 *    Enable / Disable hijacking of mdnie settings (default = disabled)
 *
 * /sys/class/mdnie/mdnie/sharpen : 0 = disbaled / 1 = enabled
 *
 *    Enable / Disable screen sharpness (default = enabled, stock Samsung Update 12+)
 *
 * /sys/class/mdnie/mdnie/black : -128 to +128
 *
 *    Global black level, will shift black RGB setting by this value (default = 0)
 *
 *    NB: Effectively applied values for black will never be below 0 nor above 255 !
 *
 *        So there is no use setting Black RGB to (0,0,0) and this to -10, the result
 *        will still be (0,0,0).
 *
 * /sys/class/mdnie/mdnie/{color}_red   : 0 to 255
 * /sys/class/mdnie/mdnie/{color}_green : 0 to 255
 * /sys/class/mdnie/mdnie/{color}_blue  : 0 to 255
 *
 *    Allows to set the RGB values for the base colors :
 *
 *      - red
 *      - green
 *      - blue
 *      - cyan
 *      - magenta
 *      - yellow
 *      - black
 *      - white
 *
 *    (default = Samsung's natural mode)
 *
 */

/* hijack */

static ssize_t hijack_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", hijack);
}

static ssize_t hijack_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
	int new_val;
	struct mdnie_info *mdnie = dev_get_drvdata(dev);

	sscanf(buf, "%d", &new_val);

	switch (new_val) {
		case HIJACK_DISABLED:
		case HIJACK_ENABLED:	hijack = new_val;
					set_mdnie_value(mdnie, 0);
					return size;
		default:		return -EINVAL;
	}
}

/* sharpen */

static ssize_t sharpen_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", (tune_hijack[13] & 0x0004) >> 2);
}

static ssize_t sharpen_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
	int new_val;
	struct mdnie_info *mdnie = dev_get_drvdata(dev);

	sscanf(buf, "%d", &new_val);

	if (new_val != ((tune_hijack[13] & 0x0004) >> 2)) {
		if (new_val < 0 || new_val > 1)
			return -EINVAL;
		tune_hijack[13] = (new_val << 2) + (tune_hijack[13] & 0xFFFB);
		if (hijack == HIJACK_ENABLED)
			set_mdnie_value(mdnie, 0);
	}
	return size;
}

/* red */

static ssize_t red_red_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", ((tune_hijack[25] & 0xFF00) >> 8));
}

static ssize_t red_red_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
	int new_val;
	struct mdnie_info *mdnie = dev_get_drvdata(dev);

	sscanf(buf, "%d", &new_val);

	if (new_val != ((tune_hijack[25] & 0xFF00) >> 8)) {
		if (new_val < 0 || new_val > 255)
			return -EINVAL;
		tune_hijack[25] = (new_val << 8) + (tune_hijack[25] & 0x00FF);
		if (hijack == HIJACK_ENABLED)
			set_mdnie_value(mdnie, 0);
	}
	return size;
}

static ssize_t red_green_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", ((tune_hijack[33] & 0xFF00) >> 8));
}

static ssize_t red_green_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
	int new_val;
	struct mdnie_info *mdnie = dev_get_drvdata(dev);

	sscanf(buf, "%d", &new_val);

	if (new_val != ((tune_hijack[33] & 0xFF00) >> 8)) {
		if (new_val < 0 || new_val > 255)
			return -EINVAL;
		tune_hijack[33] = (new_val << 8) + (tune_hijack[33] & 0x00FF);
		if (hijack == HIJACK_ENABLED)
			set_mdnie_value(mdnie, 0);
	}
	return size;
}

static ssize_t red_blue_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", ((tune_hijack[41] & 0xFF00) >> 8));
}

static ssize_t red_blue_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
	int new_val;
	struct mdnie_info *mdnie = dev_get_drvdata(dev);

	sscanf(buf, "%d", &new_val);

	if (new_val != ((tune_hijack[41] & 0xFF00) >> 8)) {
		if (new_val < 0 || new_val > 255)
			return -EINVAL;
		tune_hijack[41] = (new_val << 8) + (tune_hijack[41] & 0x00FF);
		if (hijack == HIJACK_ENABLED)
			set_mdnie_value(mdnie, 0);
	}
	return size;
}

/* cyan */

static ssize_t cyan_red_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", (tune_hijack[23] & 0x00FF));
}

static ssize_t cyan_red_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
	int new_val;
	struct mdnie_info *mdnie = dev_get_drvdata(dev);

	sscanf(buf, "%d", &new_val);

	if (new_val != (tune_hijack[23] & 0x00FF)) {
		if (new_val < 0 || new_val > 255)
			return -EINVAL;
		tune_hijack[23] = new_val + (tune_hijack[23] & 0xFF00);
		if (hijack == HIJACK_ENABLED)
			set_mdnie_value(mdnie, 0);
	}
	return size;
}

static ssize_t cyan_green_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", (tune_hijack[31] & 0x00FF));
}

static ssize_t cyan_green_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
	int new_val;
	struct mdnie_info *mdnie = dev_get_drvdata(dev);

	sscanf(buf, "%d", &new_val);

	if (new_val != (tune_hijack[31] & 0x00FF)) {
		if (new_val < 0 || new_val > 255)
			return -EINVAL;
		tune_hijack[31] = new_val + (tune_hijack[31] & 0xFF00);
		if (hijack == HIJACK_ENABLED)
			set_mdnie_value(mdnie, 0);
	}
	return size;
}

static ssize_t cyan_blue_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", (tune_hijack[39] & 0x00FF));
}

static ssize_t cyan_blue_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
	int new_val;
	struct mdnie_info *mdnie = dev_get_drvdata(dev);

	sscanf(buf, "%d", &new_val);

	if (new_val != (tune_hijack[39] & 0x00FF)) {
		if (new_val < 0 || new_val > 255)
			return -EINVAL;
		tune_hijack[39] = new_val + (tune_hijack[39] & 0xFF00);
		if (hijack == HIJACK_ENABLED)
			set_mdnie_value(mdnie, 0);
	}
	return size;
}

/* green */

static ssize_t green_red_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", ((tune_hijack[23] & 0xFF00) >> 8));
}

static ssize_t green_red_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
	int new_val;
	struct mdnie_info *mdnie = dev_get_drvdata(dev);

	sscanf(buf, "%d", &new_val);

	if (new_val != ((tune_hijack[23] & 0xFF00) >> 8)) {
		if (new_val < 0 || new_val > 255)
			return -EINVAL;
		tune_hijack[23] = (new_val << 8) + (tune_hijack[23] & 0x00FF);
		if (hijack == HIJACK_ENABLED)
			set_mdnie_value(mdnie, 0);
	}
	return size;
}

static ssize_t green_green_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", ((tune_hijack[31] & 0xFF00) >> 8));
}

static ssize_t green_green_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
	int new_val;
	struct mdnie_info *mdnie = dev_get_drvdata(dev);

	sscanf(buf, "%d", &new_val);

	if (new_val != ((tune_hijack[31] & 0xFF00) >> 8)) {
		if (new_val < 0 || new_val > 255)
			return -EINVAL;
		tune_hijack[31] = (new_val << 8) + (tune_hijack[31] & 0x00FF);
		if (hijack == HIJACK_ENABLED)
			set_mdnie_value(mdnie, 0);
	}
	return size;
}

static ssize_t green_blue_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", ((tune_hijack[39] & 0xFF00) >> 8));
}

static ssize_t green_blue_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
	int new_val;
	struct mdnie_info *mdnie = dev_get_drvdata(dev);

	sscanf(buf, "%d", &new_val);

	if (new_val != ((tune_hijack[39] & 0xFF00) >> 8)) {
		if (new_val < 0 || new_val > 255)
			return -EINVAL;
		tune_hijack[39] = (new_val << 8) + (tune_hijack[39] & 0x00FF);
		if (hijack == HIJACK_ENABLED)
			set_mdnie_value(mdnie, 0);
	}
	return size;
}

/* magenta */

static ssize_t magenta_red_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", (tune_hijack[25] & 0x00FF));
}

static ssize_t magenta_red_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
	int new_val;
	struct mdnie_info *mdnie = dev_get_drvdata(dev);

	sscanf(buf, "%d", &new_val);

	if (new_val != (tune_hijack[25] & 0x00FF)) {
		if (new_val < 0 || new_val > 255)
			return -EINVAL;
		tune_hijack[25] = new_val + (tune_hijack[25] & 0xFF00);
		if (hijack == HIJACK_ENABLED)
			set_mdnie_value(mdnie, 0);
	}
	return size;
}

static ssize_t magenta_green_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", (tune_hijack[33] & 0x00FF));
}

static ssize_t magenta_green_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
	int new_val;
	struct mdnie_info *mdnie = dev_get_drvdata(dev);

	sscanf(buf, "%d", &new_val);

	if (new_val != (tune_hijack[33] & 0x00FF)) {
		if (new_val < 0 || new_val > 255)
			return -EINVAL;
		tune_hijack[33] = new_val + (tune_hijack[33] & 0xFF00);
		if (hijack == HIJACK_ENABLED)
			set_mdnie_value(mdnie, 0);
	}
	return size;
}

static ssize_t magenta_blue_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", (tune_hijack[41] & 0x00FF));
}

static ssize_t magenta_blue_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
	int new_val;
	struct mdnie_info *mdnie = dev_get_drvdata(dev);

	sscanf(buf, "%d", &new_val);

	if (new_val != (tune_hijack[41] & 0x00FF)) {
		if (new_val < 0 || new_val > 255)
			return -EINVAL;
		tune_hijack[41] = new_val + (tune_hijack[41] & 0xFF00);
		if (hijack == HIJACK_ENABLED)
			set_mdnie_value(mdnie, 0);
	}
	return size;
}

/* blue */

static ssize_t blue_red_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", ((tune_hijack[21] & 0xFF00) >> 8));
}

static ssize_t blue_red_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
	int new_val;
	struct mdnie_info *mdnie = dev_get_drvdata(dev);

	sscanf(buf, "%d", &new_val);

	if (new_val != ((tune_hijack[21] & 0xFF00) >> 8)) {
		if (new_val < 0 || new_val > 255)
			return -EINVAL;
		tune_hijack[21] = (new_val << 8) + (tune_hijack[21] & 0x00FF);
		if (hijack == HIJACK_ENABLED)
			set_mdnie_value(mdnie, 0);
	}
	return size;
}

static ssize_t blue_green_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", ((tune_hijack[29] & 0xFF00) >> 8));
}

static ssize_t blue_green_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
	int new_val;
	struct mdnie_info *mdnie = dev_get_drvdata(dev);

	sscanf(buf, "%d", &new_val);

	if (new_val != ((tune_hijack[29] & 0xFF00) >> 8)) {
		if (new_val < 0 || new_val > 255)
			return -EINVAL;
		tune_hijack[29] = (new_val << 8) + (tune_hijack[29] & 0x00FF);
		if (hijack == HIJACK_ENABLED)
			set_mdnie_value(mdnie, 0);
	}
	return size;
}

static ssize_t blue_blue_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", ((tune_hijack[37] & 0xFF00) >> 8));
}

static ssize_t blue_blue_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
	int new_val;
	struct mdnie_info *mdnie = dev_get_drvdata(dev);

	sscanf(buf, "%d", &new_val);

	if (new_val != ((tune_hijack[37] & 0xFF00) >> 8)) {
		if (new_val < 0 || new_val > 255)
			return -EINVAL;
		tune_hijack[37] = (new_val << 8) + (tune_hijack[37] & 0x00FF);
		if (hijack == HIJACK_ENABLED)
			set_mdnie_value(mdnie, 0);
	}
	return size;
}

/* yellow */

static ssize_t yellow_red_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", (tune_hijack[27] & 0x00FF));
}

static ssize_t yellow_red_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
	int new_val;
	struct mdnie_info *mdnie = dev_get_drvdata(dev);

	sscanf(buf, "%d", &new_val);

	if (new_val != (tune_hijack[27] & 0x00FF)) {
		if (new_val < 0 || new_val > 255)
			return -EINVAL;
		tune_hijack[27] = new_val + (tune_hijack[27] & 0xFF00);
		if (hijack == HIJACK_ENABLED)
			set_mdnie_value(mdnie, 0);
	}
	return size;
}

static ssize_t yellow_green_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", (tune_hijack[35] & 0x00FF));
}

static ssize_t yellow_green_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
	int new_val;
	struct mdnie_info *mdnie = dev_get_drvdata(dev);

	sscanf(buf, "%d", &new_val);

	if (new_val != (tune_hijack[35] & 0x00FF)) {
		if (new_val < 0 || new_val > 255)
			return -EINVAL;
		tune_hijack[35] = new_val + (tune_hijack[35] & 0xFF00);
		if (hijack == HIJACK_ENABLED)
			set_mdnie_value(mdnie, 0);
	}
	return size;
}

static ssize_t yellow_blue_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", (tune_hijack[43] & 0x00FF));
}

static ssize_t yellow_blue_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
	int new_val;
	struct mdnie_info *mdnie = dev_get_drvdata(dev);

	sscanf(buf, "%d", &new_val);

	if (new_val != (tune_hijack[43] & 0x00FF)) {
		if (new_val < 0 || new_val > 255)
			return -EINVAL;
		tune_hijack[43] = new_val + (tune_hijack[43] & 0xFF00);
		if (hijack == HIJACK_ENABLED)
			set_mdnie_value(mdnie, 0);
	}
	return size;
}

/* black */

static ssize_t black_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", black);
}

static ssize_t black_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
	int new_val;
	struct mdnie_info *mdnie = dev_get_drvdata(dev);

	sscanf(buf, "%d", &new_val);

	if (new_val != black) {
		if (new_val < -128 || new_val > 128)
			return -EINVAL;
		black = new_val;
		tune_hijack[21] = ((max(0,min(255, black_r + black))) << 8) + (tune_hijack[21] & 0x00FF);
		tune_hijack[29] = ((max(0,min(255, black_g + black))) << 8) + (tune_hijack[29] & 0x00FF);
		tune_hijack[37] = ((max(0,min(255, black_b + black))) << 8) + (tune_hijack[37] & 0x00FF);
		if (hijack == HIJACK_ENABLED)
			set_mdnie_value(mdnie, 0);
	}
	return size;
}

static ssize_t black_red_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", black_r);
}

static ssize_t black_red_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
	int new_val;
	struct mdnie_info *mdnie = dev_get_drvdata(dev);

	sscanf(buf, "%d", &new_val);

	if (new_val != black_r) {
		if (new_val < 0 || new_val > 255)
			return -EINVAL;
		black_r = new_val;
		tune_hijack[21] = ((max(0,min(255, black_r + black))) << 8) + (tune_hijack[21] & 0x00FF);
		if (hijack == HIJACK_ENABLED)
			set_mdnie_value(mdnie, 0);
	}
	return size;
}

static ssize_t black_green_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", black_g);
}

static ssize_t black_green_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
	int new_val;
	struct mdnie_info *mdnie = dev_get_drvdata(dev);

	sscanf(buf, "%d", &new_val);

	if (new_val != black_g) {
		if (new_val < 0 || new_val > 255)
			return -EINVAL;
		black_g = new_val;
		tune_hijack[29] = ((max(0,min(255, black_g + black))) << 8) + (tune_hijack[29] & 0x00FF);
		if (hijack == HIJACK_ENABLED)
			set_mdnie_value(mdnie, 0);
	}
	return size;
}

static ssize_t black_blue_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", max(0, black_b));
}

static ssize_t black_blue_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
	int new_val;
	struct mdnie_info *mdnie = dev_get_drvdata(dev);

	sscanf(buf, "%d", &new_val);

	if (new_val != black_b) {
		if (new_val < 0 || new_val > 255)
			return -EINVAL;
		black_b = new_val;
		tune_hijack[37] = ((max(0,min(255, black_b + black))) << 8) + (tune_hijack[37] & 0x00FF);
		if (hijack == HIJACK_ENABLED)
			set_mdnie_value(mdnie, 0);
	}
	return size;
}

/* white */

static ssize_t white_red_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", (tune_hijack[27] & 0x00FF));
}

static ssize_t white_red_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
	int new_val;
	struct mdnie_info *mdnie = dev_get_drvdata(dev);

	sscanf(buf, "%d", &new_val);

	if (new_val != (tune_hijack[27] & 0x00FF)) {
		if (new_val < 0 || new_val > 255)
			return -EINVAL;
		tune_hijack[27] = new_val + (tune_hijack[27] & 0xFF00);
		if (hijack == HIJACK_ENABLED)
			set_mdnie_value(mdnie, 0);
	}
	return size;
}

static ssize_t white_green_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", (tune_hijack[35] & 0x00FF));
}

static ssize_t white_green_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
	int new_val;
	struct mdnie_info *mdnie = dev_get_drvdata(dev);

	sscanf(buf, "%d", &new_val);

	if (new_val != (tune_hijack[35] & 0x00FF)) {
		if (new_val < 0 || new_val > 255)
			return -EINVAL;
		tune_hijack[35] = new_val + (tune_hijack[35] & 0xFF00);
		if (hijack == HIJACK_ENABLED)
			set_mdnie_value(mdnie, 0);
	}
	return size;
}

static ssize_t white_blue_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", (tune_hijack[43] & 0x00FF));
}

static ssize_t white_blue_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
	int new_val;
	struct mdnie_info *mdnie = dev_get_drvdata(dev);

	sscanf(buf, "%d", &new_val);

	if (new_val != (tune_hijack[43] & 0x00FF)) {
		if (new_val < 0 || new_val > 255)
			return -EINVAL;
		tune_hijack[43] = new_val + (tune_hijack[43] & 0xFF00);
		if (hijack == HIJACK_ENABLED)
			set_mdnie_value(mdnie, 0);
	}
	return size;
}
#endif

static struct device_attribute mdnie_attributes[] = {
	__ATTR(mode, 0664, mode_show, mode_store),
	__ATTR(scenario, 0664, scenario_show, scenario_store),
	__ATTR(outdoor, 0664, outdoor_show, outdoor_store),
#if defined(CONFIG_FB_MDNIE_PWM)
#if defined(CONFIG_FB_S5P_S6C1372)
	__ATTR(cabc, 0444, cabc_show, NULL),
#else
	__ATTR(cabc, 0664, cabc_show, cabc_store),
#endif
#endif
#ifdef CONFIG_FB_S5P_MDNIE_HIJACK
	// Yank555.lu : add hijack sysfs interface
	__ATTR(hijack, 0664, hijack_show, hijack_store),
	__ATTR(sharpen, 0664, sharpen_show, sharpen_store),
	__ATTR(red_red, 0664, red_red_show, red_red_store),
	__ATTR(red_green, 0664, red_green_show, red_green_store),
	__ATTR(red_blue, 0664, red_blue_show, red_blue_store),
	__ATTR(cyan_red, 0664, cyan_red_show, cyan_red_store),
	__ATTR(cyan_green, 0664, cyan_green_show, cyan_green_store),
	__ATTR(cyan_blue, 0664, cyan_blue_show, cyan_blue_store),
	__ATTR(green_red, 0664, green_red_show, green_red_store),
	__ATTR(green_green, 0664, green_green_show, green_green_store),
	__ATTR(green_blue, 0664, green_blue_show, green_blue_store),
	__ATTR(magenta_red, 0664, magenta_red_show, magenta_red_store),
	__ATTR(magenta_green, 0664, magenta_green_show, magenta_green_store),
	__ATTR(magenta_blue, 0664, magenta_blue_show, magenta_blue_store),
	__ATTR(blue_red, 0664, blue_red_show, blue_red_store),
	__ATTR(blue_green, 0664, blue_green_show, blue_green_store),
	__ATTR(blue_blue, 0664, blue_blue_show, blue_blue_store),
	__ATTR(yellow_red, 0664, yellow_red_show, yellow_red_store),
	__ATTR(yellow_green, 0664, yellow_green_show, yellow_green_store),
	__ATTR(yellow_blue, 0664, yellow_blue_show, yellow_blue_store),
	__ATTR(black, 0664, black_show, black_store),
	__ATTR(black_red, 0664, black_red_show, black_red_store),
	__ATTR(black_green, 0664, black_green_show, black_green_store),
	__ATTR(black_blue, 0664, black_blue_show, black_blue_store),
	__ATTR(white_red, 0664, white_red_show, white_red_store),
	__ATTR(white_green, 0664, white_green_show, white_green_store),
	__ATTR(white_blue, 0664, white_blue_show, white_blue_store),
#endif
	__ATTR(tunning, 0664, tunning_show, tunning_store),
	__ATTR(negative, 0664, negative_show, negative_store),
	__ATTR_NULL,
};

#ifdef CONFIG_PM
#if defined(CONFIG_HAS_EARLYSUSPEND)
void mdnie_early_suspend(struct early_suspend *h)
{
#if defined(CONFIG_FB_MDNIE_PWM)
	struct mdnie_info *mdnie = container_of(h, struct mdnie_info, early_suspend);
	struct lcd_platform_data *pd = NULL;
	pd = mdnie->lcd_pd;

	dev_info(mdnie->dev, "+%s\n", __func__);

	mdnie->bd_enable = FALSE;

	if (mdnie->enable)
		mdnie_pwm_control(mdnie, 0);

	if (!pd)
		dev_info(&mdnie->bd->dev, "platform data is NULL.\n");

	if (!pd->power_on)
		dev_info(&mdnie->bd->dev, "power_on is NULL.\n");
	else
		pd->power_on(NULL, 0);

	dev_info(mdnie->dev, "-%s\n", __func__);
#endif

	return ;
}

void mdnie_late_resume(struct early_suspend *h)
{
	u32 i;
	struct mdnie_info *mdnie = container_of(h, struct mdnie_info, early_suspend);
#if defined(CONFIG_FB_MDNIE_PWM)
	struct lcd_platform_data *pd = NULL;

	dev_info(mdnie->dev, "+%s\n", __func__);
	pd = mdnie->lcd_pd;

	if (mdnie->enable)
		mdnie_pwm_control(mdnie, 0);

	if (!pd)
		dev_info(&mdnie->bd->dev, "platform data is NULL.\n");

	if (!pd->power_on)
		dev_info(&mdnie->bd->dev, "power_on is NULL.\n");
	else
		pd->power_on(NULL, 1);

	if (mdnie->enable) {
		dev_info(&mdnie->bd->dev, "brightness=%d\n", mdnie->bd->props.brightness);
		update_brightness(mdnie);
	}

	mdnie->bd_enable = TRUE;
	dev_info(mdnie->dev, "-%s\n", __func__);
#endif
	for (i = 0; i < 5; i++) {
		if (negative[i].enable)
			dev_info(mdnie->dev, "pid=%d, %s, %s\n", negative[i].pid, negative[i].comm, negative[i].time);
	}

	return ;
}
#endif
#endif

//gm
void mdnie_toggle_negative(void)
{
	mutex_lock(&g_mdnie->lock);
	g_mdnie->negative = !g_mdnie->negative;
	mutex_unlock(&g_mdnie->lock);

	set_mdnie_value(g_mdnie, 0);
}

static int mdnie_probe(struct platform_device *pdev)
{
#if defined(CONFIG_FB_MDNIE_PWM)
	struct platform_mdnie_data *pdata = pdev->dev.platform_data;
#endif
	struct mdnie_info *mdnie;
	int ret = 0;

	mdnie_class = class_create(THIS_MODULE, dev_name(&pdev->dev));
	if (IS_ERR_OR_NULL(mdnie_class)) {
		pr_err("failed to create mdnie class\n");
		ret = -EINVAL;
		goto error0;
	}

	mdnie_class->dev_attrs = mdnie_attributes;

	mdnie = kzalloc(sizeof(struct mdnie_info), GFP_KERNEL);
	if (!mdnie) {
		pr_err("failed to allocate mdnie\n");
		ret = -ENOMEM;
		goto error1;
	}

	mdnie->dev = device_create(mdnie_class, &pdev->dev, 0, &mdnie, "mdnie");
	if (IS_ERR_OR_NULL(mdnie->dev)) {
		pr_err("failed to create mdnie device\n");
		ret = -EINVAL;
		goto error2;
	}

#if defined(CONFIG_FB_MDNIE_PWM)
	if (!pdata) {
		pr_err("no platform data specified\n");
		ret = -EINVAL;
		goto error2;
	}

	mdnie->bd = backlight_device_register("panel", mdnie->dev,
		mdnie, &mdnie_backlight_ops, NULL);
	mdnie->bd->props.max_brightness = MAX_BRIGHTNESS_LEVEL;
	mdnie->bd->props.brightness = DEFAULT_BRIGHTNESS;
	mdnie->bd_enable = TRUE;
	mdnie->lcd_pd = pdata->lcd_pd;

	ret = device_create_file(&mdnie->bd->dev, &dev_attr_auto_brightness);
	if (ret < 0)
		dev_err(&mdnie->bd->dev, "failed to add sysfs entries, %d\n", __LINE__);
#endif

	mdnie->scenario = CYANOGENMOD_MODE;
	mdnie->mode = STANDARD;
	mdnie->tone = TONE_NORMAL;
	mdnie->outdoor = OUTDOOR_OFF;
#if defined(CONFIG_FB_MDNIE_PWM)
	mdnie->cabc = CABC_ON;
	mdnie->power_lut_idx = LUT_LEVEL_MANUAL_AND_INDOOR;
	mdnie->auto_brightness = 0;
#else
	mdnie->cabc = CABC_OFF;
#endif

#if defined(CONFIG_FB_S5P_S6C1372)
	mdnie->cabc = CABC_OFF;
#endif
	mdnie->enable = TRUE;
	mdnie->tunning = FALSE;
	mdnie->negative = NEGATIVE_OFF;

	mutex_init(&mdnie->lock);
	mutex_init(&mdnie->dev_lock);

	platform_set_drvdata(pdev, mdnie);
	dev_set_drvdata(mdnie->dev, mdnie);

#ifdef CONFIG_HAS_WAKELOCK
#ifdef CONFIG_HAS_EARLYSUSPEND
#if 1 /* defined(CONFIG_FB_MDNIE_PWM) */
	mdnie->early_suspend.suspend = mdnie_early_suspend;
	mdnie->early_suspend.resume = mdnie_late_resume;
	mdnie->early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB - 1;
	register_early_suspend(&mdnie->early_suspend);
#endif
#endif
#endif

#if defined(CONFIG_FB_S5P_S6C1372)
	check_lcd_type();
	dev_info(mdnie->dev, "lcdtype = %d\n", pdata->display_type);
	if (pdata->display_type == 1) {
		b_value.max = 1441;
		b_value.mid = 784;
		b_value.low = 16;
		b_value.dim = 16;
	} else {
		b_value.max = 1216;	/* 76% */
		b_value.mid = 679;	/* 39% */
		b_value.low = 16;	/* 1% */
		b_value.dim = 16;	/* 1% */
	}
#endif

#if defined(CONFIG_FB_S5P_S6F1202A)
	if (pdata->display_type == 0) {
		memcpy(tunning_table, tunning_table_hydis, sizeof(tunning_table));
		memcpy(etc_table, etc_table_hydis, sizeof(etc_table));
		memcpy(camera_table, camera_table_hydis, sizeof(camera_table));
	} else if (pdata->display_type == 1) {
		memcpy(tunning_table, tunning_table_sec, sizeof(tunning_table));
		memcpy(etc_table, etc_table_sec, sizeof(etc_table));
		memcpy(camera_table, camera_table_sec, sizeof(camera_table));
	} else if (pdata->display_type == 2) {
		memcpy(tunning_table, tunning_table_boe, sizeof(tunning_table));
		memcpy(etc_table, etc_table_boe, sizeof(etc_table));
		memcpy(camera_table, camera_table_boe, sizeof(camera_table));
	}
#endif

	g_mdnie = mdnie;

	set_mdnie_value(mdnie, 0);

	dev_info(mdnie->dev, "registered successfully\n");

	return 0;

error2:
	kfree(mdnie);
error1:
	class_destroy(mdnie_class);
error0:
	return ret;
}

static int mdnie_remove(struct platform_device *pdev)
{
	struct mdnie_info *mdnie = dev_get_drvdata(&pdev->dev);

#if defined(CONFIG_FB_MDNIE_PWM)
	backlight_device_unregister(mdnie->bd);
#endif
	class_destroy(mdnie_class);
	kfree(mdnie);

	return 0;
}

static void mdnie_shutdown(struct platform_device *pdev)
{
#if defined(CONFIG_FB_MDNIE_PWM)
	struct mdnie_info *mdnie = dev_get_drvdata(&pdev->dev);
	struct lcd_platform_data *pd = NULL;
	pd = mdnie->lcd_pd;

	dev_info(mdnie->dev, "+%s\n", __func__);

	mdnie->bd_enable = FALSE;

	if (mdnie->enable)
		mdnie_pwm_control(mdnie, 0);

	if (!pd)
		dev_info(&mdnie->bd->dev, "platform data is NULL.\n");

	if (!pd->power_on)
		dev_info(&mdnie->bd->dev, "power_on is NULL.\n");
	else
		pd->power_on(NULL, 0);

	dev_info(mdnie->dev, "-%s\n", __func__);
#endif
}


static struct platform_driver mdnie_driver = {
	.driver		= {
		.name	= "mdnie",
		.owner	= THIS_MODULE,
	},
	.probe		= mdnie_probe,
	.remove		= mdnie_remove,
#ifndef CONFIG_HAS_EARLYSUSPEND
	.suspend	= mdnie_suspend,
	.resume		= mdnie_resume,
#endif
	.shutdown	= mdnie_shutdown,
};

static int __init mdnie_init(void)
{
	return platform_driver_register(&mdnie_driver);
}
module_init(mdnie_init);

static void __exit mdnie_exit(void)
{
	platform_driver_unregister(&mdnie_driver);
}
module_exit(mdnie_exit);

MODULE_DESCRIPTION("mDNIe Driver");
MODULE_LICENSE("GPL");
