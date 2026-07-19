.globl _start
_start:
# Initialize stack pointer
la sp, stack_top
# Jump to C main function
call main
# Infinite loop after main returns
1:  j 1b
.section .bss
.space 4096  # Reserve 4KB for stack
stack_top:
