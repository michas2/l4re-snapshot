/*
 * Copyright (C) 2015 Kernkonzept GmbH.
 * Author(s): Sarah Hoffmann <sarah.hoffmann@kernkonzept.com>
 *
 * This file is distributed under the terms of the GNU General Public
 * License, version 2.  Please see the COPYING-GPL-2 file for details.
 */

#include "generic_guest.h"

namespace Vmm {

Generic_guest::Generic_guest(L4::Cap<L4Re::Dataspace> ram, l4_addr_t vm_base)
: _registry(&_bm),
  _ram(ram, vm_base),
  _task(L4Re::chkcap(L4Re::Util::cap_alloc.alloc<L4::Task>())),
  _ram_pf_handler(_ram.ram(), _ram.local_start())
{
  // attach RAM to VM
  _memmap[Region::ss(_ram.vm_start(), _ram.size())] = &_ram_pf_handler;
}

Cpu
Generic_guest::create_cpu()
{
  auto *e = L4Re::Env::env();
  l4_addr_t vcpu_addr = 0x10000000;

  L4Re::chksys(e->rm()->reserve_area(&vcpu_addr, L4_PAGESIZE,
                                     L4Re::Rm::Search_addr));
  L4Re::chksys(e->task()->add_ku_mem(l4_fpage(vcpu_addr, L4_PAGESHIFT,
                                              L4_FPAGE_RWX)),
               "kumem alloc");

  Cpu vcpu = Cpu((l4_vcpu_state_t *)vcpu_addr);
  vcpu.thread_attach();
  vcpu->user_task = _task.get().cap();

  return vcpu;
}

void
Generic_guest::load_device_tree_at(char const *name, l4_addr_t base,
                                 l4_size_t padding)
{
  _device_tree = _ram.load_file(name, base);

  auto dt = device_tree();
  dt.check_tree();
  dt.add_to_size(padding);

  // fill in memory node in the device tree
  int mem_nd = dt.path_offset("/memory");
  dt.setprop_u32(mem_nd, "reg", _ram.vm_start());
  dt.appendprop_u32(mem_nd, "reg", _ram.size());
}

l4_size_t
Generic_guest::load_ramdisk_at(char const *ram_disk, l4_addr_t offset)
{
  Dbg info(Dbg::Info);
  info.printf("load ramdisk image %s\n", ram_disk);

  l4_size_t size;
  auto initrd = _ram.load_file(ram_disk, offset, &size);

  if (has_device_tree())
    {
      auto dt = device_tree();
      int node = dt.path_offset("/chosen");
      dt.setprop_u32(node, "linux,initrd-start", initrd.get());
      dt.setprop_u32(node, "linux,initrd-end", initrd.get() + size);
    }

  return size;
}

} // namespace
