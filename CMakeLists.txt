# CMakeLists.txt for IR Remote Control Project

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ir_remote_control)

# 包含目录
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 源文件
target_sources(app PRIVATE
    src/main.c
    src/ir_hal.c
    src/irdb_protocol.c
    src/irdb_loader.c
    src/ir_service.c
    src/ir_learning.c
)

# 如果有Shell支持，添加学习应用示例
if(CONFIG_SHELL)
    target_sources(app PRIVATE
        src/ir_learning_app.c
    )
endif()