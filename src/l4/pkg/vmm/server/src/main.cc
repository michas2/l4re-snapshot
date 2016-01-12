/*
 * (c) 2013-2014 Alexander Warg <warg@os.inf.tu-dresden.de>
 *     economic rights: Technische Universität Dresden (Germany)
 *
 * This file is part of TUD:OS and distributed under the terms of the
 * GNU General Public License 2.
 * Please see the COPYING-GPL-2 file for details.
 */
/*
 * Copyright (C) 2015 Kernkonzept GmbH.
 * Author(s): Sarah Hoffmann <sarah.hoffmann@kernkonzept.com>
 *
 * This file is distributed under the terms of the GNU General Public
 * License, version 2.  Please see the COPYING-GPL-2 file for details.
 */
#include "debug.h"
#include "device_tree.h"
#include "guest.h"

#include <cstdio>
#include <cerrno>
#include <cstring>
#include <iostream>

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>

#include <l4/re/env>
#include <l4/re/error_helper>
#include <l4/re/util/cap_alloc>
#include <l4/re/debug>

#include <l4/sys/thread>
#include <l4/sys/task>

#include <l4/cxx/utils>
#include <l4/cxx/ipc_stream>
#include <l4/cxx/ipc_server>

__thread unsigned vmm_current_cpu_id;


static void config_virtio_device(Vmm::Guest *vmm, Vmm::Device_tree dt, int node)
{
  Dbg info;
  Dbg warn(Dbg::Warn);

  int type_len;
  auto *type = dt.get_prop<char const>(node, "l4vmm,virtiotype", &type_len);
  if (!type)
    {
      warn.printf("'l4vmm,virtiotype' property missing from virtio device, disable it\n");
      dt.setprop_string(node, "status", "disabled");
      return;
    }

  auto *prop = dt.check_prop<fdt32_t const>(node, "reg", 2);
  uint32_t base = fdt32_to_cpu(prop[0]);
  uint32_t size = fdt32_to_cpu(prop[1]);

  auto irq = vmm->create_irq_from_dt(node);
  info.printf("VIRTIO: @ %x %x type=%.*s\n", base, size, type_len, type);

  if (fdt_stringlist_contains(type, type_len, "console"))
    vmm->create_virtio_cons(irq, base, size);
  else if (fdt_stringlist_contains(type, type_len, "net"))
    vmm->create_virtio_proxy(irq, base, size,
                             L4Re::Env::env()->get_cap<L4virtio::Device>("net"));
  else
    {
      Err().printf("invalid virtio device type: '%.*s', disable device\n", type_len, type);
      dt.setprop_string(node, "status", "disabled");
    }
}

static char const *const options = "+k:d:r:c:";
static struct option const loptions[] =
  {
    { "kernel",   1, NULL, 'k' },
    { "dtb",      1, NULL, 'd' },
    { "ramdisk",  1, NULL, 'r' },
    { "cmdline",  1, NULL, 'c' },
    { 0, 0, 0, 0}
  };

static int run(int argc, char *argv[])
{
  L4Re::Env const *e = L4Re::Env::env();
  Dbg info;
  Dbg warn(Dbg::Warn);

  Dbg::set_level(0xffff);

  info.printf("Hello out there.\n");

  char const *cmd_line     = nullptr;
  char const *kernel_image = "rom/zImage";
  char const *device_tree  = nullptr;
  char const *ram_disk     = nullptr;

  int opt;
  while ((opt = getopt_long(argc, argv, options, loptions, NULL)) != -1)
    {
      switch (opt)
        {
        case 'c': cmd_line     = optarg; break;
        case 'k': kernel_image = optarg; break;
        case 'd': device_tree  = optarg; break;
        case 'r': ram_disk     = optarg; break;
        default:
          Err().printf("unknown command-line option\n");
          return 1;
        }
    }

  // get RAM data space and attach it to our (VMMs) address space
  auto ram = L4Re::chkcap(e->get_cap<L4Re::Dataspace>("ram"),
                          "ram dataspace cap");
  // create VM BUS connection to IO for GICC pass through and device pass through
  auto vbus = L4Re::chkcap(e->get_cap<L4vbus::Vbus>("vm_bus"),
                           "Error getting vm_bus capability",
                           -L4_ENOENT);
  auto vmm = Vmm::Guest::create_instance(ram, vbus);
  auto vcpu = vmm->create_cpu();

  if (device_tree)
    {
      // load device tree
      vmm->load_device_tree(device_tree);

      // create all virtio devices for the VM
      auto dt = vmm->device_tree();
      for (int node = -1;;)
        {
          node = dt.by_compatible("virtio,mmio", node);
          if (node < 0)
            break;

          config_virtio_device(vmm, dt, node);
        }
    }

  vmm->load_linux_kernel(kernel_image, cmd_line, vcpu);

  if (ram_disk)
    vmm->load_ramdisk(ram_disk);

  vmm->run(vcpu);

  Err().printf("ERROR: we must never reach this....\n");
  return 0;
}

int main(int argc, char *argv[])
{
  try
    {
      return run(argc, argv);
    }
  catch (L4::Runtime_error &e)
    {
      Err().printf("FATAL: %s %s\n", e.str(), e.extra_str());
    }
  return 1;
}


