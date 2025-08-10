// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Joshua Krusell

#include "common.h"

static int dbus_slot_gc(void *, size_t);
const JanetAbstractType dbus_slot_type = { .name = "sdbus/slot",
                                           .gc   = dbus_slot_gc,
                                           JANET_ATEND_GC };

static int dbus_slot_gc(void *data, size_t size) {
  UNUSED(size);

  sd_bus_slot_unref(*(sd_bus_slot **) data);

  return 0;
}

JANET_FN(cfun_cancel, "(sdbus/cancel call)",
         "Cancel a pending asynchronous D-Bus call. Returns `nil`.") {
  janet_fixarity(argc, 1);

  sd_bus_slot **slot_ptr = janet_getabstract(argv, 0, &dbus_slot_type);
  sd_bus_slot_unref(*slot_ptr);
  *slot_ptr = NULL;

  return janet_wrap_nil();
}

JanetRegExt cfuns_slot[] = { JANET_REG("cancel", cfun_cancel), JANET_REG_END };
