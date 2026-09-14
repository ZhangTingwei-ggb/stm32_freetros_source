cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME Source)

# Include toolchain file
include("cmake/gcc-arm-none-eabi.cmake")

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # FreeRTOS kernel —— 放在 Third_Party/ 下, CubeMX 重新生成不会清空
    # (Middlewares/ 在每次 Generate 时会被整个删掉, 不要再放用户自管源码)
    Third_Party/FreeRTOS/tasks.c
    Third_Party/FreeRTOS/queue.c
    Third_Party/FreeRTOS/list.c
    Third_Party/FreeRTOS/timers.c
    Third_Party/FreeRTOS/event_groups.c
    Third_Party/FreeRTOS/stream_buffer.c
    # 协程，用不到可以注释掉以节省 Flash
    # Third_Party/FreeRTOS/croutine.c
    # 内存管理：heap_4.c 带碎片合并，最常用
    Third_Party/FreeRTOS/portable/MemMang/heap_4.c
    # 移植层：GCC + Cortex-M3（不要改用 portable/RVDS，那是 ARMCC/Keil 的汇编语法）
    Third_Party/FreeRTOS/portable/GCC/ARM_CM3/port.c

    # 板级驱动 BSP
    Core/BSP/lcd.c
    Core/BSP/dht11.c
    Core/BSP/bsp_adc.c
    Core/BSP/bsp_uart_dma.c
    Core/BSP/bsp_flash.c
    Core/BSP/crc16.c
    Core/BSP/ring_buffer.c
    Core/BSP/param_storage.c

    # 应用层: 自定义协议 + Modbus 从站
    Core/App/custom_protocol.c
    Core/App/modbus_rtu.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    Third_Party/FreeRTOS/include                  # FreeRTOS.h / task.h / queue.h ...
    Third_Party/FreeRTOS/portable/GCC/ARM_CM3     # portmacro.h
    Third_Party/FreeRTOS                          # FreeRTOSConfig.h
    Core/BSP                                      # lcd.h / lcdfont.h / dht11.h ...
    Core/App                                      # app_shared.h / 协议头文件
    # Core/Inc、Drivers/... 已由 cmake/stm32cubemx 以 INTERFACE 方式提供，无需重复添加
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
