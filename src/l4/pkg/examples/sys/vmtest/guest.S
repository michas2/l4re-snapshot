/*
 * Copyright (C) 2014-2015 Kernkonzept GmbH.
 * Author(s): Steffen Liebergeld <steffen.liebergeld@kernkonzept.com>
 *
 * This file is distributed under the terms of the GNU General Public
 * License, version 2.  Please see the COPYING-GPL-2 file for details.
 */
.section .bss
.p2align 12
.globl is_vmx
is_vmx:
.long 0x0

/**
 * Guest code.
 *
 * Guest memory layout is defined in memory_layout in vm.h
 * Make sure the jumps are short (relative) jumps.
 */
.section .text
.code32
.globl guestcode
.type guestcode, @function
.p2align 12
guestcode:
  pushl %eax // test the stack
  popl %eax
  nop
  nop
  nop
  movl %eax, %ecx
  inc %ecx
  nop
  nop
  nop
  cmpl $0, (0x2000) // is_vmx
  jne _vmx
  vmmcall
  jmp _next
_vmx:
  vmcall
_next:
  nop
  nop
  nop
  int3
  nop
  nop
  nop
  jmp guestcode
  ret

