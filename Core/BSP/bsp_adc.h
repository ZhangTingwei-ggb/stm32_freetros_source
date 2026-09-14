#ifndef __BSP_ADC_H
#define __BSP_ADC_H

#include "main.h"

/* 板载电位器 -> ADC1_IN1(PA1), 12 位, 参考 3.3V。
 * 单次软件触发, 采集任务里轮询读, 不用 DMA。 */

void     BSP_ADC_Init( void );
uint16_t BSP_ADC_ReadRaw( void );                    /* 0~4095, 超时失败返回上次值 */
uint16_t BSP_ADC_ToMillivolt( uint16_t raw );        /* raw -> mV */

#endif /* __BSP_ADC_H */
