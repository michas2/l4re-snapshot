/*
 * Copyright (C) 2015 Kernkonzept GmbH.
 * Author(s): Sarah Hoffmann <sarah.hoffmann@kernkonzept.com>
 *
 * This file is distributed under the terms of the GNU General Public
 * License, version 2.  Please see the COPYING-GPL-2 file for details.
 */
#pragma once

#include "gic.h"

namespace Vmm {

class Irq_emitter
{
public:
  Irq_emitter(Gic::Dist *gic, unsigned irq)
  : _irq(irq), _gic(gic)
  {}

  void inject(unsigned current_cpu) const
  { _gic->inject_spi(_irq - 32, current_cpu); }

private:
  unsigned _irq;
  Gic::Dist *_gic;
};

} // namespace
