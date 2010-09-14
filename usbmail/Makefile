KVERS= $(shell uname -r)
KDIR := /lib/modules/$(KVERS)/build/
PWD = $(shell pwd)

obj-m = usbmail.o
usbmail-objs = usb_mail.o 


	
modules::
	$(MAKE)  -C $(KDIR) M=$(PWD) modules

clean::
	$(MAKE)  -C $(KDIR) M=$(PWD) clean
