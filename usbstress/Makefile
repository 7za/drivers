KVERS= $(shell uname -r)
KDIR := /lib/modules/$(KVERS)/build/
PWD = $(shell pwd)

obj-m = stickdrv.o
stickdrv-objs = stick_drv.o 


	
modules::
	$(MAKE)  -C $(KDIR) M=$(PWD) modules

clean::
	rm -fr *.o *.ko modules.order  Module.symvers stick*.mod.c .stick* .tmp_versions
