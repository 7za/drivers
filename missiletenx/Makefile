missile_tenx-objs := missile_tenx_cmd.o missile_tenx_core.o missile_tenx_netlink.o
obj-m += missile_tenx.o


KVERSION = $(shell uname -r)
all:
	        make -C /lib/modules/$(KVERSION)/build M=$(PWD) modules
clean:
	        make -C /lib/modules/$(KVERSION)/build M=$(PWD) clean
install:
		sudo cp *.ko /lib/modules/$(KVERSION)/kernel/drivers/usb/
		sudo depmod -ae
