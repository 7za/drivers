#include <linux/init.h>
#include <linux/usb/input.h>
#include <linux/usb.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/hid.h>

/* define list */

#ifndef  __unused
# define __unused __attribute__((unused))
#endif

#define STICKDRV_VERSION	"v1.0"
#define STICKDRV_AUTHOR		"frederic ferrandis"
#define STICKDRV_DESC		"usb stress mouse hid driver"
#define STICKDRV_LICENSE	"GPL"

#define STICKDRV_VENDOR_ID  	(0x1d34) 
#define STICKDRV_DEVICE_ID	(0x0020)

#define STICK_STICKDRV_NAME	"stickdrv"

/*
 * according to hid report descriptor, value are included
 * physically between [0 et 255] and logically between 
 * 0x80 -> 0x7f
 */
#define X_AXIS_MIN (0)
#define X_AXIS_MAX (255)
#define Y_AXIS_MIN (0)
#define Y_AXIS_MAX (255)


#define BM_LEN		(8)
#define BM_VALUE	(0x200)
#define BM_INDEX	(0)
#define BM_REQUESTYPE	(0x21)
#define BM_REQUEST	(0x9)

#define SS_INTPUT_PHYSLEN (64)
#define SS_INTPUT_NAMELEN (32)

/* private prototype */

static void __exit stick_stress_exit(void);

static int  __init stick_stress_init(void);

static int  __devinit stick_stress_probe(struct usb_interface *,
					 const struct usb_device_id *);

static void __devexit stick_stress_disconnect(struct usb_interface *);

/* record device id in usbcore subsystem */

static struct usb_device_id stick_stress_id[] = {
	{ USB_DEVICE(STICKDRV_VENDOR_ID, STICKDRV_DEVICE_ID) },
	{ }
};

MODULE_DEVICE_TABLE(usb, stick_stress_id);

/* usb driver that manage this device */

static struct usb_driver stick_stress_driver = {
	.name		=	STICK_STICKDRV_NAME,
	.probe		=	stick_stress_probe,
	.disconnect	=	stick_stress_disconnect,
	.id_table	=	stick_stress_id,
};

/*
 * stick_stress driver descriptor
 * @us_ep	: endpoint used by interrupt reception
 * @us_dev	: usb_device representation
 * @us_itf	: hid descriptor whose descriptor report can be parsed
 * @us_input	: input device representation
 * @us_urbctrl	: urb control object
 * @us_reqctrl	: controler request
 * @us_urbint	: urb interrupt object
 * @us_buffint	: interrupt (in) buffer
 * @us_dmaint	: interrupt (in) dma
 * @us_buffctrl : ctrl (in) buffer
 * @us_dmactrl	: ctrl (in) dma
 * @us_lock	: spinlock
 * @us_mutex	: mutex
 */
struct stick_stress {
	struct usb_host_endpoint*us_ep;
	struct usb_device	*us_dev; 
	struct usb_interface	*us_itf;
	struct hid_descriptor	*us_hid;

	struct input_dev	*us_input;

	struct urb		*us_urbctrl;
	struct usb_ctrlrequest	*us_reqctrl;

	struct urb		*us_urbint;
	char			*us_buffint;
	dma_addr_t		 us_dmaint;

	char			*us_buffctrl;
	dma_addr_t		 us_dmactrl;

	spinlock_t us_lock;
	struct mutex us_mutex;

	char us_phys[SS_INTPUT_PHYSLEN];
	char us_name[SS_INTPUT_NAMELEN];
};

/* all field 0 initialized */
static struct stick_stress stickdrv;

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

static void stick_stress_ctrl_completion(struct urb *urb)
{
	struct device *dev = &stickdrv.us_dev->dev;
	int status;
	if(unlikely(urb == NULL)) {
		dev_dbg(dev, "urb completion NULL \n");
		return;
	}
	status = urb->status;

	if(status) {
		char *errstr = usb_completion_status_err(status);
		dev_dbg(dev, "urbctrl completion err[%s]\n", errstr);
	}
}

static int stick_stress_ask_stick_status(void)
{
	int ret;
	char *msg_err;
	static int cpt = 0;
	spin_lock(&stickdrv.us_lock);
	if(cpt & 0x1) {
		stickdrv.us_buffctrl[0] = 0x0f; 
		stickdrv.us_buffctrl[7] = 0x08; 
	} else {
		stickdrv.us_buffctrl[0] = 0x0;
		stickdrv.us_buffctrl[7] = 0x09;
	}
	cpt++;
	spin_unlock(&stickdrv.us_lock);
	
	ret = usb_submit_urb(stickdrv.us_urbint, GFP_ATOMIC);
	msg_err = usb_submit_urb_err(ret);
	dev_dbg(&stickdrv.us_dev->dev, "inturb_submit : %s\n ",
		msg_err);

	ret = usb_submit_urb(stickdrv.us_urbctrl, GFP_ATOMIC);
	msg_err = usb_submit_urb_err(ret);
	dev_dbg(&stickdrv.us_dev->dev, "ctrlurb_submit : %s\n ",
		msg_err);
	return ret;
}

static void stick_stress_process_packet(int x, int y)
{
	if(x < 0) {
		x = X_AXIS_MIN + abs(x);
	} else {
		x = X_AXIS_MAX - x;
	}
	input_report_abs(stickdrv.us_input, REL_X, x);

	y = Y_AXIS_MAX - abs(y); 	
	input_report_abs(stickdrv.us_input, REL_Y, y);

	input_sync(stickdrv.us_input);

}

static void stick_stress_irq(struct urb *urb)
{
	u32 cpt;
	int x, y;
	int status = urb->status;
	struct device *dev = &stickdrv.us_dev->dev;
	if(status == 0) {
		dev_dbg(dev, "urb transmited\n");
		if(urb->actual_length == 8) {
			x = stickdrv.us_buffint[1];
			y = stickdrv.us_buffint[2];
			if(x || y) {
				stick_stress_process_packet(x, y);
				for(	cpt = 0; 
					cpt< urb->transfer_buffer_length; 
					++cpt) {
					printk("%d ", stickdrv.us_buffint[cpt]);
				}
				printk("\n");
			}
		}
	} else {
		char *errstr = usb_completion_status_err(status);
		dev_dbg(dev, "urbint completion err[%s]\n", errstr);
	}
	
	stick_stress_ask_stick_status();
}

static int stick_stress_open(struct input_dev *dev)
{
	return stick_stress_ask_stick_status();
}

static void stick_stress_close(struct input_dev *dev)
{
	usb_kill_urb(stickdrv.us_urbint);
	usb_kill_urb(stickdrv.us_urbctrl);
}

static void __devexit stick_stress_free_urb(void)
{
	if(stickdrv.us_buffctrl) {
		usb_free_coherent(stickdrv.us_dev,
				BM_LEN,
				stickdrv.us_buffctrl,
			  	stickdrv.us_dmactrl);
		 stickdrv.us_buffctrl = NULL;
	}
	if(stickdrv.us_reqctrl) {
		kfree(stickdrv.us_reqctrl);
		stickdrv.us_reqctrl = NULL;
	}
	if(stickdrv.us_urbctrl) {
		usb_free_urb(stickdrv.us_urbctrl);
		stickdrv.us_urbctrl = NULL;
	}
	if(stickdrv.us_buffint) {
		usb_free_coherent(stickdrv.us_dev,
				BM_LEN,
				stickdrv.us_buffint,
				stickdrv.us_dmaint);
		stickdrv.us_buffint = NULL;
	}
	if(stickdrv.us_urbint) {
		usb_free_urb(stickdrv.us_urbint);
		stickdrv.us_urbint = NULL;
	}
}

static void __devexit stick_stress_disconnect(struct usb_interface *itf)
{
	struct usb_device *dev;
	dev_dbg(&interface_to_usbdev(itf)->dev, "disconnect\n");
	usb_set_intfdata(itf, NULL);
	dev = interface_to_usbdev(itf);
	usb_put_dev(dev);

	if(stickdrv.us_input) {
		input_unregister_device(stickdrv.us_input);
	}

	usb_kill_urb(stickdrv.us_urbctrl);
	usb_kill_urb(stickdrv.us_urbint);

	stick_stress_free_urb();
}

static void __devinit stick_stress_find_endpoint(struct usb_interface *itf)
{
	struct usb_host_endpoint *curr = NULL;
	struct usb_host_interface *interface;
	struct usb_interface_descriptor *desc;
	__u8 i     = 0;
	__u8 numep = 0;

	interface	= itf->cur_altsetting;
	desc		= &interface->desc;
	numep		= desc->bNumEndpoints;

	for(i = 0, curr = interface->endpoint;
	    i < numep && !stickdrv.us_ep; 
	    ++i, ++curr) {
		int type = usb_endpoint_is_int_in(&curr->desc);
		if(type) {
			stickdrv.us_ep = curr;
		}
	}
}

/*------------------ init urbs -------------------------------*/
static int __devinit stick_stress_alloc_urb(void)
{
	stickdrv.us_urbctrl = usb_alloc_urb(0, GFP_KERNEL);
	if(stickdrv.us_urbctrl == NULL) {
		return -ENOMEM;
	}

	stickdrv.us_reqctrl = 
			kzalloc(sizeof(*stickdrv.us_reqctrl), GFP_KERNEL);

	stickdrv.us_urbint = usb_alloc_urb(0, GFP_KERNEL);
	if(stickdrv.us_urbint == NULL) {
		goto usb_alloc_urb_int_err;
	}

	stickdrv.us_buffctrl = usb_alloc_coherent(	stickdrv.us_dev,
							BM_LEN,
							GFP_ATOMIC,
							&stickdrv.us_dmactrl);
	if(stickdrv.us_buffctrl == NULL) {
		goto usb_alloc_dmactrl_err;
	}
	
	stickdrv.us_buffint = usb_alloc_coherent(	stickdrv.us_dev,
							BM_LEN,
							GFP_ATOMIC,
							&stickdrv.us_dmaint);
	if(stickdrv.us_buffint == NULL) {
		goto usb_alloc_dmaint_err;
	}

	return 0;

usb_alloc_dmaint_err:
	usb_free_coherent(stickdrv.us_dev, BM_LEN,  stickdrv.us_buffctrl,
			stickdrv.us_dmactrl);
usb_alloc_dmactrl_err:
	usb_free_urb(stickdrv.us_urbint);
usb_alloc_urb_int_err:	
	usb_free_urb(stickdrv.us_urbctrl);

	return -ENOMEM;
}

static int __devinit stick_stress_init_ctrlurb(void)
{
	unsigned int ctrlpipe = usb_sndctrlpipe(stickdrv.us_dev, 0);
	stickdrv.us_reqctrl->bRequestType  = 	(USB_TYPE_CLASS | USB_DIR_OUT |
						USB_RECIP_INTERFACE);
	stickdrv.us_reqctrl->bRequest = USB_REQ_SET_CONFIGURATION;
	stickdrv.us_reqctrl->wValue   = cpu_to_le16(BM_VALUE);
	stickdrv.us_reqctrl->wIndex   = cpu_to_le16(BM_INDEX);
	stickdrv.us_reqctrl->wLength  = cpu_to_le16(4*BM_LEN);

	usb_fill_control_urb(	stickdrv.us_urbctrl,
				stickdrv.us_dev,
				ctrlpipe,
				(unsigned char*)stickdrv.us_reqctrl,
				stickdrv.us_buffctrl,
				BM_LEN,
				stick_stress_ctrl_completion,
				&stickdrv);
	stickdrv.us_urbctrl->transfer_dma    = stickdrv.us_dmactrl;
	stickdrv.us_urbctrl->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	return 0;						
}

static int __devinit stick_stress_init_inturb(void)
{
	__u8 ep_addr  = stickdrv.us_ep->desc.bEndpointAddress;
	__u8 ep_itval = stickdrv.us_ep->desc.bInterval;
	unsigned int intpipe = usb_rcvintpipe(	stickdrv.us_dev,
						ep_addr);

	usb_fill_int_urb(stickdrv.us_urbint,
			 stickdrv.us_dev,
			 intpipe,
			 stickdrv.us_buffint,
			 BM_LEN,
			 stick_stress_irq,
			 &stickdrv,
			 ep_itval);
	stickdrv.us_urbint->transfer_dma    = stickdrv.us_dmaint;
	stickdrv.us_urbint->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	return 0;
}

static int __devinit stick_stress_prepare_urb(void)
{
	int ret;

	ret = stick_stress_alloc_urb();
	if(ret) {
		return ret;
	}
	stick_stress_init_ctrlurb();
	stick_stress_init_inturb();
	return ret;
}

static int __devinit stick_stress_init_input_dev(struct usb_interface *itf)
{
	int ret;
	stickdrv.us_input = input_allocate_device();
	if(stickdrv.us_input == NULL) {
		return -ENOMEM;
	}
	ret = usb_make_path(	stickdrv.us_dev,
				stickdrv.us_phys,
				sizeof(stickdrv.us_phys));
	if(ret < 0) {
		goto makepath_err;
	}
	strlcat(stickdrv.us_phys, "/input0", sizeof(stickdrv.us_phys));
	snprintf(stickdrv.us_name, sizeof(stickdrv.us_name), "stick stress");

	stickdrv.us_input->name = stickdrv.us_name;
	stickdrv.us_input->phys = stickdrv.us_phys;
	usb_to_input_id(stickdrv.us_dev, &stickdrv.us_input->id);
	stickdrv.us_input->dev.parent =  &itf->dev;
	
	stickdrv.us_input->evbit[0] = BIT_MASK(EV_ABS);
	input_set_abs_params(stickdrv.us_input, ABS_X, 0, X_AXIS_MAX, 0, 0);
	input_set_abs_params(stickdrv.us_input, ABS_Y, 0, Y_AXIS_MAX, 0, 0);

	stickdrv.us_input->open  = stick_stress_open;
	stickdrv.us_input->close = stick_stress_close;

	ret = input_register_device(stickdrv.us_input);
	if(ret) {
		goto makepath_err;
	}

	return 0;

makepath_err:
	input_free_device(stickdrv.us_input);
	return -ENOMEM;
}

/*-----------------------------------------------------------------*/

static int __devinit stick_stress_probe(struct usb_interface *itf,
					const struct usb_device_id *id __unused)
{
	struct usb_device *device;
	int ret = 0;

	device = interface_to_usbdev(itf);
	stickdrv.us_dev = usb_get_dev(device);
	stickdrv.us_itf = itf;

	stick_stress_find_endpoint(itf);
	if(unlikely(stickdrv.us_ep == NULL)) {
		dev_dbg(&device->dev, "%s : can't find endpoint\n", __func__);
		return -EIO;
	}
	usb_set_intfdata(itf, &stickdrv);
	ret = stick_stress_prepare_urb();

	if(ret) {
		dev_dbg(&device->dev, "prepare urb error\n");
		goto probe_err;
	}

	
	ret = stick_stress_init_input_dev(itf);
	if(ret) {
		dev_dbg(&device->dev, "input register error\n");
		goto probe_err;
	}
	
	dev_dbg(&device->dev, "%s probe success\n", __func__);

	return 0;
probe_err:
	usb_set_intfdata(itf, NULL);
	usb_put_dev(device);
	return ret;
}

/* __init/__exit functions  */
static void __init stick_stress_init_data(void)
{
	spin_lock_init(&stickdrv.us_lock);
	mutex_init(&stickdrv.us_mutex);
}

static int __init stick_stress_init(void)
{
	int retval;
	stick_stress_init_data();

	retval = usb_register(&stick_stress_driver);
	if(likely(retval != 0)) {
		err("error usb_register\n");
	}

	return retval;
}

static void __exit stick_stress_exit(void)
{
	usb_deregister(&stick_stress_driver);
}

module_init(stick_stress_init);
module_exit(stick_stress_exit);

MODULE_AUTHOR(STICKDRV_AUTHOR);
MODULE_LICENSE(STICKDRV_LICENSE);
MODULE_DESCRIPTION(STICKDRV_DESC);
MODULE_VERSION(STICKDRV_VERSION);

/*

static int __devinit stick_stress_get_hid_desc(struct usb_host_interface *itf)
{
	int ret;

	if(itf == NULL) {
		return -EINVAL;
	}

	ret = usb_get_extra_descriptor(itf, HID_DT_HID, &stickdrv.us_hid);
	if(ret) {
		ret =  usb_get_extra_descriptor(&itf->endpoint[0], 
						HID_DT_REPORT,
						&stickdrv.us_hid);
	}
	if(ret) {
		dev_dbg(&stickdrv.us_dev->dev, "no hid desc found\n");
		ret = -ENODEV;
	}
	return ret;
}

static int __devinit stick_stress_get_hid_report(struct usb_interface *itf)
{
	struct usb_device *dev = stickdrv.us_dev;
	__le16 len = stickdrv.us_hid->desc[0].wDescriptorLength;
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
		dev_dbg(&dev->dev, "can't get report descriptor\n");
	}

	kfree(buff);
	return (ret > 0) ? 0 : ret;
}

*/
