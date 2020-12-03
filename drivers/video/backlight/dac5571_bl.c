// SPDX-License-Identifier: GPL-2.0-only
/*
 * Maxim dac5571 Backlight Driver
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
#include <linux/regulator/consumer.h>

#define DEFAULT_BL_NAME "dac5571-backlight"
#define MAX_BRIGHTNESS 100
#define DEFAULT_BL_VALE 100
#define TOPCON_BL_ON 1
#define TOPCON_BL_OFF 0
#define BL_VOLT_MAX 3300000
#define BL_VOLT_MIN 0000001

struct dac5571 {
	u8 initial_brightness;
	int current_brightness;
	unsigned int dac_value;
	struct i2c_client *client;
	struct backlight_device *bl;
	struct device *dev;
	struct gpio_desc *enable_gpio;
	struct regulator *regulator;
};

struct dac5571;

/*
 * ADC_4096 / brightness_100
 */
static int mcp4725_set_value(struct dac5571 *dac5571_dev, int val)
{
	struct i2c_client *cl = dac5571_dev->client;
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
static int mcp4725_get_value(struct dac5571 *dac5571_dev)
{
	struct i2c_client *cl = dac5571_dev->client;
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
	dac5571_dev->dac_value = dac_value;
}
#endif

static int dac5571_bl_on_off(struct dac5571 *dac5571_dev, unsigned char onoff)
{
	int ret;

	if (onoff == TOPCON_BL_ON)
		ret = regulator_set_voltage(dac5571_dev->regulator,
			BL_VOLT_MAX, BL_VOLT_MAX);
	else
		ret = regulator_set_voltage(dac5571_dev->regulator, BL_VOLT_MIN,
			BL_VOLT_MIN);

	return ret;
}

static int dac5571_bl_update_status(struct backlight_device *bl)
{
	struct dac5571 *dac5571_dev = bl_get_data(bl);
	int brightness = bl->props.brightness;
	int ret;

	if (bl->props.state & (BL_CORE_SUSPENDED | BL_CORE_FBBLANK))
		brightness = 0;

	if (bl->props.power == TOPCON_BL_OFF)
		ret = dac5571_bl_on_off(dac5571_dev, TOPCON_BL_OFF);
	else
		ret = dac5571_bl_on_off(dac5571_dev, TOPCON_BL_ON);

	dac5571_dev->current_brightness = brightness;
	mcp4725_set_value(dac5571_dev, brightness);

	return ret;
}

static int dac5571_bl_get_brightness(struct backlight_device *dev)
{
	struct dac5571 *dac5571_dev = bl_get_data(dev);

	return dac5571_dev->current_brightness;
}

static const struct backlight_ops dac5571_bl_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.update_status = dac5571_bl_update_status,
	.get_brightness = dac5571_bl_get_brightness,
};

static int dac5571_backlight_register(struct dac5571 *dac5571_dev)
{
	struct backlight_device *bl;
	struct backlight_properties props;
	const char *name = DEFAULT_BL_NAME;

	memset(&props, 0, sizeof(props));
	props.type = BACKLIGHT_PLATFORM;
	props.max_brightness = MAX_BRIGHTNESS;

	if (dac5571_dev->initial_brightness > props.max_brightness)
		dac5571_dev->initial_brightness = props.max_brightness;

	props.brightness = dac5571_dev->initial_brightness;
	props.scale = BACKLIGHT_SCALE_LINEAR;

	bl = devm_backlight_device_register(dac5571_dev->dev, name,
		dac5571_dev->dev, dac5571_dev, &dac5571_bl_ops, &props);
	if (IS_ERR(bl))
		return PTR_ERR(bl);

	dac5571_dev->bl = bl;

	return 0;
}

static int dac5571_probe(struct i2c_client *cl, const struct i2c_device_id *id)
{
	struct dac5571 *dac5571_dev;
	int ret;

	if (!i2c_check_functionality(cl->adapter,
		I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA))
		return -EIO;

	dac5571_dev = devm_kzalloc(&cl->dev, sizeof(*dac5571_dev), GFP_KERNEL);
	if (!dac5571_dev)
		return -ENOMEM;

	dac5571_dev->client = cl;
	dac5571_dev->dev = &cl->dev;

	dac5571_dev->regulator = devm_regulator_get(dac5571_dev->dev, "bl");
	if (IS_ERR(dac5571_dev->regulator)) {
		dev_err(dac5571_dev->dev, "cannot get regulator\n");
		return PTR_ERR(dac5571_dev->regulator);
	}

	i2c_set_clientdata(cl, dac5571_dev);

	dac5571_dev->initial_brightness = DEFAULT_BL_VALE;
	ret = dac5571_backlight_register(dac5571_dev);
	if (ret) {
		dev_err(dac5571_dev->dev,
			"failed to register backlight. err: %d\n", ret);
		return ret;
	}

	dac5571_dev->bl->props.state &= ~(BL_CORE_SUSPENDED | BL_CORE_FBBLANK);
	dac5571_dev->bl->props.power = TOPCON_BL_ON;

	backlight_update_status(dac5571_dev->bl);
	return 0;
}

static void dac5571_shutdown(struct i2c_client *cl)
{
	struct dac5571 *dac5571_dev = i2c_get_clientdata(cl);

	dac5571_dev->bl->props.brightness = 0;
	backlight_update_status(dac5571_dev->bl);
	dac5571_bl_on_off(dac5571_dev, TOPCON_BL_OFF);
}

static int dac5571_remove(struct i2c_client *cl)
{
	struct dac5571 *dac5571_dev = i2c_get_clientdata(cl);

	dac5571_dev->bl->props.brightness = 0;
	backlight_update_status(dac5571_dev->bl);
	dac5571_bl_on_off(dac5571_dev, TOPCON_BL_OFF);
	return 0;
}

static const struct of_device_id dac5571_dt_ids[] = {
	{ .compatible = "dac5571,bl", },
	{ }
};
MODULE_DEVICE_TABLE(of, dac5571_dt_ids);

static const struct i2c_device_id dac5571_ids[] = {
	{"dac5571", 0},
	{ }
};
MODULE_DEVICE_TABLE(i2c, dac5571_ids);

static struct i2c_driver dac5571_driver = {
	.driver = {
		   .name = "dac5571-bl",
		   .of_match_table = of_match_ptr(dac5571_dt_ids),
	},
	.probe = dac5571_probe,
	.remove = dac5571_remove,
	.shutdown = dac5571_shutdown,
	.id_table = dac5571_ids,
};

module_i2c_driver(dac5571_driver);

MODULE_DESCRIPTION("DAC5571 Backlight driver");
MODULE_AUTHOR("Slash Huang <slash.linux.c@gmail.com>");
MODULE_LICENSE("GPL");
