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
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "config_manager.h"
#include "error_manager.h"
#include "state_machine.h"
#include "power_manager.h"
#include "sensor_manager.h"
#include "safety_monitor.h"
#include "can_manager.h"
#include "coolant_pump.h"
#include "watchdog.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
typedef StaticTask_t osStaticThreadDef_t;
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

CAN_HandleTypeDef hcan1;

CRC_HandleTypeDef hcrc;

/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for LED_Blink */
osThreadId_t LED_BlinkHandle;
uint32_t LED_BlinkBuffer[ 128 ];
osStaticThreadDef_t LED_BlinkControlBlock;
const osThreadAttr_t LED_Blink_attributes = {
  .name = "LED_Blink",
  .cb_mem = &LED_BlinkControlBlock,
  .cb_size = sizeof(LED_BlinkControlBlock),
  .stack_mem = &LED_BlinkBuffer[0],
  .stack_size = sizeof(LED_BlinkBuffer),
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for can_messages */
osThreadId_t can_messagesHandle;
uint32_t can_messagesBuffer[ 128 ];
osStaticThreadDef_t can_messagesControlBlock;
const osThreadAttr_t can_messages_attributes = {
  .name = "can_messages",
  .cb_mem = &can_messagesControlBlock,
  .cb_size = sizeof(can_messagesControlBlock),
  .stack_mem = &can_messagesBuffer[0],
  .stack_size = sizeof(can_messagesBuffer),
  .priority = (osPriority_t) osPriorityNormal,
};
/* USER CODE BEGIN PV */
/* IWDG handle is owned by watchdog.c (started inside Watchdog_Init). */

/* New task handles. Created in USER CODE BEGIN RTOS_THREADS. */
static osThreadId_t s_can_task_handle;
static osThreadId_t s_power_task_handle;
static osThreadId_t s_sensor_task_handle;
static osThreadId_t s_safety_task_handle;
static osThreadId_t s_watchdog_task_handle;

static const osThreadAttr_t s_can_task_attr = {
    .name = "CAN_Mgr", .stack_size = 1024, .priority = (osPriority_t)osPriorityHigh,
};
static const osThreadAttr_t s_power_task_attr = {
    .name = "PowerMgr", .stack_size = 512, .priority = (osPriority_t)osPriorityAboveNormal,
};
static const osThreadAttr_t s_sensor_task_attr = {
    .name = "SensorMgr", .stack_size = 512, .priority = (osPriority_t)osPriorityNormal,
};
static const osThreadAttr_t s_safety_task_attr = {
    .name = "SafetyMon", .stack_size = 384, .priority = (osPriority_t)osPriorityHigh,
};
static const osThreadAttr_t s_watchdog_task_attr = {
    .name = "Watchdog", .stack_size = 256, .priority = (osPriority_t)osPriorityRealtime,
};

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_CAN1_Init(void);
static void MX_CRC_Init(void);
static void MX_ADC1_Init(void);
void StartDefaultTask(void *argument);
void LED_BlinkTask(void *argument);
void can_messages_task(void *argument);

/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  /* Coming from the bootloader: disable interrupts and quiesce SysTick
   * before HAL_Init reconfigures the core. Mirrors BMS-Firmware-RTOS. */
  __disable_irq();

  SysTick->CTRL = 0;
  SysTick->LOAD = 0;
  SysTick->VAL  = 0;

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
  MX_CAN1_Init();
  MX_CRC_Init();
  MX_ADC1_Init();
  /* USER CODE BEGIN 2 */
  /* Pre-RTOS calibration. IWDG is started later from Watchdog_Init so the
   * full LSI-tolerance window is available *after* all init and once the
   * RTOS is up — mirrors the BMS firmware order. */
  HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED);

  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();

  /* USER CODE BEGIN RTOS_MUTEX */
  /* Initialize firmware modules. Order matters:
   *  config -> errors -> state machine -> power -> sensors -> safety -> CAN -> watchdog */
  if (Config_Init()        != HAL_OK) Error_Handler();
  if (ErrorMgr_Init()      != HAL_OK) Error_Handler();
  if (StateMachine_Init()  != HAL_OK) Error_Handler();
  if (PowerMgr_Init()      != HAL_OK) Error_Handler();
  if (Sensor_Init()        != HAL_OK) Error_Handler();
  if (SafetyMonitor_Init() != HAL_OK) Error_Handler();
  if (CoolantPump_Init()   != HAL_OK) Error_Handler();
  if (CAN_Manager_Init()   != HAL_OK) Error_Handler();
  /* Promote out of INIT now that everything is constructed. Safety monitor
   * task will move STANDBY -> ACTIVE once safety inputs are clean. */
  StateMachine_SetState(MOBO_STATE_STANDBY);
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* creation of LED_Blink */
  LED_BlinkHandle = osThreadNew(LED_BlinkTask, NULL, &LED_Blink_attributes);

  /* creation of can_messages */
  can_messagesHandle = osThreadNew(can_messages_task, NULL, &can_messages_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  s_can_task_handle      = osThreadNew(CAN_ManagerTask,    NULL, &s_can_task_attr);
  s_power_task_handle    = osThreadNew(PowerMgrTask,       NULL, &s_power_task_attr);
  s_sensor_task_handle   = osThreadNew(SensorTask,         NULL, &s_sensor_task_attr);
  s_safety_task_handle   = osThreadNew(SafetyMonitorTask,  NULL, &s_safety_task_attr);
  /* WatchdogTask intentionally NOT created — watchdog disabled. */
  (void)s_watchdog_task_handle;
  (void)s_watchdog_task_attr;
  if (s_can_task_handle == NULL || s_power_task_handle == NULL ||
      s_sensor_task_handle == NULL || s_safety_task_handle == NULL) {
    Error_Handler();
  }
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

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

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 2;
  RCC_OscInitStruct.PLL.PLLN = 20;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
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
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV1;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc1.Init.OversamplingMode = DISABLE;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_5;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_2CYCLES_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief CAN1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_CAN1_Init(void)
{

  /* USER CODE BEGIN CAN1_Init 0 */

  /* USER CODE END CAN1_Init 0 */

  /* USER CODE BEGIN CAN1_Init 1 */

  /* USER CODE END CAN1_Init 1 */
  hcan1.Instance = CAN1;
  hcan1.Init.Prescaler = 10;
  hcan1.Init.Mode = CAN_MODE_NORMAL;
  hcan1.Init.SyncJumpWidth = CAN_SJW_1TQ;
  hcan1.Init.TimeSeg1 = CAN_BS1_13TQ;
  hcan1.Init.TimeSeg2 = CAN_BS2_2TQ;
  hcan1.Init.TimeTriggeredMode = DISABLE;
  hcan1.Init.AutoBusOff = DISABLE;
  hcan1.Init.AutoWakeUp = DISABLE;
  hcan1.Init.AutoRetransmission = DISABLE;
  hcan1.Init.ReceiveFifoLocked = DISABLE;
  hcan1.Init.TransmitFifoPriority = DISABLE;
  if (HAL_CAN_Init(&hcan1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN CAN1_Init 2 */

  /* USER CODE END CAN1_Init 2 */

}

/**
  * @brief CRC Initialization Function
  * @param None
  * @retval None
  */
static void MX_CRC_Init(void)
{

  /* USER CODE BEGIN CRC_Init 0 */

  /* USER CODE END CRC_Init 0 */

  /* USER CODE BEGIN CRC_Init 1 */

  /* USER CODE END CRC_Init 1 */
  hcrc.Instance = CRC;
  hcrc.Init.DefaultPolynomialUse = DEFAULT_POLYNOMIAL_ENABLE;
  hcrc.Init.DefaultInitValueUse = DEFAULT_INIT_VALUE_ENABLE;
  hcrc.Init.InputDataInversionMode = CRC_INPUTDATA_INVERSION_NONE;
  hcrc.Init.OutputDataInversionMode = CRC_OUTPUTDATA_INVERSION_DISABLE;
  hcrc.InputDataFormat = CRC_INPUTDATA_FORMAT_BYTES;
  if (HAL_CRC_Init(&hcrc) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN CRC_Init 2 */

  /* USER CODE END CRC_Init 2 */

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
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, RAD_Ctrl_Pin|FANS_Ctrl_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, LD3_Pin|PUMP_Ctrl_Pin|DRS_Ctrl_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : SDC_3_Pin SDC_1_Pin BSPD_Pin IMD_Pin */
  GPIO_InitStruct.Pin = SDC_3_Pin|SDC_1_Pin|BSPD_Pin|IMD_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : RAD_Ctrl_Pin FANS_Ctrl_Pin */
  GPIO_InitStruct.Pin = RAD_Ctrl_Pin|FANS_Ctrl_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : SDC_2_Pin BMS_Pin */
  GPIO_InitStruct.Pin = SDC_2_Pin|BMS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : LD3_Pin */
  GPIO_InitStruct.Pin = LD3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD3_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : PUMP_Ctrl_Pin DRS_Ctrl_Pin */
  GPIO_InitStruct.Pin = PUMP_Ctrl_Pin|DRS_Ctrl_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
/* USER CODE END 4 */

/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN 5 */
  (void)argument;

  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END 5 */
}

/* USER CODE BEGIN Header_LED_BlinkTask */
/**
* @brief Function implementing the LED_Blink thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_LED_BlinkTask */
void LED_BlinkTask(void *argument)
{
  /* USER CODE BEGIN LED_BlinkTask */
  /* Infinite loop */
  for(;;)
  {


    HAL_GPIO_TogglePin(LD3_GPIO_Port, LD3_Pin);

    osDelay(500);


  }
  /* USER CODE END LED_BlinkTask */
}

/* USER CODE BEGIN Header_can_messages_task */
/**
* @brief Function implementing the can_messages thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_can_messages_task */
void can_messages_task(void *argument)
{
  /* USER CODE BEGIN can_messages_task */
  /* Legacy CubeMX-generated task entry retained for ABI but unused; the
   * actual CAN manager runs as `s_can_task_handle` started in USER CODE
   * BEGIN RTOS_THREADS. Park this thread harmlessly. */
  (void)argument;
  for(;;) {
    osDelay(60000);
  }
  /* USER CODE END can_messages_task */
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM6 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM6)
  {
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
  osKernelLock();
  HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, GPIO_PIN_RESET);
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
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
