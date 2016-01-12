/*
 * Copyright (C) 2015 Kernkonzept GmbH.
 * Author(s): Sarah Hoffmann <sarah.hoffmann@kernkonzept.com>
 *
 * This file is distributed under the terms of the GNU General Public
 * License, version 2.  Please see the COPYING-GPL-2 file for details.
 */
#pragma once

#include <l4/re/error_helper>

#include "debug.h"

extern "C" {
#include <libfdt.h>
}

namespace Vmm {

class Device_tree
{
public:
  explicit Device_tree(void *dt) : _tree(dt) {}

  void check_tree()
  {
    if (fdt_check_header(_tree) < 0)
      throw L4::Runtime_error(-L4_EINVAL, "Not a device tree");
  }

  void add_to_size(l4_size_t padding) const
  { fdt_set_totalsize(_tree, fdt_totalsize(_tree) + padding); }

  int path_offset(char const *path) const
  {
    int ret = fdt_path_offset(_tree, path);
    if (ret < 0)
      {
        Err().printf("cannot find node '%s'\n", path);
        L4Re::chksys(-L4_EEXIST);
      }

    return ret;
  }

  void setprop_u32(int node, char const* name, l4_uint32_t value) const
  {
    if (fdt_setprop_u32(_tree, node, name, value) < 0)
      {
        Err().printf("cannot set property '%s' to '0x%x'\n", name, value);
        L4Re::chksys(-L4_EIO);
      }
  }

  void setprop_string(int node, char const* name, char const *value) const
  {
    if (fdt_setprop_string(_tree, node, name, value) < 0)
      {
        Err().printf("cannot set property '%s' to '%s'\n", name, value);
        L4Re::chksys(-L4_EIO);
      }
  }

  void appendprop_u32(int node, char const* name, l4_uint32_t value) const
  {
    if (fdt_appendprop_u32(_tree, node, name, value) < 0)
      {
        Err().printf("cannot append '0x%x' to property '%s'\n", value, name);
        L4Re::chksys(-L4_EIO);
      }
  }

  int by_compatible(const char *compatible, int startoffset = -1) const
  { return fdt_node_offset_by_compatible(_tree, startoffset, compatible); }

  template <typename T>
  T *get_prop(int node, char const *name, int *size)
  { return reinterpret_cast<T *>(fdt_getprop(_tree, node, name, size)); }

  template <typename T>
  T *check_prop(int node, char const *name, int size)
  {
    int len;
    void const *prop = fdt_getprop(_tree, node, name, &len);
    if (!prop)
      {
        char buf[256];
        if (fdt_get_path(_tree, node, buf, sizeof(buf)) < 0)
          buf[0] = 0;
        Err().printf("could net get '%s' property of %s: %d\n", name, buf, len);
        L4Re::chksys(-L4_EINVAL);
      }

    if (len < (int) sizeof(T) * size)
      {
        char buf[256];
        if (fdt_get_path(_tree, node, buf, sizeof(buf)) < 0)
          buf[0] = 0;
        Err().printf("'%s' property of %s is too small (%d need %u)\n",
                     name, buf, len, (unsigned) (sizeof(T) * size));
        L4Re::chksys(-L4_ERANGE);
      }

    return reinterpret_cast<T *>(prop);
  }

private:
  void *_tree;
};

}
