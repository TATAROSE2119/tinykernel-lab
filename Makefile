# Makefile（根目录）
PROJECT_ROOT ?= $(CURDIR)
BUILD_DIR    ?= $(PROJECT_ROOT)/build
KERNELDIR    ?= /home/tatarose_laptop_wsl/linux-imx-rel_imx_4.1.15_2.1.0_ga_alientek
NFS_ROOTFS   ?= /home/tatarose_laptop_wsl/rootfs
ARCH         ?= arm
CROSS_COMPILE?= arm-linux-gnueabihf-
TOOLCHAIN_PREFIX ?=
CMAKE        ?= cmake
TOOLCHAIN_FILE := $(PROJECT_ROOT)/cmake/toolchains/Toolchain-arm-linux-gnueabihf.cmake
COMPILE_DB_MERGER := $(PROJECT_ROOT)/cmake/MergeCompileCommands.cmake

ifneq ($(strip $(TOOLCHAIN_PREFIX)),)
EXPECTED_C_COMPILER := $(abspath $(TOOLCHAIN_PREFIX))/bin/$(CROSS_COMPILE)gcc
else
EXPECTED_C_COMPILER := $(shell command -v $(CROSS_COMPILE)gcc 2>/dev/null)
endif

CMAKE_ARGS := -DCMAKE_TOOLCHAIN_FILE=$(TOOLCHAIN_FILE)
CMAKE_ARGS += -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
CMAKE_ARGS += -DKERNEL_SRC=$(KERNELDIR)
CMAKE_ARGS += -DNFS_ROOTFS=$(NFS_ROOTFS)
CMAKE_ARGS += -DKERNEL_ARCH=$(ARCH)
CMAKE_ARGS += -DKERNEL_CROSS_COMPILE=$(CROSS_COMPILE)
CMAKE_ARGS += -DCROSS_COMPILE=$(CROSS_COMPILE)

ifneq ($(strip $(TOOLCHAIN_PREFIX)),)
CMAKE_ARGS += -DTOOLCHAIN_PREFIX=$(TOOLCHAIN_PREFIX)
endif

.PHONY: all build cmake-config cmake-build compile_db clean

all: build

# ========= 1. 配置 CMake =========
cmake-config:
	@if [ -f "$(BUILD_DIR)/CMakeCache.txt" ]; then \
		cached_compiler=$$(sed -n 's|^TINYKERNEL_C_COMPILER:INTERNAL=||p' "$(BUILD_DIR)/CMakeCache.txt"); \
		toolchain_active=$$(sed -n 's|^TINYKERNEL_TOOLCHAIN_ACTIVE:INTERNAL=||p' "$(BUILD_DIR)/CMakeCache.txt"); \
		cache_mismatch=0; \
		[ "$$toolchain_active" = "TRUE" ] || cache_mismatch=1; \
		if [ -n "$(EXPECTED_C_COMPILER)" ] && [ "$$cached_compiler" != "$(EXPECTED_C_COMPILER)" ]; then \
			cache_mismatch=1; \
		fi; \
		if [ "$$cache_mismatch" -eq 1 ]; then \
			echo "CMake compiler cache is stale ($${cached_compiler:-unknown}); recreating $(BUILD_DIR)"; \
			$(CMAKE) -E remove_directory "$(BUILD_DIR)"; \
		fi; \
	fi
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && \
	 ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) \
	 $(CMAKE) $(CMAKE_ARGS) ..

# ========= 2. 构建（用户态 + 内核模块）=========
cmake-build: compile_db
	@cd $(BUILD_DIR) && cmake --build .

# ========= 3. 完整构建（配置 + 构建 + 合并 JSON）=========
build: cmake-build
	@echo "Merging compile_commands.json..."
	@$(CMAKE) \
		-DPRIMARY=$(BUILD_DIR)/compile_commands.json \
		-DSECONDARY=$(PROJECT_ROOT)/compile_commands.json \
		-DOUTPUT=$(PROJECT_ROOT)/compile_commands.tmp.json \
		-P $(COMPILE_DB_MERGER)
	@$(CMAKE) -E rename \
		$(PROJECT_ROOT)/compile_commands.tmp.json \
		$(PROJECT_ROOT)/compile_commands.json

# ========= 4. 生成 compile_commands.json（拦截所有命令）=========
compile_db: cmake-config
	@echo "Generating compile_commands.json with bear..."
	@cd $(BUILD_DIR) && \
	 bear --output $(PROJECT_ROOT)/compile_commands.json -- cmake --build .


# ========= 5. 清理 =========
clean:
	@rm -rf $(BUILD_DIR)
	@rm -f $(PROJECT_ROOT)/compile_commands.json $(PROJECT_ROOT)/compile_commands.tmp.json
	@find $(PROJECT_ROOT) -name "*.ko" -o -name "*.o" -o -name "*.mod.c" -o -name "Module.symvers" | xargs rm -f
