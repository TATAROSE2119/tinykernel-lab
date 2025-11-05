KERNELDIR     := /home/py/linux/linux-imx-rel_imx_4.1.15_2.1.0_ga_alientek
CURRENT_PATH  := $(shell pwd)

# 交叉编译可选（不需要就删掉这两行）
ARCH          ?= arm
CROSS_COMPILE ?= arm-linux-gnueabihf-

# 只编译内核模块
obj-m :=  leddriver.o

# 若有多个源文件，可用：
# timer-objs := timer_core.o timer_of.o

.PHONY: all build kernel_modules clean compile_db app

all: build app

build: kernel_modules

# 编译内核模块
kernel_modules:
	$(MAKE) -C $(KERNELDIR) M=$(CURRENT_PATH) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

# 编译用户空间应用程序
app:
	$(CROSS_COMPILE)gcc -o platledApp platledApp.c

# 如果没有使用交叉编译工具链，使用系统默认gcc
app-native:
	gcc -o platledApp platledApp.c

clean:
	$(MAKE) -C $(KERNELDIR) M=$(CURRENT_PATH) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean
	rm -f platledApp
	@rm -f compile_commands.json

# 生成 compile_commands.json（推荐）
compile_db:
	bear $(MAKE) -C $(KERNELDIR) M=$(CURRENT_PATH) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules
ifeq ($(wildcard $(CURRENT_PATH)/.build_success),)
	@echo "Build success"
else
	@echo "Build failed"
endif
