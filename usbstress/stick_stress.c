#include <linux/module.h>
#include <linux/usb.h>
#include <linux/slab.h>
#include <linux/hid.h>


#define SS_VENDOR_ID  	(0x1d34) 
#define SS_DEVICE_ID	(0x0020)

#define SS_DRV_VERSION	"v0.1"
#define SS_DRV_LICENSE	"GPL"
#define SS_DRV_AUTHOR	"frederic ferrandis"
MODULE_AUTHOR(SS_DRV_AUTHOR);
MODULE_LICENSE(SS_DRV_LICENSE);
MODULE_VERSION(SS_DRV_VERSION);


struct stick_stress {
	struct hid_device *ss_dev;
	struct mutex	  *ss_mutex;
};

static struct stick_stress;

static const struct ss_hid_id __devinitconst ss_id = {
	{HID_USB_DEVICE(SS_VENDOR_ID, SS_DEVICE_ID)},
	{}
};

MODULE_DEVICE_TABLE(hid, ss_id);

/* modprobe/rmmod drv function */

static struct hid_driver ss_driver = {
	.name		= "stress_stick",
	.id_table	= ss_id,
	.report_fixup	= ss_report_fixup,
	.input_mapped	= ss_input_mapped,
	.event		= ss_event,
	.probe		= ss_probe,
};

static int __init ss_init(void)
{
	int ret = hid_register_driver(&ss_driver);
	return ret;
}

static void __exit ss_exit(void)
{
	hid_unregister_driver(&ss_driver);
}

module_init(ss_init);
module_exit(ss_exit);
