/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "lcd.h"
#include "dht11.h"
#include "app_shared.h"
#include "bsp_adc.h"
#include "bsp_uart_dma.h"
#include "param_storage.h"
#include "custom_protocol.h"
#include "modbus_rtu.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* Task stack depth is counted in words: 256 * 4 = 1 KB.
 * LcdTask calls snprintf, so it eats more stack than the plain GPIO
 * tasks; 256 leaves a comfortable margin. */
#define TASK_STACK_DEPTH        256U

/* Priorities: sampler > uart protocol > display.
 * The sampler runs first so no sample is dropped; the uart task sits above
 * display so command responses stay fast (< 10 ms, doc 9.3). */
#define DHT11_TASK_PRIO         3U
#define UART_TASK_PRIO          4U
#define LCD_TASK_PRIO           2U

/* Sampling period is no longer a fixed macro: it comes from Flash params
 * (Param_GetPeriodMs, default 2000 ms, 500~60000 ms via protocol).
 * DHT11 datasheet asks for >= 1 s between samples. */
#define UART_TASK_STACK_DEPTH   256U

/* USART1 single-port baud. Doc says custom 115200 + Modbus 9600, but the
 * board has only one USART1, so both protocols share 115200 (see bsp_uart_dma.h).
 * The Modbus master must also use 115200. */
#define UART_BAUD               115200U

/* ---------------------------------------------------------------------------
 * UI layout - PORTRAIT 240 x 320
 * ---------------------------------------------------------------------------
 * The font is 16 x 24 px, so a 240 px line holds 15 characters. That is the
 * only real constraint here: every string below has to end before x = 239.
 *
 *   8    DHT11 MONITOR     heading (13 chars)
 *   44   Temperature       caption
 *   72   23.5 C            reading (6 chars, fixed)
 *   108  Humidity          caption
 *   136  45.0 %            reading (6 chars, fixed)
 *   172  Voltage           caption
 *   200  3.30 V            reading (6 chars, fixed)
 *   236  Status: OK        status line (12 chars, fixed)
 *   272  OK:12   E:0       counters, one line, 14 chars fixed
 * ------------------------------------------------------------------------- */
#define UI_X            12U     /* left margin, shared by every line */
#define UI_TITLE_Y      8U
#define UI_TEMP_LBL_Y   44U
#define UI_TEMP_VAL_Y   72U
#define UI_HUMI_LBL_Y   108U
#define UI_HUMI_VAL_Y   136U
#define UI_VOLT_LBL_Y   172U
#define UI_VOLT_VAL_Y   200U
#define UI_STAT_Y       236U
#define UI_CNT_Y        272U

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SRAM_HandleTypeDef hsram1;

/* USER CODE BEGIN PV */
/* Data channels, defined here (declared in app_shared.h):
 * q_sensor2display: depth 1 + xQueueOverwrite, always newest sample only.
 * q_sensor2uart: depth 5, upload frames queue up if the uart task is busy.
 * g_latest: newest sample snapshot for the Modbus task (single writer). */
QueueHandle_t        q_sensor2display = NULL;
QueueHandle_t        q_sensor2uart    = NULL;
TaskHandle_t         uartTaskHandle   = NULL;
volatile SensorData_t g_latest = { 0, 0, 0, 0, 0 };

static TaskHandle_t  dht11TaskHandle = NULL;
static TaskHandle_t  lcdTaskHandle   = NULL;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_FSMC_Init(void);
/* USER CODE BEGIN PFP */
static void Dht11Task( void *argument );    /* sampling: DHT11 + ADC */
static void LcdTask( void *argument );      /* display: refresh on new data   */
static void UartTask( void *argument );     /* custom protocol + Modbus slave */
static void UI_DrawStatic( void );          /* draw the fixed labels          */
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
  * @brief  Draw what is drawn only once: the title bar plus fixed labels
  */
static void UI_DrawStatic( void )
{
    char cnt[ 16 ];

    LCD_Clear( LCD_COLOR_BG );

    LCD_ShowString( UI_X, UI_TITLE_Y,    "DHT11 MONITOR", LCD_COLOR_TEXT );
    LCD_ShowString( UI_X, UI_TEMP_LBL_Y, "Temperature",   LCD_COLOR_LABEL );
    LCD_ShowString( UI_X, UI_HUMI_LBL_Y, "Humidity",      LCD_COLOR_LABEL );
    LCD_ShowString( UI_X, UI_VOLT_LBL_Y, "Voltage",       LCD_COLOR_LABEL );

    /* Placeholders, each exactly as long as a real reading (6 chars), so the
     * first sample overwrites them without leaving anything behind. */
    LCD_ShowString( UI_X, UI_TEMP_VAL_Y, "--.- C", LCD_COLOR_LABEL );
    LCD_ShowString( UI_X, UI_HUMI_VAL_Y, "--.- %", LCD_COLOR_LABEL );
    LCD_ShowString( UI_X, UI_VOLT_VAL_Y, "-.-- V", LCD_COLOR_LABEL );

    /* Exactly 12 characters, like "Status: OK  " and "Status: ERR " below, so
     * all three states overwrite each other cleanly. */
    LCD_ShowString( UI_X, UI_STAT_Y, "Status: WAIT", LCD_COLOR_WARN );

    /* 14 chars fixed, same format string as the live counters below. */
    snprintf( cnt, sizeof( cnt ), "OK:%-4lu E:%-4lu", 0UL, 0UL );
    LCD_ShowString( UI_X, UI_CNT_Y, cnt, LCD_COLOR_LABEL );
}

/**
  * @brief  Sampling task
  * @note   DHT11_Init() calls vTaskDelay() internally, so it must be
  *         called after the scheduler has started (that is, from a task
  *         body); it cannot go in main().
  */
static void Dht11Task( void *argument )
{
    DHT11_Data_t dht;
    SensorData_t data;

    ( void ) argument;

    DHT11_Init();           /* start DWT, pull the bus high, wait 1 s to
                             * get past the power-on unstable period */

    for( ;; )
    {
        /* A failed read is still forwarded, so the screen shows an error state
         * instead of freezing on the last good value */
        if( DHT11_Read( &dht ) == 0U )
        {
            data.temp_x10 = 0;
            data.humi_x10 = 0U;
            data.valid    = 0U;
        }
        else
        {
            data.temp_x10 = ( int16_t )( ( int16_t ) dht.temp_int * 10 +
                                         dht.temp_dec );
            data.humi_x10 = ( uint16_t )( ( uint16_t ) dht.humi_int * 10U +
                                          dht.humi_dec );
            data.valid    = 1U;
        }

        /* ADC 电位器电压, 每次采样都读, 和温湿度打进同一个包 */
        data.volt_mv  = BSP_ADC_ToMillivolt( BSP_ADC_ReadRaw() );
        data.timestamp = xTaskGetTickCount();

        g_latest = data;    /* Modbus 从站直接读这份快照 */

        /* Display queue: depth 1 + Overwrite, never blocks. Uart queue:
         * depth 5, drop (0 timeout) if the uart task is backed up. */
        xQueueOverwrite( q_sensor2display, &data );
        xQueueSend( q_sensor2uart, &data, 0 );

        /* 周期来自 Flash 参数, 上位机/Modbus 可改, 下次循环生效 */
        vTaskDelay( pdMS_TO_TICKS( Param_GetPeriodMs() ) );
    }
}

/**
  * @brief  Display task: block on the queue, refresh whenever data arrives
  */
static void LcdTask( void *argument )
{
    SensorData_t data;
    char buf[ 16 ];         /* longest string is "OK:9999 E:9999" = 14 chars */
    uint32_t okCnt  = 0U;
    uint32_t errCnt = 0U;

    ( void ) argument;

    LCD_Init();             /* FSMC must already have been configured by
                             * MX_FSMC_Init() */
    UI_DrawStatic();

    for( ;; )
    {
        /* Block here while there is no data: costs no CPU */
        if( xQueueReceive( q_sensor2display, &data, portMAX_DELAY ) != pdPASS )
        {
            continue;
        }

        /* Voltage is shown on every refresh, good or bad sample alike. */
        snprintf( buf, sizeof( buf ), "%d.%02d V",
                  data.volt_mv / 1000U, ( data.volt_mv % 1000U ) / 10U );
        LCD_ShowString( UI_X, UI_VOLT_VAL_Y, buf, LCD_COLOR_TEXT );

        if( data.valid != 0U )
        {
            okCnt++;

            /* "%2d.%d C" is always 6 characters (" 9.5 C" as well as "23.5 C"),
             * so it overwrites the previous reading exactly. */
            snprintf( buf, sizeof( buf ), "%2d.%d C",
                      data.temp_x10 / 10, data.temp_x10 % 10 );
            LCD_ShowString( UI_X, UI_TEMP_VAL_Y, buf, LCD_COLOR_TEMP );

            snprintf( buf, sizeof( buf ), "%2d.%d %%",
                      data.humi_x10 / 10U, data.humi_x10 % 10U );
            LCD_ShowString( UI_X, UI_HUMI_VAL_Y, buf, LCD_COLOR_HUMI );

            LCD_ShowString( UI_X, UI_STAT_Y, "Status: OK  ", LCD_COLOR_OK );
        }
        else
        {
            errCnt++;

            /* The same 6 characters as a real reading, so the error text erases
             * the old value instead of leaving a stray digit behind. */
            LCD_ShowString( UI_X, UI_TEMP_VAL_Y, "--.- C", LCD_COLOR_ERR );
            LCD_ShowString( UI_X, UI_HUMI_VAL_Y, "--.- %", LCD_COLOR_ERR );

            LCD_ShowString( UI_X, UI_STAT_Y, "Status: ERR ", LCD_COLOR_ERR );
        }

        /* Fixed 14 chars ("OK:12   E:0   "): "%-4lu" is a MINIMUM width, so a
         * longer count would push past the right edge where the address
         * counter wraps it back left as garbage. Clamp at 9999. */
        snprintf( buf, sizeof( buf ), "OK:%-4lu E:%-4lu",
                  ( okCnt  > 9999UL ) ? 9999UL : okCnt,
                  ( errCnt > 9999UL ) ? 9999UL : errCnt );
        LCD_ShowString( UI_X, UI_CNT_Y, buf, LCD_COLOR_LABEL );
    }
}

/**
  * @brief  Uart task: one USART1 serves two protocols (doc 4.3 + 6.x).
  * @note   TX: each sensor sample goes out as a custom 0x01 upload frame.
  *         RX: bytes starting with 0xAA go to the custom-protocol state
  *         machine, everything else is collected with a 10 ms silence gap
  *         and treated as a Modbus RTU frame.
  */
#define UART_STAGE_SIZE   96U

static void UartTask( void *argument )
{
    CProto_Parser_t cproto;
    static uint8_t  stage[ UART_STAGE_SIZE ];   /* static: keep task stack small */
    static uint16_t stageLen = 0U;
    uint8_t  rx[ 64 ];
    uint8_t  tx[ 96 ];
    SensorData_t s;

    ( void ) argument;

    CProto_ParserInit( &cproto );

    for( ;; )
    {
        /* Sleep until the IDLE ISR knocks, or 20 ms timeout to drain uploads */
        ulTaskNotifyTake( pdTRUE, pdMS_TO_TICKS( 20 ) );

        /* 1) 上报: 每个采样一帧 0x01, 发完再看有没有堆积的 */
        while( xQueueReceive( q_sensor2uart, &s, 0 ) == pdPASS )
        {
            uint16_t n = CProto_BuildUpload( &s, tx, sizeof( tx ) );

            if( n > 0U )
            {
                BSP_UART_Send( tx, n );
            }
        }

        /* 2) 收字节并分流: 自定义帧逐字节喂状态机, 其余攒进 stage */
        uint16_t n = BSP_UART_ReadBytes( rx, sizeof( rx ) );

        for( uint16_t i = 0U; i < n; i++ )
        {
            uint8_t b = rx[ i ];

            if( CProto_InFrame( &cproto ) )
            {
                uint16_t rl = 0U;

                if( CProto_ParseByte( &cproto, b, tx, &rl ) && rl > 0U )
                {
                    BSP_UART_Send( tx, rl );    /* 只 0x03 查询有应答 */
                }
            }
            else if( stageLen == 0U && b == CPROTO_HEAD )
            {
                uint16_t rl = 0U;

                /* 帧头另起一帧, 不进 Modbus 暂存 */
                if( CProto_ParseByte( &cproto, b, tx, &rl ) && rl > 0U )
                {
                    BSP_UART_Send( tx, rl );
                }
            }
            else if( stageLen < UART_STAGE_SIZE )
            {
                stage[ stageLen++ ] = b;
            }
            else
            {
                stageLen = 0U;  /* 溢出: 之前攒的肯定不是合法帧, 扔掉重来 */
            }
        }

        /* 3) Modbus: 10 ms 静默 = 一帧收齐, 当帧处理 (TIM4 超时用空闲代替) */
        if( stageLen >= 4U )
        {
            vTaskDelay( pdMS_TO_TICKS( 10 ) );

            uint16_t m = BSP_UART_ReadBytes( rx, sizeof( rx ) );
            uint16_t added = 0U;

            for( uint16_t i = 0U; i < m; i++ )
            {
                uint8_t b = rx[ i ];

                if( CProto_InFrame( &cproto ) ||
                    ( stageLen == 0U && b == CPROTO_HEAD ) )
                {
                    uint16_t rl = 0U;

                    if( CProto_ParseByte( &cproto, b, tx, &rl ) && rl > 0U )
                    {
                        BSP_UART_Send( tx, rl );
                    }
                }
                else if( stageLen < UART_STAGE_SIZE )
                {
                    stage[ stageLen++ ] = b;
                    added++;
                }
                else
                {
                    stageLen = 0U;
                    added    = 1U;
                    break;
                }
            }

            if( added == 0U )
            {
                uint16_t rl = Modbus_Process( stage, stageLen, tx, sizeof( tx ) );

                if( rl > 0U )
                {
                    BSP_UART_Send( tx, rl );    /* 广播/地址不符返回 0, 不发 */
                }

                stageLen = 0U;
            }
            /* 还有新字节进来: 说明帧没收完, 下一轮循环继续等静默 */
        }
    }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  /* Queues must exist before the tasks start. Display queue depth 1
   * (newest-only), uart upload queue depth 5 (doc 4.4). */
  q_sensor2display = xQueueCreate( 1, sizeof( SensorData_t ) );
  q_sensor2uart    = xQueueCreate( 5, sizeof( SensorData_t ) );
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_FSMC_Init();
  /* USER CODE BEGIN 2 */
  /* Note: after enabling FSMC in CubeMX and regenerating the project,
   * CubeMX inserts the declaration and the call of MX_FSMC_Init() right
   * after MX_GPIO_Init() on its own - no need to write it by hand.
   * Calling LCD_Init() before the FSMC is up has no effect.
   *
   * ADC1 (PA1) and USART1 (PA9/PA10 + DMA1 Ch5) are NOT in Source.ioc -
   * their init lives in BSP (bsp_adc.c / bsp_uart_dma.c) so a CubeMX
   * regenerate cannot wipe them. If you add them in CubeMX later, remove
   * the hand init here to avoid double configuration. */

  Param_Load();                 /* Flash 参数 -> 内存, 坏块则默认值 */
  BSP_ADC_Init();               /* 电位器 ADC, 无 RTOS 调用, 调度器前可调 */
  BSP_UART_Init( UART_BAUD );   /* DMA 接收 + 空闲中断, 中断里已判调度器状态 */

  if( q_sensor2display != NULL && q_sensor2uart != NULL )
  {
      xTaskCreate( Dht11Task, "dht11", TASK_STACK_DEPTH, NULL, DHT11_TASK_PRIO, &dht11TaskHandle );
      xTaskCreate( LcdTask,   "lcd",   TASK_STACK_DEPTH, NULL, LCD_TASK_PRIO,   &lcdTaskHandle );
      xTaskCreate( UartTask,  "uart",  UART_TASK_STACK_DEPTH, NULL, UART_TASK_PRIO, &uartTaskHandle );

      vTaskStartScheduler();   /* start the scheduler; normally it never
                               * returns */
  }

  /* Reaching this point means the heap was too small or the scheduler
   * failed to start */
  Error_Handler();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
/* USER CODE BEGIN MX_GPIO_Init_1 */
/* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0|LED0_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOG, GPIO_PIN_9, GPIO_PIN_SET);

  /*Configure GPIO pins : KEY1_Pin KEY0_Pin */
  GPIO_InitStruct.Pin = KEY1_Pin|KEY0_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pin : LED1_Pin */
  GPIO_InitStruct.Pin = LED1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : KEY_UP_Pin */
  GPIO_InitStruct.Pin = KEY_UP_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(KEY_UP_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : PB0 LED0_Pin */
  GPIO_InitStruct.Pin = GPIO_PIN_0|LED0_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : PG9 */
  GPIO_InitStruct.Pin = GPIO_PIN_9;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI0_IRQn);

  HAL_NVIC_SetPriority(EXTI3_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI3_IRQn);

  HAL_NVIC_SetPriority(EXTI4_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI4_IRQn);

/* USER CODE BEGIN MX_GPIO_Init_2 */
  /* These key interrupts have no callback handler at the moment, but the
   * priority is still changed: 0 means "highest priority, not managed by
   * FreeRTOS", and calling any xxxFromISR API from there later would trip
   * configASSERT and hang. Changing it keeps things safe if you wire the
   * callbacks back up later. */
  HAL_NVIC_SetPriority(EXTI0_IRQn, 6, 0);
  HAL_NVIC_SetPriority(EXTI3_IRQn, 6, 0);
  HAL_NVIC_SetPriority(EXTI4_IRQn, 6, 0);
/* USER CODE END MX_GPIO_Init_2 */
}

/* FSMC initialization function */
static void MX_FSMC_Init(void)
{

  /* USER CODE BEGIN FSMC_Init 0 */

  /* USER CODE END FSMC_Init 0 */

  FSMC_NORSRAM_TimingTypeDef Timing = {0};

  /* USER CODE BEGIN FSMC_Init 1 */

  /* USER CODE END FSMC_Init 1 */

  /** Perform the SRAM1 memory initialization sequence
  */
  hsram1.Instance = FSMC_NORSRAM_DEVICE;
  hsram1.Extended = FSMC_NORSRAM_EXTENDED_DEVICE;
  /* hsram1.Init */
  hsram1.Init.NSBank = FSMC_NORSRAM_BANK4;
  hsram1.Init.DataAddressMux = FSMC_DATA_ADDRESS_MUX_DISABLE;
  hsram1.Init.MemoryType = FSMC_MEMORY_TYPE_SRAM;
  hsram1.Init.MemoryDataWidth = FSMC_NORSRAM_MEM_BUS_WIDTH_16;
  hsram1.Init.BurstAccessMode = FSMC_BURST_ACCESS_MODE_DISABLE;
  hsram1.Init.WaitSignalPolarity = FSMC_WAIT_SIGNAL_POLARITY_LOW;
  hsram1.Init.WrapMode = FSMC_WRAP_MODE_DISABLE;
  hsram1.Init.WaitSignalActive = FSMC_WAIT_TIMING_BEFORE_WS;
  hsram1.Init.WriteOperation = FSMC_WRITE_OPERATION_ENABLE;
  hsram1.Init.WaitSignal = FSMC_WAIT_SIGNAL_DISABLE;
  hsram1.Init.ExtendedMode = FSMC_EXTENDED_MODE_DISABLE;
  hsram1.Init.AsynchronousWait = FSMC_ASYNCHRONOUS_WAIT_DISABLE;
  hsram1.Init.WriteBurst = FSMC_WRITE_BURST_DISABLE;
  /* Timing */
  Timing.AddressSetupTime = 2;
  Timing.AddressHoldTime = 15;
  Timing.DataSetupTime = 4;
  Timing.BusTurnAroundDuration = 0;
  Timing.CLKDivision = 16;
  Timing.DataLatency = 17;
  Timing.AccessMode = FSMC_ACCESS_MODE_A;
  /* ExtTiming */

  if (HAL_SRAM_Init(&hsram1, &Timing, NULL) != HAL_OK)
  {
    Error_Handler( );
  }

  /** Disconnect NADV
  */

  __HAL_AFIO_FSMCNADV_DISCONNECTED();

  /* USER CODE BEGIN FSMC_Init 2 */

  /* USER CODE END FSMC_Init 2 */
}

/* USER CODE BEGIN 4 */
/* USER CODE END 4 */

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM1 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM1) {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
