/*
 * Copyright (C) 2015 Kernkonzept GmbH.
 * Author(s): Sarah Hoffmann <sarah.hoffmann@kernkonzept.com>
 *
 * This file is distributed under the terms of the GNU General Public
 * License, version 2.  Please see the COPYING-GPL-2 file for details.
 */
#pragma once

#include <l4/cxx/static_container>
#include <l4/vbus/vbus>

#include "generic_guest.h"
#include "gic.h"
#include "virt_bus.h"
#include "virtio_proxy.h"
#include "vcpu.h"

namespace Vmm {

/**
 * ARM virtual machine monitor.
 */
class Guest : public Generic_guest
{
public:
  Guest(L4::Cap<L4Re::Dataspace> ram,
        L4::Cap<L4vbus::Vbus> vbus,
        l4_addr_t vm_base = ~0UL);

  void create_virtio_cons(Irq_emitter const &irq,
                          l4_umword_t base, l4_umword_t size);

  void create_virtio_proxy(Irq_emitter const &irq,
                           l4_umword_t base, l4_umword_t size,
                           L4::Cap<L4virtio::Device> svr);

  void load_device_tree(char const *name)
  {
    load_device_tree_at(name, 0x100, 0x200);
    config_gic_from_dt();
  }

  Irq_emitter create_irq_from_dt(int node);

  void load_linux_kernel(char const *kernel, char const *cmd_line, Cpu vcpu);

  void load_ramdisk(char const *ram_disk)
  { load_ramdisk_at(ram_disk, 0x2000000); }

  void run(Cpu vcpu);

  l4_msgtag_t handle_entry(Cpu vcpu);

  static Guest *
  create_instance(L4::Cap<L4Re::Dataspace> ram, L4::Cap<L4vbus::Vbus> vbus);

private:
  void config_gic_from_dt();

  cxx::Static_container<Gic::Dist> _gic;
  Virt_bus _vbus;
};

} // namespace
