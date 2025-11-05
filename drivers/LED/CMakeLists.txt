# drivers/LED/CMakeLists.txt
set(MODULE_NAME leddrv)
set(MODULE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/leddriver.c)

# ========== 使用内核 make 编译 .ko ==========
add_custom_command(
    OUTPUT ${MODULE_NAME}.ko
    COMMAND ${CMAKE_MAKE_PROGRAM}
            -C ${KERNEL_SRC}
            M=${CMAKE_CURRENT_SOURCE_DIR}
            ARCH=arm
            CROSS_COMPILE=arm-linux-gnueabihf-
            modules
    DEPENDS ${MODULE_SRC}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Building kernel module ${MODULE_NAME}.ko"
    VERBATIM
)

add_custom_target(${MODULE_NAME} ALL DEPENDS ${MODULE_NAME}.ko)

# ========== 部署到 NFS ==========
# add_custom_command(
#     TARGET ${MODULE_NAME} POST_BUILD
#     COMMAND ${CMAKE_COMMAND} -E copy
#         ${CMAKE_CURRENT_SOURCE_DIR}/${MODULE_NAME}.ko
#         ${MODULE_INSTALL_DIR}/
#     COMMENT "Deploying ${MODULE_NAME}.ko to NFS"
# )
# =========================================