/*
 * ISL79985 - Renesas Video Decoder Driver
 * Slash.Huang <slash.huang@dfi.com>, <slash.linux.c@gmail.com>
 * Copyright 2021 DFI, Inc. and/or its affiliates.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed .as is. WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/videodev2.h>
#include <linux/ioctl.h>
#include <linux/slab.h>
#include <linux/of_graph.h>
#include <linux/videodev2.h>
#include <linux/v4l2-dv-timings.h>
#include <linux/mutex.h>
#include <linux/gpio/consumer.h>
#include <media/v4l2-dv-timings.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>

#define V4L2_CID_ISL79985_BASE (V4L2_CID_USER_BASE + 0xC000)
#define V4L2_CID_ISL79985_CH_SWITCH (V4L2_CID_ISL79985_BASE + 0)

/* PAGE 0 */
#define ISL79985_REG_CHIP_ID 0x00
#define CHIP_ID_79985 0x85

#define VIDEO_INPUT_CHANNEL_CTL 0x07
#define ISL79985_PRI_CH1 0x10
#define ISL79985_PRI_CH2 0x04
#define ISL79985_REG_PAGE 0xFF

enum {
	REG_PAGE_0 = 0,
	REG_PAGE_1,
	REG_PAGE_2,
	REG_PAGE_3,
	REG_PAGE_4,
	REG_PAGE_5,
	REG_PAGE_INIT,
	REG_PAGE_1_4 = 0xFF,
};

enum {
	ISL79985_CH1 = 1,
	ISL79985_CH2,
	ISL79985_CH3,
	ISL79985_CH4,
};

/* Page 1 - 4 */
#define ISL79989_REG_DECODER_SAT1 0x03
#define DECODER_VDLOSS (1 << 7)
#define DECODER_HLOCK (1 << 6)
#define DECODER_SLOCK (1 << 5)
#define DECODER_FIELD (1 << 4)
#define DECODER_VLOCK (1 << 3)
#define DECODER_MONO (1 << 1)
#define DECODER_DET50 (1 << 0)

#define ISL79985_REG_CH1_SAT 0x1B
#define ISL79985_REG_CH2_SAT 0x1C
#define ISL79985_REG_CH3_SAT 0x1D
#define ISL79985_REG_CH4_SAT 0x1E
#define VDLOSS_STS (1 << 3)
#define CH_DET50_STS (1 << 2)
#define SLOCK_STS (1 << 1)
#define HVLOCK_STS (1 << 0)

#define ISL79985_REG_STD_SEL 0x1C
#define STDNOW_MASK 0x07

#define ISL79989_REG_MISC3 0x2F
#define NTSC_COLOR_KILLER (1 << 7)

#define ISL79989_REG_DATA_CONVER 0x3D
#define YUV422 0
#define RGB565 1

#define ISL79985_REG_ACNTL 0x06
#define ISL79985_REG_BRIGHTNESS 0x10
#define ISL79985_REG_CONTRAST 0x11
#define ISL79985_REG_SHARPNESS 0x12
#define ISL79985_REG_CHROMA_U 0x13
#define ISL79985_REG_CHROMA_V 0x14
#define ISL79985_REG_HUE 0x15
#define ISL79985_REG_IAGC 0x21
#define ISL79985_REG_AGCGAIN 0x22
#define ISL79985_REG_COLOR_KILLER_LEVEL 0x2A

/* Page 5 */
#define ISL79985_REG_MIPI_ANALOG 0x35
#define ISL79985_REG_TEST_PATTERN_EN 0x0D
#define TEST_PATTERN_DISABLED 0
#define TEST_PATTERN_YELLOW 1
#define TEST_PATTERN_BLUE 2
#define TEST_PATTERN_GREEN 3
#define TEST_PATTERN_PINK 4

enum {
	STDNOW_NTSC_M=0,
	STDNOW_PAL_BDGHI,
	STDNOW_SECAM,
	STDNOW_NTSC_443,
	STDNOW_PAL_M,
	STDNOW_PAL_CN,
	STDNOW_PAL_60,
	STDNOW_AUTO_DET_NA,
};

struct isl79985_ctrls {
	struct v4l2_ctrl_handler handler;
	struct v4l2_ctrl *auto_gain;
	struct v4l2_ctrl *brightness;
	struct v4l2_ctrl *contrast;
	struct v4l2_ctrl *sharpness;
	struct v4l2_ctrl *saturation;
	struct v4l2_ctrl *hue;
	struct v4l2_ctrl *gain;
	struct v4l2_ctrl *color_kill;
	struct v4l2_ctrl *test_pattern;
};

struct isl79985 {
	dev_t isl79985_devt;
	struct v4l2_subdev sd;
	struct i2c_client *i2c_client;
	struct v4l2_dv_timings timings;
	struct v4l2_fwnode_bus_mipi_csi2 bus;
	struct v4l2_mbus_framefmt fmt;
	struct isl79985_ctrls ctrls;
	struct media_pad pad;
	struct mutex mutex;
	struct gpio_desc *reset_gpio;
	u32 mbus_fmt_code;
	u32 refclk_hz;
	u32 slave_addr;
	u32 csi_lanes;
	int channel;
	u8 input;
	u8 cur_page;
	int norm;
	int streaming;
};

struct isl79985 *state;

static inline struct isl79985 *sd_to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct isl79985, sd);
}

static inline struct v4l2_subdev *ctrl_to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler,
		struct isl79985, ctrls.handler)->sd;
}

static int write_reg(struct i2c_client *client, u8 reg, u8 value)
{
	int ret;

	ret = i2c_smbus_write_byte_data(client, reg, value);
	if (ret < 0)
		dev_err(&client->dev, "isl79985 %s fail\n", __func__);

	usleep_range(100, 150);
	return ret;
}

static int read_reg(struct i2c_client *client, u8 reg)
{
	int ret;

	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0)
		dev_err(&client->dev, "isl79985 %s fail\n", __func__);

	return ret;
}

static int isl79985_change_page(struct i2c_client *client, u8 page)
{
	struct isl79985 *state;

	state = i2c_get_clientdata(client);

	if (state->cur_page == page)
		return 0;

	if (page > REG_PAGE_5 && page != REG_PAGE_1_4) {
		dev_err(&client->dev, "isl79985 page select fail(0x%x)\n", page);
		return -EINVAL;
	}

	state->cur_page = page;
	return write_reg(client, ISL79985_REG_PAGE, page);
}

int isl79985_ch_switch(struct i2c_client *client, u8 change_ch)
{
	struct isl79985 *state = i2c_get_clientdata(client);
	int tmp;

	mutex_lock(&state->mutex);
	isl79985_change_page(client, REG_PAGE_0);

	if (change_ch == ISL79985_CH1) {
		state->channel = ISL79985_CH1;
		tmp = ISL79985_PRI_CH1;
	} else if (change_ch == ISL79985_CH2) {
		state->channel = ISL79985_CH1;
		tmp = ISL79985_PRI_CH2;
	} else {
		mutex_unlock(&state->mutex);
		return -EINVAL;
	}

	tmp = write_reg(client, VIDEO_INPUT_CHANNEL_CTL, tmp);
	mutex_unlock(&state->mutex);
	return tmp;
}

static ssize_t isl79985_channel_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct isl79985 *state = i2c_get_clientdata(client);
	int channel;
	int len = 2;

	if (state == NULL)
		return -EINVAL;

	mutex_lock(&state->mutex);

	isl79985_change_page(client, REG_PAGE_0);
	channel = read_reg(client, VIDEO_INPUT_CHANNEL_CTL);

	switch (channel) {
		case ISL79985_PRI_CH1:
			channel = 1;
			break;
		case ISL79985_PRI_CH2:
			channel = 2;
			break;
		default:
			dev_err(&client->dev, "show channel fail\n");
			len = -EINVAL;
	}

	sprintf(buf, "%d\n", channel);
	mutex_unlock(&state->mutex);

	return len;
}

static ssize_t isl79985_channel_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t len)
{
	struct i2c_client *client = to_i2c_client(dev);
	ssize_t ret;
	long value;

	if (len > 2)
		return -EINVAL;

	ret = kstrtol(buf, 0, &value);
	if (ret)
		return -EINVAL;

	if (value != ISL79985_CH1 && value != ISL79985_CH2)
		return -EINVAL;

	dev_info(&client->dev, "%s value %ld\n", __func__, value);
	isl79985_ch_switch(client, value);

	return len;
}

/* sysfs attributes */
static DEVICE_ATTR_RW(isl79985_channel);

static int isl79985_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = ctrl_to_sd(ctrl);
	struct isl79985 *state = sd_to_state(sd);
	struct i2c_client *client = state->i2c_client;
	int sat_v, sat_u;
	int agc_8, agc_0_7;
	int reg, page;

	dev_info(&client->dev, "%s ctrl->id 0x%x\n", __func__, ctrl->id);

	if (state->channel == ISL79985_CH1)
		page = REG_PAGE_1;
	else if (state->channel == ISL79985_CH2)
		page = REG_PAGE_2;
	else {
		page = state->channel;
		dev_err(&client->dev, "%s page fail(0x%x)\n", __func__, page);
		return -EINVAL;
	}

	isl79985_change_page(client, page);

	switch (ctrl->id) {
	case V4L2_CID_GAIN:
		reg = read_reg(client, ISL79985_REG_IAGC);
		agc_8 =  (0x01 & read_reg(client, ISL79985_REG_IAGC));
		agc_0_7 = (0x0F & read_reg(client, ISL79985_REG_AGCGAIN));
		ctrl->val = ((agc_8 << 8) | agc_0_7);
		return 0;

	case V4L2_CID_COLOR_KILLER:
		ctrl->val = (read_reg(client, ISL79989_REG_MISC3) & NTSC_COLOR_KILLER);
		ctrl->val = ctrl->val >> 7;
		return 0;

	case V4L2_CID_AUTOGAIN:
		reg = read_reg(client, ISL79985_REG_ACNTL);
		ctrl->val = reg >> 4;
		return 0;

	case V4L2_CID_BRIGHTNESS:
		ctrl->val = read_reg(client, ISL79985_REG_BRIGHTNESS);
		return 0;

	case V4L2_CID_CONTRAST:
		ctrl->val = read_reg(client, ISL79985_REG_CONTRAST);
		return 0;

	case V4L2_CID_SATURATION:
		sat_u = read_reg(client, ISL79985_REG_CHROMA_U);
		sat_v = read_reg(client, ISL79985_REG_CHROMA_V);
		ctrl->val = ((sat_v << 8) | sat_u);
		return 0;

	case V4L2_CID_HUE:
		ctrl->val = read_reg(client, ISL79985_REG_HUE);
		return 0;

	case V4L2_CID_SHARPNESS:
		ctrl->val = read_reg(client, ISL79985_REG_SHARPNESS);
		return 0;

	default:
		break;
	}

	return -EINVAL;
}

static int isl79985_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = ctrl_to_sd(ctrl);
	struct isl79985 *state = sd_to_state(sd);
	struct i2c_client *client = state->i2c_client;
	int tmp;
	int sat_v, sat_u;
	int agc_8, agc_0_7;
	int reg, page;

	if (state->channel == ISL79985_CH1)
		page = REG_PAGE_1;
	else if (state->channel == ISL79985_CH2)
		page = REG_PAGE_2;
	else {
		page = state->channel;
		dev_err(&client->dev, "%s page fail(0x%x)\n", __func__, page);
		return -EINVAL;
	}

	isl79985_change_page(client, page);

	switch (ctrl->id) {
	case V4L2_CID_GAIN:
		agc_8 = (ctrl->val & 0x100) >> 8;
		agc_0_7 = ctrl->val & 0xFF;
		reg = read_reg(client, ISL79985_REG_IAGC);
		reg = reg & ~(0x01);
		reg = reg | agc_8;
		write_reg(client, ISL79985_REG_IAGC, reg);
		write_reg(client, ISL79985_REG_AGCGAIN, agc_0_7);
		return 0;

	case V4L2_CID_COLOR_KILLER:
		tmp = (read_reg(client, ISL79989_REG_MISC3) & ~NTSC_COLOR_KILLER);
		tmp |= ctrl->val << 7;
		return write_reg(client, ISL79989_REG_MISC3, tmp);

	case V4L2_CID_AUTOGAIN:
		return write_reg(client, ISL79985_REG_ACNTL, ctrl->val << 4);

	case V4L2_CID_BRIGHTNESS:
		return write_reg(client, ISL79985_REG_BRIGHTNESS, ctrl->val);

	case V4L2_CID_CONTRAST:
		return write_reg(client, ISL79985_REG_CONTRAST, ctrl->val);

	case V4L2_CID_SATURATION:
		sat_u = ctrl->val & 0xF;
		sat_v = (ctrl->val & 0xF0) >> 8;
		write_reg(client, ISL79985_REG_CHROMA_U, sat_u);
		write_reg(client, ISL79985_REG_CHROMA_V, sat_v);
		return 0;

	case V4L2_CID_HUE:
		return write_reg(client, ISL79985_REG_HUE, ctrl->val);

	case V4L2_CID_SHARPNESS:
		reg = read_reg(client, ISL79985_REG_SHARPNESS);
		reg = reg & ~(0x0F);
		reg |= ctrl->val;
		return write_reg(client, ISL79985_REG_SHARPNESS, reg);

	case V4L2_CID_TEST_PATTERN:
		isl79985_change_page(client, REG_PAGE_5);
		tmp = read_reg(client, ISL79985_REG_TEST_PATTERN_EN);
		tmp |= 0xF0;

		switch (ctrl->val) {
		case TEST_PATTERN_DISABLED:
			tmp &= ~0xF0;
			break;

		case TEST_PATTERN_YELLOW:
			tmp &= ~(0x3 << 2);
			break;

		case TEST_PATTERN_BLUE:
			tmp |= (0x1 << 2);
			break;

		case TEST_PATTERN_GREEN:
			tmp |= (0x2 << 2);
			break;

		case TEST_PATTERN_PINK:
			tmp |= (0x3 << 2);
			break;
		}
		write_reg(client, ISL79985_REG_TEST_PATTERN_EN, tmp);
		isl79985_change_page(client, page);
		return 0;

	case V4L2_CID_ISL79985_CH_SWITCH:
		isl79985_ch_switch(client, ctrl->val);
		return 0;

	default:
		break;
	}

	return -EINVAL;
}

static void isl79985_reset(struct isl79985 *state)
{
	if (!state->reset_gpio) {
		pr_err("%s not assigned reset pin\n", __func__);
		return;
	}

	gpiod_set_value_cansleep(state->reset_gpio, 1);
	usleep_range(1000, 2000);

	gpiod_set_value_cansleep(state->reset_gpio, 0);
	usleep_range(1000, 2000);

	gpiod_set_value_cansleep(state->reset_gpio, 1);
	usleep_range(20000, 25000);
}

static int isl79985_s_power(struct v4l2_subdev *sd, int on)
{
	struct isl79985 *state = sd_to_state(sd);
	struct i2c_client *client = state->i2c_client;

	mutex_lock(&state->mutex);
	if (on == 1) {
		isl79985_change_page(client, REG_PAGE_5);
		write_reg(client, 0x34, 0x18);
		write_reg(client, ISL79985_REG_MIPI_ANALOG, 0x00);
	}
	else {
		isl79985_change_page(client, REG_PAGE_5);
		write_reg(client, 0x34, 0x06);
		write_reg(client, ISL79985_REG_MIPI_ANALOG, 0x0F);
	}
	mutex_unlock(&state->mutex);

	return 0;
}

static int no_signal(struct v4l2_subdev *sd)
{
	struct isl79985 *state = sd_to_state(sd);
	struct i2c_client *i2cdev = state->i2c_client;
	int ret, reg_ch;

	reg_ch = ISL79985_REG_CH1_SAT + state->channel - 1;
	ret = read_reg(i2cdev, reg_ch);
	ret = ret & ~(CH_DET50_STS);

	if (ret & (VDLOSS_STS | SLOCK_STS | HVLOCK_STS))
		return V4L2_IN_ST_NO_SIGNAL;

	return 0;
}

static int no_sync(struct v4l2_subdev *sd)
{
	struct isl79985 *state = sd_to_state(sd);
	struct i2c_client *client = state->i2c_client;
	int ret;

	isl79985_change_page(client, state->channel);
	ret = read_reg(client, ISL79989_REG_DECODER_SAT1);
	ret = ret & ~(DECODER_DET50 | DECODER_MONO | DECODER_FIELD | DECODER_SLOCK);
	if (ret & (DECODER_VDLOSS | DECODER_HLOCK | DECODER_VLOCK))
		return V4L2_IN_ST_NO_SYNC;

	return 0;
}

static int isl79985_g_input_status(struct v4l2_subdev *sd, u32 *status)
{
	*status = 0;
	*status = no_signal(sd);
	*status |= no_sync(sd) ? V4L2_IN_ST_NO_SYNC : 0;

	return 0;
}

static int isl79985_g_mbus_config(struct v4l2_subdev *sd,
			     struct v4l2_mbus_config *cfg)
{
	struct isl79985 *state = sd_to_state(sd);

	cfg->type = V4L2_MBUS_CSI2_DPHY;
	cfg->flags = V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;

	switch (state->csi_lanes) {
	case 1:
		cfg->flags |= V4L2_MBUS_CSI2_1_LANE;
		break;
	case 2:
		cfg->flags |= V4L2_MBUS_CSI2_2_LANE;
		break;
	case 3:
	case 4:
	default:
		return -EINVAL;
	}

	return 0;
}

static int isl79985_s_stream(struct v4l2_subdev *sd, int enable)
{
	return 0;
}

static int isl79985_g_std(struct v4l2_subdev *sd, v4l2_std_id *norm)
{
	struct isl79985 *state = sd_to_state(sd);

	*norm = state->norm;
	return 0;
}

static int isl79985_s_std(struct v4l2_subdev *sd, v4l2_std_id norm)
{
	struct isl79985 *state = sd_to_state(sd);
	struct i2c_client *client = state->i2c_client;
	v4l2_std_id g_norm = norm;
	u8 w_reg = STDNOW_NTSC_M;

	if (g_norm & V4L2_STD_525_60) {
		if (g_norm & V4L2_STD_NTSC_M) {
			w_reg = STDNOW_NTSC_M;
			state->norm = V4L2_STD_NTSC_M;
		} else if (g_norm & V4L2_STD_NTSC_443) {
			w_reg = STDNOW_NTSC_443;
			state->norm = V4L2_STD_NTSC_443;
		}
	} else {
		w_reg = STDNOW_NTSC_M;
		state->norm = V4L2_STD_NTSC_M;
	}

	isl79985_change_page(client, state->channel);
	return write_reg(client, ISL79985_REG_STD_SEL, w_reg);
}

static int isl79985_g_tvnorms(struct v4l2_subdev *sd, v4l2_std_id *norm)
{
	struct isl79985 *state = sd_to_state(sd);
	struct i2c_client *client = state->i2c_client;
	u32 r_reg;

	isl79985_change_page(client, state->channel);
	r_reg = read_reg(client, ISL79985_REG_STD_SEL);
	r_reg = (r_reg >> 4) & STDNOW_MASK;

	switch (r_reg) {
	case STDNOW_NTSC_M:
		*norm = V4L2_STD_NTSC;
		break;

	case STDNOW_PAL_BDGHI:
		*norm = V4L2_STD_PAL;
		break;

	case STDNOW_SECAM:
		*norm = V4L2_STD_SECAM;
		break;

	case STDNOW_NTSC_443:
		*norm = V4L2_STD_NTSC_443;
		break;

	case STDNOW_PAL_M:
		*norm = V4L2_STD_PAL_M;
		break;

	case STDNOW_PAL_CN:
		*norm = V4L2_STD_PAL_Nc;
		break;

	case STDNOW_PAL_60:
		*norm = V4L2_STD_PAL_60;
		break;

	case STDNOW_AUTO_DET_NA:
	default:
		dev_info(&client->dev, "isl79985 STDNOW_NA\n");
		*norm = V4L2_STD_NTSC;
		break;
	}

	if (state->norm != *norm) {
		dev_info(&client->dev, "state->norm different isl79985\n");
		state->norm = *norm;
	}

	return 0;
}

static const struct v4l2_ctrl_ops isl79985_ctrl_ops = {
	.g_volatile_ctrl = isl79985_g_volatile_ctrl,
	.s_ctrl = isl79985_s_ctrl,
};

static const struct v4l2_subdev_video_ops isl79985_video_ops = {
	.s_std = isl79985_s_std,
	.g_std = isl79985_g_std,
	.g_input_status = isl79985_g_input_status,
	.g_mbus_config = isl79985_g_mbus_config,
	.s_stream = isl79985_s_stream,
	.g_tvnorms = isl79985_g_tvnorms,
};

static const struct v4l2_subdev_core_ops isl79985_core_ops = {
	.s_power = isl79985_s_power,
};

static int isl79985_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *format)
{
	struct isl79985 *state = sd_to_state(sd);
	struct v4l2_mbus_framefmt *fmt;

	if (format->pad != 0)
		return -EINVAL;

	mutex_lock(&state->mutex);

	fmt = &state->fmt;
	format->format = *fmt;

	mutex_unlock(&state->mutex);

	return 0;
}

static int isl79985_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *format)
{
	return 0;
}

static const struct v4l2_subdev_pad_ops isl79985_pad_ops = {
	.get_fmt = isl79985_get_fmt,
	.set_fmt = isl79985_set_fmt,
};

static const struct v4l2_subdev_ops isl79985_ops = {
	.core = &isl79985_core_ops,
	.video = &isl79985_video_ops,
	.pad = &isl79985_pad_ops,
};

static int isl79985_hardware_init(struct isl79985 *state)
{
	struct i2c_client *client = state->i2c_client;

	dev_info(&client->dev, "%s\n", __func__);

	/* Page 0 */
	isl79985_change_page(client, REG_PAGE_0);
	write_reg(client, 0x03, 0x00);
	write_reg(client, 0x07, 0x10);

	if (state->csi_lanes == 1)
		write_reg(client, 0x0B, 0x40);
	else
		write_reg(client, 0x0B, 0x41);

	write_reg(client, 0x07, ISL79985_PRI_CH1);

	write_reg(client, 0x09, 0x43);
	write_reg(client, 0x0A, 0x00);
	write_reg(client, 0x0D, 0xC9);
	write_reg(client, 0x0E, 0xC9);
	write_reg(client, 0x10, 0x01);
	write_reg(client, 0x11, 0x03);

	/* Page 1-4 */
	isl79985_change_page(client, REG_PAGE_1_4);
	write_reg(client, 0x0A, 0x13);
	write_reg(client, 0x09, 0xF0);
	write_reg(client, 0x07, 0x02);

	write_reg(client, 0x1C, 0x00);
	write_reg(client, 0x2F, 0xE6);
	write_reg(client, 0x33, 0x85);

	/* Page 5 */
	isl79985_change_page(client, REG_PAGE_5);
	write_reg(client, 0x03, 0x08);
	write_reg(client, 0x05, 0x00);
	write_reg(client, 0x06, 0x00);
	write_reg(client, 0x07, 0x46);
	write_reg(client, 0x08, 0x02);
	write_reg(client, 0x09, 0x00);
	write_reg(client, 0x0A, 0x68);
	write_reg(client, 0x0C, 0x00);
	write_reg(client, 0x0D, 0x06);
	write_reg(client, 0x0E, 0x00);
	write_reg(client, 0x12, 0x76);
	write_reg(client, 0x13, 0x2F);
	write_reg(client, 0x14, 0x0E);
	write_reg(client, 0x15, 0x36);
	write_reg(client, 0x16, 0x12);
	write_reg(client, 0x17, 0xF6);
	write_reg(client, 0x19, 0x17);
	write_reg(client, 0x1A, 0x0A);
	write_reg(client, 0x1B, 0x61);
	write_reg(client, 0x1C, 0x7A);
	write_reg(client, 0x1D, 0x0F);
	write_reg(client, 0x1E, 0x8C);
	write_reg(client, 0x1F, 0x02);
	write_reg(client, 0x21, 0x0C);
	write_reg(client, 0x23, 0x00);
	write_reg(client, 0x25, 0xF0);
	write_reg(client, 0x26, 0x00);
	write_reg(client, 0x27, 0x00);
	write_reg(client, 0x28, 0x01);
	write_reg(client, 0x29, 0x0E);
	write_reg(client, 0x2B, 0x19);
	write_reg(client, 0x2C, 0x18);
	write_reg(client, 0x2D, 0xF1);
	write_reg(client, 0x2E, 0x00);
	write_reg(client, 0x2F, 0xF1);

	if (state->csi_lanes == 1)
		write_reg(client, 0x00, 0x01);
	else
		write_reg(client, 0x00, 0x02);

	msleep(1);
	return 0;
}

static int isl79985_probe_of(struct isl79985 *state)
{
	struct device *dev = &state->i2c_client->dev;
	struct v4l2_fwnode_endpoint endpoint = { .bus_type = 0 };
	struct device_node *ep;
	int ret;

	state->slave_addr = state->i2c_client->addr;
	dev_info(dev, "i2c_client->addr : 0x%x\n", state->i2c_client->addr);

	ep = of_graph_get_next_endpoint(dev->of_node, NULL);
	if (!ep) {
		dev_err(dev, "missing endpoint node\n");
		return -EINVAL;
	}

	pr_info("ep->full_name %s\n", ep->full_name);

	if (of_property_read_u32(ep, "data-lanes", &state->csi_lanes)) {
		dev_err(dev, "data-lanes fail in dts\n");
		return -EINVAL;
	}

	if (!state->csi_lanes || state->csi_lanes > 2) {
		dev_err(dev, "data-lanes fail %d\n", state->csi_lanes);
		return -EINVAL;
	}

	ret = v4l2_fwnode_endpoint_alloc_parse(of_fwnode_handle(ep), &endpoint);
	if (ret) {
		dev_err(dev, "failed to parse endpoint\n");
		goto put_node;
	}

	if (endpoint.bus_type != V4L2_MBUS_CSI2_DPHY ||
		endpoint.bus.mipi_csi2.num_data_lanes == 0) {
		dev_err(dev, "missing CSI-2 properties in endpoint\n");
		ret = -EINVAL;
		goto free_endpoint;
	}

	if (endpoint.bus.mipi_csi2.num_data_lanes > 2) {
		dev_err(dev, "invalid number of lanes\n");
		ret = -EINVAL;
		goto free_endpoint;
	}

	state->bus = endpoint.bus.mipi_csi2;

	/* request optional reset pin */
	state->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						     GPIOD_OUT_HIGH);
	if (IS_ERR(state->reset_gpio)) {
		ret = -EINVAL;
		goto free_endpoint;
	}

	ret = 0;

free_endpoint:
	v4l2_fwnode_endpoint_free(&endpoint);

put_node:
	of_node_put(ep);

	return ret;
}

static int isl79985_link_setup(struct media_entity *entity,
			   const struct media_pad *local,
			   const struct media_pad *remote, u32 flags)
{
	return 0;
}

static const struct media_entity_operations isl79985_sd_media_ops = {
	.link_setup = isl79985_link_setup,
};

static const struct v4l2_ctrl_config isl79985_ch_sw = {
	.ops = &isl79985_ctrl_ops,
	.id = V4L2_CID_ISL79985_CH_SWITCH,
	.name = "Channel Switching",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.flags = V4L2_CTRL_FLAG_SLIDER,
	.def = 1,
	.min = 1,
	.max = 2,
	.step = 1,
};

static const char *const test_patterns[] = {
	"Disabled",
	"top screen yellow",
	"top screen blue",
	"top screen green",
	"top screen pink",
};

static int isl79985_init_controls(struct isl79985 *state)
{
	const struct v4l2_ctrl_ops *ops = &isl79985_ctrl_ops;
	struct isl79985_ctrls *ctrls = &state->ctrls;
	struct v4l2_ctrl_handler *hdl = &ctrls->handler;
	int ret;

	v4l2_ctrl_handler_init(hdl, 32);
	hdl->lock = &state->mutex;

	ctrls->brightness = v4l2_ctrl_new_std(hdl, ops,
					   V4L2_CID_BRIGHTNESS,
					   -128, 127, 1, 0);

	ctrls->auto_gain = v4l2_ctrl_new_std(hdl, ops,
					   V4L2_CID_AUTOGAIN,
					   0, 1, 1, 1);

	ctrls->gain = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_GAIN,
						0, 511, 1, 0xF0);


	ctrls->contrast = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_CONTRAST,
					       0, 255, 1, 0x64);

	ctrls->sharpness = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_SHARPNESS,
					    0, 16, 1, 1);

	ctrls->saturation = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_SATURATION,
					     0, 65535, 1, 0x8080);

	ctrls->hue = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_HUE,
					0, 256, 1, 0);

	ctrls->color_kill = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_COLOR_KILLER,
					      0, 1, 1, 1);

	ctrls->test_pattern =
		v4l2_ctrl_new_std_menu_items(hdl, ops, V4L2_CID_TEST_PATTERN,
						ARRAY_SIZE(test_patterns) - 1,
					     0, 0, test_patterns);

	v4l2_ctrl_new_custom(hdl, &isl79985_ch_sw, NULL);

	if (hdl->error) {
		pr_err("%s v4l2_ctrl fail\n", __func__);
		ret = hdl->error;
		goto free_ctrls;
	}

	state->sd.ctrl_handler = hdl;
	return 0;

free_ctrls:
	v4l2_ctrl_handler_free(hdl);
	return ret;
}

static int isl79985_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = client->adapter;
	struct v4l2_subdev *sd;
	int err;
	struct v4l2_mbus_framefmt *fmt;

	dev_info(&client->dev, "%s\n", __func__);

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	state = devm_kzalloc(&client->dev, sizeof(*state), GFP_KERNEL);
	if (state == NULL) {
		dev_err(&client->dev, "%s devm_kzalloc fail\n", __func__);
		return -ENOMEM;
	}

	state->i2c_client = client;
	i2c_set_clientdata(client, state);
	mutex_init(&state->mutex);
	state->cur_page = REG_PAGE_INIT;

	err = isl79985_probe_of(state);
	if (err) {
		dev_err(&client->dev, "isl79985_probe_of fail\n");
		goto err_hdl2;
	}

	isl79985_reset(state);

	isl79985_change_page(client, REG_PAGE_0);
	if (read_reg(client, ISL79985_REG_CHIP_ID) != CHIP_ID_79985) {
		dev_err(&client->dev, "not a isl79985 device\n");
		return -ENODEV;
	}

	err = isl79985_hardware_init(state);
	if (err) {
		dev_err(&client->dev, "isl79985_hardware_init fail\n");
		goto err_hdl2;
	}

	fmt = &state->fmt;
	fmt->code = MEDIA_BUS_FMT_YUYV8_2X8;
	fmt->colorspace = V4L2_COLORSPACE_SRGB;
	fmt->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(fmt->colorspace);
	fmt->quantization = V4L2_QUANTIZATION_FULL_RANGE;
	fmt->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(fmt->colorspace);
	fmt->width = 720;
	fmt->height = 240;

	fmt->field = V4L2_FIELD_INTERLACED;

	state->mbus_fmt_code = MEDIA_BUS_FMT_UYVY8_2X8;
	state->channel = ISL79985_CH1;
	state->norm = V4L2_STD_NTSC;

	sd = &state->sd;
	v4l2_i2c_subdev_init(sd, client, &isl79985_ops);
	sd->dev = &client->dev;

	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
	state->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	sd->entity.ops = &isl79985_sd_media_ops;
	err = media_entity_pads_init(&sd->entity, 1, &state->pad);
	if (err < 0) {
		dev_err(&client->dev, "media_entity_pads_init fail\n");
		goto err_hdl2;
	}

	err = v4l2_async_register_subdev_sensor_common(sd);
	if (err < 0) {
		dev_err(&client->dev, "v4l2_async_register_subdev fail\n");
		goto err_hdl2;
	}

	err = isl79985_init_controls(state);
	if (err)
		dev_err(&client->dev, "isl79985_init_controls fail\n");

	if (device_create_file(&client->dev, &dev_attr_isl79985_channel) != 0) {
		dev_err(&client->dev, "sysfs ident entry creation failed\n");
		goto err_hdl1;
	}

	return 0;

err_hdl1:
	v4l2_device_unregister_subdev(&state->sd);

err_hdl2:
	return -EINVAL;
}

static int isl79985_remove(struct i2c_client *client)
{
	struct isl79985 *state = i2c_get_clientdata(client);

	v4l2_device_unregister_subdev(&state->sd);
	return 0;
}

static const struct i2c_device_id isl79985_id[] = {
	{ "isl79985", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, isl79985_id);

static struct i2c_driver isl79985_driver = {
	.driver = {
		.name	= "isl79985",
	},
	.probe		= isl79985_probe,
	.remove		= isl79985_remove,
	.id_table	= isl79985_id,
};

module_i2c_driver(isl79985_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ISL79985 V4L2 Driver");
MODULE_AUTHOR("slash.linux.c@gmail.com");
