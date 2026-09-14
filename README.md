# STM32F103 数据采集与监控终端

正点原子精英版 V2（STM32F103ZET6）+ 原生 FreeRTOS。
DHT11 温湿度 + 板载电位器电压，2.8 寸 LCD 显示，
一路 USART1 同时跑自定义串口协议和 Modbus RTU 从站，
参数存内部 Flash，掉电不丢。

详细设计见 `embedded.doc`，LCD 驱动见 `Docs/LCD_Guide.md`，
本次补齐的后端模块见 `Docs/Backend_Modules.md`。

## 硬件接线

| 外设 | 引脚 | 说明 |
|---|---|---|
| DHT11 数据脚 | PG9 | 需 4.7K 上拉到 3.3V |
| 2.8 寸 LCD（ILI9341） | FSMC | CS=PG12，RS=PG0，背光=PB0，240x320 竖屏 |
| 板载电位器 | PA1（ADC1_IN1） | 12 位，参考 3.3V |
| 串口 | PA9/PA10（USART1） | 115200-8-N-1，板载 CH340 |
| LED | PB5（LED0）、PE5（LED1） | 可被 Modbus 线圈控制 |

## 功能

- 采集任务：DHT11 温湿度 + ADC 电压，周期默认 2 秒，可改（500ms~60s）
- 显示任务：LCD 显示温度 / 湿度 / 电压 / 状态 / 计数
- 串口任务：每个采样上报一帧自定义协议；同时响应下行命令和 Modbus 请求
- 掉电保存：设备 ID、采样周期、Modbus 地址存 Flash 末页

## 目录

```
Core/BSP/     lcd、dht11、bsp_adc、bsp_uart_dma、bsp_flash、
              crc16、ring_buffer、param_storage
Core/App/     app_shared、custom_protocol、modbus_rtu
Core/Src/     main.c（三个任务）、中断向量、HAL 时基
Third_Party/  FreeRTOS 内核（heap_4，堆 12KB）
Docs/         LCD_Guide.md、Backend_Modules.md
```

## 编译

需要 ARM 交叉编译器（arm-none-eabi-gcc）和 Ninja：

```bash
cmake --preset Debug
cmake --build --preset Debug
```

固件在 `build/Debug/` 下（Source.elf），转 bin：

```bash
arm-none-eabi-objcopy -O binary build/Debug/Source.elf firmware.bin
```

## 烧录 / 调试

用 ST-Link + OpenOCD（示例，按实际接口改）：

```bash
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg \
  -c "program build/Debug/Source.elf verify reset exit"
```

VS Code 装 Cortex-Debug 插件可单步调试。

## 协议速查

自定义协议（115200）：`AA | 设备ID | CMD | LEN | DATA | CRC16低 | CRC16高`

| CMD | 方向 | DATA |
|---|---|---|
| 0x01 | 上行 | 温度float + 湿度float + 电压float（小端） |
| 0x02 | 下行 | 周期u16小端ms + 设备IDu8（自动存 Flash） |
| 0x03 | 下行/上行 | 查询无 DATA；应答为固件版本u16 + 设备IDu8 |

Modbus RTU（115200，地址默认 1）：

| 类型 | 地址 | 内容 |
|---|---|---|
| 输入寄存器 04 | 0~2 | 温度x10、湿度x10、电压mV |
| 保持寄存器 03/06/10 | 0~2 | 采样周期ms、设备ID、从站地址 |
| 线圈 01/05/0F | 0~1 | LED0、LED1 |

## 测试

1. 上电看 LCD 三行读数，转电位器电压应连续变化
2. 串口助手 115200：每周期收到 18 字节 0x01 上报帧
3. 发 0x03 查询帧，应回固件版本 + 设备 ID
4. 发 0x02 设置帧，上报周期变化，断电重启保持
5. Modbus Poll（115200，地址 1）：04 读传感器，03/06/10 读写参数，01/05/0F 控 LED

## 和原文档不一样的地方

- 单串口，Modbus 也是 115200（不是 9600）
- 没用 LVGL，直接字符串显示，省 RAM
- Modbus 超时用串口空闲中断代替 TIM4
- FreeRTOS 堆 12KB（多了一个串口任务）

原因见 `Docs/Backend_Modules.md`。
