# 后续模块补齐说明（对照 embedded.doc）

已完成且未动：DHT11 采集（PG9）、2.8 寸 LCD 显示（FSMC）、FreeRTOS 移植。
本次补齐：ADC 电压、串口自定义协议、Modbus RTU 从站、Flash 掉电保存。

## 新增文件

- Core/BSP/bsp_adc.h/c：ADC1_IN1（PA1）板载电位器，单次软件触发，输出毫伏
- Core/BSP/bsp_uart_dma.h/c：USART1 + DMA1_Channel5 循环接收 + 空闲中断 + ring 缓冲
- Core/BSP/bsp_flash.h/c：末页 0x0807F800 整页擦 + 半字写 + 读
- Core/BSP/crc16.h/c：CRC16-MODBUS（协议帧和参数校验共用）
- Core/BSP/ring_buffer.h/c：单生产者单消费者环形缓冲
- Core/BSP/param_storage.h/c：设备 ID / 采样周期 / Modbus 地址，magic + CRC 校验
- Core/App/app_shared.h：SensorData_t + 队列/任务句柄共享声明
- Core/App/custom_protocol.h/c：0x01 上报 / 0x02 设置 / 0x03 查询
- Core/App/modbus_rtu.h/c：轻量从站（见下表）

## 与文档不一致的地方（有意为之）

1. 单串口：板上只有一个 USART1，自定义协议和 Modbus 复用，统一 115200。
   Modbus 主站（Modbus Poll）也要设 115200，而不是文档的 9600。
2. 无 LVGL：64KB RAM 紧张，直接用 LCD_ShowString 显示三行读数，省掉
   LVGL + TIM3 心跳。显示任务逻辑不变，只是多了电压行。
3. 无 TIM4：Modbus 的 3.5 字符超时用“串口空闲中断 + 任务内 10ms 静默”
   代替，115200 下等价。
4. 无 FreeModbus：手写最小从站，只占几 KB Flash，功能见下表。
5. SHT30 → DHT11：按现有代码，温湿度来自 DHT11，ADC 电压是新增的。
6. FreeRTOS 堆 10KB → 12KB：多了一个 UartTask。

## Modbus 寄存器表

- 输入寄存器（0x04）：0x0000 温度x10（int16）/ 0x0001 湿度x10 / 0x0002 电压mV
- 保持寄存器（0x03/0x06/0x10）：0x0000 采样周期ms（500~60000）/
  0x0001 设备ID / 0x0002 从站地址（1~247，立即生效，都掉电保存）
- 线圈（0x01/0x05/0x0F）：0x0000 LED0（PB5）/ 0x0001 LED1（PE5）

## 自定义协议帧（115200-8-N-1）

帧头 0xAA | 设备ID | CMD | LEN | DATA | CRC16低 | CRC16高，
CRC 范围是 设备ID + CMD + LEN + DATA。

- 上行 0x01：DATA = 温度float + 湿度float + 电压float（小端，各4字节）
- 下行 0x02：DATA = 周期u16小端ms + 设备IDu8（自动存 Flash，无应答）
- 下行 0x03：无 DATA；应答 DATA = 固件版本u16 + 设备IDu8

## CubeMX 说明

ADC1 和 USART1/DMA 不在 Source.ioc 里，初始化全在 BSP 手写，
CubeMX 重新生成冲不掉。如以后在 CubeMX 里补配了这两个外设，
记得删掉 BSP 里的对应初始化，避免重复配置。

## 测试（对照文档 11 章）

1. 上电看 LCD：温湿度 + 电压三行，转电位器电压应连续变化。
2. 串口助手 115200：每周期收到 18 字节 0x01 上报帧。
3. 发 0x03 查询帧：应回固件版本 + 设备 ID。
4. 发 0x02 设置帧：上报周期变化，断电重启保持。
5. Modbus Poll（115200-8-N-1，地址1）：04 读输入寄存器看传感器值；
   03/06/10 读写保持寄存器；01/05/0F 读写线圈控 LED。

## 主机侧验证（本机已跑过）

协议与工具代码是纯 C，已在 /tmp/proto_test 用 gcc 编译自测：
CRC16 标准向量（0x4B37 / 01 03 00 00 00 01→0x0A84）、ring 回绕、
组帧/状态机解析、查询/设置、Modbus 各功能码/异常码/广播/错地址/
错 CRC，全部通过。BSP 与 main.c 另用 gcc -fsyntax-only 过检，无警告。
板上实测仍要按上节走一遍（本机无 ARM 交叉编译器，未实际编译烧录）。
