// SPDX-License-Identifier: GPL-2.0+
/*
 * Base on Intel XHCI (Cherry Trail, Broxton and others) USB OTG role switch driver
 *
 * Author: Slash.Huang
 * slash.linux.c@gmail.com
 */

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/usb/role.h>
#include <linux/gpio/consumer.h>

#define DRV_NAME "imx_gpio_otg_sw"

struct gpio_otg_sw_data {
	struct device *dev;
	struct usb_role_switch *role_sw;
	struct gpio_desc *gpio_role;
	int irq;
};

static enum usb_role otg_sw_get_role(struct gpio_otg_sw_data *data)
{
	enum usb_role role;
	u32 val;

	val = gpiod_get_value(data->gpio_role);

	if (!val)
		role = USB_ROLE_HOST;
	else
		role = USB_ROLE_DEVICE;

	return role;
}

static irqreturn_t otg_sw_isr(int irq, void *dev_id)
{
	struct gpio_otg_sw_data *data;
	int val;

	data = (struct gpio_otg_sw_data *)dev_id;

	val = gpiod_get_value(data->gpio_role);

	if (!val)
		usb_role_switch_set_role(data->role_sw, USB_ROLE_HOST);
	else
		usb_role_switch_set_role(data->role_sw, USB_ROLE_DEVICE);

	return IRQ_HANDLED;
}

static int gpio_otg_sw_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct gpio_otg_sw_data *data;
	char pin_name[5];
	int ret, irq;
	struct fwnode_handle *connector, *ep;
	enum usb_role role;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dev = &pdev->dev;

	sprintf(pin_name, "role");
	data->gpio_role = devm_gpiod_get(dev, pin_name, GPIOD_IN);
	if (IS_ERR(data->gpio_role)) {
		dev_err(dev, "otg role switch gpio get fail\n");
		return PTR_ERR(data->gpio_role);
	}

	irq = gpiod_to_irq(data->gpio_role);
	data->irq = irq;

	platform_set_drvdata(pdev, data);

	/* For backward compatibility check the connector child node first */
	connector = device_get_named_child_node(data->dev, "connector");
	if (connector) {
		data->role_sw = fwnode_usb_role_switch_get(connector);
	} else {
		ep = fwnode_graph_get_next_endpoint(dev_fwnode(data->dev), NULL);
		if (!ep) {
			dev_err(&pdev->dev, "can't fwnode_graph_get_next_endpoint\n");
			goto irq_fail;
		}

		connector = fwnode_graph_get_remote_port_parent(ep);
		fwnode_handle_put(ep);
		if (!connector) {
			dev_err(&pdev->dev, "can't find connector in dts\n");
			goto irq_fail;
		}

		data->role_sw = usb_role_switch_get(data->dev);
	}

	if (IS_ERR(data->role_sw)) {
		ret = PTR_ERR(data->role_sw);
		goto err_put_fwnode;
	}

	role = otg_sw_get_role(data);
	if (role == USB_ROLE_HOST)
		usb_role_switch_set_role(data->role_sw, USB_ROLE_HOST);
	else
		usb_role_switch_set_role(data->role_sw, USB_ROLE_DEVICE);

	/* Request IRQ */
	ret = devm_request_irq(&pdev->dev, irq, otg_sw_isr,
				IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
				pdev->name, data);
	if (ret) {
		dev_err(&pdev->dev, "can't claim irq %d\n", irq);
		goto irq_fail;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	return 0;

err_put_fwnode:
	fwnode_handle_put(connector);

irq_fail:
	kfree(data);

	return -EINVAL;
}

static int gpio_otg_sw_remove(struct platform_device *pdev)
{
	struct gpio_otg_sw_data *data = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);
	usb_role_switch_unregister(data->role_sw);
	kfree(data);
	return 0;
}

static const struct of_device_id gpio_otg_sw_of_match[] = {
	{
		.compatible = "imx8, gpio-otg-sw",
	},
	{ /* end of table */ }
};
MODULE_DEVICE_TABLE(of, gpio_otg_sw_of_match);

static struct platform_driver gpio_otg_sw_driver = {
	.driver = {
		.name = DRV_NAME,
		.of_match_table = gpio_otg_sw_of_match,
	},

	.probe = gpio_otg_sw_probe,
	.remove = gpio_otg_sw_remove,
};

module_platform_driver(gpio_otg_sw_driver);

MODULE_AUTHOR("slash.huang <slash.linux.c@gmail.com>");
MODULE_DESCRIPTION("IMX8 usb3 gpio role switch driver");
MODULE_LICENSE("GPL");
