#ifndef __BSP_UART_DMA_H
#define __BSP_UART_DMA_H

#include "main.h"

/* USART1(PA9/PA10) + DMA1_Channel5 循环接收 + 空闲中断, 文档 5.1 的 bsp_uart_dma。
 * 中断把字节搬进 ring 缓冲, 协议任务负责组帧。发送用轮询, 同一时刻只有一个
 * 任务(UartTask)会发, 不用加锁。
 *
 * 注意: 文档写 Modbus 用 9600, 但板上只有一个 USART1, 和自定义协议复用,
 * 所以统一跑 115200, Modbus 主站那边也要设 115200。 */

#define UART_RX_DMA_SIZE    256U
#define UART_RING_SIZE      512U

extern UART_HandleTypeDef huart1;
extern DMA_HandleTypeDef  hdma_usart1_rx;

void     BSP_UART_Init( uint32_t baud );
int      BSP_UART_Send( const uint8_t *data, uint16_t len );  /* 1=成功 */
uint16_t BSP_UART_ReadBytes( uint8_t *dst, uint16_t max );    /* 任务侧取数 */

/* 在 USART1_IRQHandler 里调, 处理 IDLE 组帧 + HAL 中断后半段。it.c 里声明。 */
void BSP_UART_IdleIsr( void );

#endif /* __BSP_UART_DMA_H */
