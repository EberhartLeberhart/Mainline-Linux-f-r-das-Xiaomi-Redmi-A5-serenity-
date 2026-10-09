# Out-of-tree-Modul, gegen den eigenen Redmi-A5-Kernel bauen:
#   make KDIR=/pfad/zu/linux ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
obj-m += wcn_starttest.o

KDIR ?= ../../../linux
ARCH ?= arm64
CROSS_COMPILE ?= aarch64-linux-gnu-

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean
