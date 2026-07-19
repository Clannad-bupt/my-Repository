#ifndef __FINKA_H__
#define __FINKA_H__

#include "timer.h"
#include "prescaler.h"
#include "interrupt.h"
#include "gpio.h"
#include "uart.h"

// manually keep this in sync with SpinalHDL Finka.scala source

#define APB_BASE 0x00F00000

#define GPIO_A          ((Gpio_Reg *)          (APB_BASE + 0x00000))
#define UART            ((Uart_Reg *)          (APB_BASE + 0x10000))
#define TIMER_PRESCALER ((Prescaler_Reg *)     (APB_BASE + 0x20000))
#define TIMER_INTERRUPT ((InterruptCtrl_Reg*)  (APB_BASE + 0x20010))
#define TIMER_A         ((Timer_Reg *)         (APB_BASE + 0x20040))
#define TIMER_B         ((Timer_Reg *)         (APB_BASE + 0x20050))

#define AXI_M_BASE 0x00C00000

#define AXI_M1          ((volatile uint32_t *) (AXI_M_BASE + 0x00000))

#endif /* __FINKA_H__ */
