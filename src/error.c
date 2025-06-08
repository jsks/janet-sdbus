// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Joshua Krusell

#include "common.h"

// -------------------------------------------------------------------
// D-Bus error abstract type
// -------------------------------------------------------------------

int dbus_error_gc(void *, size_t);
const JanetAbstractType dbus_error_type = { .name = "sdbus/error",
                                            .gc   = dbus_error_gc,
                                            JANET_ATEND_GC };

int dbus_error_gc(void *data, size_t size) {
  UNUSED(size);

  sd_bus_error_free((sd_bus_error *) data);

  return 0;
}
