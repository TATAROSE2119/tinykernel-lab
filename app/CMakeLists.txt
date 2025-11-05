# app/CMakeLists.txt
add_executable(myctl myctl.c)

# 优化体积 - 根据 CMake 版本使用不同命令
target_compile_options(myctl PRIVATE -Os)
if(CMAKE_VERSION VERSION_LESS "3.13")
    set_target_properties(myctl PROPERTIES LINK_FLAGS "-s")
else()
    target_link_options(myctl PRIVATE -s)
endif()

# 设置交叉编译选项
set_target_properties(myctl PROPERTIES
    COMPILE_FLAGS "-Wall -O2"
)
