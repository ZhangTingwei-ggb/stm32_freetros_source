# 项目长期约定（STM32F103ZETx / FreeRTOS）

## 硬件
- 正点原子精英版 V2，STM32F103ZET6。LED0=PB5、LED1=PE5（低电平点亮）；KEY0=PE4、KEY1=PE3（低电平按下）、KEY_UP=PA0（高电平按下）。
- HSE 8MHz×9 = 72MHz。HAL 时基用 TIM1_UP_IRQn，SysTick 归 FreeRTOS。

## 外设 / BSP
- 板级驱动统一放 `Core/BSP/`（已在顶层 CMakeLists 的 target_sources 与 include 路径里登记；新增 .c 要手动加，CubeMX 不会管这个目录）。
- TFTLCD（2.8" ILI9341）走 FSMC：Bank1 sector4 基址 0x6C000000，A10(PG0)=RS，NE4(PG12)=CS，BL=PB0。16 位总线 A10 对应地址 bit11，故 `LCD_BASE = 0x6C000000 | 0x7FE`。
- DHT11 数据脚 PG9（单总线，需 4.7K 上拉）。驱动用 DWT->CYCCNT 做 us 延时；M3 上 DWT 默认关闭需手动开。
- 屏幕方向：`lcd.h` 有 4 个方向宏，**只改 `LCD_MADCTL` 一行即可换向**，`LCD_W`/`LCD_H` 由 `#if (LCD_MADCTL & 0x20U)`（MV 位）自动推导。当前 = `LCD_MADCTL_MV1_MX0_MY1`（`0xA8`，240x320 正立，2026-09-12 实测定稿）。
  - **⚠️ 这块屏的 MV 位与 ILI9341 手册相反（实测结论，别再照手册推）：**
    | MADCTL | 实测 |
    |---|---|
    | `0xA8` | 240x320 正立 ← 用这个 |
    | `0x68` | 240x320 倒 180° |
    | `0xC8` | 320x240 |
    | `0x08` | 320x240 倒 180° |
    即 **MV=1 那一组是 240x320**，手册说 MV=1 是 320x240。`LCD_W`/`LCD_H` 的推导分支必须按实测写（MV=1 → 240x320），换行为正常的屏要把两个分支对调。
  - 结论怎么来的：用户先后报告 `0x68` 是竖屏（只是上下反了）、`0xA8` 是竖屏正立、`0xC8` 是横屏。**按手册模型这三个全错，按上表全对** —— 所以是反推确定的。教训：**方向问题以用户实测为准，不要拿手册反驳用户。**
  - 宏名用 `MV0_MX1_MY1` 这种把三个位编进名字的写法，避免再查"`0xA8` 是哪个旋转"。
  - `0x28` / `0xE8` 是**镜像**（文字左右反），不能用。
- **面板极性（反色）：这块屏是常白型，初始化必须发 `0x21`（Display Inversion ON）**，否则整个主题反相（2026-09-12 定稿）。
  - `lcd.h` 有 `LCD_INVERSION_OFF(0x20)` / `LCD_INVERSION_ON(0x21)` / `LCD_INVERSION`，`lcd.c` 的 `ILI9341_Init()` 里发一条 `LCD_WriteCmd(LCD_INVERSION)`。换屏只改 `LCD_INVERSION` 一行。
  - **判定方法：全屏刷 `0x0000`，出来是白的就是反相了**（常白面板把 0 显示成白），要发 `0x21`；出来是黑的才用 `0x20`。
  - **症状极具欺骗性**：反相后的画面是"白底 + 深色字 + 浅色标题栏"，看起来像一个**完全正常的浅色 UI**。原来的主题（黑底/蓝标题栏/白字）反相后是"白底/黄标题栏/黑字"，同样像正常界面，所以这个 bug 潜伏了很久没被发现。只有专门做暗色主题、期待黑底却看到一片白时才暴露。
  - 注意：**反色 ≠ BGR 位**。BGR 位（D3）只交换红蓝，颜色会怪但不会黑变白。`0x0000` 显示成白色只有极性一种解释。
- **LCD 驱动已按"最小可用"重写（2026-09-12）**：对外只有 3 个函数 —— `LCD_Init()` / `LCD_Clear(color)` / `LCD_ShowString(x, y, str, color)`。内部另有 static 的 `LCD_WriteCmd` / `LCD_WriteData` / `LCD_SetWindow` / `ILI9341_Init` / `LCD_ShowChar`。
  - **文字背景色写死在驱动里**（`LCD_ShowChar` 里用 `LCD_COLOR_BG`），所以 `LCD_ShowString` 只有 4 个参数、没有 bg。好处是文字自动覆盖旧内容；代价是不能在色块上写字（需要时再加 bg 参数）。
  - **已删除**：`LCD_Fill`（画框/分隔线）、`LCD_ShowCharScale` / `LCD_ShowStringScale`（放大字体）、`LCD_W/LCD_H` 的方向自动推导、`main.c` 的 `#if (LCD_W > LCD_H) #error` 断言（尺寸现在直接写死，没有推导就没有可推导错的）。
  - **想加回来时**：`LCD_Fill` = `LCD_SetWindow` + 循环写，和 `LCD_Clear` 同构；放大字体 = 在 `LCD_ShowChar` 里把"写 1 像素"改成写 scale² 个、行重复 scale 次。别为了以后可能用到就先加着。
- **界面布局**：`main.c` 的 `UI_*` 宏按 240x320 竖排，全部共用左边距 `UI_X = 12`，一行一个元素（标题 12 / 温度标签 64 + 值 92 / 湿度标签 136 + 值 164 / 状态 216 / 计数器 256、288）。240 宽一行只放得下 15 个 16px 字符。
- **改完界面必须跑一遍像素宽度审计**（2026-09-12 抓出 3 处越界）。规则：`x + len(s)*16*scale - 1 < LCD_W`，纵向也要查 `y + 高度 - 1 < LCD_H`。
  - **数长度不要靠眼睛**，用脚本 `len(s)`：我曾两次把 12 数成 14、把 8 数成 7。
  - **`"%-4lu"` / `"%2d"` 里的数字是最小宽度不是最大宽度**，算布局要按最大位数算。越界时 ILI9341 的地址计数器会**回绕到左边**，表现为屏幕乱码而非截断，很难反查。
  - 两个计数器**不能挤一行**：`"OK:1234"`(7 字符) + `"ERR:1234"`(8 字符) = 15 字符 = 240px，正好占满零余量。现在是一行一个（y=256 / y=288），clamp 到 9999。

## 编码约定（重要）
- **全工程注释一律用英文（ASCII）**，不仅限于 main.c。CubeMX 每次 Generate 都用系统 ANSI 代码页（GBK）读写 main.c / stm32f1xx_it.c，会把 UTF-8 中文注释搞成不可逆的乱码（已发生过两次，最后一次在 2026-09-12 修好：main.c 里出现大量 U+FFFD）。
- CubeMX 管理的两个文件（`Core/Src/main.c`、`Core/Src/stm32f1xx_it.c`）要改注释时，用**按行号替换的一次性脚本**，不要用编辑器整文件重写，避免再次被编码转换写坏。
- 改 `stm32f1xx_it.c` 里被注释掉的 SVC/PendSV/SysTick handler 时，**不要给替换行补 `*/`** —— 原文本是跨行块注释，提前闭合会让后面的 `}` 变成裸代码。
- 中文写进 CubeMX 不管理的文件即可，例如新加的 `Docs/LCD_Guide.md`（LCD 驱动逐函数讲解）。

- **屏幕颜色用语义化主题宏，不用通用色名**（2026-09-12 起）：`lcd.h` 里是 `LCD_COLOR_BG / TEXT / LABEL / TEMP / HUMI / OK / WARN / ERR`，每个都带 `#RRGGBB` 源码色注释。底色 `#000000`、正文 `#E6EDF3`、温度 `#FFB454`、湿度 `#5EE6C4`。
  - （2026-09-12 简化时删掉了 `PANEL` / `EDGE` / `TITLE` / `MUTED` —— 它们只服务于标题栏和分隔线，而那两个东西已经不在"最小驱动"的范围内了。`TITLE` 改名为 `TEXT`。）
  - `#RRGGBB` → RGB565：`r5=round(R*31/255)`、`g6=round(G*63/255)`、`b5=round(B*31/255)`，`(r5<<11)|(g6<<5)|b5`。**用脚本算，别手算。**
- **死代码识别**：`grep -rn "\b函数名\b" Core/` 如果只有声明+定义、没有调用点，就是死代码。2026-09-12 删掉了 `LCD_ReadID` / `LCD_ReadData` / `LCD_DrawPoint` / `LCD_ShowChar` 和 main.c 里的 LED0 心跳灯。
- **改 CubeMX 管理的文件（main.c / stm32f1xx_it.c）用字节级替换，不要整文件 Write**：文件现在是纯 ASCII + 全 CRLF，可以用 Python 脚本按标记切片替换（`buf.find(开始标记)` 到 `buf.find(结束标记)`），替换后**校验"无非 ASCII 字节 + LF 数 == CRLF 数"**，任一不满足就中止不写盘。注意 Python 三引号里的 `\n` 是裸 LF，写回前要 `replace('\n', '\r\n')` —— 否则会把 CRLF 文件搞成混合行尾。

## 构建
- CMake + Ninja + arm-none-eabi-gcc，构建目录 `build/Debug`。
- cmake 不在 PATH：`C:\Users\zhangtingwei\AppData\Local\stm32cube\bundles\cmake\4.2.3+st.1\bin\cmake.exe`；bash 里要用 `cygpath -w` 转 Windows 路径再传给 cmake。
- ninja 也不在 PATH（`which ninja` 找不到）：`C:\Users\zhangtingwei\AppData\Local\stm32cube\bundles\ninja\1.13.2+st.1\bin\ninja.exe`。只改了源文件时直接在 `build/Debug` 里跑它最快（增量编译），不用重跑 cmake。
- `core.autocrlf = true`（全局）：工作区是 CRLF、库里是 LF，所以磁盘字节数会比 blob 大（每行 +1）。判断"有没有改"要用 `git hash-object`，不要看 `ls` 的大小。

## 用户特点
- FreeRTOS 处于入门阶段：队列、挂起、任务切换等概念需要从原理讲起，配合图解效果最好。解释新 API 时逐行拆解 + 说明"不这么写会怎样"。

## FreeRTOS 陷阱（踩过的坑）
- **`xQueueCreate()` 等任何 FreeRTOS 对象创建必须放在 `HAL_Init()` 之后、`vTaskStartScheduler()` 之前。** 调度器启动前 `uxCriticalNesting` 还是哨兵值 `0xaaaaaaaa`，`taskEXIT_CRITICAL()` 减完不等于 0，于是**不会清 BASEPRI**，BASEPRI 一直停在 5 → 优先级 5..15 的中断全被屏蔽 → HAL 时基 TIM1（优先级 15）不再产生中断 → **`HAL_Delay()` 在 `main()` 里（调度器启动前）会死等**。任务里同一个 `HAL_Delay()` 却正常，因为 `vPortSVCHandler()` 会把 BASEPRI 清零。症状极具迷惑性："任务里能跑，挪到 main 就白屏"。
- `TICK_INT_PRIORITY = 15`（CubeMX 默认）与 `configMAX_SYSCALL_INTERRUPT_PRIORITY = 5` 的关系：HAL 时基会被 FreeRTOS 临界区屏蔽，`HAL_Delay()` 在临界区内会偏长。若要精确，可把 HAL tick 优先级改到 ≤4（它不调 FreeRTOS API，安全）。
- 中断入口用 **USER CODE 块转发**（`extern void xPortPendSVHandler(void); xPortPendSVHandler();`），**不要**用 FreeRTOSConfig.h 里的宏重映射——CubeMX 每次 Generate 都会把三个空 handler 加回来，宏重映射会直接重复定义链接失败。

## 调试方法
- **这个仓库有 git 历史**：`cecf590 v1`（9-9，裸 CubeMX 工程，无 LCD）、`37e87b7 Create Middlewares`、`06c65f0 freertos`（9-10 23:06，即"能正常显示"那版）、`b900cfd merge`（当前 HEAD）。遇到"以前能跑现在不行"，先 `git diff HEAD -- <file>` 定性，比逐行读快得多。
- 没有仿真器时，诊断代码**不要用 `HAL_Delay()` 计时**，改用 `DWT->CYCCNT`（`CoreDebug->DEMCR |= TRCENA` + `DWT->CTRL |= CYCCNTENA`），这样 HAL tick 挂了也能把结果报出来。
- 报错通道优先级：**屏幕整片亮暗（背光 PB0）> LED1(PE5) > LED0(PB5)**。整屏闪比小灯好数得多。
- 在 `HardFault_Handler` 里做现场指示要用**直接寄存器写**（`GPIOB->BSRR = GPIO_PIN_0`），不要调 HAL，也不要假设堆栈可用。
- **改 LCD 文字布局前先跑越界审计**：`~/.workbuddy-ai/skills/lcd-ui-audit/audit.py`（配 SKILL.md）。它会自己从 `lcd.h` 求值 `#if ( LCD_MADCTL & 0x20U )` 推出当前画布，展开 `snprintf` 格式串，逐行给 `OK/TIGHT/OVER`。改完布局/改完方向都跑一次，别靠肉眼数字符。

## Git 仓库修复（2026-09-11 事件）
- 本地 `.git` 曾出现**对象库损坏**：b900cfd 的父提交 `06c65f0b`、`Core/BSP` 树 `0c3c722e`、`.settings` 树 `1a8a97c4` 等对象丢失。症状：`git status` / `git read-tree` / `git checkout <rev> -- <path>` 全部 `fatal: unable to read tree`，`git fetch` 报 `Could not read 06c65f0b`，但单文件 `git show <rev>:<path>` 仍可用。
- **恢复办法（已验证有效）**：`git clone --no-checkout <remote> <临时目录>` 拿一个干净仓库，把它 `.git/objects/pack/*` 复制进本地 `.git/objects/pack/`，本地对象库即被补齐，之后 `git reset --hard <rev>` 正常工作。本地独有的松散对象（stash 等）不受影响。
- 教训：`git stash push -- <pathspec>` 在对象库损坏时**会把工作区文件截断成 0 字节**（已发生，`Core/BSP/*` 全被清空）。对象库有问题时不要 stash，先手工 `cp -r` 备份目录。
- `stash@{0}`（`529ac42c9`，"backup: DHT11+LCD 诊断 与 xQueueCreate 修复"）损坏后**已不可读**，`git stash show` 报 `unable to read tree 1788ba76`。

## 协作注意
- main.c 用户会在 VSCode 里同时编辑。文件被外部修改时，VSCode 弹窗要点「重新加载」，点「覆盖」会丢失 Agent 的改动（已发生过一次）。
- 用户会同时插拔 DHT11 和 LCD 排线；出现"什么都没改却坏了"时，除了查代码也要让用户复查排线是否插偏/松动。
- 做批量文件操作前先 `cp -r` 出一份目录备份（如 `Source_backup_before_revert/`），并**校验备份可读**再动原文件。
