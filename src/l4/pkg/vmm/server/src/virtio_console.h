/*
 * (c) 2013-2014 Alexander Warg <warg@os.inf.tu-dresden.de>
 *     economic rights: Technische Universität Dresden (Germany)
 *
 * This file is part of TUD:OS and distributed under the terms of the
 * GNU General Public License 2.
 * Please see the COPYING-GPL-2 file for details.
 */
#pragma once

#include "virtio_dev.h"
#include "irq_emitter.h"

#include <l4/cxx/ipc_server>
#include <l4/cxx/ipc_stream>

#include <l4/sys/vcon>

#include <l4/re/error_helper>


class Virtio_console :
  public Virtio::Dev,
  private L4::Server_object_t<L4::Vcon>
{
  typedef L4virtio::Svr::Virtqueue::Desc Desc;
  typedef L4virtio::Svr::Request_processor Request_processor;

  struct Payload {
      char *data;
      unsigned len;
      bool writable;
  };

public:
  struct Features : Virtio::Dev::Features
  {
    CXX_BITFIELD_MEMBER(0, 0, console_size, raw);
    CXX_BITFIELD_MEMBER(1, 1, console_multiport, raw);

    explicit Features(l4_uint32_t v) : Virtio::Dev::Features(v) {}
  };

  Virtio_console(Vmm::Vm_ram *iommu, L4::Cap<L4::Vcon> con,
                 Vmm::Irq_emitter const &irq)
  : Virtio::Dev(iommu, 0x44, 3), _con(con), _irq(irq)
  {
    _q[0].config.num_max = 0x100;
    _q[1].config.num_max = 0x100;

    l4_vcon_attr_t attr;
    L4Re::chksys(con->get_attr(&attr), "console get_attr");
    attr.l_flags &= ~L4_VCON_ECHO;
    attr.o_flags &= ~L4_VCON_ONLRET;
    L4Re::chksys(con->set_attr(&attr), "console set_attr");
  }

  void reset()
  {
    _q[0].disable();
    _q[1].disable();
  }

  virtual void kick()
  {
    handle_input();
    auto *q = &_q[1];

    auto r = q->next_avail();
    if (r)
      {
        Request_processor rp;
        Payload p;
        rp.start(this, r, &p);
        _con->write(p.data, p.len);
        q->consumed(r);
        _irq_status |= 1;
        _irq.inject(vmm_current_cpu_id);
      }
  }

  l4_uint32_t host_feature(unsigned id)
  {
    switch (id)
      {
      case 1:
        {
          Features feat(0);
          feat.ring_indirect_desc() = true;
          return feat.raw;
        }
      default:
        return 0;
      }
  }

  Virtio::Virtqueue *queue(unsigned idx)
  {
    if (idx < 2)
      return &_q[idx];
    return 0;
  }

  void load_desc(Desc const &desc, Request_processor const *, Payload *p)
  {
    // XXX boundary check?
    p->data = (char *)_iommu->access(desc.addr);
    p->len = desc.len;
    p->writable = desc.flags.write();
  }

  void load_desc(Desc const &desc, Request_processor const *,
                 Desc const **table)
  {
    // XXX boundary check?
    *table = static_cast<Desc const *>(_iommu->access(desc.addr));
  }


  void handle_input()
  {
    auto *q = &_q[0];

    l4_uint32_t irqs = 0;

    while (1)
      {
        int r = _con->read(NULL, 0);

        if (r <= 0)
          break; // empty

        auto req = q->next_avail();

        if (!req)
          break;

        Request_processor rp;
        Payload p;
        rp.start(this, req, &p);

        if (!p.writable)
          {
            Err().printf("Virtio_console: error read-only buffer in input queue\n");
            break;
          }

        r = _con->read(p.data, p.len);
        if (r < 0)
          {
            Err().printf("Virtio_console: read error: %d\n", r);
            break;
          }

        if ((unsigned)r <= p.len)
          {
            q->consumed(req, r);
            irqs = true;
            break;
          }

        q->consumed(req, p.len);
        irqs = true;
      }

    if (irqs)
      {
        _irq_status |= 1;
        _irq.inject(vmm_current_cpu_id);
      }
  }

  template<typename REG>
  void register_obj(REG *registry)
  {
    _con->bind(0, L4Re::chkcap(registry->register_irq_obj(this)));
  }

  int dispatch(l4_umword_t /*obj*/, L4::Ipc::Iostream &/*ios*/)
  {
    handle_input();
    return 0;
  }

private:
  Virtio::Virtqueue _q[2];
  L4::Cap<L4::Vcon> _con;
  Vmm::Irq_emitter _irq;
};
