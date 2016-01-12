/*
 * Copyright (C) 2014-2015 Kernkonzept GmbH.
 * Author(s): Steffen Liebergeld <steffen.liebergeld@kernkonzept.com>
 *
 * This file is distributed under the terms of the GNU General Public
 * License, version 2.  Please see the COPYING-GPL-2 file for details.
 */
#include "vm.h"

#include <l4/util/util.h>
#include <l4/re/env>
#include <l4/re/util/cap_alloc>
#include <l4/sys/factory>
#include <l4/sys/thread>
#include <l4/vcpu/vcpu.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

extern char guestcode[]; // as implemented in guest.S

static unsigned long idt[32 * 2] __attribute__((aligned(4096)));
static unsigned long gdt[32 * 2] __attribute__((aligned(4096)));
static char guest_stack[STACK_SIZE] __attribute__((aligned(4096)));
static char handler_stack[STACK_SIZE];

l4_uint64_t pde __attribute__((aligned(4096)));
l4_uint64_t pdpte __attribute__((aligned(4096)));
l4_uint64_t pml4e __attribute__((aligned(4096)));

void
Vm::setup_vm()
{
  l4_addr_t ext_state;
  l4_msgtag_t msg;
  int ret;

  printf("Checking if CPU is capable of virtualization: ");
  if (!cpu_virt_capable())
    {
      printf("CPU does not support virtualization. Bye.\n");
      exit(1);
    }
  printf("It is.\n");

  printf("Allocating a VM capability: ");
  vm_cap = L4Re::Util::cap_alloc.alloc<L4::Vm>();
  if (!vm_cap.is_valid())
    {
      printf("Failure.\n");
      exit(1);
    }
  printf("Success.\n");

  printf("Creating a VM kernel object: ");
  msg = L4Re::Env::env()->factory()->create_vm(vm_cap);
  if (l4_error(msg))
    {
      printf("Failure.\n");
      exit(1);
    }
  printf("Success.\n");

  printf("Trying to allocate vCPU extended state: ");
  ret = l4vcpu_ext_alloc(&vcpu, &ext_state, L4_BASE_TASK_CAP,
                         L4Re::Env::env()->rm().cap());
  if (ret)
    {
      printf("Could not find vCPU extended state mem: %d\n", ret);
      exit(1);
    }
  printf("Success.\n");

  vmcb = (void*)ext_state;

  vcpu->state = L4_VCPU_F_FPU_ENABLED;
  vcpu->saved_state = L4_VCPU_F_USER_MODE
                      | L4_VCPU_F_FPU_ENABLED
                      | L4_VCPU_F_IRQ;

  vcpu->entry_ip = (l4_umword_t)&handler;
  vcpu->entry_sp = (l4_umword_t)(handler_stack + STACK_SIZE);
  vcpu->user_task = vm_cap.cap();


  printf("Trying to switch vCPU to extended operation: ");
  msg = l4_thread_vcpu_control_ext(L4_INVALID_CAP, (l4_addr_t)vcpu);
  ret = l4_error(msg);
  if (ret)
    {
      printf("Could not enable ext vCPU: %d\n", ret);
      exit(1);
    }
  printf("Success.\n");

  printf("Clearing guest stack.\n");
  memset(guest_stack, 0, sizeof(guest_stack));
  printf("Clearing handler stack.\n");
  memset(handler_stack, 0, sizeof(handler_stack));
  printf("done.\n");

  l4_touch_ro((void*)guestcode, 2);
  printf("Mapping code from %p to %p: ", (void*)guestcode, (void*)Code);
  msg = vm_cap->map(L4Re::Env::env()->task(),
                    l4_fpage((l4_umword_t)guestcode & L4_PAGEMASK, L4_PAGESHIFT,
                             L4_FPAGE_RWX),
                    l4_map_control((l4_umword_t)Code, 0, L4_MAP_ITEM_MAP));
  if ((ret = l4_error(msg)))
    {
      printf("failure: %d\n", ret);
      exit(1);
    }
  else
    printf("success\n");

  l4_touch_ro(&is_vmx, 2);
  printf("Mapping flags from %p to %p: ", &is_vmx, (void*)Flags);
  msg = vm_cap->map(L4Re::Env::env()->task(),
                    l4_fpage((l4_umword_t)&is_vmx & L4_PAGEMASK, L4_PAGESHIFT,
                             L4_FPAGE_RX),
                    l4_map_control((l4_umword_t)Flags, 0, L4_MAP_ITEM_MAP));
  if ((ret = l4_error(msg)))
    {
      printf("failure: %d\n", ret);
      exit(1);
    }
  else
    printf("success\n");

  printf("Mapping stack: \n");
  for (l4_umword_t ofs = 0; ofs < STACK_SIZE; ofs += L4_PAGESIZE)
    {
      printf("%p -> %p\n", (void*)((l4_umword_t)guest_stack + ofs), (void*)((l4_umword_t)Stack +  ofs));
      msg = vm_cap->map(L4Re::Env::env()->task(),
                        l4_fpage(((l4_umword_t)(guest_stack) + ofs) & L4_PAGEMASK,
                                 L4_PAGESHIFT, L4_FPAGE_RWX),
                        l4_map_control(Stack +  ofs, 0,
                                       L4_MAP_ITEM_MAP));
    }
  if ((ret = l4_error(msg)))
    {
      printf("failure: %d\n", ret);
      exit(1);
    }
  else
    printf("success\n");

  idt[26] = 0x80000; // #13 general protection fault
  idt[27] = 0x8e00;

  idt[28] = 0x80000; // #14 page fault
  idt[29] = 0x8e00;

  // code segment 0x08
  gdt[2] = 0xffff;
  gdt[3] = 0xcf9b00;

  // stack segment 0x10
  gdt[4] = 0xffff;
  gdt[5] = 0xcf9300;

  // data segment 0x20
  gdt[8] = 0xffff;
  gdt[9] = 0xcff300;

  // tss 0x28
  gdt[10] = 0x67;
  gdt[11] = 0x8b00;

  printf("Mapping idt from %p to %p: ", (void*)idt, (void*)Idt);
  msg = vm_cap->map(L4Re::Env::env()->task(),
                    l4_fpage((l4_addr_t)idt & L4_PAGEMASK, L4_PAGESHIFT,
                             L4_FPAGE_RW),
                    l4_map_control((l4_addr_t)Idt, 0, L4_MAP_ITEM_MAP));
  if ((ret = l4_error(msg)))
    {
      printf("failure: %d\n", ret);
      exit(1);
    }
  else
    printf("success\n");

  printf("Mapping gdt from %p to %p: ", (void*)gdt, (void*)Gdt);
  msg = vm_cap->map(L4Re::Env::env()->task(),
                    l4_fpage((l4_addr_t)gdt & L4_PAGEMASK, L4_PAGESHIFT,
                             L4_FPAGE_RW),
                    l4_map_control((l4_addr_t)Gdt, 0, L4_MAP_ITEM_MAP));
  if ((ret = l4_error(msg)))
    {
      printf("failure: %d\n", ret);
      exit(1);
    }
  else
    printf("success\n");
}

void
Vm::vm_resume()
{
  int ret;
  l4_msgtag_t msg;

  msg = l4_thread_vcpu_resume_commit(L4_INVALID_CAP,
                                     l4_thread_vcpu_resume_start());

  ret = l4_error(msg);
  if (ret)
    {
      printf("vm_resume failed: %s (%d)\n", l4sys_errtostr(ret), ret);
      test_ok = false;
      return;
    }

  if (handle_vmexit())
    vm_resume();
}

void
Vm::run_tests()
{
  if (npt_available())
    {
      run_test(1, 0);
      run_test(1, 1);
    }
  run_test(0, 0);
}

void
Vm::run_test(unsigned npt, unsigned long_mode)
{
  l4_uint64_t eflags;

  test_ok = true;

  if (long_mode & !npt)
    {
      printf("Long mode requires npt. ERROR.\n");
      test_ok = false;
      return;
    }

  printf("Starting test run with npt %s in %s mode.\n",
         npt ? "enabled" : "disabled",
         long_mode ? "ia32e" : "ia32");

  vcpu->r.dx = 1;
  vcpu->r.cx = 2;
  vcpu->r.bx = 3;
  vcpu->r.bp = 4;
  vcpu->r.si = 5;
  vcpu->r.di = 6;
  set_rax(0);
  if (npt)
    enable_npt();
  else
    disable_npt();

  initialize_vmcb();

  asm volatile("pushf     \n"
               "pop %0   \n"
               : "=r" (eflags));

  // clear interrupt
  eflags = (eflags & 0xfffffdff);
  eflags &= ~(0x1 << 17);

  set_rsp((l4_umword_t)Stack + STACK_SIZE);
  set_rflags(eflags);
  set_rip((l4_umword_t)Code);
  set_cr0(long_mode ? 0x8001003b : 0x1003b);
  set_cr3(long_mode ? make_ia32e_cr3() : 0);
  set_cr4(long_mode ? 0x6b0 : 0x690);
  set_dr7(0x300);
  set_efer(long_mode ? 0x1500: 0x1000);

  printf("Starting VM with EIP = %p\n", (void*)Code);
  vm_resume();
  printf("Test finished %s.\n", test_ok ? "successfully" : "with errors");
  return;
}

void
Vm::handler()
{
  printf("Received an IRQ. Considering this as ERROR.\n");
  exit(1);
}

/**
 * Create a very primitive PAE pagetable that establishes an identity mapping
 * of the first 2MB of guest address space.
 *
 * \returns The cr3 to use when running the guest in ia32e mode.
 */
l4_umword_t
Vm::make_ia32e_cr3()
{
  l4_umword_t cr3;
  l4_msgtag_t tag;
  int ret;

  cr3 = (unsigned long)&pml4e;

  // prepare pml4e
  pml4e = (unsigned long)&pdpte
          | 0x2 /*write allowed*/
          | 0x1 /*present*/;
  // prepare pdpte
  pdpte = (unsigned long)&pde
          | 0x2 /*write enabled*/
          | 0x1 /*present*/;
  // prepare pte
  pde = 0x0
          | (1 << 7) /*2mb page*/
          | 0x2 /*write enabled*/
          | 0x1 /*present*/;

  if ((l4_umword_t)&pml4e < 0x80000000)
    printf("Page tables reside below 4G.\n");

  // map pml4e
  printf("Mapping pml4e from %p to %p ", &pml4e, &pml4e);
  tag = vm_cap->map(L4Re::Env::env()->task(),
                    l4_fpage((l4_umword_t)&pml4e,
                             L4_PAGESHIFT, L4_FPAGE_RW),
                        l4_map_control((l4_umword_t)&pml4e, 0,
                                       L4_MAP_ITEM_MAP));
  if ((ret = l4_error(tag)))
    {
      printf("error %d\n", ret);
      return ~0;
    }
  else
    printf("success\n");

  // map pdpte
  printf("Mapping pdpte from %p to %p ", &pdpte, &pdpte);
  tag = vm_cap->map(L4Re::Env::env()->task(),
                    l4_fpage((l4_umword_t)&pdpte,
                             L4_PAGESHIFT, L4_FPAGE_RW),
                        l4_map_control((l4_umword_t)&pdpte, 0,
                                       L4_MAP_ITEM_MAP));

  if ((ret = l4_error(tag)))
    {
      printf("error %d\n", ret);
      return ~0;
    }
  else
    printf("success\n");

  // map pde
  printf("Mapping pde from %p to %p ", &pde, &pde);
  tag = vm_cap->map(L4Re::Env::env()->task(),
                    l4_fpage((l4_umword_t)&pde,
                             L4_PAGESHIFT, L4_FPAGE_RW),
                        l4_map_control((l4_umword_t)&pde, 0,
                                       L4_MAP_ITEM_MAP));

  if ((ret = l4_error(tag)))
    {
      printf("error %d.\n", ret);
      return ~0;
    }
  else
    printf("success\n");

  return cr3;
}

