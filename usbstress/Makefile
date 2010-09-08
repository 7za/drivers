KVERS= $(shell uname -r)
KDIR := /lib/modules/$(KVERS)/build/
PWD = $(shell pwd)

obj-m = usb_stress.o
usb_stress-objs = usbstress.o 


	
modules::
	$(MAKE)  -C $(KDIR) M=$(PWD) modules

clean::
	rm -fr *.o *.ko modules.order  Module.symvers usb_stress.mod.c .usb* .tmp_versions
