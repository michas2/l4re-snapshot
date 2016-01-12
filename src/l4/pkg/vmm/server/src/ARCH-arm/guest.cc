/*
 * Copyright (C) 2015 Kernkonzept GmbH.
 * Author(s): Sarah Hoffmann <sarah.hoffmann@kernkonzept.com>
 *
 * This file is distributed under the terms of the GNU General Public
 * License, version 2.  Please see the COPYING-GPL-2 file for details.
 */

#include <l4/cxx/static_container>
#include <l4/re/env>
#include <l4/re/error_helper>
#include <l4/sys/debugger.h>
#include <l4/sys/kdebug.h>

#include "irq_emitter.h"
#include "guest.h"
#include "arm_mmio_device.h"
#include "virtio_console.h"
#include "virtio_proxy.h"

static cxx::Static_container<Vmm::Guest> guest;

extern "C" void vcpu_entry(l4_vcpu_state_t *vcpu);

asm
(
 "vcpu_entry:                     \n"
 "  mov    r6, sp                 \n"
 "  bic    sp, #7                 \n"
 "  sub    sp, sp, #16            \n"
 "  mrc    p15, 0, r5, c13, c0, 2 \n"
 "  stmia  sp, {r4, r5, r6, lr}   \n"
 "  bl     c_vcpu_entry           \n"
 "  movw   r2, #0xf803            \n"
 "  movt   r2, #0xffff            \n"
 "  mov    r3, #0                 \n"
 "  mov    r5, sp                 \n"
 "  ldmia  r5, {r4, r6, sp, lr}   \n"
 "  mcr    p15, 0, r6, c13, c0, 2 \n"
 "  mov    pc, #" L4_stringify(L4_SYSCALL_INVOKE) " \n"
);

extern "C" l4_msgtag_t c_vcpu_entry(l4_vcpu_state_t *vcpu_state);

l4_msgtag_t __attribute__((flatten))
c_vcpu_entry(l4_vcpu_state_t *vcpu)
{
  return guest->handle_entry(Vmm::Cpu(vcpu));
}

class Arm_virtio_console
: public Virtio_console,
  public Vmm::Mmio_device_t<Arm_virtio_console>,
  public Virtio::Mmio_connector<Arm_virtio_console>
{
public:
  Arm_virtio_console(Vmm::Vm_ram *iommu, L4::Cap<L4::Vcon> con,
                     Vmm::Irq_emitter const &irq)
  : Virtio_console(iommu, con, irq)
  {}
};

class Arm_virtio_proxy
: public Virtio::Proxy_dev,
  public Vmm::Mmio_device_t<Arm_virtio_proxy>
{
public:
  Arm_virtio_proxy(Vmm::Vm_ram *iommu, Vmm::Irq_emitter const &irq)
  : Virtio::Proxy_dev(iommu, irq)
  {}
};


namespace Vmm {

Guest::Guest(L4::Cap<L4Re::Dataspace> ram,
         L4::Cap<L4vbus::Vbus> vbus,
         l4_addr_t vm_base)
: Generic_guest(ram, vm_base),
  _vbus(vbus)
{
  if (_ram.vm_start() & ~0xf0000000)
    Dbg(Dbg::Info).printf(
        "WARNING: Guest memory not 256MB aligned!\n"
        "         If you run Linux as a guest, you might hit a bug\n"
        "         in the arch/arm/boot/compressed/head.S code\n"
        "         that misses an ISB after code has been relocated.\n"
        "         According to the internet a fix for this issue\n"
        "         is floating around.\n");

  auto *e = L4Re::Env::env();
  // create the VM task
  L4Re::chksys(e->factory()->create_task(_task.get(), l4_fpage_invalid()),
               "allocate vm");
  l4_debugger_set_object_name(_task.get().cap(), "vm-task");
}

Guest *
Guest::create_instance(L4::Cap<L4Re::Dataspace> ram, L4::Cap<L4vbus::Vbus> vbus)
{
  guest.construct(ram, vbus);
  return guest;
}

void
Guest::config_gic_from_dt()
{
  Dbg info;
  Device_tree dt = device_tree();

  if (!has_device_tree())
    {
      Err().printf("No device tree configured. Needed for GIC configuration.\n");
      L4Re::chksys(-L4_EIO);
    }

  int node = dt.by_compatible("arm,cortex-a9-gic");
  if (node < 0)
    {
      node = dt.by_compatible("arm,cortex-a15-gic");
      if (node < 0)
        {
          Err().printf("could not find GIC in device tree: %d\n", node);
          L4Re::chksys(-L4_EIO);
        }
    }

  auto *prop = dt.check_prop<fdt32_t const>(node, "reg", 4);

  uint32_t base = fdt32_to_cpu(prop[0]);
  uint32_t size = fdt32_to_cpu(prop[1]);

  info.printf("GICD: @ %x %x\n", base, size);

  // 4 * 32 spis, 2 cpus
  _gic.construct(4, 2);

  // attach GICD to VM
  _memmap[Region::ss(base, size)] = _gic.get();

  base = fdt32_to_cpu(prop[2]);
  size = fdt32_to_cpu(prop[3]);

  info.printf("GICC: @ %x %x\n", base, size);

  _vbus.map_mmio(_memmap, "GICC", "arm-gicc", base);

  // pass all HW IRQs allowed by the vm_bus to the VM
  // TODO properly cleanup irq servers
  Irq_svr *irqs = new Irq_svr;
  unsigned nr_irqs = _vbus.num_irqs();
  for (unsigned i = 0; i < nr_irqs; ++i)
    {
      if (_vbus.map_irq(&_registry, _gic.get(), irqs, i, i + 32))
        irqs = new Irq_svr;
    }

}

void
Guest::create_virtio_cons(Irq_emitter const &irq, l4_umword_t base,
                          l4_umword_t size)
{
  Dbg().printf("Create virtual console\n");

  auto *cons = new Arm_virtio_console(&_ram, L4Re::Env::env()->log(), irq);
  cons->register_obj(&_registry);

  _memmap[Region::ss(base, size)] = cons;
}

void
Guest::create_virtio_proxy(Irq_emitter const &irq,
                           l4_umword_t base, l4_umword_t size,
                           L4::Cap<L4virtio::Device> svr)
{
  Dbg().printf("Create virtual net\n");

  auto *proxy = new Arm_virtio_proxy(&_ram, irq);
  proxy->register_obj(&_registry, svr, _ram.ram(), _ram.vm_start());

  _memmap[Region::ss(base, size)] = proxy;
}

Irq_emitter
Guest::create_irq_from_dt(int node)
{
  auto dt = device_tree();

  auto *prop = dt.check_prop<fdt32_t const>(node, "interrupts", 3);
  if (fdt32_to_cpu(prop[0]) != 0)
    {
      Err().printf("virtio devices must use SPIs\n");
      L4Re::chksys(-L4_EINVAL);
    }

  return Irq_emitter(_gic.get(), fdt32_to_cpu(prop[1]) + 32);
}

void
Guest::load_linux_kernel(char const *kernel, char const *cmd_line, Cpu vcpu)
{
  auto kernel_vm = _ram.load_file(kernel, 0x208000);

  if (cmd_line)
    {
      Dbg(Dbg::Info).printf("setting command line in device tree to: '%s'\n",
                            cmd_line);

      if (has_device_tree())
        {
          auto dt = device_tree();
          int node = dt.path_offset("/chosen");
          dt.setprop_string(node, "bootargs", cmd_line);
        }
    }

  // now set up the VCPU state as expected by Linux entry
  vcpu->r.flags = 0x00000013;
  vcpu->r.sp    = 0;
  vcpu->r.r[0]  = 0;
  vcpu->r.r[1]  = ~0UL;
  vcpu->r.r[2]  = _device_tree.get();
  vcpu->r.r[3]  = 0;
  vcpu->r.ip    = kernel_vm.get();
}

void
Guest::run(Cpu vcpu)
{
  vcpu->saved_state =  L4_VCPU_F_FPU_ENABLED
                         | L4_VCPU_F_USER_MODE
                         | L4_VCPU_F_IRQ
                         | L4_VCPU_F_PAGE_FAULTS
                         | L4_VCPU_F_EXCEPTIONS;
  vcpu->entry_ip = (l4_umword_t) &vcpu_entry;

  auto *vm = vcpu.state();

  vm->vm_regs.hcr &= ~(1 << 27);
  vm->vm_regs.hcr |= 1 << 13;
  _gic->set_cpu(0, &vm->gic);
  vmm_current_cpu_id = 0;

  L4::Cap<L4::Thread> myself;
  myself->vcpu_resume_commit(myself->vcpu_resume_start());
}


inline l4_msgtag_t
Guest::handle_entry(Cpu vcpu)
{
  auto *utcb = vcpu.saved_utcb();
  asm volatile("mcr p15, 0, %0, c13, c0, 2" : : "r"(utcb));
  auto hsr = vcpu.hsr();

  switch (hsr.ec())
    {
    case 0x20: // insn abt
      // fall through
    case 0x24: // data abt
        {
          l4_addr_t pfa = vcpu->r.pfa;

          if (handle_mmio(pfa, vcpu))
            break;

          long res;
#if MAP_OTHER
          res = io_ds->map(pfa, L4Re::Dataspace::Map_rw, pfa,
                           l4_trunc_page(pfa), l4_round_page(pfa + 1),
                           vm_task);
          if (res < 0)
            {
              Err().printf("cannot handle VM memory access @ %lx ip=%lx\n",
                           pfa, vcpu->r.ip);
              enter_kdebug("STOP");
            }
#else

          l4_addr_t local_addr = 0;
          L4Re::Env const *e = L4Re::Env::env();
          res = e->rm()->reserve_area(&local_addr, L4_PAGESIZE,
                                      L4Re::Rm::Search_addr);
          if (res < 0)
            {
              Err().printf("cannot handle VM memory access @ %lx ip=%lx "
                           "(VM allocation failure)\n",
                           pfa, vcpu->r.ip);
              enter_kdebug("STOP");
              break;
            }

          res = _vbus.io_ds()->map(pfa, L4Re::Dataspace::Map_rw, local_addr,
                                   l4_trunc_page(local_addr),
                                   l4_round_page(local_addr + 1));
          if (res < 0)
            {
              Err().printf("cannot handle VM memory access @ %lx ip=%lx "
                           "(getting)\n",
                           pfa, vcpu->r.ip);
              enter_kdebug("STOP");
              break;
            }

          res = l4_error(_task->map(e->task(),
                                    l4_fpage(local_addr,
                                             L4_PAGESHIFT, L4_FPAGE_RW),
                                    l4_trunc_page(pfa)));
          if (res < 0)
            {
              Err().printf("cannot handle VM memory access @ %lx ip=%lx "
                           "(map to VM failure)\n",
                           pfa, vcpu->r.ip);
              enter_kdebug("STOP");
              break;
            }

#endif /* MAP_OTHER */
          break;
        }

    case 0x3d: // VIRTUAL PPI
      switch (hsr.svc_imm())
        {
        case 0: // VGIC IRQ
          _gic->handle_maintenance_irq(vmm_current_cpu_id);
          break;
        case 1: // VTMR IRQ
          _gic->inject_local(27, vmm_current_cpu_id);
          break;
        default:
          Err().printf("unknown virtual PPI: %d\n", (int)hsr.svc_imm());
          break;
        }
      break;

    case 0x3f: // IRQ
      handle_ipc(vcpu->i.tag, vcpu->i.label, utcb);
      break;

    case 0x01: // WFI, WFE
      if (hsr.wfe_trapped()) // WFE
        {
          // yield
        }
      else // WFI
        {
          if (_gic->schedule_irqs(vmm_current_cpu_id))
            {
              vcpu->r.ip += 2 << hsr.il();
              break;
            }

          l4_timeout_t to = L4_IPC_NEVER;
          auto *vm = vcpu.state();

          if ((vm->cntv_ctl & 3) == 1) // timer enabled and not masked
            {
              // calculate the timeout based on the VTIMER values !
              l4_uint64_t cnt, cmp;
              asm volatile ("mrrc p15, 1, %Q0, %R0, c14" : "=r"(cnt));
              asm volatile ("mrrc p15, 3, %Q0, %R0, c14" : "=r"(cmp));

              if (cmp <= cnt)
                {
                  vcpu->r.ip += 2 << hsr.il();
                  break;
                }

              l4_uint64_t diff = (cmp - cnt) / 24;
              if (0)
                printf("diff=%lld\n", diff);
              l4_rcv_timeout(l4_timeout_abs_u(l4_kip_clock(l4re_kip()) + diff, 8, utcb), &to);
            }

          l4_umword_t src;
          l4_msgtag_t tag = l4_ipc_wait(utcb, &src, to);
          if (!tag.has_error())
            handle_ipc(tag, src, utcb);

          // skip insn
          vcpu->r.ip += 2 << hsr.il();
        }
      break;

    case 0x05: // MCR/MRC CP 14

      if (   hsr.mcr_opc1() == 0
          && hsr.mcr_crn() == 0
          && hsr.mcr_crm() == 1
          && hsr.mcr_opc2() == 0
          && hsr.mcr_read()) // DCC Status
        {
          // printascii in Linux is doing busyuart which wants to see a
          // busy flag to quit its loop while waituart does not want to
          // see a busy flag; this little trick makes it work
          static l4_umword_t flip;
          flip ^= 1 << 29;
          vcpu->r.r[hsr.mcr_rt()] = flip;
        }
      else if (   hsr.mcr_opc1() == 0
               && hsr.mcr_crn() == 0
               && hsr.mcr_crm() == 5
               && hsr.mcr_opc2() == 0) // DCC Get/Put
        {
          if (hsr.mcr_read())
            vcpu->r.r[hsr.mcr_rt()] = 0;
          else
            putchar(vcpu->r.r[hsr.mcr_rt()]);
        }
      else
        printf("%08lx: %s p14, %d, r%d, c%d, c%d, %d (hsr=%08lx)\n",
               vcpu->r.ip, hsr.mcr_read() ? "MRC" : "MCR",
               (unsigned)hsr.mcr_opc1(),
               (unsigned)hsr.mcr_rt(),
               (unsigned)hsr.mcr_crn(),
               (unsigned)hsr.mcr_crm(),
               (unsigned)hsr.mcr_opc2(),
               (l4_umword_t)hsr.raw());

      vcpu->r.ip += 2 << hsr.il();
      break;

    default:
      Err().printf("unknown trap: err=%lx ec=0x%x ip=%lx\n",
                   vcpu->r.err, (int)hsr.ec(), vcpu->r.ip);
      if (0)
        enter_kdebug("Unknown trap");
      break;
    }
  L4::Cap<L4::Thread> myself;

#if 1
  while (vcpu->sticky_flags & L4_VCPU_SF_IRQ_PENDING)
    {
      l4_umword_t src;
      l4_msgtag_t res = l4_ipc_wait(utcb, &src, L4_IPC_BOTH_TIMEOUT_0);
      if (!res.has_error())
        handle_ipc(res, src, utcb);
    }
#endif

  _gic->schedule_irqs(vmm_current_cpu_id);

  return myself->vcpu_resume_start(utcb);
}

} // namespace
