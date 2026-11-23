obj-m += redmi_vsp_drv.o
KDIR ?= $(HOME)/ums9230-linux
all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- W=1 modules
clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- clean
