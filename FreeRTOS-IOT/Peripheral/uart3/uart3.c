#include "uart3.h"
#include <string.h>
#include "FreeRTOS.h"
#include "queue.h"

extern UART_HandleTypeDef huart3;
extern QueueHandle_t Uart3_Message_Queue;

uint8_t Uart3_RxBuff[DMA_UART3_RX_SIZE];
uint8_t Uart3_TxBuff[DMA_UART3_TX_SIZE];
uint16_t Uart3_RxLength;
static uint8_t Uart3_FrameBuff[DMA_UART3_RX_SIZE];
static uint16_t Uart3_OldPos = 0;

static void uart3_start_rx_dma(void)
{
    if (HAL_UART_Receive_DMA(&huart3, Uart3_RxBuff, DMA_UART3_RX_SIZE) == HAL_OK)
    {
        if (huart3.hdmarx != NULL)
        {
            __HAL_DMA_DISABLE_IT(huart3.hdmarx, DMA_IT_HT);
        }
    }
}

/*
 * 配置UART3接收 (HAL DMA + IDLE)
 */
void uart3_dma_rx_configuration(void)
{
    __HAL_UART_ENABLE_IT(&huart3, UART_IT_IDLE);
    Uart3_RxLength = 0;
    Uart3_OldPos = 0;
    uart3_start_rx_dma();
}

/*
 * UART3 发送数据
 */
uint16_t Uart3_DMA_Send_Data(void * buffer, uint16_t size)
{
    RS485_TX_EN();
    HAL_UART_Transmit(&huart3, (uint8_t*)buffer, size, HAL_MAX_DELAY);
    RS485_RX_EN();
    return size;
}

void USART3_sendonechar(uint8_t data)
{
    RS485_TX_EN();
    HAL_UART_Transmit(&huart3, &data, 1, HAL_MAX_DELAY);
    RS485_RX_EN();
}

void USART3_sendstring(char *data, uint16_t length)
{
    RS485_TX_EN();
    HAL_UART_Transmit(&huart3, (uint8_t*)data, length, HAL_MAX_DELAY);
    RS485_RX_EN();
}

/* 
 * 自定义串口3空闲中断处理逻辑
 * 需要在 stm32f1xx_it.c 的 USART3_IRQHandler 中调用
 */
void Uart3_Idle_Callback(void)
{
    uint16_t pos;
    uint16_t len = 0;

    if(__HAL_UART_GET_FLAG(&huart3, UART_FLAG_IDLE) != RESET)
    {
        __HAL_UART_CLEAR_IDLEFLAG(&huart3);
        pos = (uint16_t)(DMA_UART3_RX_SIZE - __HAL_DMA_GET_COUNTER(huart3.hdmarx));

        if(pos != Uart3_OldPos)
        {
            memset(Uart3_FrameBuff, 0, DMA_UART3_RX_SIZE);
            if(pos > Uart3_OldPos)
            {
                len = pos - Uart3_OldPos;
                memcpy(Uart3_FrameBuff, &Uart3_RxBuff[Uart3_OldPos], len);
            }
            else
            {
                len = DMA_UART3_RX_SIZE - Uart3_OldPos;
                memcpy(Uart3_FrameBuff, &Uart3_RxBuff[Uart3_OldPos], len);
                if(pos > 0)
                {
                    memcpy(&Uart3_FrameBuff[len], Uart3_RxBuff, pos);
                    len += pos;
                }
            }
            Uart3_OldPos = pos;
        }

        Uart3_RxLength = len;
        
        if(Uart3_RxLength > 0 && Uart3_Message_Queue != NULL)
        {
            // 发送消息到队列
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            Uart3_Frame_t frame;
            frame.length = Uart3_RxLength;
            memset(frame.data, 0, DMA_UART3_RX_SIZE);
            memcpy(frame.data, Uart3_FrameBuff, Uart3_RxLength);
            xQueueSendFromISR(Uart3_Message_Queue, &frame, &xHigherPriorityTaskWoken);
            if(xHigherPriorityTaskWoken == pdTRUE)
            {
                portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
            }
        }
        
    }
}

uint16_t Uart3_GetFrame(uint8_t *buffer, uint16_t max_size, uint32_t timeout_ms)
{
    Uart3_Frame_t frame;
    uint16_t copy_len;

    if(buffer == NULL || max_size == 0 || Uart3_Message_Queue == NULL)
    {
        return 0;
    }

    if(xQueueReceive(Uart3_Message_Queue, &frame, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)
    {
        return 0;
    }

    copy_len = frame.length;
    if(copy_len > max_size)
    {
        copy_len = max_size;
    }

    memcpy(buffer, frame.data, copy_len);
    return copy_len;
}
