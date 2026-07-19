/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
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
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "lcd_ui.h"
#include "wifi.h"
#include "onenet_mqtt.h"
#include "seniverse_http.h"
#include "net_time.h"
#include "uart1.h"
#include "uart2.h"
#include "uart3.h"
#include "dht12.h"
#include "../../system/usart/usart.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
// Uart1 - UsbDebug
#define Uart1_MESSAGE_Q_NUM   4
QueueHandle_t Uart1_Message_Queue;

// Uart2 - Wifi
#define Wifi_MESSAGE_Q_NUM   4
QueueHandle_t Wifi_Message_Queue;

// Uart3 - RS485
#define Uart3_MESSAGE_Q_NUM   4
QueueHandle_t Uart3_Message_Queue;

// Task Handles
TaskHandle_t WIFITask_Handler;
TaskHandle_t UITask_Handler;
TaskHandle_t CLOCKIFSTask_Handler;
TaskHandle_t STATSTask_Handler;

// Timer Handle
TimerHandle_t netClockCountTimer_Handle;
extern struct xDevice Device;


/* USER CODE END Variables */
osThreadId defaultTaskHandle;

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
void wifi_task(void *pvParameters);
void uiRefresh_task(void *pvParameters);
void clockIFS_task(void *pvParameters);
void stats_task(void *pvParameters);
void netClockCountCallback(TimerHandle_t xTimer);


/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void const * argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* GetIdleTaskMemory prototype (linked to static allocation support) */
void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize );

/* USER CODE BEGIN GET_IDLE_TASK_MEMORY */
static StaticTask_t xIdleTaskTCBBuffer;
static StackType_t xIdleStack[configMINIMAL_STACK_SIZE];

void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize )
{
  *ppxIdleTaskTCBBuffer = &xIdleTaskTCBBuffer;
  *ppxIdleTaskStackBuffer = &xIdleStack[0];
  *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
  /* place for user code */
}
/* USER CODE END GET_IDLE_TASK_MEMORY */

/* GetTimerTaskMemory prototype (linked to static allocation support) */
void vApplicationGetTimerTaskMemory( StaticTask_t **ppxTimerTaskTCBBuffer, StackType_t **ppxTimerTaskStackBuffer, uint32_t *pulTimerTaskStackSize );

/* USER CODE BEGIN GET_TIMER_TASK_MEMORY */
static StaticTask_t xTimerTaskTCBBuffer;
static StackType_t xTimerStack[configTIMER_TASK_STACK_DEPTH];

void vApplicationGetTimerTaskMemory( StaticTask_t **ppxTimerTaskTCBBuffer, StackType_t **ppxTimerTaskStackBuffer, uint32_t *pulTimerTaskStackSize )
{
  *ppxTimerTaskTCBBuffer = &xTimerTaskTCBBuffer;
  *ppxTimerTaskStackBuffer = &xTimerStack[0];
  *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
  /* place for user code */
}
/* USER CODE END GET_TIMER_TASK_MEMORY */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  netClockCountTimer_Handle = xTimerCreate("netClockCountTimer", pdMS_TO_TICKS(1000), pdTRUE, (void*)1, netClockCountCallback);
  if(netClockCountTimer_Handle != NULL) xTimerStart(netClockCountTimer_Handle, 0);

  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  Uart1_Message_Queue = xQueueCreate(Uart1_MESSAGE_Q_NUM, DMA_UART1_RX_SIZE);
  Wifi_Message_Queue = xQueueCreate(Wifi_MESSAGE_Q_NUM, 1);
  Uart3_Message_Queue = xQueueCreate(Uart3_MESSAGE_Q_NUM, sizeof(Uart3_Frame_t));

  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* definition and creation of defaultTask */
  osThreadDef(defaultTask, StartDefaultTask, osPriorityNormal, 0, 128);
  defaultTaskHandle = osThreadCreate(osThread(defaultTask), NULL);

  /* USER CODE BEGIN RTOS_THREADS */
  xTaskCreate((TaskFunction_t)wifi_task, "wifi_task", 512, NULL, 2, &WIFITask_Handler);
  xTaskCreate((TaskFunction_t)uiRefresh_task, "ui_task", 256, NULL, 3, &UITask_Handler);
  xTaskCreate((TaskFunction_t)clockIFS_task, "clockIFS_task", 256, NULL, 3, &CLOCKIFSTask_Handler);
  xTaskCreate((TaskFunction_t)stats_task, "stats_task", 256, NULL, 1, &STATSTask_Handler);

  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void const * argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
void wifi_task(void *pvParameters)
{
    uint16_t wnum = 0;
    uint16_t postNum = 0;
    uint8_t netFailCount = 0;
    DHT12_Data_t dht12Data;
    debug_usart_send_string("wifi_task started\r\n");
    while(1)
    {
        if (WifiMsg.Mode != 1)
        {
            if(!WiFi_Init())
            {
                printf("ESP8266 init done\r\n");
                WifiMsg.Mode = 1;
                OneNET_MQTT_Reset();
            }
            else
            {
                printf("ESP8266 init fail, retry in 3s\r\n");
                vTaskDelay(pdMS_TO_TICKS(3000));
            }
        }
        else
        {
            (void)OneNET_MQTT_Tick();
            if(Uart2_HasRxOverflow())
            {
                netFailCount++;
                if(netFailCount >= 3)
                {
                    WifiMsg.Mode = 0;
                    WifiMsg.ConnectFlag = 0;
                    OneNET_MQTT_Reset();
                    netFailCount = 0;
                }
            }
            else if(OneNET_MQTT_GetStatus() == ONENET_MQTT_CONNECTED)
            {
                netFailCount = 0;
                WifiMsg.Status = 0;
            }
            vTaskDelay(pdMS_TO_TICKS(200));
            if(postNum == 0 || postNum >= 30)
            {
                postNum = 0;
                if(DHT12_ReadData(&dht12Data) == 0 &&
                   OneNET_MQTT_GetStatus() == ONENET_MQTT_CONNECTED)
                {
                    if(OneNET_MQTT_PublishTempHumi(dht12Data.temperature_x10, dht12Data.humidity_x10) != 0U)
                    {
                        u1_printf("MQTT: temp/humi publish failed\r\n");
                    }
                }
            }
            if((wnum == 0) || (wnum > 200))
            {
                wnum = 0;
                OneNET_MQTT_PauseForHttp();
                Seniverse_GetWeather();
                OneNET_MQTT_ResumeAfterHttp();
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            wnum++;
            postNum++;
        }
    }
}

void uiRefresh_task(void *pvParameters)
{
    while(1)
    {
        lcd_ui_ShowAllWeather(&Device.Weather);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void clockIFS_task(void *pvParameters)
{
    while(1)
    {
        lcd_ui_ShowTime(&Device.Time);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void stats_task(void *pvParameters)
{
    (void)pvParameters;

    while(1)
    {
        u1_printf("STAT heap=%lu wifi_stack=%lu ui_stack=%lu clk_stack=%lu wifi_irq=%lu rx_overflow=%u\r\n",
                  (unsigned long)xPortGetFreeHeapSize(),
                  (unsigned long)uxTaskGetStackHighWaterMark(WIFITask_Handler),
                  (unsigned long)uxTaskGetStackHighWaterMark(UITask_Handler),
                  (unsigned long)uxTaskGetStackHighWaterMark(CLOCKIFSTask_Handler),
                  (unsigned long)Uart2_GetRxIrqCount(),
                  (unsigned)Uart2_HasRxOverflow());
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}
void netClockCountCallback(TimerHandle_t xTimer)
{
    TimePlusOneSecond(&Device.Time);
}


/* USER CODE END Application */






