#include "uart1.h"
#include <string.h>
#include "FreeRTOS.h"
#include "queue.h"

extern UART_HandleTypeDef huart1;
extern QueueHandle_t Uart1_Message_Queue;

uint8_t Uart1_RxBuff[DMA_UART1_RX_SIZE];
uint8_t Uart1_TxBuff[DMA_UART1_TX_SIZE];
uint16_t Uart1_RxLength;
static uint8_t Uart1_FrameBuff[DMA_UART1_RX_SIZE];
static uint16_t Uart1_OldPos = 0;

static void uart1_start_rx_dma(void)
{
    if (HAL_UART_Receive_DMA(&huart1, Uart1_RxBuff, DMA_UART1_RX_SIZE) == HAL_OK)
    {
        if (huart1.hdmarx != NULL)
        {
            __HAL_DMA_DISABLE_IT(huart1.hdmarx, DMA_IT_HT);
        }
    }
}

/*
 * 配置UART1接收 (HAL DMA + IDLE)
 */
void uart1_dma_rx_configuration(void)
{
    __HAL_UART_ENABLE_IT(&huart1, UART_IT_IDLE);
    Uart1_RxLength = 0;
    Uart1_OldPos = 0;
    uart1_start_rx_dma();
}

/*
 * UART1 发送数据
 */
uint16_t Uart1_DMA_Send_Data(void * buffer, uint16_t size)
{
    HAL_UART_Transmit(&huart1, (uint8_t*)buffer, size, 1000);
    return size;
}

void USART1_sendonechar(uint8_t data)
{
    HAL_UART_Transmit(&huart1, &data, 1, 1000);
}

void USART1_sendstring(char *data, uint16_t length)
{
    HAL_UART_Transmit(&huart1, (uint8_t*)data, length, 1000);
}

/* 
 * 自定义串口1空闲中断处理逻辑
 * 需要在 stm32f1xx_it.c 的 USART1_IRQHandler 中调用
 */
void Uart1_Idle_Callback(void)
{
    uint16_t pos;
    uint16_t len = 0;

    if(__HAL_UART_GET_FLAG(&huart1, UART_FLAG_IDLE) != RESET)
    {
        __HAL_UART_CLEAR_IDLEFLAG(&huart1);
        pos = (uint16_t)(DMA_UART1_RX_SIZE - __HAL_DMA_GET_COUNTER(huart1.hdmarx));

        if(pos != Uart1_OldPos)
        {
            memset(Uart1_FrameBuff, 0, DMA_UART1_RX_SIZE);
            if(pos > Uart1_OldPos)
            {
                len = pos - Uart1_OldPos;
                memcpy(Uart1_FrameBuff, &Uart1_RxBuff[Uart1_OldPos], len);
            }
            else
            {
                len = DMA_UART1_RX_SIZE - Uart1_OldPos;
                memcpy(Uart1_FrameBuff, &Uart1_RxBuff[Uart1_OldPos], len);
                if(pos > 0)
                {
                    memcpy(&Uart1_FrameBuff[len], Uart1_RxBuff, pos);
                    len += pos;
                }
            }
            Uart1_OldPos = pos;
        }

        Uart1_RxLength = len;
        
        if(Uart1_RxLength > 0 && Uart1_Message_Queue != NULL)
        {
            // 发送消息到队列
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            xQueueSendFromISR(Uart1_Message_Queue, Uart1_FrameBuff, &xHigherPriorityTaskWoken);
            if(xHigherPriorityTaskWoken == pdTRUE)
            {
                portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
            }
        }
        
    }
}
