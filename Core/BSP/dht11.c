#include "dht11.h"

#include "FreeRTOS.h"
#include "task.h"

/* ==========================================================================
 * 为什么要用 DWT 做微秒延时?
 * --------------------------------------------------------------------------
 * HAL_Delay() 最小单位是 1ms, 而 DHT11 的时序是微秒级(26us vs 70us 区分 0/1)。
 * Cortex-M3 内核自带一个 24 位的时钟周期计数器 DWT->CYCCNT, 每 1 个 CPU 周期
 * 加 1。72MHz 下 1us = 72 个计数, 精度足够。它不受中断、不受 RTOS 调度影响,
 * 是最靠谱的裸机微秒计时源。
 * ==========================================================================
 */

static uint32_t usTicks = 72U;      /* 1us 对应的 CPU 周期数, Init 时重算 */

/* ------------------------------------------------------------------ */
/*                          底层工具函数                              */
/* ------------------------------------------------------------------ */

/**
 * @brief 打开 DWT 周期计数器(Cortex-M3 上默认是关的)
 */
static void DWT_Init( void )
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;  /* 允许访问 DWT 寄存器 */
    DWT->CYCCNT       = 0UL;                         /* 计数清零            */
    DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;      /* 启动计数            */
}

/**
 * @brief  微秒级阻塞延时
 */
static void DelayUs( uint32_t us )
{
    uint32_t start = DWT->CYCCNT;
    uint32_t wait  = us * usTicks;

    while( ( DWT->CYCCNT - start ) < wait )
    {
        /* 依赖 32 位无符号数回绕做减法, 回绕也安全 */
    }
}

/**
 * @brief  把 PG9 配成推挽输出(主机拉低总线用)
 */
static void PinMode_Output( void )
{
    GPIO_InitTypeDef gpio = { 0 };

    gpio.Pin   = DHT11_Pin;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init( DHT11_GPIO_Port, &gpio );
}

/**
 * @brief  把 PG9 配成上拉输入(释放总线, 由外部 4.7K 电阻拉高)
 */
static void PinMode_Input( void )
{
    GPIO_InitTypeDef gpio = { 0 };

    gpio.Pin   = DHT11_Pin;
    gpio.Mode  = GPIO_MODE_INPUT;
    gpio.Pull  = GPIO_PULLUP;       /* 内部上拉约 40K, 只是兜底, 仍建议外接 4.7K */
    HAL_GPIO_Init( DHT11_GPIO_Port, &gpio );
}

/**
 * @brief  等到总线变成指定电平, 带超时
 * @param  level     : 要等的电平(GPIO_PIN_RESET / GPIO_PIN_SET)
 * @param  timeoutUs : 超时时间
 * @retval 0 = 等到了; 1 = 超时(说明传感器没响应 / 掉线)
 */
static uint8_t WaitForLevel( GPIO_PinState level, uint32_t timeoutUs )
{
    uint32_t start = DWT->CYCCNT;
    uint32_t wait  = timeoutUs * usTicks;

    while( HAL_GPIO_ReadPin( DHT11_GPIO_Port, DHT11_Pin ) != level )
    {
        if( ( DWT->CYCCNT - start ) > wait )
        {
            return 1U;      /* 超时 */
        }
    }
    return 0U;
}

/**
 * @brief  从总线上读 1 个字节(8 bit)
 * @retval 读到的字节; 中途超时返回 0(调用方会因校验失败而丢弃整帧)
 */
static uint8_t ReadByte( void )
{
    uint8_t i;
    uint8_t data = 0U;

    for( i = 0U; i < 8U; i++ )
    {
        uint32_t t0;
        uint32_t highUs;

        data <<= 1;

        /* 每一 bit 都以 50us 低电平开头 */
        if( WaitForLevel( GPIO_PIN_RESET, 100U ) != 0U )
        {
            return 0U;
        }

        /* 低电平结束 -> 高电平开始, 此刻起开始计时 */
        if( WaitForLevel( GPIO_PIN_SET, 100U ) != 0U )
        {
            return 0U;
        }
        t0 = DWT->CYCCNT;

        /* 等这个高电平结束, 就得出了它的宽度 */
        WaitForLevel( GPIO_PIN_RESET, 100U );

        highUs = ( DWT->CYCCNT - t0 ) / usTicks;

        if( highUs > DHT11_BIT_1_MIN_US )
        {
            data |= 0x01U;      /* 70us -> '1' */
        }
        /* 26~28us -> '0', 什么都不用做 */
    }

    return data;
}

/* ------------------------------------------------------------------ */
/*                            对外接口                                */
/* ------------------------------------------------------------------ */

void DHT11_Init( void )
{
    __HAL_RCC_GPIOG_CLK_ENABLE();   /* 就算 CubeMX 配过, 再开一次也无害 */

    usTicks = SystemCoreClock / 1000000UL;   /* 72MHz -> 72 */

    DWT_Init();

    PinMode_Output();
    HAL_GPIO_WritePin( DHT11_GPIO_Port, DHT11_Pin, GPIO_PIN_SET );  /* 空闲态 = 高 */

    vTaskDelay( pdMS_TO_TICKS( 1000 ) );     /* 上电后等 1s 越过不稳定期 */
}

uint8_t DHT11_Read( DHT11_Data_t *pData )
{
    uint8_t buf[ 5 ];
    uint8_t i;
    uint8_t sum;

    if( pData == NULL )
    {
        return 0U;
    }

    /* ------- 1. 主机发起始信号 ----------------------------------------
     * 拉低 >= 18ms。这 20ms 里用 vTaskDelay, 让调度器能跑别的任务,
     * 不浪费 CPU(这段对时序不敏感, 长一点也没关系)。                   */
    PinMode_Output();
    HAL_GPIO_WritePin( DHT11_GPIO_Port, DHT11_Pin, GPIO_PIN_RESET );
    vTaskDelay( pdMS_TO_TICKS( 20 ) );

    HAL_GPIO_WritePin( DHT11_GPIO_Port, DHT11_Pin, GPIO_PIN_SET );
    DelayUs( DHT11_START_HIGH_US );          /* 释放总线 20~40us */

    PinMode_Input();                         /* 转输入, 交出总线控制权 */

    /* ------- 2. 下面是微秒级严格时序, 必须关调度 ----------------------
     * taskENTER_CRITICAL() 把 BASEPRI 写成 5, 屏蔽掉优先级数值 >= 5 的
     * 中断和任务切换。万一被别的中断打断几百 us, 40 个 bit 的电平宽度
     * 就全乱了, 读出来的是乱码。                                        */
    taskENTER_CRITICAL();

    /* DHT11 响应: 先拉低 80us, 再拉高 80us */
    if( WaitForLevel( GPIO_PIN_RESET, 100U ) != 0U )    /* 等响应低电平 */
    {
        taskEXIT_CRITICAL();
        PinMode_Output();
        HAL_GPIO_WritePin( DHT11_GPIO_Port, DHT11_Pin, GPIO_PIN_SET );
        return 0U;
    }

    if( WaitForLevel( GPIO_PIN_SET, 100U ) != 0U )      /* 等响应高电平 */
    {
        taskEXIT_CRITICAL();
        PinMode_Output();
        HAL_GPIO_WritePin( DHT11_GPIO_Port, DHT11_Pin, GPIO_PIN_SET );
        return 0U;
    }

    /* ------- 3. 收 40 bit = 5 字节 -----------------------------------
     * 顺序: 湿度整数 / 湿度小数 / 温度整数 / 温度小数 / 校验和          */
    for( i = 0U; i < 5U; i++ )
    {
        buf[ i ] = ReadByte();
    }

    taskEXIT_CRITICAL();

    /* 释放总线回到空闲态 */
    PinMode_Output();
    HAL_GPIO_WritePin( DHT11_GPIO_Port, DHT11_Pin, GPIO_PIN_SET );

    /* ------- 4. 校验 --------------------------------------------------
     * 校验和 = 前 4 个字节之和的低 8 位                                 */
    sum = ( uint8_t )( buf[ 0 ] + buf[ 1 ] + buf[ 2 ] + buf[ 3 ] );

    if( sum != buf[ 4 ] )
    {
        return 0U;      /* 数据有误, 放弃这一帧 */
    }

    pData->humi_int = buf[ 0 ];
    pData->humi_dec = buf[ 1 ];
    pData->temp_int = buf[ 2 ];
    pData->temp_dec = buf[ 3 ];
    pData->valid    = 1U;

    return 1U;
}
