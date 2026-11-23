obj-m += redmi_vsp_drv.o
KDIR ?= $(HOME)/ums9230-linux
CROSS ?= aarch64-linux-gnu-

all: modul test

modul:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=arm64 CROSS_COMPILE=$(CROSS) W=1 modules

test: redmi_vsp_test.c
	$(CROSS)gcc -static -O2 -Wall -Wextra -o redmi_vsp_test redmi_vsp_test.c

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=arm64 CROSS_COMPILE=$(CROSS) clean
	rm -f redmi_vsp_test
