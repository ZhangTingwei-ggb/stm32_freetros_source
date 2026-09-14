#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* ==========================================================================
 * FreeRTOS Kernel V11.1.0 (LTS) 配置文件
 * 目标: STM32F103ZETx / Cortex-M3 / GCC / heap_4 / 全动态分配
 *
 * !!! 重要 !!!
 * 1. 本文件放在 Third_Party/FreeRTOS/ 下, 不在 Middlewares/。
 *    CubeMX 重新生成工程会清空 Middlewares/ 但不会动 Third_Party/。
 * 2. 配套要求 stm32f1xx_it.c 里 SVC_Handler / PendSV_Handler /
 *    SysTick_Handler 必须删除 (FreeRTOSConfig.h 末尾的宏重映射会
 *    把 port.c 里的 xPortPendSVHandler 变成 PendSV_Handler, 重复定义
 *    会直接链接失败)。CubeMX 重新生成会加回来, 记得再删一次。
 * 3. 凡是要在中断里调 xxxFromISR 的中断, 优先级数值必须 >=
 *    configMAX_SYSCALL_INTERRUPT_PRIORITY 对应的逻辑优先级(5), 即
 *    NVIC_SetPriority(x, 5..15, 0)。数值 0~4 会触发 configASSERT 冻板。
 * ==========================================================================
 */

/* ---------- 基础配置 ----------
 * configCPU_CLOCK_HZ: 写常量 72000000 即可, 不能用 SystemCoreClock 因为
 *                     FreeRTOSConfig.h 在 port.c 引入 CMSIS 头之前被解析,
 *                     彼时 SystemCoreClock 还未声明。
 */
#define configCPU_CLOCK_HZ                       ( 72000000UL )
#define configTICK_RATE_HZ                       ( ( TickType_t ) 1000 )
#define configMAX_PRIORITIES                     ( 7 )
#define configMINIMAL_STACK_SIZE                 ( ( uint16_t ) 128 )
#define configMAX_TASK_NAME_LEN                  ( 16 )
/* V11.1.0 互斥: 只能保留 configTICK_TYPE_WIDTH_IN_BITS, 不能再设 configUSE_16_BIT_TICKS */
#define configTICK_TYPE_WIDTH_IN_BITS            ( TICK_TYPE_WIDTH_32_BITS )
#define configUSE_PREEMPTION                     1
#define configIDLE_SHOULD_YIELD                  1
#define configUSE_TASK_NOTIFICATIONS             1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES    1
#define configUSE_MUTEXES                        1
#define configUSE_RECURSIVE_MUTEXES              1
#define configUSE_COUNTING_SEMAPHORES            1
#define configUSE_QUEUE_SETS                     1
#define configUSE_TIMERS                         1
#define configTIMER_TASK_PRIORITY                ( configMAX_PRIORITIES - 1 )
#define configTIMER_QUEUE_LENGTH                 10
#define configTIMER_TASK_STACK_DEPTH             256

/* ---------- 协程: 关掉, 不用可省 Flash ---------- */
#define configUSE_CO_ROUTINES                    0
#define configMAX_CO_ROUTINE_PRIORITIES          ( 2 )

/* ---------- 钩子: 留空 ---------- */
#define configUSE_IDLE_HOOK                      0
#define configUSE_TICK_HOOK                      0
#define configUSE_DAEMON_TASK_STARTUP_HOOK       0
#define configCHECK_FOR_STACK_OVERFLOW           0
#define configUSE_MALLOC_FAILED_HOOK             0

/* ---------- 运行时统计: 关, 避免必须配 TIM 输入捕获 ---------- */
#define configGENERATE_RUN_TIME_STATS            0

/* ---------- Tickless Idle: 关, SysTick 关掉后 HAL 调度会偏 ---------- */
#define configUSE_TICKLESS_IDLE                  0

/* ---------- 内存: heap_4 + 纯动态 ---------- */
#define configSUPPORT_STATIC_ALLOCATION          0
#define configSUPPORT_DYNAMIC_ALLOCATION         1
#define configAPPLICATION_PROVIDES_cOutputBuffer 0
#define configTOTAL_HEAP_SIZE                    ( ( size_t ) ( 12U * 1024U ) )
#define configAPPLICATION_ALLOCATED_HEAP         0

/* ---------- 中断优先级 ----------
 * STM32F1 是 Cortex-M3, 优先级寄存器 8 bit 但只实现高 4 bit
 *   -> configPRIO_BITS = 4
 * configKERNEL_INTERRUPT_PRIORITY: 最低, 给 SysTick/PendSV/SVC
 *   = 15 << (8-4) = 0xF0
 * configMAX_SYSCALL_INTERRUPT_PRIORITY:
 *   = 5 << (8-4) = 0x50
 * 数值 >= 0x50 (即逻辑优先级 5..15) 的中断 FreeRTOS 会保护起来, 可安全调 xxxFromISR。
 */
#define configPRIO_BITS                          4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY  15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY  5
#define configKERNEL_INTERRUPT_PRIORITY          ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY  << ( 8 - configPRIO_BITS ) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY     ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )

/* ---------- 可选 API: 全部打开, 方便演示队列/挂起/恢复 ---------- */
#define INCLUDE_vTaskPrioritySet                 1
#define INCLUDE_uxTaskPriorityGet                1
#define INCLUDE_vTaskDelete                      1
#define INCLUDE_vTaskSuspend                     1
#define INCLUDE_vTaskDelayUntil                  1
#define INCLUDE_vTaskDelay                       1
#define INCLUDE_xTaskGetSchedulerState           1
#define INCLUDE_xTaskGetCurrentTaskHandle        1
#define INCLUDE_uxTaskGetStackHighWaterMark      1
#define INCLUDE_xTaskGetIdleTaskHandle           1
#define INCLUDE_eTaskGetState                    1
#define INCLUDE_xTimerPendFunctionCall           1
#define INCLUDE_xTaskAbortDelay                  1
#define INCLUDE_xTaskGetHandle                   1
#define INCLUDE_xQueueGetMutexHolder             1
#define INCLUDE_xSemaphoreGetMutexHolder         1
#define INCLUDE_xTaskGetApplicationTaskTag       1
#define INCLUDE_xTaskResumeFromISR               1
#define INCLUDE_xTaskGetTickCount                1
#define INCLUDE_xTaskGetTickCountFromISR         1
#define INCLUDE_xTaskCatchUpTicks                1
#define INCLUDE_xTaskResume                      1

/* ---------- Cortex-M 端口优化 (查表法 O(1) 就绪任务选择) ---------- */
#define configUSE_PORT_OPTIMISED_TASK_SELECTION  1

/* ---------- 断言: 触发就锁死, 方便定位 ---------- */
#ifndef configASSERT
    #define configASSERT( x )                                        \
        do {                                                          \
            if( ( x ) == 0 ) {                                        \
                taskDISABLE_INTERRUPTS();                             \
                for( ;; ) { }                                         \
            }                                                         \
        } while( 0 )
#endif

/* ---------- CMSIS 异常重映射 ----------
 * 把 FreeRTOS 移植层的 xPortPendSVHandler / vPortSVCHandler / xPortSysTickHandler
 * 重命名到 CMSIS 标准异常名。代价是 stm32f1xx_it.c 里同名的空函数必须删掉。
 */
#define vPortSVCHandler       SVC_Handler
#define xPortPendSVHandler    PendSV_Handler
#define xPortSysTickHandler   SysTick_Handler

#endif /* FREERTOS_CONFIG_H */
