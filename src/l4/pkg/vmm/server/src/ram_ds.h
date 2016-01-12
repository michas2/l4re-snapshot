/*
 * Copyright (C) 2015 Kernkonzept GmbH.
 * Author(s): Sarah Hoffmann <sarah.hoffmann@kernkonzept.com>
 *
 * This file is distributed under the terms of the GNU General Public
 * License, version 2.  Please see the COPYING-GPL-2 file for details.
 */
#pragma once

#include <l4/re/dataspace>
#include <l4/re/dma_space>
#include <l4/re/util/cap_alloc>

#include <l4/l4virtio/virtqueue>

#include "vm_ram.h"

namespace Vmm {

class Ram_ds : public Vm_ram
{
public:
  explicit Ram_ds(L4::Cap<L4Re::Dataspace> ram, l4_addr_t vm_base = ~0UL);

  L4virtio::Ptr<void>
  load_file(char const *name, l4_addr_t offset, l4_size_t *_size = 0);

  L4::Cap<L4Re::Dataspace> ram() const noexcept
  { return _ram; }

private:
  L4::Cap<L4Re::Dataspace> _ram;
  L4Re::Util::Auto_cap<L4Re::Dma_space>::Cap _dma;
};

} // namespace
