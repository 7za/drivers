#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb/input.h>
#include <linux/usb.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/timer.h>

/* define list */

#define STRESS_DRIVER_VERSION	"v1.0"
#define STRESS_DRIVER_AUTHOR	"frederic ferrandis"
#define STRESS_DRIVER_DESC	"usb stress mouse hid driver"
#define STRESS_DRIVER_LICENSE	"GPL"

#define USB_STRESS_VENDOR_ID  	(0x1d34) 
#define USB_STRESS_DEVICE_ID	(0x0020)

#define USB_STRESS_DRIVER_NAME	"usbstress"

#define BM_REQUEST_TYPE		(0x22)  /*interface|class|host_to_device*/
#define BM_REQUEST		(0x09)  /* set_report*/
#define BM_VALUE		(0x0200)
#define BM_INDEX		(0)
#define BM_LEN			(0x08)

#ifndef __lock_ctx
#define __lock_ctx
#endif

/* private prototype */

static void __exit usb_stress_exit(void);

static int  __init usb_stress_init(void);

static int  __devinit usb_stress_probe(	struct usb_interface *,
				const struct usb_device_id *);

static void __devexit usb_stress_disconnect(struct usb_interface *);

static void __devexit usb_stress_unset_in_endpoint(void);

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

/* usb stress driver specific data  */

struct usb_stress_endpoint_desc {
	size_t len;
	__u8   addr;
	u8     buff[8];
};

#define USB_STRESS_ENDPOINT_INITIALIZER \
	{ .len = 0, .addr = 0, .buff = { [0 ... 7] = 0 } }

struct usb_stress {
	struct usb_stress_endpoint_desc in;
	struct usb_stress_endpoint_desc out;
	struct usb_device *usbdev;   /* kernel representation of a usbdev    */
	struct usb_interface *usbitf;

	struct timer_list usbtimer;

	spinlock_t usbspin;
	struct mutex usbmutex;

	struct urb *urbint;
	struct urb *urbctrl;
	struct usb_ctrlrequest *ctrl;
};

#define USB_STRESS_INITIALIZER 					\
	{	.in	= USB_STRESS_ENDPOINT_INITIALIZER,	\
	  	.out	= USB_STRESS_ENDPOINT_INITIALIZER,	\
	  	.usbdev  = NULL,			  	\
	  	.usbitf  = NULL,				\
		.urbint  = NULL,				\
	  	.urbctrl = NULL,				\
		.ctrl	 = NULL,				\
	}

static struct usb_stress usbstress = USB_STRESS_INITIALIZER;


static char* usb_submit_urb_err(int err)
{
	char *ret = NULL;
	static const char *unknow = "unknown error";
	static const char *usb_submit_ret_to_str [] = {
		[0]		= "succcess\n",
		[ENOMEM]	= "oom\n",
		[EPIPE]		= "stalled ep\n",
		[ENODEV]	= "no device\n",
		[EAGAIN]	= "too many packet\n",
		[EFBIG]		= "too request frame\n",
	};
	err = -err;
	if(err < 0 || err >= ARRAY_SIZE(usb_submit_ret_to_str) ) {
		return (char*)unknow;	
	}
	ret = (char*)usb_submit_ret_to_str[err];
	if(ret == NULL) {
		ret = (char*)unknow;
	}
	return ret;
}

static int usb_stress_init_ctrlrequest(	struct usb_ctrlrequest *req,
					__u8  breqtype,
					__u8  breq,
					__u16 val,
					__u16 index,
					__u16 len)
{
	if(unlikely(req == NULL)) {
		return -ENOMEM;
	}

	req->bRequestType	= breqtype;
	req->bRequest		= breq;
	req->wValue		= cpu_to_le16(val);
	req->wIndex		= cpu_to_le16(index);
	req->wLength		= cpu_to_le16(len);
	return 0;
}

static void usb_stress_interrupt_handler(struct urb *urb)
{
	int status;

	if(unlikely(urb == NULL)) {
		dev_info(&usbstress.usbdev->dev, "urb completion NULL \n");
		return;	
	}
	status = urb->status;
	if(status != 0) {
		dev_info(&usbstress.usbdev->dev, "urb int_in err[%d]\n",
			status);
		return;
	} else {
		dev_info(&usbstress.usbdev->dev, "urb int_in success[%d]\n",
			status);
		return;
	}
}

static void usb_stress_ctrl_completion(struct urb *urb)
{
	int status;
	u32 cpt;
	if(unlikely(urb == NULL)) {
		dev_info(&usbstress.usbdev->dev, "urb completion NULL \n");
		return;
	}
	status = urb->status;
	dev_info(&usbstress.usbdev->dev, "status = %d\n", status);

	if(status == 0) {
		struct usb_ctrlrequest *curreq = (struct usb_ctrlrequest*)
							urb->context;
		char *data  = urb->transfer_buffer;
		dev_info(&usbstress.usbdev->dev, "urb transmited\n");
		if(data) {
			dev_info(&usbstress.usbdev->dev, "msg_%s : ",
				(urb->pipe & USB_DIR_IN)?"in":"out");
			for(cpt = 0; cpt < urb->transfer_buffer_length; ++cpt) {
				printk("0x%x ", data[cpt]);				
			}
			printk("\n");
		}
		if(curreq) {
			kfree(curreq);
		}
	} else {
		if(status == -EOVERFLOW) {
			dev_info(&usbstress.usbdev->dev, "urb overflow error\n");
		} else {
			dev_info(&usbstress.usbdev->dev, "urb error = %d\n", status);
		}
	}
	usb_free_urb(urb);
}

static int usb_stress_ask_stick_status(void)
{
	struct urb *urb = NULL;
	char *msg;
	struct usb_ctrlrequest *request = kmalloc(sizeof(*request), GFP_KERNEL);
	struct usb_device *dev = usbstress.usbdev;
	int controlpipe = 0, ret;
	char *msg_err;

	if(unlikely(request == NULL)) {
		dev_info(&dev->dev, "allocation error\n");
		return -ENOMEM;
	}
	usb_stress_init_ctrlrequest(	request,
					BM_REQUEST_TYPE, 
					BM_REQUEST,
					BM_VALUE,
					BM_INDEX,
					BM_LEN);

	urb = usb_alloc_urb(0, GFP_KERNEL);

	if(unlikely(urb == NULL)) {
		dev_info(&dev->dev, "urb allocation error\n");
		goto urb_alloc_error;
	}

	msg = kzalloc(8*sizeof(char), GFP_KERNEL);
	msg[7] = 9;

	/* create controlpipe thanks to usbdevice and associated 0 endpoint */
	controlpipe = usb_sndctrlpipe(dev, 0);
	usb_fill_control_urb(	urb,
				dev,
				controlpipe,
				(unsigned char*)request,
				(void*)msg,
				BM_LEN,
				usb_stress_ctrl_completion,
				(void*)request);

	ret = usb_submit_urb(urb, GFP_ATOMIC);
	msg_err = usb_submit_urb_err(ret);
	dev_info(&dev->dev, "urb_submit : %s", msg_err);
	return ret;

urb_alloc_error:
	kfree(request);
	return -ENOMEM;
}

static void usb_stress_poll(unsigned long data)
{
	int ret;
	struct urb *urb = usbstress.urbint;

	ret = usb_stress_ask_stick_status();

	if(ret != 0) {
		dev_info(&usbstress.usbdev->dev, "int urb error %d\n", ret);
	}
	mod_timer(&usbstress.usbtimer, jiffies + 4 * HZ);
}

static void __devexit usb_stress_disconnect(struct usb_interface *itf)
{
	struct usb_device *dev;
	dev_info(&interface_to_usbdev(itf)->dev, "disconnect\n");
	usb_set_intfdata(itf, NULL);
	dev = interface_to_usbdev(itf);
	usb_put_dev(dev);
	mutex_lock(&usbstress.usbmutex);
	usb_stress_unset_in_endpoint();
	mutex_unlock(&usbstress.usbmutex);
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
	if(ret && ret->enabled)
		return ret;
	else
		return NULL;
}

__lock_ctx
static void __devexit usb_stress_unset_in_endpoint(void)
{
}

__lock_ctx
static void __devinit usb_stress_set_in_endpoint(struct usb_host_endpoint *ep)
{
	struct usb_stress_endpoint_desc *desc = &usbstress.in;
	desc->len  = le16_to_cpu(ep->desc.wMaxPacketSize);
	desc->addr = ep->desc.bEndpointAddress;
}

__lock_ctx
static void __devinit usb_stress_set_out_endpoint(struct usb_host_endpoint *ep)
{
	struct usb_stress_endpoint_desc *desc = &usbstress.out;
	desc->addr = ep->desc.bEndpointAddress;
}

static int __devinit usb_stress_prepare_int_in_ep(struct usb_host_endpoint *e)
{
	unsigned int pipe = 0;
	__u16 maxpacket = 0;
	char *data = NULL;

	usbstress.urbint = usb_alloc_urb(0, GFP_KERNEL);
	if(unlikely(usbstress.urbint == NULL)) {
		return -ENOMEM;
	}
	
	pipe = usb_rcvintpipe(usbstress.usbdev, e->desc.bEndpointAddress);
	maxpacket = usb_maxpacket(usbstress.usbdev, pipe, usb_pipeout(pipe)); 
	data = kzalloc(maxpacket, GFP_KERNEL);
	if(unlikely(data == NULL)) {
		goto urb_int_buffer_alloc_err;
	}

	usb_fill_int_urb(usbstress.urbint,
			 usbstress.usbdev,
			 pipe,
			 data,
			 maxpacket > 8 ? 8 : maxpacket,
			 usb_stress_interrupt_handler,
			 &usbstress,
			 e->desc.bInterval);

	usbstress.urbint->transfer_flags = 0x3;
	dev_info(&usbstress.usbdev->dev,
		"create urb int_in[tflag=%u, pkt_size=%u, interval=%u, epaddr=%d]\n",
		usbstress.urbint->transfer_flags,
		maxpacket,
		e->desc.bInterval,
		e->desc.bEndpointAddress);
	return 0;

urb_int_buffer_alloc_err:
	dev_info(&usbstress.usbdev->dev, "can't alloc urb_int buffer\n");
	return -ENOMEM;
}

static int __devinit usb_stress_probe(	struct usb_interface *itf,
					const struct usb_device_id *id)
{
	struct usb_host_endpoint *endpoint;
	struct usb_device *device;
	int ret;

	device = interface_to_usbdev(itf);
	usbstress.usbdev = usb_get_dev(device);
	usbstress.usbitf = itf;

	endpoint = usb_stress_find_endpoint(itf);
	if(unlikely(endpoint == NULL)) {
		dev_info(&device->dev, "%s : can't find endpoint\n", __func__);
		return -EIO;
	}
	dev_info(&device->dev, "find endpoint[addr=%x, len=%x, dtype=%x]\n",
		endpoint->desc.bEndpointAddress, 
		endpoint->desc.bLength,
		endpoint->desc.bDescriptorType);
	mutex_lock(&usbstress.usbmutex);
	usb_stress_set_in_endpoint(endpoint);
	usb_stress_set_out_endpoint(endpoint);
	mutex_unlock(&usbstress.usbmutex);

	usb_set_intfdata(itf, &usbstress);
	usb_disable_autosuspend(device);


	/*
	ret = usb_stress_prepare_int_in_ep(endpoint);
	if(ret) {
		return ret;
	}
	dev_info(&device->dev, "submit urb in %s = %d", __func__, ret);
	*/
	dev_info(&device->dev, "%s probe success\n", __func__);

	setup_timer(	&usbstress.usbtimer,
			usb_stress_poll,
			(unsigned long)&usbstress);
	mod_timer(&usbstress.usbtimer, jiffies + 4 * HZ);
	return 0;
}

#ifndef  __unused
# define __unused __attribute__((unused))
#endif

static void __unused usb_stress_display_attr(struct usb_interface *inter)
{
	struct usb_device *dev = interface_to_usbdev(inter);
	struct usb_host_interface *interface;
	dev_info(&dev->dev, "display current function setting\n");
	interface = inter->cur_altsetting;
	dev_info(&dev->dev, "function_data[name=%s, extra=%.*s]\n",
		interface->string, 
		interface->extralen,
		interface->extra);
	dev_info(&dev->dev, "function_desc[len=%u, type=%u, idfunc=%u]\n",
			interface->desc.bLength,
			interface->desc.bDescriptorType,
			interface->desc.bInterfaceNumber);
	dev_info(&dev->dev, "function_desc[nbep=%u, class=%u, subclass=%u]\n",
			interface->desc.bNumEndpoints,
			interface->desc.bInterfaceClass,
			interface->desc.bInterfaceSubClass);
	dev_info(&dev->dev, "function_desc[proto=%u, itf=%u]\n",
			interface->desc.bInterfaceProtocol,
			interface->desc.iInterface);
}

static void __init usb_stress_init_data(void)
{
	spin_lock_init(&usbstress.usbspin);
	mutex_init(&usbstress.usbmutex);
	init_timer(&usbstress.usbtimer);
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
	del_timer(&usbstress.usbtimer);
	usb_deregister(&usb_stress_driver);
}

module_init(usb_stress_init);
module_exit(usb_stress_exit);

MODULE_AUTHOR(STRESS_DRIVER_AUTHOR);
MODULE_LICENSE(STRESS_DRIVER_LICENSE);
MODULE_DESCRIPTION(STRESS_DRIVER_DESC);
MODULE_VERSION(STRESS_DRIVER_VERSION);
