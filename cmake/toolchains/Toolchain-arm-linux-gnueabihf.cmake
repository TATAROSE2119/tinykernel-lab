# Toolchain for i.MX6ULL - Linaro 4.9.4

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# ========== 你的实际路径 ==========
set(TOOLCHAIN_PREFIX /usr/local/arm/gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabihf)
# ====================================

set(CROSS_COMPILE arm-linux-gnueabihf-)

# 指定编译器
set(CMAKE_C_COMPILER   ${TOOLCHAIN_PREFIX}/bin/${CROSS_COMPILE}gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}/bin/${CROSS_COMPILE}g++)

# sysroot（Linaro 工具链自带）
set(CMAKE_SYSROOT ${TOOLCHAIN_PREFIX}/arm-linux-gnueabihf/libc)

# 查找路径
set(CMAKE_FIND_ROOT_PATH 
    ${CMAKE_SYSROOT}
    ${TOOLCHAIN_PREFIX}/lib
    ${TOOLCHAIN_PREFIX}/include
)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# 验证编译器是否存在
if(NOT EXISTS ${CMAKE_C_COMPILER})
    message(FATAL_ERROR "Cross compiler not found: ${CMAKE_C_COMPILER}")
endif()

# 强制使用 -march=armv7-a（i.MX6ULL 必须）
add_compile_options(-march=armv7-a -mtune=cortex-a7 -mfpu=neon -mfloat-abi=hard)