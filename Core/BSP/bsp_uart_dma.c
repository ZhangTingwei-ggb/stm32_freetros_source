#include "bsp_uart_dma.h"
#include "ring_buffer.h"
#include "FreeRTOS.h"
#include "task.h"

UART_HandleTypeDef huart1;
DMA_HandleTypeDef  hdma_usart1_rx;

/* uartTaskHandle 在 main.c 里定义, 调度器启动前是 NULL, 中断里要判空。 */
extern TaskHandle_t uartTaskHandle;

static uint8_t   dma_buf[ UART_RX_DMA_SIZE ];
static uint8_t   ring_storage[ UART_RING_SIZE ];
static RingBuf_t rx_ring;
static uint16_t  dma_old_pos = 0U;

/* CubeMX 没配 USART1/DMA, Msp 全手写, 覆盖 HAL 弱函数。 */
void HAL_UART_MspInit( UART_HandleTypeDef *huart )
{
    GPIO_InitTypeDef gpio = { 0 };

    if( huart->Instance != USART1 )
    {
        return;
    }

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();

    /* PA9 TX 推挽复用, PA10 RX 浮空输入 */
    gpio.Pin   = GPIO_PIN_9;
    gpio.Mode  = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init( GPIOA, &gpio );

    gpio.Pin  = GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init( GPIOA, &gpio );

    /* USART1_RX -> DMA1 Channel5, 循环 + 字节搬运 */
    hdma_usart1_rx.Instance                 = DMA1_Channel5;
    hdma_usart1_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    hdma_usart1_rx.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_usart1_rx.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_usart1_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart1_rx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    hdma_usart1_rx.Init.Mode                = DMA_CIRCULAR;
    hdma_usart1_rx.Init.Priority            = DMA_PRIORITY_HIGH;
    HAL_DMA_Init( &hdma_usart1_rx );
    __HAL_LINKDMA( huart, hdmarx, hdma_usart1_rx );

    /* FreeRTOS 能管的中断优先级数值 >= 5, 这里用 6 */
    HAL_NVIC_SetPriority( DMA1_Channel5_IRQn, 6, 0 );
    HAL_NVIC_EnableIRQ( DMA1_Channel5_IRQn );

    HAL_NVIC_SetPriority( USART1_IRQn, 6, 0 );
    HAL_NVIC_EnableIRQ( USART1_IRQn );
}

void BSP_UART_Init( uint32_t baud )
{
    RingBuf_Init( &rx_ring, ring_storage, UART_RING_SIZE );
    dma_old_pos = 0U;

    huart1.Instance          = USART1;
    huart1.Init.BaudRate     = baud;
    huart1.Init.WordLength   = UART_WORDLENGTH_8B;
    huart1.Init.StopBits     = UART_STOPBITS_1;
    huart1.Init.Parity       = UART_PARITY_NONE;
    huart1.Init.Mode         = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl   = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init( &huart1 );

    HAL_UART_Receive_DMA( &huart1, dma_buf, UART_RX_DMA_SIZE );
    __HAL_UART_ENABLE_IT( &huart1, UART_IT_IDLE );
}

int BSP_UART_Send( const uint8_t *data, uint16_t len )
{
    if( data == NULL || len == 0U )
    {
        return 0;
    }

    return ( HAL_UART_Transmit( &huart1, ( uint8_t * ) data, len, 100U ) == HAL_OK ) ? 1 : 0;
}

uint16_t BSP_UART_ReadBytes( uint8_t *dst, uint16_t max )
{
    uint16_t n = 0U;
    uint8_t  b;

    while( n < max && RingBuf_Read( &rx_ring, &b ) == 0 )
    {
        dst[ n++ ] = b;
    }

    return n;
}

void BSP_UART_IdleIsr( void )
{
    /* 先处理空闲: 总线静默一帧时间 = 一帧收完, 把 DMA 新到的数据推进 ring */
    if( __HAL_UART_GET_FLAG( &huart1, UART_FLAG_IDLE ) != RESET )
    {
        __HAL_UART_CLEAR_IDLEFLAG( &huart1 );

        uint16_t pos = ( uint16_t )( UART_RX_DMA_SIZE -
                                     __HAL_DMA_GET_COUNTER( &hdma_usart1_rx ) );

        while( dma_old_pos != pos )
        {
            RingBuf_Write( &rx_ring, dma_buf[ dma_old_pos ] );
            dma_old_pos = ( uint16_t )( ( dma_old_pos + 1U ) % UART_RX_DMA_SIZE );
        }

        /* 唤醒 UartTask 组帧, 调度器没起时不调(上电瞬间可能有杂字节) */
        if( uartTaskHandle != NULL && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING )
        {
            BaseType_t woken = pdFALSE;
            vTaskNotifyGiveFromISR( uartTaskHandle, &woken );
            portYIELD_FROM_ISR( woken );
        }
    }

    HAL_UART_IRQHandler( &huart1 );
}
