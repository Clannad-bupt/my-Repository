#include "sys.h"

/* 以下几个函数保留用于兼容，使用汇编实现 */
//执行WFI指令
__asm void WFI_SET(void)
{
	WFI;		  
}

//关闭所有中断
__asm void INTX_DISABLE(void)
{
	CPSID   I
	BX      LR	  
}

//开启所有中断
__asm void INTX_ENABLE(void)
{
	CPSIE   I
	BX      LR  
}

//设置栈顶地址
__asm void MSR_MSP(u32 addr) 
{
	MSR MSP, r0
	BX r14
}
