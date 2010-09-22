#include <linux/module.h>
#include <linux/kallsyms.h>
#include <linux/kprobes.h>


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


static __attribute__((used)) noinline  int color_handler (struct kprobe *probe, struct pt_regs *reg)
{
	struct um_device_driver *dd = kallsyms_lookup_name("um_desc");

	

	if(dd) {
		pr_info("current color = %s\n", dd->um_currcolor);
	}

	if(reg && reg->cx) {
		char *new_color = (char*)reg->cx;
		pr_info("try to set color with %s\n", new_color);
	}

	return 0;
}

//EXPORT_SYMBOL(color_handler);
MODULE_LICENSE("GPL");
