#ifndef __APP_SHARED_H
#define __APP_SHARED_H

/* 各任务之间共享的数据类型和内核对象, 放这里避免循环包含。 */

#include "main.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

/* 统一传感器数据包: 采集任务 -> 显示任务 / 串口任务 */
typedef struct
{
    int16_t  temp_x10;      /* 温度 x10, 如 235 = 23.5C (DHT11 只有整数, x10 后个位恒 0) */
    uint16_t humi_x10;      /* 湿度 x10 */
    uint16_t volt_mv;       /* ADC 电压, 毫伏 */
    uint8_t  valid;         /* 1 = DHT11 校验通过 */
    uint32_t timestamp;     /* xTaskGetTickCount 快照 */
} SensorData_t;

extern QueueHandle_t      q_sensor2display;   /* 深 1, 最新值覆盖 */
extern QueueHandle_t      q_sensor2uart;      /* 深 5, 上报帧排队 */
extern TaskHandle_t       uartTaskHandle;     /* 串口空闲中断唤醒用 */
extern volatile SensorData_t g_latest;        /* 最新采样, Modbus 直接读 */

#endif /* __APP_SHARED_H */
