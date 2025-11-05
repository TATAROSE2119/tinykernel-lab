# CMakeLists.txt
cmake_minimum_required(VERSION 3.5)
set(CMAKE_TOOLCHAIN_FILE ${CMAKE_SOURCE_DIR}/cmake/toolchains/Toolchain-arm-linux-gnueabihf.cmake)

project(TinyLinux-IoT-KernelLab LANGUAGES C)

# ========== NFS 部署 ==========
set(NFS_ROOTFS /home/py/linux/nfs/rootfs)
set(MODULE_INSTALL_DIR ${NFS_ROOTFS}/lib/modules/4.1.15/tinyLinux_IoT_kernellab)
set(BIN_INSTALL_DIR    ${NFS_ROOTFS}/lib/modules/4.1.15/tinyLinux_IoT_kernellab/usr/bin)
file(MAKE_DIRECTORY ${MODULE_INSTALL_DIR} ${BIN_INSTALL_DIR})

# ========== 内核路径 ==========
set(KERNEL_SRC /home/py/linux/linux-imx-rel_imx_4.1.15_2.1.0_ga_alientek)
if(NOT EXISTS ${KERNEL_SRC}/Makefile)
    message(FATAL_ERROR "KERNEL_SRC not found: ${KERNEL_SRC}")
endif()

# ========== 编译用户态 app ==========
add_subdirectory(app)

# ========== 编译内核模块 ==========
add_subdirectory(drivers/LED)