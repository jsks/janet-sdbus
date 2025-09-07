// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Joshua Krusell

#ifndef _JANET_SDBUS_COMMON_H
#define _JANET_SDBUS_COMMON_H

#define _POSIX_C_SOURCE 199309L

#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

#include <janet.h>
#include <systemd/sd-bus.h>

#define UNUSED(x)                                                              \
  do {                                                                         \
    (void) (x);                                                                \
  } while (0)

#define END_LISTENER(fiber)                                                    \
  do {                                                                         \
    janet_schedule(fiber, janet_wrap_nil());                                   \
    janet_async_end(fiber);                                                    \
  } while (0)

#define CANCEL_LISTENER(fiber, msg)                                            \
  do {                                                                         \
    janet_cancel(fiber, msg);                                                  \
    janet_async_end(fiber);                                                    \
  } while (0)

#define CALL_SD_BUS_FUNC(func, ...)                                            \
  check_sd_bus_return(#func, func(__VA_ARGS__))

int check_sd_bus_return(const char *, int);

// D-Bus bus connection
typedef struct {
  sd_bus *bus;             // D-Bus message bus
  JanetStream *bus_stream; // Unix fd for bus connection
  JanetStream *timer;      // Timer fd for bus timeouts
  struct AsyncCall *queue; // Queue of pending async calls
} Conn;

extern const JanetAbstractType dbus_bus_type;
extern JanetRegExt cfuns_bus[];

// Pending async call
typedef struct AsyncCall {
  sd_bus_slot **slot;
  JanetChannel *chan;
  struct AsyncCall *next, *prev;
} AsyncCall;

extern AsyncCall *create_async_call(JanetChannel *);
extern void queue_call(AsyncCall **, AsyncCall *);
extern void dequeue_call(AsyncCall **, AsyncCall *);
extern void init_async(Conn *);
extern void settimeout(Conn *);
extern void setevents(Conn *);

// D-Bus call
extern const JanetAbstractType dbus_error_type;
extern JanetRegExt cfuns_call[];

// D-Bus message
extern const JanetAbstractType dbus_message_type;
extern JanetRegExt cfuns_message[];

// D-Bus export
extern JanetRegExt cfuns_export[];

// D-Bus slot
extern const JanetAbstractType dbus_slot_type;
extern JanetRegExt cfuns_slot[];

#endif
