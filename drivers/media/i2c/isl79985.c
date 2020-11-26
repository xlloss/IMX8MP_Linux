/*
 * ISL79985 - Renesas Video Decoder driver
 * Slash <slash.huang@dfi.com>
 * Copyright 2013 Cisco Systems, Inc. and/or its affiliates.
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
#include <linux/clk.h>
#include <media/v4l2-dv-timings.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <linux/mutex.h>


/* PAGE 0 */
#define ISL79985_REG_CHIP_ID 0x00
#define CHIP_ID_79985 0x85

#define ISL79985_REG_PAGE 0xFF

enum {
	REG_PAGE_0 = 0,
	REG_PAGE_1,
	REG_PAGE_2,
	REG_PAGE_3,
	REG_PAGE_4,
	REG_PAGE_5,
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
/* struct v4l2_ctrl *light_freq; */
};

/* Page 5 */
#define ISL79985_REG_MIPI_ANALOG 0x35
#define ISL79985_REG_TEST_PATTERN_EN 0x0D


struct isl79985 {
	struct v4l2_subdev sd;
	struct i2c_client *i2c_client;
	struct v4l2_dv_timings timings;
	struct v4l2_fwnode_bus_mipi_csi2 bus;
	struct isl79985_ctrls ctrls;
	struct media_pad pad;
	struct mutex mutex;
	u32 mbus_fmt_code;
	u32 refclk_hz;
	u32 slave_addr;
	u32 csi_lanes;
	u8 channel;
	u8 input;
	int cur_page;
	int norm;
	int streaming;
};

enum {
	ISL79985_CH1 = 1,
	ISL79985_CH2,
	ISL79985_CH3,
	ISL79985_CH4,
};

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
	/*
	u32 tmpaddr ;
	int ret;


	struct isl79985 *priv = i2c_get_clientdata(client);

	tmpaddr = client->addr;
	client->addr = priv->slave_addr;

	ret = i2c_smbus_write_byte_data(client, reg, value);
	client->addr = tmpaddr;
	return ret;
	*/
	return 0;
}

static int read_reg(struct i2c_client *client, u8 reg)
{
	/*
	u32 tmpaddr ;
	int ret;


	struct isl79985 *priv = i2c_get_clientdata(client);

	tmpaddr = client->addr;
	client->addr = priv->slave_addr;
	ret = i2c_smbus_read_byte_data(client, reg);
	client->addr = tmpaddr;
	return ret;
	*/
	return 0;
}

static int isl79985_change_page(struct isl79985 *state, u8 page)
{
	struct i2c_client *client = state->i2c_client;

	if (state->cur_page == page)
		return 0;

	if (page < 0 || page > 5) {
		pr_err("ISL79985 page select fail\n");
		return -EINVAL;
	}

	state->cur_page = page;
	return write_reg(client, ISL79985_REG_PAGE, page);
}

static int isl79985_log_status(struct v4l2_subdev *sd)
{
	return v4l2_ctrl_subdev_log_status(sd);
}

/*
 * These volatile controls are needed because all four channels share
 * these controls. So a change made to them through one channel would
 * require another channel to be updated.
 *
 * Normally this would have been done in a different way, but since the one
 * board that uses this driver sees this single chip as if it was on four
 * different i2c adapters (each adapter belonging to a separate instance of
 * the same USB driver) there is no reliable method that I have found to let
 * the instances know about each other.
 *
 * So implementing these global registers as volatile is the best we can do.
 */

static int isl79985_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = ctrl_to_sd(ctrl);
	struct isl79985 *state = sd_to_state(sd);
	struct i2c_client *client = state->i2c_client;
	int sat_v, sat_u;
	int agc_8, agc_0_7;
	int reg;

	pr_info("%s ctrl->id 0x%x\n", __func__, ctrl->id);

	isl79985_change_page(state, state->channel);

	switch (ctrl->id) {
	case V4L2_CID_GAIN:
		pr_info("%s ID V4L2_CID_AUTOGAIN\n", __func__);
		reg = read_reg(client, ISL79985_REG_IAGC);
		agc_8 =  (0x01 & read_reg(client, ISL79985_REG_IAGC));
		agc_0_7 = (0x0F & read_reg(client, ISL79985_REG_AGCGAIN));
		ctrl->val = ((agc_8 << 8) | agc_0_7);
		return 0;

	case V4L2_CID_COLOR_KILLER:
		pr_info("%s ID V4L2_CID_COLOR_KILLER\n", __func__);
		ctrl->val = read_reg(client, ISL79985_REG_COLOR_KILLER_LEVEL);
		return 0;

	case V4L2_CID_AUTOGAIN:
		pr_info("%s ID V4L2_CID_GAIN\n", __func__);
		reg = read_reg(client, ISL79985_REG_ACNTL);
		ctrl->val = reg >> 4;
		return 0;

	case V4L2_CID_BRIGHTNESS:
		pr_info("%s ID V4L2_CID_BRIGHTNESS\n", __func__);
		ctrl->val = read_reg(client, ISL79985_REG_BRIGHTNESS);
		return 0;

	case V4L2_CID_CONTRAST:
		pr_info("%s ID V4L2_CID_CONTRAST\n", __func__);
		ctrl->val = read_reg(client, ISL79985_REG_CONTRAST);
		return 0;

	case V4L2_CID_SATURATION:
		pr_info("%s ID V4L2_CID_SATURATION\n", __func__);
		sat_u = read_reg(client, ISL79985_REG_CHROMA_U);
		sat_v = read_reg(client, ISL79985_REG_CHROMA_V);
		ctrl->val = ((sat_v << 8) | sat_u);
		return 0;

	case V4L2_CID_HUE:
		pr_info("%s ID V4L2_CID_HUE\n", __func__);
		ctrl->val = read_reg(client, ISL79985_REG_HUE);
		return 0;

	case V4L2_CID_SHARPNESS:
		pr_info("%s ID V4L2_CID_SHARPNESS\n", __func__);
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
	int ckilmax, ckilmin;
	int sat_v, sat_u;
	int sha_cti, sha_sharp;
	int agc_8, agc_0_7;
	int gen_color, test_pn;
	int reg;

	pr_info("%s ctrl->id 0x%x\n", __func__, ctrl->id);

	isl79985_change_page(state, state->channel);

	switch (ctrl->id) {
	case V4L2_CID_GAIN:
		pr_info("%s ID V4L2_CID_AUTOGAIN\n", __func__);
		agc_8 = (ctrl->val & 0x100) >> 8;
		agc_0_7 = ctrl->val & 0xFF;
		reg = read_reg(client, ISL79985_REG_IAGC);
		reg = reg & ~(0x01);
		reg = reg | agc_8;
		write_reg(client, ISL79985_REG_IAGC, reg);
		write_reg(client, ISL79985_REG_AGCGAIN, agc_0_7);
		return 0;

	case V4L2_CID_COLOR_KILLER:
		pr_info("%s ID V4L2_CID_COLOR_KILLER\n", __func__);
		ckilmax = ctrl->val & 0xC0;
		ckilmin = ctrl->val & 0x3F;

		return write_reg(client, ISL79985_REG_COLOR_KILLER_LEVEL,
							ckilmax | ckilmin);

	case V4L2_CID_AUTOGAIN:
		pr_info("%s ID V4L2_CID_GAIN\n", __func__);
		return write_reg(client, ISL79985_REG_ACNTL, ctrl->val << 4);

	case V4L2_CID_BRIGHTNESS:
		pr_info("%s ID V4L2_CID_BRIGHTNESS\n", __func__);
		return write_reg(client, ISL79985_REG_BRIGHTNESS, ctrl->val);

	case V4L2_CID_CONTRAST:
		pr_info("%s ID V4L2_CID_CONTRAST\n", __func__);
		return write_reg(client, ISL79985_REG_CONTRAST, ctrl->val);

	case V4L2_CID_SATURATION:
		pr_info("%s ID V4L2_CID_SATURATION\n", __func__);
		sat_u = ctrl->val & 0xF;
		sat_v = (ctrl->val & 0xF0) >> 8;
		write_reg(client, ISL79985_REG_CHROMA_U, sat_u);
		write_reg(client, ISL79985_REG_CHROMA_V, sat_v);
		return 0;

	case V4L2_CID_HUE:
		pr_info("%s ID V4L2_CID_HUE\n", __func__);
		return write_reg(client, ISL79985_REG_HUE, ctrl->val);

	case V4L2_CID_SHARPNESS:
		pr_info("%s ID V4L2_CID_SHARPNESS\n", __func__);
		sha_cti = ctrl->val & 0x30;
		sha_sharp = ctrl->val & 0x0F;

		reg = read_reg(client, ISL79985_REG_SHARPNESS);
		reg = reg & ~(0x3F);
		reg = sha_cti | sha_sharp;
		return write_reg(client, ISL79985_REG_SHARPNESS, reg);

	case V4L2_CID_TEST_PATTERN:
		pr_info("%s ID V4L2_CID_TEST_PATTERN\n", __func__);
		isl79985_change_page(state, REG_PAGE_5);
		test_pn = ctrl->val & 0xF0;
		gen_color = ctrl->val & 0x0C;
		return write_reg(client, ISL79985_REG_TEST_PATTERN_EN,
			test_pn | gen_color);

	default:
		break;
	}

	return -EINVAL;
}

static int isl79985_s_power(struct v4l2_subdev *sd, int on)
{
	struct isl79985 *state = sd_to_state(sd);
	struct i2c_client *client = state->i2c_client;
	int ret;

	pr_info("%s on %d\n", __func__, on);
	mutex_lock(&state->mutex);
	isl79985_change_page(state, REG_PAGE_5);
	if (on == 1)
		ret = write_reg(client, ISL79985_REG_MIPI_ANALOG, 0x0F);
	else
		ret = write_reg(client, ISL79985_REG_MIPI_ANALOG, 0x00);
	mutex_unlock(&state->mutex);
	return ret;
}

static int no_signal(struct v4l2_subdev *sd)
{
	struct isl79985 *state = sd_to_state(sd);
	struct i2c_client *i2cdev = state->i2c_client;
	int ret, reg_ch;

	pr_info("%s\n", __func__);

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

	pr_info("%s\n", __func__);
	isl79985_change_page(state, state->channel);
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

	pr_info("%s\n", __func__);

	cfg->type = V4L2_MBUS_CSI2_DPHY;

	/* Support for non-continuous CSI-2 clock is missing in the driver */
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
	pr_info("%s\n", __func__);
	return isl79985_s_power(sd, enable);
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
	u32 w_reg;

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

	isl79985_change_page(state, state->channel);
	return write_reg(client, ISL79985_REG_STD_SEL, w_reg);
}

static int isl79985_g_tvnorms(struct v4l2_subdev *sd, v4l2_std_id *norm)
{
	struct isl79985 *state = sd_to_state(sd);
	struct i2c_client *client = state->i2c_client;
	u32 r_reg;

	isl79985_change_page(state, state->channel);
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
		pr_info("ISL79985 STDNOW_NA\n");
		*norm = V4L2_STD_NTSC;
		break;
	}

	if (state->norm != *norm) {
		pr_warning("state->norm different ISL79985\n");
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
	.g_std		= isl79985_g_std,
	//.s_routing = isl79985_s_video_routing,
	.g_input_status = isl79985_g_input_status,
	.g_mbus_config = isl79985_g_mbus_config,
	.s_stream = isl79985_s_stream,
	.g_tvnorms = isl79985_g_tvnorms,
};

static const struct v4l2_subdev_core_ops isl79985_core_ops = {
	.s_power = isl79985_s_power,
	.log_status = isl79985_log_status,
};

static const struct v4l2_subdev_ops isl79985_ops = {
	.core = &isl79985_core_ops,
	.video = &isl79985_video_ops,
};

static int isl79985_hardware_init(struct isl79985 *state)
{
	struct i2c_client *client = state->i2c_client;

	/* Page 0 */
	isl79985_change_page(state, REG_PAGE_0);
	write_reg(client, 0x03, 0x00);

	if (state->csi_lanes == 1)
		write_reg(client, 0x0B, 0x41);
	else
		write_reg(client, 0x0B, 0x40);

	write_reg(client, 0x0D, 0xC9);
	write_reg(client, 0x0E, 0xC9);
	write_reg(client, 0x10, 0x01);
	write_reg(client, 0x11, 0x03);
	/* write_reg(client, 0x12, 0x00); */
	/* write_reg(client, 0x13, 0x00); */
	/* write_reg(client, 0x14, 0x00); */
	/* write_reg(client, 0xFF, 0x00); */

	/* Page 1 */
	isl79985_change_page(state, REG_PAGE_1);
	write_reg(client, 0x2F, 0xE6);
	write_reg(client, 0x33, 0x85);
	/* write_reg(client, 0xFF, 0x01); */

	/* Page 2 */
	isl79985_change_page(state, REG_PAGE_2);
	write_reg(client, 0x2F, 0xE6);
	write_reg(client, 0x33, 0x85);
	/* write_reg(client, 0xFF, 0x02); */

	/* Page 3 */
	isl79985_change_page(state, REG_PAGE_3);
	write_reg(client, 0x2F, 0xE6);
	write_reg(client, 0x33, 0x85);
	/* write_reg(client, 0xFF, 0x03); */

	/* Page 4 */
	isl79985_change_page(state, REG_PAGE_4);
	write_reg(client, 0x2F, 0xE6);
	write_reg(client, 0x33, 0x85);
	/* write_reg(client, 0xFF, 0x04); */

	/* Page 5 */
	isl79985_change_page(state, REG_PAGE_5);
	/* write_reg(client, 0x01, 0x85); */
	/* write_reg(client, 0x02, 0xA0); */
	write_reg(client, 0x03, 0x08);
	/* write_reg(client, 0x04, 0xE4); */
	write_reg(client, 0x05, 0x00);
	write_reg(client, 0x06, 0x00);
	write_reg(client, 0x07, 0x46);
	write_reg(client, 0x08, 0x02);
	write_reg(client, 0x09, 0x00);
	write_reg(client, 0x0A, 0x68);
	/* write_reg(client, 0x0B, 0x02); */
	write_reg(client, 0x0C, 0x00);
	write_reg(client, 0x0D, 0x06);
	write_reg(client, 0x0E, 0x00);
	/* write_reg(client, 0x0F, 0x00); */
	/* write_reg(client, 0x10, 0x05); */
	/* write_reg(client, 0x11, 0xA0); */
	write_reg(client, 0x12, 0x76);
	write_reg(client, 0x13, 0x2F);
	write_reg(client, 0x14, 0x0E);
	write_reg(client, 0x15, 0x36);
	write_reg(client, 0x16, 0x12);
	write_reg(client, 0x17, 0xF6);
	/* write_reg(client, 0x18, 0x00); */
	write_reg(client, 0x19, 0x17);
	write_reg(client, 0x1A, 0x0A);
	write_reg(client, 0x1B, 0x61);
	write_reg(client, 0x1C, 0x7A);
	write_reg(client, 0x1D, 0x0F);
	write_reg(client, 0x1E, 0x8C);
	write_reg(client, 0x1F, 0x02);
	/* write_reg(client, 0x20, 0x00); */
	write_reg(client, 0x21, 0x0C);
	/* write_reg(client, 0x22, 0x00); */
	write_reg(client, 0x23, 0x00);
	/* write_reg(client, 0x24, 0x00); */
	write_reg(client, 0x25, 0xF0);
	write_reg(client, 0x26, 0x00);
	write_reg(client, 0x27, 0x00);
	write_reg(client, 0x28, 0x01);
	write_reg(client, 0x29, 0x0E);
	/* write_reg(client, 0x2A, 0x00); */
	write_reg(client, 0x2B, 0x19);
	write_reg(client, 0x2C, 0x18);
	write_reg(client, 0x2D, 0xF1);
	write_reg(client, 0x2E, 0x00);
	write_reg(client, 0x2F, 0xF1);
	/* write_reg(client, 0x30, 0x00); */
	/* write_reg(client, 0x31, 0x00); */
	/* write_reg(client, 0x32, 0x00); */
	write_reg(client, 0x33, 0xC0);
	write_reg(client, 0x34, 0x18);
	write_reg(client, 0x35, 0x00);
	/* write_reg(client, 0x36, 0x00); */

	if (state->csi_lanes == 1)
		write_reg(client, 0x00, 0x02);
	else
		write_reg(client, 0x00, 0x01);

	/* write_reg(client, 0xFF, 0x05); */

	msleep(10);

	return 0;
}

static int isl79985_probe_of(struct isl79985 *state)
{
	struct device *dev = &state->i2c_client->dev;
	struct v4l2_fwnode_endpoint endpoint = { .bus_type = 0 };
	struct device_node *ep;
	const __be32 *addr_be;
	u32 addr;
	int ret, len;

    addr_be = of_get_property(dev->of_node, "reg", &len);
    if (!addr_be || (len < sizeof(*addr_be))) {
        dev_err(dev, "of_i2c: invalid reg on %pOF\n", dev->of_node);
        return -EINVAL;
    }

    addr = be32_to_cpup(addr_be);
    state->slave_addr = addr;
    dev_info(dev, "slave_addr : 0x%x\n", state->slave_addr);

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

	ret = 0;
	goto free_endpoint;

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
	pr_info("%s\n", __func__);
	return 0;
}

static const struct media_entity_operations isl79985_sd_media_ops = {
	.link_setup = isl79985_link_setup,
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
					   0, 255, 1, 0);

	ctrls->auto_gain = v4l2_ctrl_new_std(hdl, ops,
					   V4L2_CID_AUTOGAIN,
					   0, 1, 1, 1);

	ctrls->gain = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_GAIN,
						0, 511, 1, 0xF0);


	ctrls->contrast = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_CONTRAST,
					       0, 255, 1, 0x64);

	ctrls->sharpness = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_SHARPNESS,
					    0, 64, 1, 0x11);

	ctrls->saturation = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_SATURATION,
					     0, 65535, 1, 0x8080);

	ctrls->hue = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_HUE,
					0, 256, 1, 0);

	ctrls->color_kill = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_COLOR_KILLER,
					      0, 255, 1, 0x48);

	ctrls->test_pattern =
		v4l2_ctrl_new_std(hdl, ops, V4L2_CID_TEST_PATTERN,
					     0, 255, 1, 0);

	if (hdl->error) {
		pr_err("v4l2_ctrl fail\n");
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
	struct isl79985 *state;
	struct v4l2_subdev *sd;
	int err;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	state = devm_kzalloc(&client->dev, sizeof(*state), GFP_KERNEL);
	if (state == NULL)
		return -ENOMEM;

	state->i2c_client = client;
	i2c_set_clientdata(client, state);
	mutex_init(&state->mutex);
	state->cur_page = -1;

	/*
	if (read_reg(client, ISL79985_REG_CHIP_ID) != CHIP_ID_79985)
		pr_err("not a ISL79985 Device\n");
		return -ENODEV;
	*/

	err = isl79985_probe_of(state);
	if (err)
		goto err_hdl;

	isl79985_hardware_init(state);

	state->mbus_fmt_code = MEDIA_BUS_FMT_YUYV8_2X8;
	state->channel = ISL79985_CH1;
	state->norm = V4L2_STD_NTSC;

	pr_info("initial isl79985 slave_addr 0x%x\n", state->slave_addr);
	sd = &state->sd;
	v4l2_i2c_subdev_init(sd, client, &isl79985_ops);
	sd->dev = &client->dev;

	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
	state->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	sd->entity.ops = &isl79985_sd_media_ops;
	err = media_entity_pads_init(&sd->entity, 1, &state->pad);
	if (err < 0) {
		pr_err("media_entity_pads_init fail\n");
		goto err_hdl;
	}

	err = v4l2_async_register_subdev_sensor_common(sd);
	if (err < 0) {
		pr_err("v4l2_async_register_subdev fail\n");
		goto err_hdl;
	}

	err = isl79985_init_controls(state);
	if (err)
		pr_err("isl79985_init_controls fsil\n");

	return 0;

err_hdl:
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
MODULE_DESCRIPTION("isl79985 V4L2 i2c driver");
MODULE_AUTHOR("slash.huang@dfi.com");
