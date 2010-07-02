#KDIR  := /home/fred/RtEmbedded/kernels/linux-2.6.27/

KVERS= $(shell uname -r)
KDIR := /lib/modules/$(KVERS)/build/
PWD = $(shell pwd)

obj-m = lktrace_fs.o
lktrace_fs-objs =  lktracefs.o lktrace_filebool.o 


	
modules::
	$(MAKE)  -C $(KDIR) M=$(PWD) modules

clean::
	rm -fr *.o *.ko modules.order  Module.symvers lktrace_fs.mod.c .lk* .tmp_versions
