/*
 * Copyright (C) 2015 Kernkonzept GmbH.
 * Author(s): Sarah Hoffmann <sarah.hoffmann@kernkonzept.com>
 *
 * This file is distributed under the terms of the GNU General Public
 * License, version 2.  Please see the COPYING-GPL-2 file for details.
 */
#pragma once

#include <l4/re/dataspace>
#include <l4/re/util/br_manager>
#include <l4/re/util/object_registry>
#include <l4/l4virtio/virtqueue>

#include "device_tree.h"
#include "ds_mmio_mapper.h"
#include "ram_ds.h"
#include "vm_memmap.h"
#include "vcpu.h"

namespace Vmm {

class Generic_guest
{
public:
    explicit Generic_guest(L4::Cap<L4Re::Dataspace> ram, l4_addr_t vm_base = ~0UL);

    virtual ~Generic_guest() = default;

    Cpu create_cpu();

    Device_tree device_tree() const
    { return Device_tree(_ram.access(_device_tree)); }

    bool has_device_tree() const
    { return _device_tree.is_valid(); }

protected:
    void load_device_tree_at(char const *src, l4_addr_t base, l4_size_t padding);
    l4_size_t load_ramdisk_at(char const *ram_disk, l4_addr_t offset);

    void handle_ipc(l4_msgtag_t tag, l4_umword_t label, l4_utcb_t *utcb)
    {
      l4_msgtag_t r = _registry.dispatch(tag, label, utcb);
      if (r.label() != -L4_ENOREPLY)
        l4_ipc_send(L4_INVALID_CAP | L4_SYSF_REPLY, utcb, r,
                    L4_IPC_SEND_TIMEOUT_0);
    }

    bool handle_mmio(l4_uint32_t pfa, Cpu vcpu)
    {
      Vm_mem::const_iterator f = _memmap.find(pfa);
      if (f == _memmap.end())
        return false;

      return f->second->access(pfa, pfa - f->first.start,
                               vcpu, _task.get(),
                               f->first.start, f->first.end);
    }

    L4Re::Util::Br_manager _bm;
    L4Re::Util::Object_registry _registry;
    Vm_mem _memmap;
    Ram_ds _ram;
    L4Re::Util::Auto_cap<L4::Task>::Cap _task;
    L4virtio::Ptr<void> _device_tree;

private:
    Ds_handler _ram_pf_handler;
};

} // namespace
