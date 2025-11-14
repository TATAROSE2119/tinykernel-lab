KERNELDIR := /home/py/linux/linux-imx-rel_imx_4.1.15_2.1.0_ga_alientek
CURRENT_PATH := $(shell pwd)

ARCH ?= arm
CROSS_COMPILE ?= arm-linux-gnueabihf-

obj-m :=  iic_ap3216c.o

.PHONY: all build kernel_modules clean compile_db app

all: build app

build: kernel_modules

kernel_modules:
	$(MAKE) -C $(KERNELDIR) M=$(CURRENT_PATH) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules
clean:
	$(MAKE) -C $(KERNELDIR) M=$(CURRENT_PATH) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean
	rm -f input_key.ko
	@rm -f compile_commands.json
compile_db:
	bear $(MAKE) -C $(KERNELDIR) M=$(CURRENT_PATH) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules
ifeq ($(wildcard $(CURRENT_PATH)/.build_success),)
	@echo "Build success"
else
	@echo "Build failed"
endif
