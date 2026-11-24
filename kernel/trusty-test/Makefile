# Redmi A5: trusty test modules (port of Unisoc/Google GPL trusty drivers)
obj-m += trusty.o trusty-irq.o trusty-log.o trusty-virtio.o trusty-ipc.o
trusty-y := trusty-core.o trusty-mem.o
ccflags-y += -I$(src)/include

KDIR ?= $(HOME)/ums9230-linux

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- modules
clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- clean
