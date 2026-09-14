#include "bsp_adc.h"

static ADC_HandleTypeDef hadc1;
static uint16_t          last_raw = 0U;

/* CubeMX 没配 ADC, 这里直接覆盖 HAL 的弱函数, 做时钟 + PA1 模拟输入。 */
void HAL_ADC_MspInit( ADC_HandleTypeDef *hadc )
{
    GPIO_InitTypeDef        gpio = { 0 };
    RCC_PeriphCLKInitTypeDef clk = { 0 };

    if( hadc->Instance != ADC1 )
    {
        return;
    }

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_ADC1_CLK_ENABLE();

    /* ADC 时钟 72MHz / 6 = 12MHz (< 14MHz 上限) */
    clk.PeriphClockSelection = RCC_PERIPHCLK_ADC;
    clk.AdcClockSelection    = RCC_ADCPCLK2_DIV6;
    HAL_RCCEx_PeriphCLKConfig( &clk );

    gpio.Pin  = GPIO_PIN_1;
    gpio.Mode = GPIO_MODE_ANALOG;
    HAL_GPIO_Init( GPIOA, &gpio );
}

void BSP_ADC_Init( void )
{
    ADC_ChannelConfTypeDef ch = { 0 };

    hadc1.Instance                   = ADC1;
    hadc1.Init.ScanConvMode          = ADC_SCAN_DISABLE;
    hadc1.Init.ContinuousConvMode    = DISABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion       = 1U;
    HAL_ADC_Init( &hadc1 );

    ch.Channel      = ADC_CHANNEL_1;
    ch.Rank         = ADC_REGULAR_RANK_1;
    ch.SamplingTime = ADC_SAMPLETIME_55CYCLES_5;
    HAL_ADC_ConfigChannel( &hadc1, &ch );

    HAL_ADCEx_Calibration_Start( &hadc1 );   /* 上电校准一次即可 */
}

uint16_t BSP_ADC_ReadRaw( void )
{
    HAL_ADC_Start( &hadc1 );

    if( HAL_ADC_PollForConversion( &hadc1, 10U ) == HAL_OK )
    {
        last_raw = ( uint16_t ) HAL_ADC_GetValue( &hadc1 );
    }

    HAL_ADC_Stop( &hadc1 );
    return last_raw;
}

uint16_t BSP_ADC_ToMillivolt( uint16_t raw )
{
    /* 12 位满量程 4095 对应 3300mV, 32 位中间值不会溢出 */
    return ( uint16_t )( ( ( uint32_t ) raw * 3300U ) / 4095U );
}
