#ifndef MISSILE_TENX_CONST_H
#define MISSILE_TENX_CONST_H

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <asm/uaccess.h>
#include <linux/usb.h>
#include <linux/mutex.h>

#define USB_MISSILE_TENX_VENDOR_ID	  (0x1130)
#define USB_MISSILE_TENX_PRODUCT_ID	  (0x0202)
#define USB_MISSILE_TENX_MINOR_BASE	  (192)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Frederic Ferrandis");

static struct usb_device_id missile_tenx_table[] = {
	{USB_DEVICE(USB_MISSILE_TENX_VENDOR_ID, USB_MISSILE_TENX_PRODUCT_ID)},
	{}
};

MODULE_DEVICE_TABLE(usb, missile_tenx_table);

#endif				/* MISSILE_TENX_CONST_H */
