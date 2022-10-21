# Ref: https://www.kernel.org/doc/Documentation/kbuild/modules.txt
ifneq ($(KERNELRELEASE),)
# kbuild part of makefile
include Kbuild

else
# normal makefile
KDIR ?= /lib/modules/`uname -r`/build

default:
	$(MAKE) -C $(KDIR) M=$$PWD

install:
	$(MAKE) -C $(KDIR) M=$$PWD modules_install
	depmod -A

clean:
	$(MAKE) -C $(KDIR) M=$$PWD clean

endif
