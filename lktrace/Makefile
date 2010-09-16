KVERS= $(shell uname -r)
KDIR := /lib/modules/$(KVERS)/build/
PWD = $(shell pwd)

obj-m = lktrace_fs.o
lktrace_fs-objs =  lktracefs.o lktrace_filebool.o lktrace_debugfs.o 


	
modules::
	$(MAKE)  -C $(KDIR) M=$(PWD) modules

clean::
	$(MAKE)  -C $(KDIR) M=$(PWD) clean
