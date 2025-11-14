# drivers/IIC_AP3216C/CMakeLists.txt
set(MODULE_NAME iic_ap3216c)
set(MODULE_SRC iic_ap3216c.c)

add_custom_command(
    OUTPUT ${MODULE_NAME}.ko
    COMMAND ${CMAKE_MAKE_PROGRAM} kernel_modules
    DEPENDS ${MODULE_SRC}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Building kernel module ${MODULE_NAME}.ko"
    VERBATIM
)

add_custom_target(
    ${MODULE_NAME}_module ALL
    DEPENDS ${MODULE_NAME}.ko
)