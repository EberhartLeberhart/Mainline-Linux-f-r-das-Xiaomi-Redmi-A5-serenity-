# Out-of-tree-Module, gegen den eigenen Redmi-A5-Kernel bauen:
#   make KDIR=~/ums9230-linux   (Standard, sonst Pfad zum eigenen Kernel-Baum)
# Laeuft auf dem Handy ein anderer Kernel-Stand als im Baum, die Version mitgeben:
#   make KERNELRELEASE=$(ssh redmi uname -r)
obj-m += redmi_vsp_frage.o redmi_vsp_an.o redmi_vsp_iommu.o redmi_vsp_auto.o

KDIR ?= $(HOME)/ums9230-linux
ARCH ?= arm64
CROSS_COMPILE ?= aarch64-linux-gnu-

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) W=1 modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean
