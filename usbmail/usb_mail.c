#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/sysfs.h>
#include <linux/mutex.h>

#define UM_AUTHOR	"frederic ferrandis"
#define UM_LICENSE	"GPL"
#define UM_VERSION	"v0.1"

#define UM_VENDOR_ID	0x1294
#define UM_DEVICE_ID	0x1320

static struct usb_device_id um_id[] = {
	{USB_DEVICE(UM_VENDOR_ID, UM_DEVICE_ID)},
	{},
};

MODULE_DEVICE_TABLE(usb, um_id);

static ssize_t color_show(struct device *dev,
				struct device_attribute *attr,
				char *buf);

static ssize_t color_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf,
				size_t count);

static DEVICE_ATTR(color,
		0666,
		color_show,
		color_store);

static DECLARE_WAIT_QUEUE_HEAD(urb_submit_headqueue);

struct um_device_driver {
	struct usb_host_endpoint	*um_inep;
	struct usb_host_endpoint	*um_outep;

	struct usb_device	*um_device;

	struct urb	*um_inurb;
	struct urb	*um_outurb;

	char	*um_inbuff;
	char	*um_outbuff;

	dma_addr_t um_indma;
	dma_addr_t um_outdma;

	char um_currcolor[12];

	struct mutex um_mutex;

	u8	um_ctx;
};

static struct um_device_driver um_desc;

static void um_intout_completion(struct urb *urb)
{
	struct device *dev = &um_desc.um_device->dev;
	int status;
	printk("count usage : %d\n", urb->use_count);
	status = urb->status;
	if(status) {
		dev_err(dev, "can't complete out urb(%d)\n", status);
	}
	--(um_desc.um_ctx);
	wake_up_interruptible(&urb_submit_headqueue);
}

static void um_intin_completion(struct urb *urb)
{
	struct device *dev = &um_desc.um_device->dev;
	int status;

	status = urb->status;
	if(status) {
		dev_err(dev, "can't complete in urb(%d)\n", status);
	}
}

static void um_send_new_color(void)
{
	static  char redbuff[] = { 0x2, 0x4, 0x4, 0x4, 0x4};
	int status;
	if(strncmp("red", um_desc.um_currcolor, 3) == 0) {
		memcpy(um_desc.um_outbuff, redbuff, sizeof(redbuff));	
	}
	printk("count usage : %d\n", um_desc.um_outurb->use_count);
	status = usb_submit_urb(um_desc.um_outurb, GFP_ATOMIC);
	if(status) {
		dev_err(&um_desc.um_device->dev, "can't send out urb\n");
	}
	return;
}

static ssize_t color_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	char *ptr = um_desc.um_currcolor[0] ? um_desc.um_currcolor : "none"; 
	return snprintf(buf, PAGE_SIZE, "%s\n", ptr);
}

static ssize_t color_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf,
				size_t count)
{
	mutex_lock(&um_desc.um_mutex);
	wait_event_interruptible(urb_submit_headqueue,
				um_desc.um_ctx == 0);
	++ (um_desc.um_ctx);
	sscanf(buf, "%12s", um_desc.um_currcolor);
	um_send_new_color();
	mutex_unlock(&um_desc.um_mutex);
	return strnlen(buf, PAGE_SIZE);
}

static void __devexit um_disconnect_free_ressources(void)
{
	usb_kill_urb(um_desc.um_outurb);
	usb_kill_urb(um_desc.um_inurb);

	usb_free_urb(um_desc.um_inurb);
	usb_free_urb(um_desc.um_outurb);

	usb_free_coherent(um_desc.um_device,
			  um_desc.um_outep->desc.wMaxPacketSize,
			  um_desc.um_outbuff,
			  um_desc.um_outdma);

	usb_free_coherent(um_desc.um_device,
			  um_desc.um_inep->desc.wMaxPacketSize,
			  um_desc.um_inbuff,
			  um_desc.um_indma);
}

static void __devexit um_disconnect(struct usb_interface *itf)
{
	device_remove_file(&um_desc.um_device->dev, &dev_attr_color);
	um_disconnect_free_ressources();
	usb_put_dev(um_desc.um_device);
}

static int __devinit um_probe_find_endpoints(struct usb_interface *inter)
{
	struct usb_host_interface *itf = inter->cur_altsetting;
	struct usb_interface_descriptor *itf_desc;
	struct usb_host_endpoint *curr;
	__u8 ep_cpt;

	if(!itf) {
		return -EINVAL;
	}
	itf_desc = &itf->desc;
	curr = itf->endpoint;

	for(ep_cpt = 0; ep_cpt < itf_desc->bNumEndpoints; ++ep_cpt, ++curr) {
		if(usb_endpoint_is_int_in(&curr->desc)) {
			um_desc.um_inep  = curr;
		} else if(usb_endpoint_is_int_out(&curr->desc)) {
			um_desc.um_outep = curr;
		}
	}
	return !(um_desc.um_inep != NULL && um_desc.um_outep != NULL);
}

static void __devinit um_probe_init_ressources(void)
{
	unsigned int intinpipe, intoutpipe;
	__u8 epinaddr, epoutaddr;

	epinaddr  = um_desc.um_inep->desc.bEndpointAddress;
	epoutaddr = um_desc.um_outep->desc.bEndpointAddress;

	intinpipe  = usb_rcvintpipe(um_desc.um_device, epinaddr);
	intoutpipe = usb_sndintpipe(um_desc.um_device, epoutaddr);

	usb_fill_int_urb(um_desc.um_inurb,
			um_desc.um_device,
			intinpipe,
			um_desc.um_inbuff,
			um_desc.um_inep->desc.wMaxPacketSize,
			um_intin_completion,
			NULL,
			um_desc.um_inep->desc.bInterval);

	um_desc.um_inurb->transfer_dma   = um_desc.um_indma;
	um_desc.um_inurb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP; 

	usb_fill_int_urb(um_desc.um_outurb,
			um_desc.um_device,
			intoutpipe,
			um_desc.um_outbuff,
			um_desc.um_outep->desc.wMaxPacketSize,
			um_intout_completion,
			NULL,
			um_desc.um_outep->desc.bInterval);

	um_desc.um_outurb->transfer_dma   = um_desc.um_outdma;
	um_desc.um_outurb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP; 
}

static int __devinit um_probe_alloc_ressources(void)
{
	um_desc.um_inurb = usb_alloc_urb(0, GFP_KERNEL);
	if(!um_desc.um_inurb) {
		return -ENOMEM;
	}

	um_desc.um_outurb = usb_alloc_urb(0, GFP_KERNEL);
	if(!um_desc.um_outurb) {
		goto um_alloc_outurb_err;
	}
	
	um_desc.um_outbuff = usb_alloc_coherent(um_desc.um_device,
					um_desc.um_outep->desc.wMaxPacketSize,
					GFP_ATOMIC,
					&um_desc.um_outdma);
	if(um_desc.um_outbuff == NULL) {
		goto um_alloc_outdma_err;
	}

	um_desc.um_inbuff = usb_alloc_coherent(um_desc.um_device,
					um_desc.um_inep->desc.wMaxPacketSize,
					GFP_ATOMIC,
					&um_desc.um_indma);
	if(um_desc.um_inbuff == NULL) {
		goto um_alloc_indma_err;
	}
	return 0;

um_alloc_indma_err:
	usb_free_coherent(um_desc.um_device,
			  um_desc.um_outep->desc.wMaxPacketSize,
			  um_desc.um_outbuff,
			  um_desc.um_outdma);
um_alloc_outdma_err:
	usb_free_urb(um_desc.um_outurb);
um_alloc_outurb_err:
	usb_free_urb(um_desc.um_inurb);

	return -ENOMEM;
}

static int __devinit um_probe(	struct usb_interface *interface,
				struct usb_device_id  const *did)
{
	struct usb_device *dev;
	int ret;

	dev = interface_to_usbdev(interface);
	if(!dev) {
		return -ENODEV;
	}

	um_desc.um_device = usb_get_dev(dev);
	ret = um_probe_find_endpoints(interface);
	if(ret) {
		return -EINVAL;
	}
	ret = um_probe_alloc_ressources();
	if(ret) {
		return ret;
	}
	um_probe_init_ressources();

	ret = device_create_file(&um_desc.um_device->dev, &dev_attr_color);

	dev_info(&dev->dev, "probe finishe\n");
	return ret;
}

static struct usb_driver um_driver = {
	.probe		= um_probe,
	.disconnect	= um_disconnect,
	.id_table	= um_id,
	.name		= "usbmail"
};

static int __init um_init(void)
{
	mutex_init(&um_desc.um_mutex);
	return usb_register(&um_driver);	
}

static void __exit um_exit(void)
{
	usb_deregister(&um_driver);
}

module_init(um_init);
module_exit(um_exit);


MODULE_LICENSE(UM_LICENSE);
MODULE_AUTHOR(UM_AUTHOR);
MODULE_VERSION(UM_VERSION);
