KVERS= $(shell uname -r)
KDIR := /lib/modules/$(KVERS)/build/
PWD = $(shell pwd)

obj-m = handler_s.o
handler_s-objs =  handler_sample.o 


	
modules::
	$(MAKE)  -C $(KDIR) M=$(PWD) modules

clean::
	$(MAKE)  -C $(KDIR) M=$(PWD) clean
