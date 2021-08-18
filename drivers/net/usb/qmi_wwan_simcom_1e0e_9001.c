/*
 * Copyright (c) 2012  Bjørn Mork <bjorn@mork.no>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#include <linux/version.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/usb.h>
#include <linux/usb/usbnet.h>
#include <linux/usb/cdc.h>
#include <linux/usb/cdc-wdm.h>


/* The name of the CDC Device Management driver */
#define DM_DRIVER "cdc_wdm"

/*
 * This driver supports wwan (3G/LTE/?) devices using a vendor
 * specific management protocol called Qualcomm MSM Interface (QMI) -
 * in addition to the more common AT commands over serial interface
 * management
 *
 * QMI is wrapped in CDC, using CDC encapsulated commands on the
 * control ("master") interface of a two-interface CDC Union
 * resembling standard CDC ECM.  The devices do not use the control
 * interface for any other CDC messages.  Most likely because the
 * management protocol is used in place of the standard CDC
 * notifications NOTIFY_NETWORK_CONNECTION and NOTIFY_SPEED_CHANGE
 *
 * Handling a protocol like QMI is out of the scope for any driver.
 * It can be exported as a character device using the cdc-wdm driver,
 * which will enable userspace applications ("modem managers") to
 * handle it.  This may be required to use the network interface
 * provided by the driver.
 *
 * These devices may alternatively/additionally be configured using AT
 * commands on any of the serial interfaces driven by the option driver
 *
 * This driver binds only to the data ("slave") interface to enable
 * the cdc-wdm driver to bind to the control interface.  It still
 * parses the CDC functional descriptors on the control interface to
 *  a) verify that this is indeed a handled interface (CDC Union
 *     header lists it as slave)
 *  b) get MAC address and other ethernet config from the CDC Ethernet
 *     header
 *  c) enable user bind requests against the control interface, which
 *     is the common way to bind to CDC Ethernet Control Model type
 *     interfaces
 *  d) provide a hint to the user about which interface is the
 *     corresponding management interface
 *
 *  For SIM8200 5G, modify by Slash.huang (slash.huang@dfi.com)
 */

/* using a counter to merge subdriver requests with our own into a combined state */
static int qmi_wwan_manage_power(struct usbnet *dev, int on)
{
    atomic_t *pmcount = (void *)&dev->data[1];
    int rv = 0;

    dev_dbg(&dev->intf->dev, "%s() pmcount=%d, on=%d\n", __func__, atomic_read(pmcount), on);

    if ((on && atomic_add_return(1, pmcount) == 1) || (!on && atomic_dec_and_test(pmcount))) {
        /* need autopm_get/put here to ensure the usbcore sees the new value */
        rv = usb_autopm_get_interface(dev->intf);

        if (rv < 0)
            goto err;

        dev->intf->needs_remote_wakeup = on;
        usb_autopm_put_interface(dev->intf);
    }

err:
    return rv;
}

static int qmi_wwan_cdc_wdm_manage_power(struct usb_interface *intf, int on)
{
    struct usbnet *dev = usb_get_intfdata(intf);
    return qmi_wwan_manage_power(dev, on);
}

/* Some devices combine the "control" and "data" functions into a
 * single interface with all three endpoints: interrupt + bulk in and
 * out
 *
 * Setting up cdc-wdm as a subdriver owning the interrupt endpoint
 * will let it provide userspace access to the encapsulated QMI
 * protocol without interfering with the usbnet operations.
  */
static int qmi_wwan_bind_shared(struct usbnet *dev, struct usb_interface *intf)
{
    int rv;
    struct usb_driver *subdriver = NULL;
    atomic_t *pmcount = (void *)&dev->data[1];

    /* ZTE makes devices where the interface descriptors and endpoint
     * configurations of two or more interfaces are identical, even
     * though the functions are completely different.  If set, then
     * driver_info->data is a bitmap of acceptable interface numbers
     * allowing us to bind to one such interface without binding to
     * all of them
     */
    if (dev->driver_info->data &&
        !test_bit(intf->cur_altsetting->desc.bInterfaceNumber, &dev->driver_info->data)) {
        dev_info(&intf->dev, "not on our whitelist - ignored");
        rv = -ENODEV;
        goto err;
    }

    atomic_set(pmcount, 0);

    /* collect all three endpoints */
    rv = usbnet_get_endpoints(dev, intf);

    if (rv < 0)
        goto err;

    /* require interrupt endpoint for subdriver */
    if (!dev->status) {
        rv = -EINVAL;
        goto err;
    }

    subdriver = usb_cdc_wdm_register(intf, &dev->status->desc, 512, &qmi_wwan_cdc_wdm_manage_power);

    if (IS_ERR(subdriver)) {
        rv = PTR_ERR(subdriver);
        goto err;
    }

    /* can't let usbnet use the interrupt endpoint */
    dev->status = NULL;

    /* save subdriver struct for suspend/resume wrappers */
    dev->data[0] = (unsigned long)subdriver;

err:
    return rv;
}

/* Gobi devices uses identical class/protocol codes for all interfaces regardless
 * of function. Some of these are CDC ACM like and have the exact same endpoints
 * we are looking for. This leaves two possible strategies for identifying the
 * correct interface:
 *   a) hardcoding interface number, or
 *   b) use the fact that the wwan interface is the only one lacking additional
 *      (CDC functional) descriptors
 *
 * Let's see if we can get away with the generic b) solution.
 */
static int qmi_wwan_bind_gobi(struct usbnet *dev, struct usb_interface *intf)
{
    int rv = -EINVAL;

    /* ignore any interface with additional descriptors */
    if (intf->cur_altsetting->extralen)
        goto err;

    rv = qmi_wwan_bind_shared(dev, intf);
err:
    return rv;
}

static void qmi_wwan_unbind_shared(struct usbnet *dev, struct usb_interface *intf)
{
    struct usb_driver *subdriver = (void *)dev->data[0];

    if (subdriver && subdriver->disconnect)
        subdriver->disconnect(intf);

    dev->data[0] = (unsigned long)NULL;
}

/* suspend/resume wrappers calling both usbnet and the cdc-wdm
 * subdriver if present.
 *
 * NOTE: cdc-wdm also supports pre/post_reset, but we cannot provide
 * wrappers for those without adding usbnet reset support first.
 */
static int qmi_wwan_suspend(struct usb_interface *intf, pm_message_t message)
{
    struct usbnet *dev = usb_get_intfdata(intf);
    struct usb_driver *subdriver = (void *)dev->data[0];
    int ret;

    ret = usbnet_suspend(intf, message);

    if (ret < 0)
        goto err;

    if (subdriver && subdriver->suspend)
        ret = subdriver->suspend(intf, message);

    if (ret < 0)
        usbnet_resume(intf);

err:
    return ret;
}

static int qmi_wwan_resume(struct usb_interface *intf)
{
    struct usbnet *dev = usb_get_intfdata(intf);
    struct usb_driver *subdriver = (void *)dev->data[0];
    int ret = 0;

    if (subdriver && subdriver->resume)
        ret = subdriver->resume(intf);

    if (ret < 0)
        goto err;

    ret = usbnet_resume(intf);

    if (ret < 0 && subdriver && subdriver->resume && subdriver->suspend)
        subdriver->suspend(intf, PMSG_SUSPEND);

err:
    return ret;
}

/* SIMCOM SIM8200 */
/* very simplistic detection of IPv4 or IPv6 headers */
static bool possibly_iphdr(const char *data)
{
    return (data[0] & 0xd0) == 0x40;
}

/* SIMCOM devices combine the "control" and "data" functions into a
 * single interface with all three endpoints: interrupt + bulk in and
 * out
 * sim7600：dev->udev->descriptor.bcdDevice == cpu_to_le16(0x318)
 * sim7100：dev->udev->descriptor.bcdDevice == cpu_to_le16(0x232)
 */
static int simcom_wwan_bind_8200(struct usbnet *dev, struct usb_interface *intf)
{
    int rv = -EINVAL;

    struct usb_driver *subdriver = NULL;
    atomic_t *pmcount = (void *)&dev->data[1];

    /* ignore any interface with additional descriptors */
    if (intf->cur_altsetting->extralen)
        goto err;

    /* Some makes devices where the interface descriptors and endpoint
     * configurations of two or more interfaces are identical, even
     * though the functions are completely different.  If set, then
     * driver_info->data is a bitmap of acceptable interface numbers
     * allowing us to bind to one such interface without binding to
     * all of them
     */
    if (dev->driver_info->data && !test_bit(intf->cur_altsetting->desc.bInterfaceNumber, &dev->driver_info->data)) {
        dev_info(&intf->dev, "not on our whitelist - ignored");
        rv = -ENODEV;
        goto err;
    }

    atomic_set(pmcount, 0);

    /* collect all three endpoints */
    rv = usbnet_get_endpoints(dev, intf);

    if (rv < 0)
        goto err;

    /* require interrupt endpoint for subdriver */
    if (!dev->status) {
        rv = -EINVAL;
        goto err;
    }

    subdriver = usb_cdc_wdm_register(intf, &dev->status->desc, 512, &qmi_wwan_cdc_wdm_manage_power);

    if (IS_ERR(subdriver)) {
        rv = PTR_ERR(subdriver);
        goto err;
    }

    /* can't let usbnet use the interrupt endpoint */
    dev->status = NULL;

    /* save subdriver struct for suspend/resume wrappers */
    dev->data[0] = (unsigned long)subdriver;

    /* can't let usbnet use the interrupt endpoint */
    //dev->status = NULL;

    printk("simcom usbnet bind here\n");

    /*
     * SIMCOM SIM7600 only support the RAW_IP mode, so the host net driver would
     * remove the arp so the packets can transmit to the modem
    */
    dev->net->flags |= IFF_NOARP;

    /* make MAC addr easily distinguishable from an IP header */
    if (possibly_iphdr(dev->net->dev_addr)) {
        dev->net->dev_addr[0] |= 0x02; /* set local assignment bit */
        dev->net->dev_addr[0] &= 0xbf; /* clear "IP" bit */
    }

    /*
     * SIMCOM SIM8200 need set line state
    */
    usb_control_msg(
                    interface_to_usbdev(intf),
                    usb_sndctrlpipe(interface_to_usbdev(intf), 0),
                    0x22,                                           //USB_CDC_REQ_SET_CONTROL_LINE_STATE
                    0x21,                                           //USB_DIR_OUT | USB_TYPE_CLASS| USB_RECIP_INTERFACE
                    1,                                              //line state 1
                    intf->cur_altsetting->desc.bInterfaceNumber,
                    NULL, 0, 100);

err:
    return rv;
}

static int simcom_wwan_bind(struct usbnet *dev, struct usb_interface *intf)
{
    int ret = -EINVAL;

    ret = simcom_wwan_bind_8200(dev, intf);

    return ret;
}

static void simcom_wwan_unbind(struct usbnet *dev, struct usb_interface *intf)
{
    struct usb_driver *subdriver = (void *)dev->data[0];

    if (dev->udev->descriptor.bcdDevice == cpu_to_le16(0x232)) {
        return;
    }

    if (subdriver && subdriver->disconnect)
        subdriver->disconnect(intf);

    dev->data[0] = (unsigned long)NULL;
}

struct sk_buff *simcom_wwan_tx_fixup(struct usbnet *dev, struct sk_buff *skb, gfp_t flags)
{
    if (dev->udev->descriptor.bcdDevice == cpu_to_le16(0x232)) {
        return skb;
    }

    /* skip ethernet header */
    if (skb_pull(skb, ETH_HLEN))
        return skb;
    else
        dev_err(&dev->intf->dev, "Packet Dropped\n");

    if (skb != NULL)
        dev_kfree_skb_any(skb);

    return NULL;
}

static int simcom_wwan_rx_fixup(struct usbnet *dev, struct sk_buff *skb)
{
    __be16 proto;

    if (dev->udev->descriptor.bcdDevice == cpu_to_le16(0x232))
        return 1;

    /* This check is no longer done by usbnet */
    if (skb->len < dev->net->hard_header_len)
        return 0;

    switch (skb->data[0] & 0xf0) {
        case 0x40:
            printk("packetv4 coming ,,,\n");
            proto = htons(ETH_P_IP);
            break;

        case 0x60:
            printk("packetv6 coming ,,,\n");
            proto = htons(ETH_P_IPV6);
            break;

        case 0x00:
            printk("packet coming ,,,\n");

            if (is_multicast_ether_addr(skb->data))
                return 1;

            /* possibly bogus destination - rewrite just in case */
            skb_reset_mac_header(skb);
            goto fix_dest;

        default:
            /* pass along other packets without modifications */
            return 1;
    }

    if (skb_headroom(skb) < ETH_HLEN)
        return 0;

    skb_push(skb, ETH_HLEN);
    skb_reset_mac_header(skb);
    eth_hdr(skb)->h_proto = proto;
    memset(eth_hdr(skb)->h_source, 0, ETH_ALEN);

fix_dest:
    memcpy(eth_hdr(skb)->h_dest, dev->net->dev_addr, ETH_ALEN);

    return 1;
}

/* SIMCOM SIM8200 */
static const struct driver_info qmi_wwan_gobi = {
    .description    = "Qualcomm Gobi wwan/QMI device",
    .flags          = FLAG_WWAN,
    .bind           = qmi_wwan_bind_gobi,
    .unbind         = qmi_wwan_unbind_shared,
    .manage_power   = qmi_wwan_manage_power,
};

static const struct driver_info simcom_wwan_usbnet_driver_info = {
    .description    = "SIMCOM wwan/QMI device",
    .flags          = FLAG_WWAN,
    .bind           = simcom_wwan_bind,
    .unbind         = simcom_wwan_unbind,
    .rx_fixup       = simcom_wwan_rx_fixup,
    .tx_fixup       = simcom_wwan_tx_fixup,
};

/* Sierra Wireless provide equally useless interface descriptors
 * Devices in QMI mode can be switched between two different
 * configurations:
 *   a) USB interface #8 is QMI/wwan
 *   b) USB interfaces #8, #19 and #20 are QMI/wwan
 *
 * Both configurations provide a number of other interfaces (serial++),
 * some of which have the same endpoint configuration as we expect, so
 * a whitelist or blacklist is necessary.
 *
 * FIXME: The below whitelist should include BIT(20).  It does not
 * because I cannot get it to work...
 */
static const struct driver_info qmi_wwan_sierra = {
    .description    = "Sierra Wireless wwan/QMI device",
    .flags          = FLAG_WWAN,
    .bind           = qmi_wwan_bind_gobi,
    .unbind         = qmi_wwan_unbind_shared,
    .manage_power   = qmi_wwan_manage_power,
    .data           = BIT(8) | BIT(19), /* interface whitelist bitmap */
};

#define HUAWEI_VENDOR_ID    0x12D1
#define QMI_GOBI_DEVICE(vend, prod) \
    USB_DEVICE(vend, prod), \
    .driver_info = (unsigned long)&qmi_wwan_gobi

static const struct usb_device_id products[] = {
    {
        /* SIM8200 Modem Device in QMI mode */
        .match_flags = USB_DEVICE_ID_MATCH_DEVICE | USB_DEVICE_ID_MATCH_INT_INFO,
        .idVendor = 0x1e0e,
        .idProduct = 0x9001,
        .bInterfaceClass = 0xff,
        .bInterfaceSubClass = 0xff,
        .bInterfaceProtocol = 0x50,
        .driver_info = (unsigned long) &simcom_wwan_usbnet_driver_info,
    },
    {
        /* SIM8200 Modem Device in QMI mode(legacy) */
        .match_flags = USB_DEVICE_ID_MATCH_DEVICE | USB_DEVICE_ID_MATCH_INT_INFO,
        .idVendor = 0x1e0e,
        .idProduct = 0x9001,
        .bInterfaceClass = 0xff,
        .bInterfaceSubClass = 0xff,
        .bInterfaceProtocol = 0xff,
        .driver_info = (unsigned long) &simcom_wwan_usbnet_driver_info,
    },
    { QMI_GOBI_DEVICE(0x05c6, 0x9205) }, /* Gobi 2000 Modem device */
    { QMI_GOBI_DEVICE(0x05c6, 0x9215) }, /* Gobi 2000 Modem device */
    {} /* END */
};
MODULE_DEVICE_TABLE(usb, products);

static struct usb_driver qmi_wwan_driver = {
    .name                   = "qmi_wwan_simcom",
    .id_table               = products,
    .probe                  = usbnet_probe,
    .disconnect             = usbnet_disconnect,
    .suspend                = qmi_wwan_suspend,
    .resume                 = qmi_wwan_resume,
    .reset_resume           = qmi_wwan_resume,
    .supports_autosuspend   = 1,
};

static int __init qmi_wwan_init(void)
{
    return usb_register(&qmi_wwan_driver);
}
module_init(qmi_wwan_init);

static void __exit qmi_wwan_exit(void)
{
    usb_deregister(&qmi_wwan_driver);
}
module_exit(qmi_wwan_exit);

MODULE_AUTHOR("xiaobin.wang@simcom.com");
MODULE_DESCRIPTION("SIMCOM (QMI) WWAN driver");
MODULE_LICENSE("GPL");
