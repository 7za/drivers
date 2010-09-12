#ifndef MISSILE_TENX_SYSFS_H
#define MISSILE_TENX_SYSFS_H

#include <linux/device.h>
#include <linux/usb.h>
#include "missile_tenx_type.h"

#define missile_tenx_sysfs_create_file(filename,actioname) \
	static ssize_t missile_tenx_sysfs_set_##actioname (struct device *dev, \
			                                   struct device_attribute *attr,\
							   char const *buf, \
							   size_t count ) \
        { \
		struct usb_interface *intf = to_usb_interface(dev); \
		missile_tenx_device_t *ptr = usb_get_intfdata(intf); \
                missile_tenx_##actioname(ptr); \
                return count; \
        } \
        static ssize_t missile_tenx_sysfs_get_##actioname ( struct device *dev, \
			                                    struct device_attribute *attr, \
							    char *buff)\
	{\
		return 0;\
	}\
	static DEVICE_ATTR(filename, S_IWUGO | S_IRUGO, \
			   missile_tenx_sysfs_get_##filename,\
			   missile_tenx_sysfs_set_##filename)

int missile_tenx_sysfs_init(struct device *);
void missile_tenx_sysfs_exit(struct device *);
#endif
