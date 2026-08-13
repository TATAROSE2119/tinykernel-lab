# CMakeLists.txt
cmake_minimum_required(VERSION 3.5)

project(TinyLinux-IoT-KernelLab LANGUAGES C)

# ========== NFS 部署 ==========
set(DEFAULT_NFS_ROOTFS /home/tatarose_laptop_wsl/rootfs)
if(DEFINED ENV{NFS_ROOTFS})
  set(DEFAULT_NFS_ROOTFS $ENV{NFS_ROOTFS})
endif()
set(NFS_ROOTFS ${DEFAULT_NFS_ROOTFS} CACHE PATH "Target root filesystem for deployment")
set(KERNEL_VERSION 4.1.15 CACHE STRING "Target kernel version")
set(MODULE_INSTALL_DIR ${NFS_ROOTFS}/lib/modules/${KERNEL_VERSION})

# ========== 内核路径 ==========
set(DEFAULT_KERNEL_SRC /home/tatarose_laptop_wsl/linux-imx-rel_imx_4.1.15_2.1.0_ga_alientek)
if(DEFINED ENV{KERNELDIR})
  set(DEFAULT_KERNEL_SRC $ENV{KERNELDIR})
endif()
set(KERNEL_SRC ${DEFAULT_KERNEL_SRC} CACHE PATH "Linux kernel source tree")
set(KERNEL_ARCH arm CACHE STRING "Linux kernel target architecture")
set(KERNEL_CROSS_COMPILE arm-linux-gnueabihf- CACHE STRING "Linux kernel cross compiler prefix")
if(NOT EXISTS ${KERNEL_SRC}/Makefile)
  message(FATAL_ERROR "KERNEL_SRC not found: ${KERNEL_SRC}")
endif()

# ========== 编译用户态 app ==========
add_subdirectory(app)

# ========== 编译内核模块 ==========
add_subdirectory(drivers/LED)
add_subdirectory(drivers/INPUT_KEY)
add_subdirectory(drivers/IIC_AP3216C)
add_subdirectory(drivers/SPIICM-20608)
add_subdirectory(drivers/BLOCK_DEV)
add_subdirectory(drivers/IIO_SPI_ICM_20608)
add_subdirectory(drivers/IIO_IIC_AP3216C)
