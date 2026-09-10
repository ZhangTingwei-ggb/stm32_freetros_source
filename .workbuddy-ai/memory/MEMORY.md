# 项目长期约定（STM32F103ZETx / FreeRTOS）

## 硬件
- 正点原子精英版 V2，STM32F103ZET6。LED0=PB5、LED1=PE5（低电平点亮）；KEY0=PE4、KEY1=PE3（低电平按下）、KEY_UP=PA0（高电平按下）。
- HSE 8MHz×9 = 72MHz。HAL 时基用 TIM1_UP_IRQn，SysTick 归 FreeRTOS。

## 外设 / BSP
- 板级驱动统一放 `Core/BSP/`（已在顶层 CMakeLists 的 target_sources 与 include 路径里登记；新增 .c 要手动加，CubeMX 不会管这个目录）。
- TFTLCD（2.8" ILI9341）走 FSMC：Bank1 sector4 基址 0x6C000000，A10(PG0)=RS，NE4(PG12)=CS，BL=PB0。16 位总线 A10 对应地址 bit11，故 `LCD_BASE = 0x6C000000 | 0x7FE`。
- DHT11 数据脚 PG9（单总线，需 4.7K 上拉）。驱动用 DWT->CYCCNT 做 us 延时；M3 上 DWT 默认关闭需手动开。

## 构建
- CMake + Ninja + arm-none-eabi-gcc，构建目录 `build/Debug`。
- cmake 不在 PATH：`C:\Users\zhangtingwei\AppData\Local\stm32cube\bundles\cmake\4.2.3+st.1\bin\cmake.exe`；bash 里要用 `cygpath -w` 转 Windows 路径再传给 cmake。

## 用户特点
- FreeRTOS 处于入门阶段：队列、挂起、任务切换等概念需要从原理讲起，配合图解效果最好。解释新 API 时逐行拆解 + 说明"不这么写会怎样"。

## 协作注意
- main.c 用户会在 VSCode 里同时编辑。文件被外部修改时，VSCode 弹窗要点「重新加载」，点「覆盖」会丢失 Agent 的改动（已发生过一次）。
