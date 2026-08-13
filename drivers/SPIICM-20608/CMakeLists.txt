# drivers/SPIICM-20608/CMakeLists.txt
set(MODULE_NAME spi_ICM20608)
set(MODULE_SRC spi_ICM20608.c)

add_custom_command(
    OUTPUT ${MODULE_NAME}.ko
    COMMAND ${CMAKE_MAKE_PROGRAM}
            KERNELDIR=${KERNEL_SRC}
            ARCH=${KERNEL_ARCH}
            CROSS_COMPILE=${KERNEL_CROSS_COMPILE}
            kernel_modules
    DEPENDS ${MODULE_SRC}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Building kernel module ${MODULE_NAME}.ko"
    VERBATIM
)

add_custom_target(
    ${MODULE_NAME}_module ALL
    DEPENDS ${MODULE_NAME}.ko
)
