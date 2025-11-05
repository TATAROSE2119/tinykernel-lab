# Makefile（根目录）
PROJECT_ROOT := $(shell pwd)
BUILD_DIR    := $(PROJECT_ROOT)/build
KERNELDIR    := /home/py/linux/linux-imx-rel_imx_4.1.15_2.1.0_ga_alientek
ARCH         ?= arm
CROSS_COMPILE?= arm-linux-gnueabihf-

.PHONY: all build cmake-config cmake-build compile_db clean

all: build

# ========= 1. 配置 CMake =========
cmake-config:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && \
	 cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ..

# ========= 2. 构建（用户态 + 内核模块）=========
cmake-build: compile_db
	@cd $(BUILD_DIR) && cmake --build .

# ========= 3. 完整构建（配置 + 构建 + 合并 JSON）=========
build: cmake-build
	@echo "Merging compile_commands.json..."
	@if [ -f $(BUILD_DIR)/compile_commands.json ]; then \
		jq -s '.[0] + .[1]' $(BUILD_DIR)/compile_commands.json $(PROJECT_ROOT)/compile_commands.json \
		> $(PROJECT_ROOT)/compile_commands.tmp.json 2>/dev/null || true; \
		mv $(PROJECT_ROOT)/compile_commands.tmp.json $(PROJECT_ROOT)/compile_commands.json; \
	else \
		[ -f $(BUILD_DIR)/compile_commands.json ] && \
		cp $(BUILD_DIR)/compile_commands.json $(PROJECT_ROOT)/compile_commands.json || true; \
	fi

# ========= 4. 生成 compile_commands.json（拦截所有命令）=========
compile_db: cmake-config
	@echo "Generating compile_commands.json with bear..."
	@cd $(BUILD_DIR) && \
	 bear --cdb $(PROJECT_ROOT)/compile_commands.json cmake --build .

# ========= 5. 清理 =========
clean:
	@rm -rf $(BUILD_DIR)
	@rm -f $(PROJECT_ROOT)/compile_commands.json
	@find $(PROJECT_ROOT) -name "*.ko" -o -name "*.o" -o -name "*.mod.c" -o -name "Module.symvers" | xargs rm -f