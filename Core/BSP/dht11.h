#ifndef __DHT11_H
#define __DHT11_H

#include "main.h"

/* ==========================================================================
 * DHT11 单总线温湿度传感器驱动
 *
 * 接线(精英版 V2 的 J2 排针 / 或直接用杜邦线):
 *   DHT11  VCC  -> 3.3V  (模块自带 LDO 的也可接 5V, 裸传感器必须 3.3V)
 *   DHT11  DATA -> PG9   (必须接 4.7K 上拉电阻到 3.3V!)
 *   DHT11  GND  -> GND
 *
 * 注意: PG9 在精英板 V2 上没有复用为 FSMC 或其他外设, 可放心当普通 IO 用。
 * ==========================================================================
 */

/* --- 数据脚定义: 改这里就能换引脚 --- */
#define DHT11_GPIO_Port     GPIOG
#define DHT11_Pin           GPIO_PIN_9

/* --- 时序阈值(单位 us), 来自 DHT11 datasheet --- */
#define DHT11_START_LOW_US  18000U   /* 主机拉低起始信号, >= 18ms */
#define DHT11_START_HIGH_US 30U      /* 主机释放总线后等待 20~40us */
#define DHT11_BIT_1_MIN_US  40U      /* 高电平 > 40us 判为 '1', 否则为 '0'
                                      * (0 -> 26~28us;  1 -> 70us) */

/* 一次采集结果 */
typedef struct
{
    uint8_t humi_int;       /* 湿度整数部分 (%)  */
    uint8_t humi_dec;       /* 湿度小数部分, DHT11 恒为 0 */
    uint8_t temp_int;       /* 温度整数部分 (C)  */
    uint8_t temp_dec;       /* 温度小数部分, DHT11 恒为 0 */
    uint8_t valid;          /* 1 = 校验通过, 数据有效; 0 = 无效(保持旧值) */
} DHT11_Data_t;

/**
 * @brief  初始化 DHT11(开 GPIOG 时钟 + 开 DWT 微秒计数器 + 拉高总线)
 * @note   1) 必须在 SystemClock_Config() 之后调用, 否则 SystemCoreClock 不对,
 *            DWT 延时会算错;
 *         2) 内部用了 vTaskDelay(), 所以只能在任务体里调用, 不能放在 main()
 *            的调度器启动之前。
 */
void DHT11_Init( void );

/**
 * @brief  读取一次温湿度
 * @param  pData : 结果存放处
 * @retval 1 = 读成功且校验通过; 0 = 失败(超时或校验错), 此时 pData 不修改
 * @note   两次调用之间建议间隔 >= 1s, 本工程用 2s。
 *         读取过程约 5ms 处于临界区(关调度), 调用者无需额外保护。
 */
uint8_t DHT11_Read( DHT11_Data_t *pData );

#endif /* __DHT11_H */
