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

// D-Bus bus connection
typedef struct {
  sd_bus *bus;          // D-Bus message bus
  JanetStream *stream;  // Unix fd for bus connection
  JanetFiber *listener; // Polling callback fiber
  JanetTable *queue;    // Queue of pending async calls
} Conn;

extern const JanetAbstractType dbus_bus_type;
extern JanetRegExt cfuns_bus[];

// D-Bus call
extern const JanetAbstractType dbus_error_type;
extern JanetRegExt cfuns_call[];

// D-Bus message
extern const JanetAbstractType dbus_message_type;
extern JanetRegExt cfuns_message[];

#endif
