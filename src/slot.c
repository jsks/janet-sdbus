// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Joshua Krusell

#include "common.h"

static int dbus_slot_gc(void *, size_t);
static Janet dbus_slot_next(void *, Janet);
const JanetAbstractType dbus_slot_type = { .name = "sdbus/slot",
                                           .gc   = dbus_slot_gc,
                                           .next = dbus_slot_next };

JANET_CFUN(cfun_cancel_slot);
static JanetMethod dbus_slot_methods[] = {
  { "close", cfun_cancel_slot },
  { NULL,    NULL             }
};

static int dbus_slot_gc(void *p, size_t size) {
  UNUSED(size);

  sd_bus_slot **slot_ptr = p;
  sd_bus_slot_unrefp(slot_ptr);
  *slot_ptr = NULL;

  return 0;
}

static Janet dbus_slot_next(void *p, Janet key) {
  UNUSED(p);
  return janet_nextmethod(dbus_slot_methods, key);
}

JANET_FN(cfun_cancel_slot, "(sdbus/cancel call)",
         "Cancel and release a bus slot representing an open/pending resource "
         "--- for example, a pending asynchronous method call, signal "
         "subscription, or exported interface. Returns nil.") {
  janet_fixarity(argc, 1);

  sd_bus_slot **slot_ptr = janet_getabstract(argv, 0, &dbus_slot_type);

  sd_bus_slot_set_floating(*slot_ptr, 0);
  sd_bus_slot_unrefp(slot_ptr);
  *slot_ptr = NULL;

  return janet_wrap_nil();
}

JanetRegExt cfuns_slot[] = { JANET_REG("cancel", cfun_cancel_slot),
                             JANET_REG_END };
