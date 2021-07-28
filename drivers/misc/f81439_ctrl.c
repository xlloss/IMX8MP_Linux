// SPDX-License-Identifier: GPL-2.0
/*
 * Description:
 * The F81439 Control Driveris programmable, monolithic multi-protocol
 * transceiver device that contains RS-232 and RS-485 / RS-422
 * drivers and receivers
 *
 * slash.huang@dfi.com
 * slash.linux.c@gmail.com
 */

#include <linux/miscdevice.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/gpio/consumer.h>

#define DEV_NAME_LEN 12
#define F81439_MODE_N 3

struct f81439_ctl_dev {
	struct miscdevice miscdev;
	struct attribute_group attrs;
	char dev_name[DEV_NAME_LEN];
	struct device *dev;
	spinlock_t gpio_lock;
	struct gpio_desc *mode[F81439_MODE_N];
	char current_mode[50];
};

enum {
	F81439_MODE0 = 0,
	F81439_MODE1,
	F81439_MODE2,
	F81439_MODE3,
	F81439_MODE4,
	F81439_MODE5,
	F81439_MODE6,
	F81439_MODE7,
	F81439_MODE_END,
};

char mode_desrc[F81439_MODE_END][50] = {
	{"rs422-full"},
	{"pure-rs232"},
	{"rs485-half tx-en low act"},
	{"rs485-half tx-en hi  act"},
	{"rs422-full rs485-half with termi bias resistor"},
	{"pure-rs232 co-exists rs485"},
	{"rs485-half with termi bias resistor"},
	{"shutdown"},
};

void f81439_ctl_set_config(struct f81439_ctl_dev *f81439_ctl,
	unsigned char set_mode)
{
	int i;

	spin_lock(&f81439_ctl->gpio_lock);
	for (i = 2; i >= 0; i--)
		gpiod_set_value_cansleep(f81439_ctl->mode[i], set_mode >> i & 0x01);
	spin_unlock(&f81439_ctl->gpio_lock);

	sprintf(f81439_ctl->current_mode, "mode %d : %s", set_mode,
		&mode_desrc[set_mode][0]);
}

static int f81439_ctl_parse_of(struct f81439_ctl_dev *f81439_ctl)
{
	struct device *dev = f81439_ctl->dev;
	struct device_node *node = dev->of_node;
	int rval, i;
	const char *default_mode;

	rval = of_property_read_string(node, "default-mode", &default_mode);
	if (rval < 0) {
		dev_info(dev, "not set default mode, prue rs232 mode\n");
		f81439_ctl_set_config(f81439_ctl, F81439_MODE1);
		return 0;
	}

	for (i = F81439_MODE0; i < F81439_MODE_END; i++) {
		if (!strcmp(default_mode, &mode_desrc[i][0])) {
			f81439_ctl_set_config(f81439_ctl, i);
			return 0;
		}
	}

	if (i >= F81439_MODE_END) {
		dev_info(dev, "can't find mode, use prue rs232 mode\n");
		f81439_ctl_set_config(f81439_ctl, F81439_MODE1);
	}

	return 0;
}

static ssize_t f81439_mode_store(struct device *dev, struct device_attribute *attr,
	    const char *buf, size_t count)
{
	struct f81439_ctl_dev *f81439_ctl = dev_get_drvdata(dev);
	unsigned char set_mode;
	int ret;

	if (set_mode < F81439_MODE0 && set_mode > F81439_MODE7)
		return -EINVAL;

	ret = kstrtou8(buf, 10, &set_mode);
	if (ret < 0) {
		dev_err(dev, "f81439_mode_store: value fail\n");
		return ret;
	}

	f81439_ctl_set_config(f81439_ctl, set_mode);

	return count;
}

static ssize_t f81439_mode_show(struct device *dev,
				   struct device_attribute *devattr,
				   char *buf)
{
	struct f81439_ctl_dev *f81439_ctl = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", f81439_ctl->current_mode);
}

static DEVICE_ATTR_RW(f81439_mode);

static struct attribute *f81439_ctl_attributes[] = {
	&dev_attr_f81439_mode.attr,
	NULL
};

static const struct attribute_group f81439_ctl_attr_group = {
	.attrs = f81439_ctl_attributes,
};

static int f81439_ctl_probe(struct platform_device *pdev)
{
	struct f81439_ctl_dev *f81439_ctl;
	struct device *dev;
	int err, i, index_mode;
	char pin_name[5];

	f81439_ctl = devm_kzalloc(&pdev->dev,
		sizeof(struct f81439_ctl_dev), GFP_KERNEL);
	if (!f81439_ctl)
		return -ENOMEM;

	f81439_ctl->dev = &pdev->dev;
	dev = f81439_ctl->dev;

	/*
	 * MODE2_PIN -> f81439_ctl->mode[0]
	 * MODE1_PIN -> f81439_ctl->mode[1]
	 * MODE0_PIN -> f81439_ctl->mode[2]
	 */
	index_mode = 2;
	for (i = 0; i <F81439_MODE_N; i++) {
		sprintf(pin_name, "mode%d", index_mode);
		f81439_ctl->mode[i] = devm_gpiod_get(dev, pin_name, GPIOD_OUT_HIGH);
		if (IS_ERR(f81439_ctl->mode[i])) {
			dev_err(dev, "mode-%d gpio get fail\n", i);
			return PTR_ERR(f81439_ctl->mode[i]);
		}
		index_mode--;
	}

	spin_lock_init(&f81439_ctl->gpio_lock);

	err = f81439_ctl_parse_of(f81439_ctl);
	if (err < 0)
		goto err_f81439_ctl_dev;

	platform_set_drvdata(pdev, f81439_ctl);

	snprintf(f81439_ctl->dev_name, DEV_NAME_LEN, "f81439_ctl");

	f81439_ctl->miscdev.minor = MISC_DYNAMIC_MINOR;
	f81439_ctl->miscdev.name = f81439_ctl->dev_name;
	f81439_ctl->miscdev.parent = dev;
	err = misc_register(&f81439_ctl->miscdev);
	if (err) {
		dev_err(dev, "error:%d. Unable to register device", err);
		goto err_f81439_ctl_dev;
	}

	err = sysfs_create_group(&f81439_ctl->dev->kobj, &f81439_ctl_attr_group);
	if (err)
		goto err_f81439_ctl_dev;

	return 0;

err_f81439_ctl_dev:
	return err;
}

static int f81439_ctl_remove(struct platform_device *pdev)
{
	struct f81439_ctl_dev *f81439_ctl;

	f81439_ctl = platform_get_drvdata(pdev);
	sysfs_remove_group(&pdev->dev.kobj, &f81439_ctl_attr_group);
	misc_deregister(&f81439_ctl->miscdev);
	return 0;
}

static const struct of_device_id f81439_ctl_of_match[] = {
	{
		.compatible = "fintek, f81439",
	},
	{ /* end of table */ }
};
MODULE_DEVICE_TABLE(of, f81439_ctl_of_match);

static struct platform_driver f81439_ctl_driver = {
	.driver = {
		.name = "f81439-ctl",
		.of_match_table = f81439_ctl_of_match,
	},
	.probe = f81439_ctl_probe,
	.remove =  f81439_ctl_remove,
};

static int __init f81439_ctl_init(void)
{
	int err;

	err = platform_driver_register(&f81439_ctl_driver);
	if (err < 0) {
		pr_err("%s Unabled to register f81439_ctl driver", __func__);
		return err;
	}
	return 0;
}

static void __exit f81439_ctl_exit(void)
{
	platform_driver_unregister(&f81439_ctl_driver);
}

module_init(f81439_ctl_init);
module_exit(f81439_ctl_exit);

MODULE_AUTHOR("slash.linux.c@gmail.com");
MODULE_DESCRIPTION("F81439 Control Driver");
MODULE_LICENSE("GPL");
