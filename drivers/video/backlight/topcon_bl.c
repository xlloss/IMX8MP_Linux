// SPDX-License-Identifier: GPL-2.0-only
/*
 * Maxim topconbl Backlight Driver
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio.h>


#define DEFAULT_BL_NAME "topcon-lcd-backlight"
#define MAX_BRIGHTNESS 100
#define DEFAULT_TOPCAN_BL_VALE 100
#define TOPCON_BL_ON 0
#define TOPCON_BL_OFF 1

struct topconbl {
	u8 initial_brightness;
	int current_brightness;
	unsigned int dac_value;
	struct i2c_client *client;
	struct backlight_device *bl;
	struct device *dev;
	struct gpio_desc *enable_gpio;
};

struct topconbl;

/*
 * ADC_4096 / brightness_100
 */
static int mcp4725_set_value(struct topconbl *topconbl_dev, int val)
{
	struct i2c_client *cl = topconbl_dev->client;
	u8 outbuf[2];
	int ret, dac_value;

	if (val == 100)
		dac_value = (1 << 12) - 1;
	else
		dac_value = val * 40;

	if (dac_value >= (1 << 12) || val < 0)
		return -EINVAL;

	outbuf[0] = (dac_value >> 8) & 0xf;
	outbuf[1] = dac_value & 0xff;

	ret = i2c_master_send(cl, outbuf, 2);
	if (ret < 0)
		return ret;
	else if (ret != 2)
		return -EIO;
	else
		return 0;
}

#ifdef DAC_DEBUG
static int mcp4725_get_value(struct topconbl *topconbl_dev)
{
	struct i2c_client *cl = topconbl_dev->client;
	u8 inbuf[4];
	unsigned int dac_value;
	int err;

	/* read current DAC value and settings */
	err = i2c_master_recv(client, inbuf, 3);
	if (err < 0) {
		dev_err(&client->dev, "failed to read DAC value");
		goto err_disable_vref_reg;
	}

	dac_value = (inbuf[1] << 4) | (inbuf[2] >> 4);
	topconbl_dev->dac_value = dac_value;
}
#endif

static void topconbl_bl_on_off(struct topconbl *topconbl_dev, unsigned char onoff)
{
	if (onoff == TOPCON_BL_ON)
		gpiod_set_value(topconbl_dev->enable_gpio, 1);
	else
		gpiod_set_value(topconbl_dev->enable_gpio, 0);
}

static int topconbl_bl_update_status(struct backlight_device *bl)
{
	struct topconbl *topconbl_dev = bl_get_data(bl);
	int brightness = bl->props.brightness;

	if (bl->props.state & (BL_CORE_SUSPENDED | BL_CORE_FBBLANK))
		brightness = 0;

	if (bl->props.power == FB_BLANK_POWERDOWN)
		topconbl_bl_on_off(topconbl_dev, TOPCON_BL_OFF);
	else
		topconbl_bl_on_off(topconbl_dev, TOPCON_BL_ON);

	topconbl_dev->current_brightness = brightness;
	mcp4725_set_value(topconbl_dev, brightness);

	return 0;
}

static int topconbl_bl_get_brightness(struct backlight_device *dev)
{
	struct topconbl *topconbl_dev = bl_get_data(dev);

	return topconbl_dev->current_brightness;
}

static const struct backlight_ops topconbl_bl_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.update_status = topconbl_bl_update_status,
	.get_brightness = topconbl_bl_get_brightness,
};

static int topconbl_backlight_register(struct topconbl *topconbl_dev)
{
	struct backlight_device *bl;
	struct backlight_properties props;
	const char *name = DEFAULT_BL_NAME;

	memset(&props, 0, sizeof(props));
	props.type = BACKLIGHT_PLATFORM;
	props.max_brightness = MAX_BRIGHTNESS;

	if (topconbl_dev->initial_brightness > props.max_brightness)
		topconbl_dev->initial_brightness = props.max_brightness;

	props.brightness = topconbl_dev->initial_brightness;
	props.scale = BACKLIGHT_SCALE_LINEAR;

	bl = devm_backlight_device_register(topconbl_dev->dev, name,
		topconbl_dev->dev, topconbl_dev, &topconbl_bl_ops, &props);
	if (IS_ERR(bl))
		return PTR_ERR(bl);

	topconbl_dev->bl = bl;

	return 0;
}

static int topconbl_probe(struct i2c_client *cl, const struct i2c_device_id *id)
{
	struct topconbl *topconbl_dev;
	int ret;

	if (!i2c_check_functionality(cl->adapter,
		I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA))
		return -EIO;

	topconbl_dev = devm_kzalloc(&cl->dev, sizeof(*topconbl_dev), GFP_KERNEL);
	if (!topconbl_dev)
		return -ENOMEM;

	topconbl_dev->client = cl;
	topconbl_dev->dev = &cl->dev;

	topconbl_dev->enable_gpio = devm_gpiod_get_optional(topconbl_dev->dev,
		"enable", GPIOD_OUT_HIGH);
	if (IS_ERR(topconbl_dev->enable_gpio)) {
		ret = PTR_ERR(topconbl_dev->enable_gpio);
		return ret;
	}

	i2c_set_clientdata(cl, topconbl_dev);

	topconbl_dev->initial_brightness = DEFAULT_TOPCAN_BL_VALE;
	ret = topconbl_backlight_register(topconbl_dev);
	if (ret) {
		dev_err(topconbl_dev->dev,
			"failed to register backlight. err: %d\n", ret);
		return ret;
	}

	backlight_update_status(topconbl_dev->bl);
	return 0;
}

static void topconbl_shutdown(struct i2c_client *cl)
{
	struct topconbl *topconbl_dev = i2c_get_clientdata(cl);

	topconbl_dev->bl->props.brightness = 0;
	backlight_update_status(topconbl_dev->bl);
	topconbl_bl_on_off(topconbl_dev, TOPCON_BL_OFF);
}

static int topconbl_remove(struct i2c_client *cl)
{
	struct topconbl *topconbl_dev = i2c_get_clientdata(cl);

	topconbl_dev->bl->props.brightness = 0;
	backlight_update_status(topconbl_dev->bl);
	topconbl_bl_on_off(topconbl_dev, TOPCON_BL_OFF);
	return 0;
}

static const struct of_device_id topconbl_dt_ids[] = {
	{ .compatible = "topcon,bl", },
	{ }
};
MODULE_DEVICE_TABLE(of, topconbl_dt_ids);

static const struct i2c_device_id topconbl_ids[] = {
	{"topconbl", 0},
	{ }
};
MODULE_DEVICE_TABLE(i2c, topconbl_ids);

static struct i2c_driver topconbl_driver = {
	.driver = {
		   .name = "topconbl",
		   .of_match_table = of_match_ptr(topconbl_dt_ids),
	},
	.probe = topconbl_probe,
	.remove = topconbl_remove,
	.shutdown = topconbl_shutdown,
	.id_table = topconbl_ids,
};

module_i2c_driver(topconbl_driver);

MODULE_DESCRIPTION("Topcon Backlight driver");
MODULE_AUTHOR("Slash Huang <slash.linux.c@gmail.com>");
MODULE_LICENSE("GPL");
