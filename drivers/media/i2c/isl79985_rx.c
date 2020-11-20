// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2005-2006 Micronas USA Inc.
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


#define ISL79985_REG_AUTOGAIN		0x02
#define ISL79985_REG_HUE		0x0f
#define ISL79985_REG_SATURATION		0x10
#define ISL79985_REG_CONTRAST		0x11
#define ISL79985_REG_BRIGHTNESS		0x12
#define ISL79985_REG_COLOR_KILLER	0x14
#define ISL79985_REG_GAIN		0x3c
#define ISL79985_REG_CHROMA_GAIN	0x3d
#define ISL79985_REG_BLUE_BALANCE	0x3e
#define ISL79985_REG_RED_BALANCE	0x3f
#define CHIP_ID_79985			0x79
#define CHIP_ID_MASK 			0xFF
#define INT_TABLE_CONT			0

struct isl79985 {
	struct i2c_client *i2c_client;
	struct v4l2_subdev sd;
	struct v4l2_ctrl_handler hdl;
	struct v4l2_dv_timings timings;
	struct v4l2_fwnode_bus_mipi_csi2 bus;
	struct media_pad pad;
	struct mutex mutex;
	u8 csi_lanes_in_use;
	u32 mbus_fmt_code;
	u8 channel;
	u8 input;
	int norm;
	int streaming;
	u32 refclk_hz;
};

static int write_reg(struct i2c_client *client, u8 reg, u8 value)
{
	return i2c_smbus_write_byte_data(client, reg, value);
}

//static int write_regs(struct i2c_client *client, const u8 *regs, u8 channel)
//{
//	int ret;
//	int i;
//
//	for (i = 0; i < INT_TABLE_CONT; i += 2) {
//		ret = i2c_smbus_write_byte_data(client, regs[i], regs[i + 1]);
//		if (ret < 0)
//			return ret;
//	}
//	return 0;
//}

static int read_reg(struct i2c_client *client, u8 reg)
{
	return i2c_smbus_read_byte_data(client, reg);
}

static inline struct isl79985 *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct isl79985, sd);
}

static inline struct isl79985 *to_state_from_ctrl(struct v4l2_ctrl *ctrl)
{
	return container_of(ctrl->handler, struct isl79985, hdl);
}

static int isl79985_log_status(struct v4l2_subdev *sd)
{
	/* struct isl79985 *state = to_state(sd); */

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
//	struct isl79985 *state = to_state_from_ctrl(ctrl);
//	struct i2c_client *client = v4l2_get_subdevdata(&state->sd);
//
//	switch (ctrl->id) {
//	case V4L2_CID_GAIN:
//		ctrl->val = read_reg(client, ISL79985_REG_GAIN, 0);
//		return 0;
//
//	case V4L2_CID_CHROMA_GAIN:
//		ctrl->val = read_reg(client, ISL79985_REG_CHROMA_GAIN, 0);
//		return 0;
//
//	case V4L2_CID_BLUE_BALANCE:
//		ctrl->val = read_reg(client, ISL79985_REG_BLUE_BALANCE, 0);
//		return 0;
//
//	case V4L2_CID_RED_BALANCE:
//		ctrl->val = read_reg(client, ISL79985_REG_RED_BALANCE, 0);
//		return 0;
//	}
	return 0;
}

static int isl79985_s_ctrl(struct v4l2_ctrl *ctrl)
{
//	struct isl79985 *state = to_state_from_ctrl(ctrl);
//	struct i2c_client *client = v4l2_get_subdevdata(&state->sd);
//	int addr;
//	int reg;
//
//	switch (ctrl->id) {
//	case V4L2_CID_AUTOGAIN:
//		addr = ISL79985_REG_AUTOGAIN;
//		reg = read_reg(client, addr, state->channel);
//		if (reg < 0)
//			return reg;
//		if (ctrl->val == 0)
//			reg &= ~(1 << 7);
//		else
//			reg |= 1 << 7;
//		return write_reg(client, addr, reg, state->channel);
//
//	case V4L2_CID_COLOR_KILLER:
//		addr = ISL79985_REG_COLOR_KILLER;
//		reg = read_reg(client, addr, state->channel);
//		if (reg < 0)
//			return reg;
//		reg = (reg & ~(0x03)) | (ctrl->val == 0 ? 0x02 : 0x03);
//		return write_reg(client, addr, reg, state->channel);
//
//	case V4L2_CID_GAIN:
//		return write_reg(client, ISL79985_REG_GAIN, ctrl->val, 0);
//
//	case V4L2_CID_CHROMA_GAIN:
//		return write_reg(client, ISL79985_REG_CHROMA_GAIN, ctrl->val, 0);
//
//	case V4L2_CID_BLUE_BALANCE:
//		return write_reg(client, ISL79985_REG_BLUE_BALANCE, ctrl->val, 0);
//
//	case V4L2_CID_RED_BALANCE:
//		return write_reg(client, ISL79985_REG_RED_BALANCE, ctrl->val, 0);
//
//	case V4L2_CID_BRIGHTNESS:
//		return write_reg(client, ISL79985_REG_BRIGHTNESS,
//				ctrl->val, state->channel);
//
//	case V4L2_CID_CONTRAST:
//		return write_reg(client, ISL79985_REG_CONTRAST,
//				ctrl->val, state->channel);
//
//	case V4L2_CID_SATURATION:
//		return write_reg(client, ISL79985_REG_SATURATION,
//				ctrl->val, state->channel);
//
//	case V4L2_CID_HUE:
//		return write_reg(client, ISL79985_REG_HUE,
//				ctrl->val, state->channel);
//
//	default:
//		break;
//	}
	return -EINVAL;
}

//static int isl79985_s_std(struct v4l2_subdev *sd, v4l2_std_id norm)
//{
//	struct isl79985 *dec = to_state(sd);
//
//	dec->norm = norm;
//	return 0;
//}

static int isl79985_s_power(struct v4l2_subdev *sd, int on)
{
	return 0;
}

static const struct v4l2_dv_timings_cap isl79985_timings_cap = {
	.type = V4L2_DV_BT_656_1120,
	/* keep this initialization for compatibility with GCC < 4.4.6 */
	.reserved = { 0 },
	/* Pixel clock from REF_01 p. 20. Min/max height/width are unknown */
	V4L2_INIT_BT_TIMINGS(640, 1920, 350, 1200, 13000000, 165000000,
			V4L2_DV_BT_STD_CEA861 | V4L2_DV_BT_STD_DMT |
			V4L2_DV_BT_STD_GTF | V4L2_DV_BT_STD_CVT,
			V4L2_DV_BT_CAP_PROGRESSIVE |
			V4L2_DV_BT_CAP_REDUCED_BLANKING |
			V4L2_DV_BT_CAP_CUSTOM)
};

static int isl79985_s_dv_timings(struct v4l2_subdev *sd,
				 struct v4l2_dv_timings *timings)
{
	struct isl79985 *state = to_state(sd);

	if (!timings)
		return -EINVAL;

//	if (debug)
//		v4l2_print_dv_timings(sd->name, "tc358743_s_dv_timings: ",
//				timings, false);

	if (v4l2_match_dv_timings(&state->timings, timings, 0, false)) {
		pr_err("%s: no change\n", __func__);
		return 0;
	}

	if (!v4l2_valid_dv_timings(timings,
				&isl79985_timings_cap, NULL, NULL)) {
		/* v4l2_dbg(1, debug, sd, "%s: timings out of range\n", __func__); */
		return -ERANGE;
	}

	state->timings = *timings;

//	enable_stream(sd, false);
//	tc358743_set_pll(sd);
//	tc358743_set_csi(sd);

	return 0;
}

static int isl79985_g_dv_timings(struct v4l2_subdev *sd,
				 struct v4l2_dv_timings *timings)
{
	struct isl79985 *state = to_state(sd);

	*timings = state->timings;

	return 0;
}

#define CHANNEL1_STATUS 0x1B
#define CHANNEL2_STATUS 0x1C
#define CHANNEL3_STATUS 0x1D
#define CHANNEL4_STATUS 0x1E

#define VDLOSS_STS (1 << 3)
#define CH1DET50_STS (1 << 2)
#define SLOCK_STS (1 << 1)
#define HVLOCK_STS (1 << 0)

static int no_signal(struct v4l2_subdev *sd)
{
	struct isl79985 *state = to_state(sd);
	struct i2c_client *i2cdev = state->i2c_client;
	int ret;

	ret = read_reg(i2cdev, CHANNEL1_STATUS);
	ret = ret & ~(CH1DET50_STS);

	if (ret & (VDLOSS_STS | SLOCK_STS | HVLOCK_STS))
		return V4L2_IN_ST_NO_SIGNAL;

	return 0;
}

static int no_sync(struct v4l2_subdev *sd)
{

	/* return !(i2c_rd8(sd, SYS_STATUS) & MASK_S_SYNC); */
}

static int isl79985_g_input_status(struct v4l2_subdev *sd, u32 *status)
{
	*status = 0;
	*status = no_signal(sd);
	/* *status |= no_sync(sd) ? V4L2_IN_ST_NO_SYNC : 0; */

	/* v4l2_dbg(1, debug, sd, "%s: status = 0x%x\n", __func__, *status); */
	return 0;
}

static int isl79985_get_detected_timings(struct v4l2_subdev *sd,
				     struct v4l2_dv_timings *timings)
{

}

static int isl79985_query_dv_timings(struct v4l2_subdev *sd,
		struct v4l2_dv_timings *timings)
{
	int ret;

	ret = isl79985_get_detected_timings(sd, timings);
	if (ret)
		return ret;

	if (!v4l2_valid_dv_timings(timings,
				&isl79985_timings_cap, NULL, NULL)) {
		
		/* v4l2_dbg(1, debug, sd, "%s: timings out of range\n", __func__); */
		return -ERANGE;
	}

	return 0;
}

static int isl79985_g_mbus_config(struct v4l2_subdev *sd,
			     struct v4l2_mbus_config *cfg)
{
	struct isl79985 *state = to_state(sd);

	
	cfg->type = V4L2_MBUS_CSI2_DPHY;
	cfg->flags = V4L2_MBUS_CSI2_1_LANE |
			V4L2_MBUS_CSI2_CHANNEL_0 |
			V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;

	return 0;
}

static int isl79985_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct isl79985 *state = to_state(sd);
	int ret;

	/* It's always safe to stop streaming, no need to take the lock */
	if (!enable) {
		state->streaming = enable;
		return 0;
	}

	/* Must wait until querystd released the lock */
	ret = mutex_lock_interruptible(&state->mutex);
	if (ret)
		return ret;

	state->streaming = enable;
	mutex_unlock(&state->mutex);

	return 0;
}

//static int isl79985_s_video_routing(struct v4l2_subdev *sd, u32 input, u32 output, u32 config)
//{
//	return 0;
//}

static const struct v4l2_ctrl_ops isl79985_ctrl_ops = {
	.g_volatile_ctrl = isl79985_g_volatile_ctrl,
	.s_ctrl = isl79985_s_ctrl,
};

static const struct v4l2_subdev_video_ops isl79985_video_ops = {
	//.s_std = isl79985_s_std,
	//.s_routing = isl79985_s_video_routing,
	.g_input_status = isl79985_g_input_status,
	.s_dv_timings = isl79985_s_dv_timings,
	.g_dv_timings = isl79985_g_dv_timings,
	.query_dv_timings = isl79985_query_dv_timings,
	.g_mbus_config = isl79985_g_mbus_config,
	.s_stream = isl79985_s_stream,
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
	struct i2c_client *i2cdev = state->i2c_client;
#if 1
	int lanes = 0;

	/* Init the isl79985 */

	// Page 0
	write_reg(i2cdev, 0xFF, 0x00);
	write_reg(i2cdev, 0x03, 0x00);

	if (lanes == 1)
		write_reg(i2cdev, 0x0B, 0x41);
	else
		write_reg(i2cdev, 0x0B, 0x40);

	write_reg(i2cdev, 0x0D, 0xC9);
	write_reg(i2cdev, 0x0E, 0xC9);
	write_reg(i2cdev, 0x10, 0x01);
	write_reg(i2cdev, 0x11, 0x03);
	write_reg(i2cdev, 0x12, 0x00);
	write_reg(i2cdev, 0x13, 0x00);
	write_reg(i2cdev, 0x14, 0x00);
	write_reg(i2cdev, 0xFF, 0x00);

	// Page 1
	write_reg(i2cdev, 0xFF, 0x01);
	write_reg(i2cdev, 0x2F, 0xE6);
	write_reg(i2cdev, 0x33, 0x85);
	write_reg(i2cdev, 0xFF, 0x01);

	// Page 2
	write_reg(i2cdev, 0xFF, 0x02);
	write_reg(i2cdev, 0x2F, 0xE6);
	write_reg(i2cdev, 0x33, 0x85);
	write_reg(i2cdev, 0xFF, 0x02);

	// Page 3
	write_reg(i2cdev, 0xFF, 0x03);
	write_reg(i2cdev, 0x2F, 0xE6);
	write_reg(i2cdev, 0x33, 0x85);
	write_reg(i2cdev, 0xFF, 0x03);

	// Page 4
	write_reg(i2cdev, 0xFF, 0x04);
	write_reg(i2cdev, 0x2F, 0xE6);
	write_reg(i2cdev, 0x33, 0x85);
	write_reg(i2cdev, 0xFF, 0x04);

	// Page 5
	write_reg(i2cdev, 0xFF, 0x05);
	write_reg(i2cdev, 0x01, 0x85);
	write_reg(i2cdev, 0x02, 0xA0);
	write_reg(i2cdev, 0x03, 0x08);
	write_reg(i2cdev, 0x04, 0xE4);
	write_reg(i2cdev, 0x05, 0x00);
	write_reg(i2cdev, 0x06, 0x00);
	write_reg(i2cdev, 0x07, 0x46);
	write_reg(i2cdev, 0x08, 0x02);
	write_reg(i2cdev, 0x09, 0x00);
	write_reg(i2cdev, 0x0A, 0x68);
	write_reg(i2cdev, 0x0B, 0x02);
	write_reg(i2cdev, 0x0C, 0x00);
	write_reg(i2cdev, 0x0D, 0x06);
	write_reg(i2cdev, 0x0E, 0x00);
	write_reg(i2cdev, 0x0F, 0x00);
	write_reg(i2cdev, 0x10, 0x05);
	write_reg(i2cdev, 0x11, 0xA0);
	write_reg(i2cdev, 0x12, 0x76);
	write_reg(i2cdev, 0x13, 0x2F);
	write_reg(i2cdev, 0x14, 0x0E);
	write_reg(i2cdev, 0x15, 0x36);
	write_reg(i2cdev, 0x16, 0x12);
	write_reg(i2cdev, 0x17, 0xF6);
	write_reg(i2cdev, 0x18, 0x00);
	write_reg(i2cdev, 0x19, 0x17);
	write_reg(i2cdev, 0x1A, 0x0A);
	write_reg(i2cdev, 0x1B, 0x61);
	write_reg(i2cdev, 0x1C, 0x7A);
	write_reg(i2cdev, 0x1D, 0x0F);
	write_reg(i2cdev, 0x1E, 0x8C);
	write_reg(i2cdev, 0x1F, 0x02);
	write_reg(i2cdev, 0x20, 0x00);
	write_reg(i2cdev, 0x21, 0x0C);
	write_reg(i2cdev, 0x22, 0x00);
	write_reg(i2cdev, 0x23, 0x00);
	write_reg(i2cdev, 0x24, 0x00);
	write_reg(i2cdev, 0x25, 0xF0);
	write_reg(i2cdev, 0x26, 0x00);
	write_reg(i2cdev, 0x27, 0x00);
	write_reg(i2cdev, 0x28, 0x01);
	write_reg(i2cdev, 0x29, 0x0E);
	write_reg(i2cdev, 0x2A, 0x00);
	write_reg(i2cdev, 0x2B, 0x19);
	write_reg(i2cdev, 0x2C, 0x18);
	write_reg(i2cdev, 0x2D, 0xF1);
	write_reg(i2cdev, 0x2E, 0x00);
	write_reg(i2cdev, 0x2F, 0xF1);
	write_reg(i2cdev, 0x30, 0x00);
	write_reg(i2cdev, 0x31, 0x00);
	write_reg(i2cdev, 0x32, 0x00);
	write_reg(i2cdev, 0x33, 0xC0);
	write_reg(i2cdev, 0x34, 0x18);
	write_reg(i2cdev, 0x35, 0x00);
	write_reg(i2cdev, 0x36, 0x00);

	if (lanes == 1)
		write_reg(i2cdev, 0x00, 0x02);
	else
		write_reg(i2cdev, 0x00, 0x01);

	write_reg(i2cdev, 0xFF, 0x05);

	msleep(10);

	return 0;
#endif
}

static int isl79985_probe_of(struct isl79985 *state)
{
	struct device *dev = &state->i2c_client->dev;
	struct v4l2_fwnode_endpoint endpoint = { .bus_type = 0 };
	struct device_node *ep;
	struct clk *refclk;
	u32 bps_pr_lane;
	int ret;

	refclk = devm_clk_get(dev, "refclk");
	if (IS_ERR(refclk)) {
		if (PTR_ERR(refclk) != -EPROBE_DEFER)
			dev_err(dev, "failed to get refclk: %ld\n",
				PTR_ERR(refclk));
		return PTR_ERR(refclk);
	}

	ep = of_graph_get_next_endpoint(dev->of_node, NULL);
	if (!ep) {
		dev_err(dev, "missing endpoint node\n");
		return -EINVAL;
	}

	ret = v4l2_fwnode_endpoint_alloc_parse(of_fwnode_handle(ep), &endpoint);
	if (ret) {
		dev_err(dev, "failed to parse endpoint\n");
		goto put_node;
	}

	if (endpoint.bus_type != V4L2_MBUS_CSI2_DPHY ||
	    endpoint.bus.mipi_csi2.num_data_lanes == 0 ||
	    endpoint.nr_of_link_frequencies == 0) {
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

	ret = clk_prepare_enable(refclk);
	if (ret) {
		dev_err(dev, "Failed! to enable clock\n");
		goto free_endpoint;
	}

	state->refclk_hz = clk_get_rate(refclk);

	/*
	 * The PLL input clock is obtained by dividing refclk by pll_prd.
	 * It must be between 6 MHz and 40 MHz, lower frequency is better.
	 */
	switch (state->refclk_hz) {
	case 26000000:
	case 27000000:
	case 42000000:
		/* state->pdata.pll_prd = state->pdata.refclk_hz / 6000000; */
		break;
	default:
		dev_err(dev, "unsupported refclk: %u Hz\n", state->refclk_hz);
		goto disable_clk;
	}

	/*
	 * The CSI bps per lane must be between 62.5 Mbps and 1 Gbps.
	 * The default is 594 Mbps for 4-lane 1080p60 or 2-lane 720p60.
	 */
//	bps_pr_lane = 2 * endpoint.link_frequencies[0];
//	if (bps_pr_lane < 62500000U || bps_pr_lane > 1000000000U) {
//		dev_err(dev, "unsupported bps per lane: %u bps\n", bps_pr_lane);
//		goto disable_clk;
//	}
//
//	/* The CSI speed per lane is refclk / pll_prd * pll_fbd */
//	state->pdata.pll_fbd = bps_pr_lane /
//			       state->pdata.refclk_hz * state->pdata.pll_prd;
//
//	/*
//	 * FIXME: These timings are from REF_02 for 594 Mbps per lane (297 MHz
//	 * link frequency). In principle it should be possible to calculate
//	 * them based on link frequency and resolution.
//	 */
//	if (bps_pr_lane != 594000000U)
//		dev_warn(dev, "untested bps per lane: %u bps\n", bps_pr_lane);
//	state->pdata.lineinitcnt = 0xe80;
//	state->pdata.lptxtimecnt = 0x003;
//	/* tclk-preparecnt: 3, tclk-zerocnt: 20 */
//	state->pdata.tclk_headercnt = 0x1403;
//	state->pdata.tclk_trailcnt = 0x00;
//	/* ths-preparecnt: 3, ths-zerocnt: 1 */
//	state->pdata.ths_headercnt = 0x0103;
//	state->pdata.twakeup = 0x4882;
//	state->pdata.tclk_postcnt = 0x008;
//	state->pdata.ths_trailcnt = 0x2;
//	state->pdata.hstxvregcnt = 0;
//
//	state->reset_gpio = devm_gpiod_get_optional(dev, "reset",
//						    GPIOD_OUT_LOW);
//	if (IS_ERR(state->reset_gpio)) {
//		dev_err(dev, "failed to get reset gpio\n");
//		ret = PTR_ERR(state->reset_gpio);
//		goto disable_clk;
//	}
//
//	if (state->reset_gpio)
//		tc358743_gpio_reset(state);

	ret = 0;
	goto free_endpoint;

disable_clk:
	clk_disable_unprepare(refclk);
free_endpoint:
	v4l2_fwnode_endpoint_free(&endpoint);
put_node:
	of_node_put(ep);

	return ret;
}

static int isl79985_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = client->adapter;
	struct isl79985 *state;
	struct v4l2_subdev *sd;
	struct v4l2_ctrl *ctrl;
	int err;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	state = devm_kzalloc(&client->dev, sizeof(*state), GFP_KERNEL);
	if (state == NULL)
		return -ENOMEM;

	state->i2c_client = client;
	/* i2c access */
	if ((read_reg(client, CHIP_ID_79985) & CHIP_ID_MASK) != 0) {
		pr_err("not a ISL79985 on address 0x%x\n", client->addr << 1);
		return -ENODEV;
	}

	err = isl79985_probe_of(state);
	if (err)
		goto err_hdl;

	mutex_init(&state->mutex);

	sd = &state->sd;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;

	state->channel = 0;
	state->norm = V4L2_STD_NTSC;
	v4l2_i2c_subdev_init(sd, client, &isl79985_ops);

	state->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;
	err = media_entity_pads_init(&sd->entity, 1, &state->pad);
	if (err < 0)
		goto err_hdl;

	state->mbus_fmt_code = MEDIA_BUS_FMT_RGB888_1X24;
	sd->dev = &client->dev;

	err = v4l2_async_register_subdev(sd);
	if (err < 0)
		goto err_hdl;

	return 0;

err_hdl:
	return -EINVAL;
}

static int isl79985_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct isl79985 *state = to_state(sd);

	v4l2_device_unregister_subdev(sd);
	v4l2_ctrl_handler_free(&state->hdl);
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
