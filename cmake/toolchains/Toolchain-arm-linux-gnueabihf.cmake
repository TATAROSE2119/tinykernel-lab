# Toolchain for i.MX6ULL - Linaro 4.9.4

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(TINYKERNEL_TOOLCHAIN_ACTIVE TRUE CACHE INTERNAL
    "TinyKernel ARM cross toolchain was loaded")

set(TOOLCHAIN_PREFIX "" CACHE PATH "Optional Linaro toolchain install prefix")
set(CROSS_COMPILE "arm-linux-gnueabihf-" CACHE STRING "Cross compiler prefix")

if(TOOLCHAIN_PREFIX)
    set(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}/bin/${CROSS_COMPILE}gcc)
    set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}/bin/${CROSS_COMPILE}g++)
else()
    find_program(ARM_GCC ${CROSS_COMPILE}gcc)
    find_program(ARM_GXX ${CROSS_COMPILE}g++)
    if(NOT ARM_GCC OR NOT ARM_GXX)
        message(FATAL_ERROR
            "Cross compiler not found. Install ${CROSS_COMPILE}gcc/${CROSS_COMPILE}g++ "
            "or configure with -DTOOLCHAIN_PREFIX=/path/to/gcc-linaro.")
    endif()
    set(CMAKE_C_COMPILER ${ARM_GCC})
    set(CMAKE_CXX_COMPILER ${ARM_GXX})
endif()

set(TINYKERNEL_C_COMPILER "${CMAKE_C_COMPILER}" CACHE INTERNAL
    "C compiler selected by the TinyKernel ARM toolchain")

if(TOOLCHAIN_PREFIX AND EXISTS ${TOOLCHAIN_PREFIX}/arm-linux-gnueabihf/libc)
    set(CMAKE_SYSROOT ${TOOLCHAIN_PREFIX}/arm-linux-gnueabihf/libc)
endif()

if(CMAKE_SYSROOT)
    set(CMAKE_FIND_ROOT_PATH
        ${CMAKE_SYSROOT}
        ${TOOLCHAIN_PREFIX}/lib
        ${TOOLCHAIN_PREFIX}/include
    )
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# 验证编译器是否存在
if(NOT EXISTS ${CMAKE_C_COMPILER})
    message(FATAL_ERROR "Cross compiler not found: ${CMAKE_C_COMPILER}")
endif()

# i.MX6ULL / Cortex-A7 ABI flags. CMAKE_C_FLAGS_INIT is intended for use in a
# toolchain file and is applied when CMake first initializes the compiler.
set(CMAKE_C_FLAGS_INIT
    "-march=armv7-a -mtune=cortex-a7 -mfpu=neon -mfloat-abi=hard")
