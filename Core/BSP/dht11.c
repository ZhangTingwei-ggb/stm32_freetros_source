#include "dht11.h"

#include "FreeRTOS.h"
#include "task.h"

/* ==========================================================================
 * Why use DWT for microsecond delays?
 * --------------------------------------------------------------------------
 * HAL_Delay() has a 1 ms granularity, but the DHT11 timing is microsecond
 * level: a '0' bit is a 26 us high pulse, a '1' bit is 70 us. Every Cortex-M3
 * carries a 24-bit cycle counter, DWT->CYCCNT, which increments once per CPU
 * cycle. At 72 MHz 1 us = 72 counts, which is plenty of resolution. It is not
 * affected by interrupts or by RTOS scheduling, which makes it the most
 * reliable bare-metal microsecond time source available here.
 * ==========================================================================
 */

static uint32_t usTicks = 72U;      /* CPU cycles per microsecond, recomputed
                                     * in DHT11_Init() from SystemCoreClock */

/* ------------------------------------------------------------------ */
/*                        Low level helpers                           */
/* ------------------------------------------------------------------ */

/**
 * @brief  Enable the DWT cycle counter (it is off by default on Cortex-M3).
 */
static void DWT_Init( void )
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;  /* grant access to DWT   */
    DWT->CYCCNT       = 0UL;                         /* reset the counter     */
    DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;      /* start counting        */
}

/**
 * @brief  Blocking microsecond delay.
 * @param  us : microseconds to wait
 * @note   Relies on unsigned 32-bit wrap-around for the subtraction, which is
 *         well defined, so a counter roll-over in the middle is harmless.
 */
static void DelayUs( uint32_t us )
{
    uint32_t start = DWT->CYCCNT;
    uint32_t wait  = us * usTicks;

    while( ( DWT->CYCCNT - start ) < wait )
    {
        /* spin */
    }
}

/**
 * @brief  Configure PG9 as push-pull output (host pulls the bus low).
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
 * @brief  Configure PG9 as input with pull-up (release the bus; the external
 *         4.7K resistor holds it high).
 */
static void PinMode_Input( void )
{
    GPIO_InitTypeDef gpio = { 0 };

    gpio.Pin   = DHT11_Pin;
    gpio.Mode  = GPIO_MODE_INPUT;
    gpio.Pull  = GPIO_PULLUP;       /* internal pull-up is ~40K: a fallback
                                     * only, an external 4.7K is still advised */
    HAL_GPIO_Init( DHT11_GPIO_Port, &gpio );
}

/**
 * @brief  Wait until the bus reaches the given level, with a timeout.
 * @param  level     : level to wait for (GPIO_PIN_RESET / GPIO_PIN_SET)
 * @param  timeoutUs : how long to wait before giving up
 * @retval 0 = level reached; 1 = timed out (sensor silent or disconnected)
 */
static uint8_t WaitForLevel( GPIO_PinState level, uint32_t timeoutUs )
{
    uint32_t start = DWT->CYCCNT;
    uint32_t wait  = timeoutUs * usTicks;

    while( HAL_GPIO_ReadPin( DHT11_GPIO_Port, DHT11_Pin ) != level )
    {
        if( ( DWT->CYCCNT - start ) > wait )
        {
            return 1U;      /* timeout */
        }
    }
    return 0U;
}

/**
 * @brief  Read one byte (8 bits) from the bus.
 * @retval the byte; 0 if a timeout happened mid-byte. The caller then discards
 *         the whole frame because the checksum will not match.
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

        /* Every bit starts with a 50 us low pulse */
        if( WaitForLevel( GPIO_PIN_RESET, 100U ) != 0U )
        {
            return 0U;
        }

        /* Low ends -> high begins; start measuring here */
        if( WaitForLevel( GPIO_PIN_SET, 100U ) != 0U )
        {
            return 0U;
        }
        t0 = DWT->CYCCNT;

        /* Wait for that high pulse to end: that gives us its width */
        WaitForLevel( GPIO_PIN_RESET, 100U );

        highUs = ( DWT->CYCCNT - t0 ) / usTicks;

        if( highUs > DHT11_BIT_1_MIN_US )
        {
            data |= 0x01U;      /* 70 us -> '1' */
        }
        /* 26..28 us -> '0', nothing to do */
    }

    return data;
}

/* ------------------------------------------------------------------ */
/*                            Public API                              */
/* ------------------------------------------------------------------ */

void DHT11_Init( void )
{
    __HAL_RCC_GPIOG_CLK_ENABLE();   /* harmless even if CubeMX already did it */

    usTicks = SystemCoreClock / 1000000UL;   /* 72 MHz -> 72 */

    DWT_Init();

    PinMode_Output();
    HAL_GPIO_WritePin( DHT11_GPIO_Port, DHT11_Pin, GPIO_PIN_SET );  /* idle = high */

    vTaskDelay( pdMS_TO_TICKS( 1000 ) );     /* 1 s to pass the power-on
                                              * unstable period */
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

    /* ------- 1. Host sends the start signal -----------------------------
     * Pull low for >= 18 ms. We use vTaskDelay for these 20 ms so the
     * scheduler can run other tasks instead of burning CPU. This part is not
     * timing critical, so a slightly longer low pulse does no harm.        */
    PinMode_Output();
    HAL_GPIO_WritePin( DHT11_GPIO_Port, DHT11_Pin, GPIO_PIN_RESET );
    vTaskDelay( pdMS_TO_TICKS( 20 ) );

    HAL_GPIO_WritePin( DHT11_GPIO_Port, DHT11_Pin, GPIO_PIN_SET );
    DelayUs( DHT11_START_HIGH_US );          /* release the bus for 20..40 us */

    PinMode_Input();                         /* switch to input, hand the bus
                                              * over to the sensor */

    /* ------- 2. From here on timing is strict: lock the scheduler -------
     * taskENTER_CRITICAL() writes BASEPRI = 5, masking every interrupt with
     * a priority number >= 5 and stopping task switches. A single interrupt
     * stealing a few hundred microseconds here would smear all 40 bit widths
     * and the result would be garbage.                                     */
    taskENTER_CRITICAL();

    /* DHT11 response: 80 us low, then 80 us high */
    if( WaitForLevel( GPIO_PIN_RESET, 100U ) != 0U )    /* wait for low */
    {
        taskEXIT_CRITICAL();
        PinMode_Output();
        HAL_GPIO_WritePin( DHT11_GPIO_Port, DHT11_Pin, GPIO_PIN_SET );
        return 0U;
    }

    if( WaitForLevel( GPIO_PIN_SET, 100U ) != 0U )      /* wait for high */
    {
        taskEXIT_CRITICAL();
        PinMode_Output();
        HAL_GPIO_WritePin( DHT11_GPIO_Port, DHT11_Pin, GPIO_PIN_SET );
        return 0U;
    }

    /* ------- 3. Receive 40 bits = 5 bytes -------------------------------
     * Order: humidity int / humidity dec / temperature int / temp dec /
     * checksum                                                            */
    for( i = 0U; i < 5U; i++ )
    {
        buf[ i ] = ReadByte();
    }

    taskEXIT_CRITICAL();

    /* Release the bus back to the idle state */
    PinMode_Output();
    HAL_GPIO_WritePin( DHT11_GPIO_Port, DHT11_Pin, GPIO_PIN_SET );

    /* ------- 4. Verify -------------------------------------------------
     * Checksum = low 8 bits of the sum of the first 4 bytes                */
    sum = ( uint8_t )( buf[ 0 ] + buf[ 1 ] + buf[ 2 ] + buf[ 3 ] );

    if( sum != buf[ 4 ] )
    {
        return 0U;      /* corrupt frame, throw it away */
    }

    pData->humi_int = buf[ 0 ];
    pData->humi_dec = buf[ 1 ];
    pData->temp_int = buf[ 2 ];
    pData->temp_dec = buf[ 3 ];
    pData->valid    = 1U;

    return 1U;
}
