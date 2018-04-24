#include "mmu.h"

# the truly system call handler
  # vectors.S sends all traps here.
.globl alltraps
alltraps:
  # Build trap frame.
  pushl %ds
  pushl %es
  pushl %fs
  pushl %gs
  pushal
  
  # Set up data segments.
  movw $(SEG_KDATA<<3), %ax
  movw %ax, %ds
  movw %ax, %es
  # movw $(SEG_KCPU<<3), %ax
  # movw %ax, %fs
  # movw %ax, %gs

  # Call trap(tf), where tf=%esp
  # the esp as an argument of trap(struct trapframe *tf), esp point to the above all frame
  pushl %esp
  call trap
  # why this need add 4, because pushl %esp before call trap
  addl $4, %esp

  # All first process return from there, also as forkret
  # Return falls through to trapret...
.globl trapret
trapret:
  popal
  popl %gs
  popl %fs
  popl %es
  popl %ds
  # why need add 8, because like vector32, pushl $0 as error code pushl $32 as vector, and jmp alltraps
  addl $0x8, %esp  # trapno and errcode
  iret
