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
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* 任务栈按 word �?: 256 * 4 = 1KB�?
 * LcdTask 里用�? snprintf, 栈消耗比�? GPIO 任务�?, �? 256 保险�? */
#define TASK_STACK_DEPTH        256U

/* 优先�?: 采集任务高于显示任务�?
 * 两个任务都是"�?/发队�?"驱动�?, 采集任务优先跑可以保证不丢采样点�? */
#define DHT11_TASK_PRIO         3U
#define LCD_TASK_PRIO           2U

/* DHT11 官方要求采样间隔 >= 1s, 这里�? 2s, 传感器更稳定 */
#define DHT11_PERIOD_MS         2000U

/* --- 屏幕上各元素的坐�?(240x320 竖屏, 字模 16x24) --- */
#define UI_LABEL_X              16U
#define UI_VALUE_X              40U
#define UI_TEMP_LBL_Y           70U
#define UI_TEMP_VAL_Y           104U
#define UI_HUMI_LBL_Y           170U
#define UI_HUMI_VAL_Y           204U
#define UI_STAT_Y               258U
#define UI_CNT_Y                290U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SRAM_HandleTypeDef hsram1;

/* USER CODE BEGIN PV */
/* 采集任务 -> 显示任务 的数据�?�道�?
 * 长度只有 1, 配合 xQueueOverwrite 使用: 永远只保�?"�?新的�?次采�?"�?
 * 这正是传感器类应用的理想模型 —�?? 旧数据没人关�?, 队列也不会因�?
 * 显示任务偶尔慢一拍�?�堆积�?? */
static QueueHandle_t dhtQueue = NULL;

static TaskHandle_t  dht11TaskHandle = NULL;
static TaskHandle_t  lcdTaskHandle   = NULL;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_FSMC_Init(void);
/* USER CODE BEGIN PFP */
static void Dht11Task( void *argument );    /* 采集: �? 2s 读一�? DHT11 */
static void LcdTask( void *argument );      /* 显示: 收到数据就刷新屏�? */
static void UI_DrawStatic( void );          /* 画标题栏和固定文�? */
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
  * @brief  画只画一次的东西: 标题�? + 固定标签
  */
static void UI_DrawStatic( void )
{
    LCD_Clear( LCD_COLOR_BLACK );

    /* 标题�? */
    LCD_Fill( 0, 0, LCD_W - 1, 33, LCD_COLOR_BLUE );
    LCD_ShowString( 20, 5, "DHT11 MONITOR", LCD_COLOR_WHITE, LCD_COLOR_BLUE );

    /* 固定标签 */
    LCD_ShowString( UI_LABEL_X, UI_TEMP_LBL_Y, "Temperature", LCD_COLOR_WHITE, LCD_COLOR_BLACK );
    LCD_ShowString( UI_LABEL_X, UI_HUMI_LBL_Y, "Humidity",    LCD_COLOR_WHITE, LCD_COLOR_BLACK );

    /* 占位, 避免还没收到数据时屏幕是空的 */
    LCD_ShowString( UI_VALUE_X, UI_TEMP_VAL_Y, "--.- C", LCD_COLOR_DARK, LCD_COLOR_BLACK );
    LCD_ShowString( UI_VALUE_X, UI_HUMI_VAL_Y, "--.- %", LCD_COLOR_DARK, LCD_COLOR_BLACK );
    LCD_ShowString( UI_LABEL_X, UI_STAT_Y,     "Status: WAIT", LCD_COLOR_YELLOW, LCD_COLOR_BLACK );
}

/**
  * @brief  采集任务
  * @note   DHT11_Init() 内部用了 vTaskDelay(), �?以必须在调度器启动之�?
  *         (也就是任务体�?) 调用, 不能放在 main() 里�??
  */
static void Dht11Task( void *argument )
{
    DHT11_Data_t data;

    ( void ) argument;

    DHT11_Init();           /* �? DWT、拉高�?�线、等 1s 越过上电不稳定期 */

    for( ;; )
    {
        if( DHT11_Read( &data ) != 0U )
        {
            /* 读成�?: 顺手翻转 LED0(PB5)。没有仿真器�?, 看灯 2s 闪一�?
             * 就知道传感器还活�?。不�?要的话删掉这�?行即可�?? */
            HAL_GPIO_TogglePin( LED0_GPIO_Port, LED0_Pin );
        }
        else
        {
            /* 读失�?: 也发�?�?, 让屏幕显示错误状态�?�不是停在上�?个�?�上 */
            data.humi_int = 0U;
            data.humi_dec = 0U;
            data.temp_int = 0U;
            data.temp_dec = 0U;
            data.valid    = 0U;
        }

        /* 写进队列。因为队列长度为 1 且用 Overwrite, 这里永远不会阻塞�? */
        xQueueOverwrite( dhtQueue, &data );

        vTaskDelay( pdMS_TO_TICKS( DHT11_PERIOD_MS ) );
    }
}

/**
  * @brief  显示任务: 阻塞等队�?, 收到就刷�?
  */
static void LcdTask( void *argument )
{
    DHT11_Data_t data;
    char buf[ 24 ];
    uint32_t okCnt  = 0U;
    uint32_t errCnt = 0U;

    ( void ) argument;

    LCD_Init();             /* FSMC 必须已经�? MX_FSMC_Init() 里初始化�? */
    UI_DrawStatic();

    for( ;; )
    {
        /* 没有数据就一直阻塞在这里, 不占 CPU */
        if( xQueueReceive( dhtQueue, &data, portMAX_DELAY ) != pdPASS )
        {
            continue;
        }

        if( data.valid != 0U )
        {
            okCnt++;

            snprintf( buf, sizeof( buf ), "%2d.%d C  ", data.temp_int, data.temp_dec );
            LCD_ShowString( UI_VALUE_X, UI_TEMP_VAL_Y, buf, LCD_COLOR_YELLOW, LCD_COLOR_BLACK );

            snprintf( buf, sizeof( buf ), "%2d.%d %%  ", data.humi_int, data.humi_dec );
            LCD_ShowString( UI_VALUE_X, UI_HUMI_VAL_Y, buf, LCD_COLOR_CYAN, LCD_COLOR_BLACK );

            LCD_ShowString( UI_LABEL_X, UI_STAT_Y, "Status: OK     ", LCD_COLOR_GREEN, LCD_COLOR_BLACK );
        }
        else
        {
            errCnt++;

            LCD_ShowString( UI_VALUE_X, UI_TEMP_VAL_Y, "--.- C  ", LCD_COLOR_RED, LCD_COLOR_BLACK );
            LCD_ShowString( UI_VALUE_X, UI_HUMI_VAL_Y, "--.- %  ", LCD_COLOR_RED, LCD_COLOR_BLACK );
            LCD_ShowString( UI_LABEL_X, UI_STAT_Y,     "Status: NO RESP", LCD_COLOR_RED, LCD_COLOR_BLACK );
        }

        snprintf( buf, sizeof( buf ), "OK:%lu  ERR:%lu", okCnt, errCnt );
        LCD_ShowString( UI_LABEL_X, UI_CNT_Y, buf, LCD_COLOR_WHITE, LCD_COLOR_BLACK );
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
  /* 队列要在两个任务启动之前建好 */
  dhtQueue = xQueueCreate( 1, sizeof( DHT11_Data_t ) );
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
  /* 注意: �? CubeMX 里使�? FSMC 并重新生成工程后, CubeMX 会自动在
   * MX_GPIO_Init() 后面插入 MX_FSMC_Init() 的声明和调用, 不用手写�?
   * �? FSMC 初始化之前调�? LCD_Init() 是无效的�? */

  if( dhtQueue != NULL )
  {
      xTaskCreate( Dht11Task, "dht11", TASK_STACK_DEPTH, NULL, DHT11_TASK_PRIO, &dht11TaskHandle );
      xTaskCreate( LcdTask,   "lcd",   TASK_STACK_DEPTH, NULL, LCD_TASK_PRIO,   &lcdTaskHandle );

      vTaskStartScheduler();   /* 启动调度�?, 正常情况下不会返�? */
  }

  /* 走到这里说明堆内存不足或调度器启动失�? */
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
  /* 这几个按键中断现在没有回调处理了, 但优先级仍然改掉:
   * 数�?? 0 �?"不受 FreeRTOS 管理"的最高级中断, 以后若在里面调用
   * 任何 xxxFromISR API 会直接触�? configASSERT 卡死。改成就算以�?
   * 接回来也是安全的�? */
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
