// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Joshua Krusell

#ifndef _JANET_SDBUS_COMMON_H
#define _JANET_SDBUS_COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <janet.h>
#include <systemd/sd-bus.h>

#define UNUSED(x)                                                              \
  do {                                                                         \
    (void) (x);                                                                \
  } while (0)

#define CALL_SD_BUS_FUNC(func, ...)                                            \
  ({                                                                           \
    int rv;                                                                    \
    if ((rv = func(__VA_ARGS__)) < 0)                                          \
      janet_panicf("failed to call %s: %s", #func, strerror(-rv));             \
    rv;                                                                        \
  })

// D-Bus error type
extern const JanetAbstractType dbus_error_type;

// D-Bus connection functions
extern const JanetAbstractType dbus_bus_type;
extern JanetRegExt cfuns_bus[];

// D-Bus message functions
extern const JanetAbstractType dbus_message_type;
extern JanetRegExt cfuns_message[];

#endif
