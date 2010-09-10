#include <linux/init.h>
#include <linux/usb/input.h>
#include <linux/usb.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/hid.h>
#include <linux/unaligned/access_ok.h>

/* define list */

#ifndef  __unused
# define __unused __attribute__((unused))
#endif

#define STRESS_DRIVER_VERSION	"v1.0"
#define STRESS_DRIVER_AUTHOR	"frederic ferrandis"
#define STRESS_DRIVER_DESC	"usb stress mouse hid driver"
#define STRESS_DRIVER_LICENSE	"GPL"

#define USB_STRESS_VENDOR_ID  	(0x1d34) 
#define USB_STRESS_DEVICE_ID	(0x0020)

#define USB_STRESS_DRIVER_NAME	"usbstress"

#define BM_LEN		(8)
#define BM_VALUE	(0x200)
#define BM_INDEX	(0)
#define BM_REQUESTYPE	(0x21)
#define BM_REQUEST	(0x9)

#undef  DEBUG
#define DEBUG

/* private prototype */

static void __exit usb_stress_exit(void);

static int  __init usb_stress_init(void);

static int  __devinit usb_stress_probe(	struct usb_interface *,
				const struct usb_device_id *);

static void __devexit usb_stress_disconnect(struct usb_interface *);


/* record device id in usbcore subsystem */

static struct usb_device_id usb_stress_id[] = {
	{ USB_DEVICE(USB_STRESS_VENDOR_ID, USB_STRESS_DEVICE_ID) },
	{ }
};

MODULE_DEVICE_TABLE(usb, usb_stress_id);

/* usb driver that manage this device */

static struct usb_driver usb_stress_driver = {
	.name		=	USB_STRESS_DRIVER_NAME,
	.probe		=	usb_stress_probe,
	.disconnect	=	usb_stress_disconnect,
	.id_table	=	usb_stress_id,
	.supports_autosuspend = 0,
};

struct usb_stress {
	struct usb_host_endpoint*us_ep;
	struct usb_device	*us_dev; 
	struct usb_interface	*us_itf;
	struct hid_descriptor	*us_hid;

	struct urb		*us_urbctrl;
	struct usb_ctrlrequest	*us_reqctrl;

	struct urb		*us_urbint;
	char			*us_buffint;
	dma_addr_t		 us_dmaint;

	char			*us_buffctrl;
	dma_addr_t		 us_dmactrl;

	struct timer_list us_timer;

	spinlock_t us_lock;
	struct mutex us_mutex;

};

static struct usb_stress usbstress;

/* convert status'err code in string */
static char* usb_completion_status_err(int err)
{
	switch(err) {
	case 0:			return "success";
	case -EOVERFLOW:	return "overflow error";
	case -EPIPE:		return "stalled endpoint";
	default:		return "generic error";
	}
}

/* convert err code return by usb_submit_urb in string */
static char* usb_submit_urb_err(int err)
{
	switch(err) {
	case 0:		return "success";
	case -EPIPE:	return "unknownn urb";
	case -ENODEV:	return "no device";
	case -EAGAIN:	return "too many packet";
	default:	return "generic error";
	}
}

static void usb_stress_ctrl_completion(struct urb *urb)
{
	struct device *dev = &usbstress.us_dev->dev;
	int status;
	u32 cpt;
	if(unlikely(urb == NULL)) {
		dev_info(dev, "urb completion NULL \n");
		return;
	}
	status = urb->status;

	if(status == 0) {
		char *data  = urb->transfer_buffer;
		dev_info(dev, "urb transmited\n");
		if(data) {
			dev_info(dev, "msg_%s : ",
				(urb->pipe & USB_DIR_IN)?"in":"out");
			for(cpt = 0; cpt < urb->transfer_buffer_length; ++cpt) {
				printk("0x%x ", data[cpt]);				
			}
			printk("\n");
		}
	} else {
		char *errstr = usb_completion_status_err(status);
		dev_err(dev, "urb completion err[%s]\n", errstr);
	}
}

static void usb_stress_irq(struct urb *urb)
{
	u32 cpt;
	int status = urb->status;
	struct device *dev = &usbstress.us_dev->dev;
	if(status == 0) {
		dev_info(dev, "urb transmited\n");
		for(cpt = 0; cpt< urb->transfer_buffer_length; ++cpt) {
			printk("%d ", usbstress.us_buffint[cpt]);
		}
		printk("\n");
	} else {
		char *errstr = usb_completion_status_err(status);
		dev_err(dev, "urb completion err[%s]\n", errstr);
	}
}

/* submit control urb to endpoint 0, with msg = {0, 0, 0, 0, 0, 0, 0, 9} */
static int usb_stress_ask_stick_status(void)
{
	int ret;
	char *msg_err;
	static int cpt = 0;
	if(cpt & 0x1) {
		usbstress.us_buffctrl[0] = 0x0f; 
		usbstress.us_buffctrl[7] = 0x08; 
	} else {
		usbstress.us_buffctrl[0] = 0x0;
		usbstress.us_buffctrl[7] = 0x09;
	}
	cpt++;
	
	ret = usb_submit_urb(usbstress.us_urbint, GFP_ATOMIC);
	msg_err = usb_submit_urb_err(ret);
	dev_info(&usbstress.us_dev->dev, "inturb_submit : %s\n ",
		msg_err);

	ret = usb_submit_urb(usbstress.us_urbctrl, GFP_ATOMIC);
	msg_err = usb_submit_urb_err(ret);
	dev_info(&usbstress.us_dev->dev, "ctrlurb_submit : %s\n ",
		msg_err);
	return ret;
}

static void usb_stress_poll(unsigned long data __unused)
{
	usb_stress_ask_stick_status();
	mod_timer(&usbstress.us_timer, jiffies + 2 * HZ);
}

static void __devexit usb_stress_free_urb(void)
{
	if(usbstress.us_buffctrl) {
		usb_free_coherent(usbstress.us_dev,
				BM_LEN,
				usbstress.us_buffctrl,
			  	usbstress.us_dmactrl);
		 usbstress.us_buffctrl = NULL;
	}
	if(usbstress.us_reqctrl) {
		kfree(usbstress.us_reqctrl);
		usbstress.us_reqctrl = NULL;
	}
	if(usbstress.us_urbctrl) {
		usb_free_urb(usbstress.us_urbctrl);
		usbstress.us_urbctrl = NULL;
	}
	if(usbstress.us_buffint) {
		usb_free_coherent(usbstress.us_dev,
				BM_LEN,
				usbstress.us_buffint,
				usbstress.us_dmaint);
		usbstress.us_buffint = NULL;
	}
	if(usbstress.us_urbint) {
		usb_free_urb(usbstress.us_urbint);
		usbstress.us_urbint = NULL;
	}
}

static void __devexit usb_stress_disconnect(struct usb_interface *itf)
{
	struct usb_device *dev;
	dev_info(&interface_to_usbdev(itf)->dev, "disconnect\n");
	usb_set_intfdata(itf, NULL);
	dev = interface_to_usbdev(itf);
	usb_put_dev(dev);
	usb_kill_urb(usbstress.us_urbctrl);

	usb_stress_free_urb();
}

static struct usb_host_endpoint* __devinit usb_stress_find_endpoint(
						struct usb_interface *itf)
{
	struct usb_host_endpoint *ret  = NULL;
	struct usb_host_endpoint *curr = NULL;
	struct usb_host_interface *interface;
	struct usb_interface_descriptor *desc;
	__u8 i     = 0;
	__u8 numep = 0;

	interface	= itf->cur_altsetting;
	desc		= &interface->desc;
	numep		= desc->bNumEndpoints;

	for(i = 0, curr = interface->endpoint; i < numep && !ret; ++i, ++curr) {
		int type = usb_endpoint_is_int_in(&curr->desc);
		if(type) {
			ret = curr;
		}
	}
	return ret;
}
/*------------------ init urbs -------------------------------*/

static int __devinit usb_stress_alloc_urb(void)
{
	usbstress.us_urbctrl = usb_alloc_urb(0, GFP_KERNEL);
	if(usbstress.us_urbctrl == NULL) {
		return -ENOMEM;
	}
	usbstress.us_urbint = usb_alloc_urb(0, GFP_KERNEL);
	if(usbstress.us_urbint == NULL) {
		goto usb_alloc_urb_int_err;
	}

	usbstress.us_reqctrl = 
			kzalloc(sizeof(*usbstress.us_reqctrl), GFP_KERNEL);
	if(usbstress.us_reqctrl == NULL) {
		goto usb_alloc_ctrl_err;
	}

	usbstress.us_buffctrl = usb_alloc_coherent(	usbstress.us_dev,
							BM_LEN,
							GFP_ATOMIC,
							&usbstress.us_dmactrl);
	if(usbstress.us_buffctrl == NULL) {
		goto usb_alloc_dmactrl_err;
	}
	
	usbstress.us_buffint = usb_alloc_coherent(	usbstress.us_dev,
							BM_LEN,
							GFP_ATOMIC,
							&usbstress.us_dmaint);
	if(usbstress.us_buffint == NULL) {
		goto usb_alloc_dmaint_err;
	}
	
	
	return 0;

usb_alloc_dmaint_err:
	usb_free_coherent(usbstress.us_dev, BM_LEN,  usbstress.us_buffctrl,
			usbstress.us_dmactrl);
usb_alloc_dmactrl_err:
	kfree(usbstress.us_reqctrl );
usb_alloc_ctrl_err:
	usb_free_urb(usbstress.us_urbint);
usb_alloc_urb_int_err:	
	usb_free_urb(usbstress.us_urbctrl);

	return -ENOMEM;
}

static int __devinit usb_stress_init_ctrlurb(void)
{
	unsigned int ctrlpipe = usb_sndctrlpipe(usbstress.us_dev, 0);
	usbstress.us_reqctrl->bRequestType  = 	(USB_TYPE_CLASS | USB_DIR_OUT |
						USB_RECIP_INTERFACE);
	usbstress.us_reqctrl->bRequest	    = USB_REQ_SET_CONFIGURATION;
	usbstress.us_reqctrl->wValue  = cpu_to_le16(BM_VALUE);
	usbstress.us_reqctrl->wIndex  = cpu_to_le16(BM_INDEX);
	usbstress.us_reqctrl->wLength = cpu_to_le16(4*BM_LEN);

	usb_fill_control_urb(	usbstress.us_urbctrl,
				usbstress.us_dev,
				ctrlpipe,
				(unsigned char*)usbstress.us_reqctrl,
				usbstress.us_buffctrl,
				BM_LEN,
				usb_stress_ctrl_completion,
				&usbstress);
	usbstress.us_urbctrl->transfer_dma = usbstress.us_dmactrl;
	usbstress.us_urbctrl->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	return 0;						
}

static int __devinit usb_stress_init_inturb(void)
{
	__u8 ep_addr  = usbstress.us_ep->desc.bEndpointAddress;
	__u8 ep_itval = usbstress.us_ep->desc.bInterval;
	unsigned int intpipe = usb_rcvintpipe(	usbstress.us_dev,
						ep_addr);

	usb_fill_int_urb(usbstress.us_urbint,
			 usbstress.us_dev,
			 intpipe,
			 usbstress.us_buffint,
			 BM_LEN,
			 usb_stress_irq,
			 &usbstress,
			 ep_itval);
	usbstress.us_urbint->transfer_dma    = usbstress.us_dmaint;
	usbstress.us_urbint->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	return 0;
}

static int __devinit usb_stress_prepare_urb(void)
{
	int ret;

	ret = usb_stress_alloc_urb();
	if(ret) {
		return ret;
	}
	usb_stress_init_ctrlurb();
	usb_stress_init_inturb();
	return ret;
}

/*-----------------------------------------------------------------*/

static int __devinit usb_stress_get_hid_desc(struct usb_host_interface *itf)
{
	int ret;

	if(itf == NULL) {
		return -EINVAL;
	}

	ret = usb_get_extra_descriptor(itf, HID_DT_HID, &usbstress.us_hid);
	if(ret) {
		ret =  usb_get_extra_descriptor(&itf->endpoint[0], 
						HID_DT_REPORT,
						&usbstress.us_hid);
	}
	if(ret) {
		dev_err(&usbstress.us_dev->dev, "no hid desc found\n");
		ret = -ENODEV;
	}
	return ret;
}

static int __devinit usb_stress_get_hid_report(struct usb_interface *itf)
{
	struct usb_device *dev = usbstress.us_dev;
	__le16 len = usbstress.us_hid->desc[0].wDescriptorLength;
	char *buff;
	int ctrlpipe = usb_rcvctrlpipe(dev, 0);
	int ret;

	buff = kzalloc(len, GFP_KERNEL);
	if(buff == NULL) {
		return -ENOMEM;
	}
	ret = usb_control_msg(	dev, ctrlpipe, USB_REQ_GET_DESCRIPTOR,
				USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_INTERFACE,
				0x200, 
				0,
				buff,
				len,
				1000);

	if(ret > 0) {
		int i;
		printk("report descriptor dump\n");
		for(i = 0; i < ret; ++i) {
			printk("%u ", buff[i]);
		}
	} else {
		dev_err(&dev->dev, "can't get report descriptor\n");
	}

	kfree(buff);
	return (ret > 0) ? 0 : ret;
}

static int __devinit usb_stress_probe(	struct usb_interface *itf,
					const struct usb_device_id *id __unused)
{
	struct usb_host_endpoint *endpoint;
	struct usb_device *device;
	int ret = 0;

	device = interface_to_usbdev(itf);
	usbstress.us_dev = usb_get_dev(device);
	usbstress.us_itf = itf;

	endpoint = usb_stress_find_endpoint(itf);
	if(unlikely(endpoint == NULL)) {
		dev_info(&device->dev, "%s : can't find endpoint\n", __func__);
		return -EIO;
	}
	usbstress.us_ep = endpoint;
	usb_set_intfdata(itf, &usbstress);
	ret = usb_stress_prepare_urb();

	if(ret) {
		dev_err(&device->dev, "prepare urb error\n");
		goto probe_err;
	}

/*
	ret = usb_stress_get_hid_desc(itf->cur_altsetting);

	if(ret) {
		goto probe_err;
	}


	ret = usb_stress_get_hid_report(itf);
	if(ret) {
		goto probe_err;
	}
	
*/
	setup_timer(	&usbstress.us_timer,
			usb_stress_poll,
			(unsigned long)&usbstress);
	mod_timer(&usbstress.us_timer, jiffies + 2 * HZ);

	dev_info(&device->dev, "%s probe success\n", __func__);

	return 0;
probe_err:
	usb_set_intfdata(itf, NULL);
	usb_put_dev(device);
	return ret;
}

static void __init usb_stress_init_data(void)
{
	spin_lock_init(&usbstress.us_lock);
	mutex_init(&usbstress.us_mutex);
	init_timer(&usbstress.us_timer);
}

static int __init usb_stress_init(void)
{
	int retval;
	usb_stress_init_data();

	retval = usb_register(&usb_stress_driver);
	if(likely(retval != 0)) {
		printk("error usb_register\n");
	}

	return retval;
}

static void __exit usb_stress_exit(void)
{
	del_timer(&usbstress.us_timer);
	usb_deregister(&usb_stress_driver);
}

module_init(usb_stress_init);
module_exit(usb_stress_exit);

MODULE_AUTHOR(STRESS_DRIVER_AUTHOR);
MODULE_LICENSE(STRESS_DRIVER_LICENSE);
MODULE_DESCRIPTION(STRESS_DRIVER_DESC);
MODULE_VERSION(STRESS_DRIVER_VERSION);

