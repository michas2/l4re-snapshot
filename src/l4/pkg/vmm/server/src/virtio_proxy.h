/*
 * (c) 2013-2014 Alexander Warg <warg@os.inf.tu-dresden.de>
 *     economic rights: Technische Universität Dresden (Germany)
 *
 * This file is part of TUD:OS and distributed under the terms of the
 * GNU General Public License 2.
 * Please see the COPYING-GPL-2 file for details.
 */
#pragma once

#include <l4/sys/capability>
#include <l4/sys/meta>

#include <l4/re/dataspace>
#include <l4/re/error_helper>
#include <l4/re/util/cap_alloc>

#include <l4/re/env>
#include <l4/re/rm>

#include <l4/cxx/ipc_stream>
#include <l4/cxx/ipc_server>

#include <l4/l4virtio/l4virtio>
#include <l4/l4virtio/virtqueue>
#include <l4/l4virtio/virtio_block.h>

#include <cstring>

#include "irq_emitter.h"
#include "vm_ram.h"

namespace L4virtio { namespace Driver {

/**
 * \brief Client-side implementation for a general virtio device.
 */
class Device
{
public:
  /**
   * Contacts the device and sets up the config page.
   *
   * \param srvcap    Capability for device communication.
   * \param guest_irq Irq capability to send to device.
   *
   * \throws L4::Runtime_error if the initialisation fails
   *
   * This function contacts the server, sets up the notification
   * channels and the configuration dataspace. After this is done,
   * the caller can set up any dataspaces it needs.
   */
  void driver_connect(L4::Cap<L4virtio::Device> srvcap,
                      L4::Cap<L4::Irq> guest_irq)
  {
    _device = srvcap;

    _host_irq = L4Re::chkcap(L4Re::Util::cap_alloc.alloc<L4::Irq>(),
                             "Cannot allocate host irq");

    _config_cap = L4Re::chkcap(L4Re::Util::cap_alloc.alloc<L4Re::Dataspace>(),
                               "Cannot allocate cap for config dataspace");

    auto *e = L4Re::Env::env();
    L4Re::chksys(e->rm()->attach(&_config, L4_PAGESIZE, L4Re::Rm::Search_addr,
                                 L4::Ipc::make_cap_rw(_config_cap.get()), 0,
                                 L4_PAGESHIFT),
                 "Cannot attach config dataspace");

    L4Re::chksys(_device->register_iface(guest_irq, _host_irq.get(),
                                         _config_cap.get()),
                 "Error registering interface with device");

    if (memcmp(&_config->magic, "virt", 4) != 0)
      L4Re::chksys(-L4_ENODEV, "Device config has wrong magic value");

    if (_config->version != 1)
      L4Re::chksys(-L4_ENODEV, "Invalid virtio version, must be 1");
  }


  /**
   * Share a dataspace with the device.
   *
   * \param ds      Dataspace to share with the device.
   * \param offset  Offset in dataspace where the shared part starts.
   * \param size    Total size in bytes of the shared space.
   * \param devaddr Start of shared space in the device address space.
   *
   * Although this function allows to share only a part of the given dataspace
   * for convenience, the granularity of sharing is always the dataspace level.
   * Thus, the remainder of the dataspace is not protected from the device.
   */
  int register_ds(L4::Cap<L4Re::Dataspace> ds, l4_umword_t offset,
                  l4_umword_t size, l4_uint64_t devaddr)
  {
    return _device->register_ds(L4::Ipc::make_cap_rw(ds), devaddr, offset, size);
  }

  int config_queue(int num)
  {
    return _device->config_queue(num);
  }

  L4virtio::Device::Config_hdr *device_config() const
  { return _config.get(); }

  unsigned selected_queue() const
  { return _queue_sel; }

  l4_uint32_t page_size() const
  { return _config->guest_page_size; }

  L4virtio::Device::Config_queue *queue_config(int num) const
  {
    return &_config->queues()[num];
  }

  l4_uint32_t read(unsigned reg)
  {
    switch (reg >> 2)
      {
      case 0: return *reinterpret_cast<l4_uint32_t const *>("virt");
      case 1: return 1;
      case 2: return _config->device;
      case 3: return _config->vendor;
      case 4: return (_host_feat_sel < 8) ?
                       _config->host_features[_host_feat_sel] : 0;
      case 13: return (_queue_sel < _config->num_queues) ?
                       _config->queues()[_queue_sel].num_max : 0;
      case 16: return (_queue_sel < _config->num_queues) ?
                       _config->queues()[_queue_sel].pfn : 0;
      case 24: return 1; // currently unused: _config->irq_status;
      case 28: return _config->status;
      default: return 0;
      }
  }

  void write(unsigned reg, l4_uint32_t value)
  {
    // XXX make sure we don't write outside config page because the
    // driver side screwed up
    switch (reg >> 2)
      {
      case 20:
        _host_irq->trigger();
        break;
      case 5:
        _host_feat_sel = value;
        break;
      case 8:
        if (_guest_feat_sel < 8)
          _config->guest_features[_guest_feat_sel] = value;
        break;
      case 9:
        _guest_feat_sel = value;
        break;
      case 10:
        _config->guest_page_size = value;
        break;
      case 12:
        _queue_sel = value;
        break;
      case 14:
        if (_queue_sel < _config->num_queues)
           _config->queues()[_queue_sel].num = value;
        break;
      case 15:
        if (_queue_sel < _config->num_queues)
           _config->queues()[_queue_sel].align = value;
        break;
      case 16:
        if (_queue_sel < _config->num_queues)
          {
           _config->queues()[_queue_sel].pfn = value;
           _device->config_queue(_queue_sel);
          }
        break;
      case 28:
        _device->set_status(value);
        break;
      }
  }


protected:
  L4::Cap<L4virtio::Device> _device;
  L4Re::Rm::Auto_region<L4virtio::Device::Config_hdr *> _config;
  L4Re::Util::Auto_cap<L4::Irq>::Cap _guest_irq;

private:
  L4Re::Util::Auto_cap<L4::Irq>::Cap _host_irq;
  L4Re::Util::Auto_cap<L4Re::Dataspace>::Cap _config_cap;

  unsigned _queue_sel = 0;
  unsigned _host_feat_sel = 0;
  unsigned _guest_feat_sel = 0;

};

} } // namespace

namespace Virtio {

class Proxy_dev : public L4::Irqep_t<Proxy_dev>
{
private:
  // -- network specific
  L4virtio::Driver::Virtqueue _txq;
  Vmm::Vm_ram *_iommu;
  // -------------------
public:
  Proxy_dev(Vmm::Vm_ram *iommu, Vmm::Irq_emitter const &irq)
  : _iommu(iommu), _irq(irq) {}

  l4_uint32_t read(unsigned reg, char, unsigned)
  { return _dev.read(reg); }

  void write(unsigned reg, char, l4_uint32_t value, unsigned)
  {
    // -- network specific
    if (reg >> 2 == 20)
        _txq.no_notify_host(true);

    _dev.write(reg, value);

    if (reg >> 2 == 16 && _dev.selected_queue() == 1)
      {
        auto *q = _dev.queue_config(1);
        _txq.setup(q->num, q->align,
                   _iommu->access(L4virtio::Ptr<void>(value * _dev.page_size())));
      }
  }

  template<typename REG>
  void register_obj(REG *registry, L4::Cap<L4virtio::Device> host,
                    L4::Cap<L4Re::Dataspace> ram, l4_addr_t ram_base)
  {
    L4::Cap<L4::Irq> guest_irq = L4Re::chkcap(registry->register_irq_obj(this));

    _dev.driver_connect(host, guest_irq);
    L4Re::chksys(_dev.register_ds(ram, 0, ram->size(), ram_base));
  }

  void handle_irq()
  {  _irq.inject(vmm_current_cpu_id); }

private:
  Vmm::Irq_emitter _irq;

  L4virtio::Driver::Device _dev;
};


}
