# STM32F103 数据采集与监控终端

正点原子精英版 V2（STM32F103ZET6）+ 原生 FreeRTOS。
DHT11 温湿度 + 板载电位器电压，2.8 寸 LCD 本地显示，
一路 USART1 同时跑自定义串口协议和 Modbus RTU 从站，
设备 ID、采样周期、Modbus 地址存内部 Flash，掉电不丢。

文档索引：总体设计 `embedded.doc`；LCD 驱动 `Docs/LCD_Guide.md`；
后端模块（ADC/串口/Flash/协议）`Docs/Backend_Modules.md`。

## 1. 硬件

主控：STM32F103ZET6，Cortex-M3，72MHz（HSE 8MHz ×9），
512KB Flash（页 2KB），64KB SRAM。HAL 时基用 TIM1，
SysTick 留给 FreeRTOS。

| 外设 | 引脚 | 说明 |
|---|---|---|
| DHT11 数据脚 | PG9 | 单总线，4.7K 上拉到 3.3V；裸传感器必须 3.3V 供电 |
| 2.8 寸 LCD（ILI9341） | FSMC | 16 位数据 PD0~PD15；CS=PG12（NE4）；RS=PG0（A10）；WR=PD5；RD=PD4；背光=PB0 高电平开；240×320 竖屏 |
| 板载电位器 | PA1（ADC1_IN1） | 12 位，参考 3.3V，单次软件触发 |
| 串口 | PA9/PA10（USART1） | 115200-8-N-1，板载 CH340 直连 PC |
| LED0 / LED1 | PB5 / PE5 | 可被 Modbus 线圈控制 |
| 按键 | PA0、PE3、PE4 | 已配 EXTI，目前无回调（中断优先级 6，FreeRTOS 安全） |

注意：RS=PG0 接 FSMC 地址线 A10，读写 LCD_REG/LCD_RAM
即区分命令/数据（详见 LCD_Guide 第 1 节）。
USART1 同时服务自定义协议和 Modbus，统一 115200；
RS-485 组网需另接 MAX485 并写 DE/RE 切换，当前未实现。

## 2. 功能总览

- 采集：DHT11 温湿度 + ADC 电压，周期默认 2 秒（Flash 参数可改 500ms~60s）
- 显示：LCD 三行读数 + 状态行 + 计数行，定长字符串覆盖刷新
- 上报：每个采样一帧自定义 0x01 帧经串口发出
- 下行：0x02 改周期/设备 ID（自动存 Flash），0x03 查固件版本/设备 ID
- Modbus：标准 RTU 从站，读传感器、读写参数、控 LED
- 掉电保存：magic + CRC 校验，坏块回默认值

## 3. 软件架构

分层：HAL（CubeMX 生成）→ BSP（传感器/LCD/串口/Flash 封装）
→ App（两种协议）→ main.c 三个任务。FreeRTOS 放在
Third_Party（CubeMX 重生成冲不掉），heap_4，堆 12KB。

| 任务 | 优先级 | 栈 | 职责 |
|---|---|---|---|
| Dht11Task | 3 | 256 字 | DHT11 + ADC 采样，分发两队列，更新 g_latest 快照 |
| UartTask | 4 | 256 字 | 自定义协议上报/解析 + Modbus 从站响应 |
| LcdTask | 2 | 256 字 | 阻塞等队列，刷新 LCD |

任务间通信：

```c
typedef struct {
    int16_t  temp_x10;   /* 温度×10，如 235 = 23.5℃ */
    uint16_t humi_x10;   /* 湿度×10 */
    uint16_t volt_mv;    /* ADC 电压，毫伏 */
    uint8_t  valid;      /* DHT11 校验结果，0 则显示 ERR */
    uint32_t timestamp;
} SensorData_t;
```

- q_sensor2display（深 1，覆盖写）：只保留最新采样，显示慢也不积压
- q_sensor2uart（深 5）：上报帧排队，满则丢最旧容忍
- g_latest（volatile 快照）：Modbus 直接读，单写者
- 无互斥量：LCD 只显示任务用，串口发送只 UartTask 用，
  不存在并发写（main.c 有注释）

串口收发：USART1 + DMA1_Channel5 循环接收 + 空闲中断，
中断把字节推进 ring 缓冲并唤醒 UartTask；发送轮询。
Modbus 判帧用 10ms 总线静默（代替 TIM4 的 T3.5 超时）。

## 4. LCD 界面

240×320 竖屏，16×24 字体每行 15 字符，所有字符串定长
（超长会越右边界回绕成乱码，见 LCD_Guide 第 6 节）：

```
DHT11 MONITOR      标题
Temperature        标签
23.5 C             读数（恒 6 字符，橙色）
Humidity           标签
45.0 %             读数（恒 6 字符，青色）
Voltage            标签
3.30 V             读数（恒 6 字符）
Status: OK         状态（恒 12 字符，绿/红）
OK:12   E:0        计数（恒 14 字符，单行）
```

失败采样显示 `--.- C` / `--.- %` 红色 + `Status: ERR `，
电压行每次都刷（与采样好坏无关）。

## 5. 自定义串口协议

115200-8-N-1。帧格式：

```
帧头0xAA | 设备ID | CMD | LEN | DATA[LEN] | CRC16低 | CRC16高
```

CRC 范围：设备ID + CMD + LEN + DATA，算法 CRC16-MODBUS。
设备 ID 不符、CRC 错整帧丢弃；LEN 越界重同步。

| CMD | 方向 | DATA |
|---|---|---|
| 0x01 | 上行 | 温度float + 湿度float + 电压float（小端，各 4 字节） |
| 0x02 | 下行 | 周期u16小端ms（500~60000，非法忽略）+ 设备IDu8（非 0）；自动存 Flash，无应答 |
| 0x03 | 下行 | 无 DATA |
| 0x03 | 上行 | 固件版本u16（0x0100）+ 设备IDu8 |

真实报文（设备 ID=1，23.5℃ / 45.0% / 3.30V）：

```
上报  AA 01 01 0C 00 00 BC 41 00 00 34 42 33 33 53 40 D5 F9
查询  AA 01 03 00 20 F0
应答  AA 01 03 03 00 01 01 85 DE
设置  AA 01 02 03 E8 03 01 39 4A        # 周期 1000ms，ID 1
```

## 6. Modbus RTU 从站

115200-8-N-1，地址默认 1（Flash 可改 1~247），地址 0 广播
写只执行不回，CRC 错/地址不符静默丢弃。手写轻量实现
（未引 FreeModbus），支持 01/03/04/05/06/0F/10。

| 类型 | 地址 | 内容 |
|---|---|---|
| 输入寄存器（0x04，只读） | 0x0000 | 温度×10（int16，如 235） |
| | 0x0001 | 湿度×10 |
| | 0x0002 | 电压 mV（如 3300） |
| 保持寄存器（0x03/06/10，读写） | 0x0000 | 采样周期 ms（500~60000，掉电保存） |
| | 0x0001 | 设备 ID（1~254，掉电保存） |
| | 0x0002 | 从站地址（1~247，立即生效，掉电保存） |
| 线圈（0x01/05/0F） | 0x0000 | LED0（PB5） |
| | 0x0001 | LED1（PE5） |

非法地址回异常 0x02，非法值回异常 0x03，不支持的功能码回 0x01。

报文例子（读输入寄存器 0~2）：

```
请求  01 04 00 00 00 03 B0 0B
应答  01 04 06 00 EB 01 C2 0C E4 E1 CF   # 235 / 450 / 3300
```

## 7. 参数与 Flash

末页 0x0807F800（2KB）存 8 字节整块：
magic（"PRMT"）+ 设备ID + 采样周期 + Modbus 地址 + CRC16。
上电 Param_Load 验 magic + CRC，失败用默认值
（ID=1，周期 2000ms，Modbus 地址 1）；
Param_Set* 系列自动擦页重写 + 回读校验。
Flash 写在任务上下文执行，会短暂阻塞同任务（毫秒级），属正常。

## 8. 目录结构

```
Core/BSP/     lcd、dht11、bsp_adc、bsp_uart_dma、bsp_flash、
              crc16、ring_buffer、param_storage
Core/App/     app_shared（SensorData_t + 队列句柄）
              custom_protocol、modbus_rtu
Core/Src/     main.c（三任务）、stm32f1xx_it.c（USART1/DMA 中断）、
              HAL 时基（TIM1）、msp（FSMC）
Core/Inc/     main.h、stm32f1xx_hal_conf.h（已开 ADC/UART 模块）
Drivers/      HAL 库 + CMSIS（CubeMX 生成）
Third_Party/  FreeRTOS 内核 + FreeRTOSConfig.h
Docs/         LCD_Guide.md、Backend_Modules.md
Source.ioc    CubeMX 配置（FSMC/GPIO/时钟）
CMakeLists.txt、cmake/、CMakePresets.json   构建
embedded.doc  总体设计文档
```

注意：ADC1、USART1/DMA 不在 Source.ioc 里，
初始化在 BSP 手写，CubeMX 重生成冲不掉。
如以后在 CubeMX 里补配了这两个外设，
记得删掉 BSP 里的对应初始化，避免重复配置。

## 9. 编译、烧录、调试

需要 arm-none-eabi-gcc 和 Ninja：

```bash
cmake --preset Debug
cmake --build --preset Debug
```

固件在 `build/Debug/`（Source.elf），转 bin：

```bash
arm-none-eabi-objcopy -O binary build/Debug/Source.elf firmware.bin
```

ST-Link + OpenOCD 烧录（按实际接口改）：

```bash
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg \
  -c "program build/Debug/Source.elf verify reset exit"
```

VS Code 装 Cortex-Debug 插件可 F5 单步调试
（断点、寄存器、变量监控）。

## 10. 测试步骤

1. 上电看 LCD：三行读数 + 状态 OK；转电位器电压连续变化；
   用手捂 DHT11（别碰引脚），温度缓慢上升（湿度只有整数，正常）
2. 串口助手 115200：每周期收到 18 字节 0x01 上报帧（见第 5 节例子）
3. 发查询帧 `AA 01 03 00 20 F0`，应回固件版本 + 设备 ID
4. 发设置帧改周期，上报频率变化；断电重启，周期保持
5. Modbus Poll（115200-8-N-1，地址 1）：
   04 读输入寄存器看传感器值；
   03/06/10 读写保持寄存器改参数；
   01/05/0F 读写线圈控 LED

## 11. 故障排查

| 现象 | 查什么 |
|---|---|
| LCD 背光不亮 | PB0、排线、复位；背光亮但白屏查 FSMC 初始化顺序 |
| LCD 白底黑字（颜色反） | 面板常白型，确认 LCD_INVERSION_ON（LCD_Guide 2.2） |
| 屏幕右侧乱码碎块 | 某字符串超 15 字符越界回绕，检查定长 |
| 温湿度恒 ERR | PG9 上拉 4.7K、3.3V 供电、DHT11 间隔≥1s |
| 串口无上报 | 波特率 115200、PA9/PA10、DMA 中断优先级 6 |
| Modbus 无应答 | 地址是否为 1、波特率是否 115200（不是 9600）、CRC 是否小端在后 |
| 参数重启丢失 | Flash 末页是否被程序覆盖（程序超 510KB 时会顶到 0x0807F800） |
| FreeRTOS 一跑就挂 | 中断优先级数值必须 ≥5；SVC/PendSV/SysTick 不得在 it.c 重定义 |

## 12. 与原总体文档的差异

总体文档（embedded.doc）已按本实现更新，
与最初版本相比的主要变化：

- 传感器 SHT30（I2C）→ DHT11（PG9 单总线）
- LCD 3.2 寸 320×240 背光 PB9 → 2.8 寸 240×320 背光 PB0
- 单串口：自定义协议和 Modbus 统一 115200（Modbus 不是 9600）
- 未移植 LVGL：定长字符串界面，省 RAM（无 TIM3 心跳）
- 未移植 FreeModbus：手写轻量从站（无 TIM4，用串口空闲判帧）
- 任务 4 个 → 3 个（协议与 Modbus 合并为 UartTask），无互斥量
- FreeRTOS 堆 10KB → 12KB
- 构建 Makefile → CMake Presets

不能全信文档的地方以代码为准；
Backend_Modules.md 记录了全部取舍原因。
