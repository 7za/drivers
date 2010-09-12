#include "missile_tenx_sysfs.h"

#include "missile_tenx_cmd.h"

#include <linux/mutex.h>

missile_tenx_sysfs_create_file(fire, fire);

missile_tenx_sysfs_create_file(left, left);

missile_tenx_sysfs_create_file(right, right);

static DEFINE_MUTEX(missile_tenx_sysfs_lock);

int missile_tenx_sysfs_init(struct device *dev)
{
	static int count = 0;
	mutex_lock(&missile_tenx_sysfs_lock);
	if (!count) {
		info("sysfs file creation\n");
		device_create_file(dev, &dev_attr_fire);
		device_create_file(dev, &dev_attr_left);
		device_create_file(dev, &dev_attr_right);
	}
	count++;
	mutex_unlock(&missile_tenx_sysfs_lock);
	return 0;
}

void missile_tenx_sysfs_exit(struct device *dev)
{
	static int count = 0;
	mutex_lock(&missile_tenx_sysfs_lock);
	if (!count) {
		info("sysfs file deletion\n");
		device_remove_file(dev, &dev_attr_fire);
		device_remove_file(dev, &dev_attr_left);
		device_remove_file(dev, &dev_attr_right);
	}
	count++;
	mutex_unlock(&missile_tenx_sysfs_lock);
}
