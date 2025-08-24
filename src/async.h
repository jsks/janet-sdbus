// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Joshua Krusell

#ifndef _JANET_SDBUS_ASYNC_H
#define _JANET_SDBUS_ASYNC_H

#include <stdbool.h>

#include "common.h"

#define END_LISTENER(conn)                                                     \
  do {                                                                         \
    janet_schedule(conn->listener, janet_wrap_nil());                          \
    janet_async_end(conn->listener);                                           \
    conn->listener = NULL;                                                     \
  } while (0)

#define CANCEL_LISTENER(conn, msg)                                             \
  do {                                                                         \
    janet_cancel(conn->listener, msg);                                         \
    janet_async_end(conn->listener);                                           \
    conn->listener = NULL;                                                     \
  } while (0)

typedef struct {
  Janet cookie;
  sd_bus_slot **slot;
  JanetChannel *chan;
} AsyncCall;

AsyncCall *create_async_call(JanetChannel *);
bool is_listener_closeable(Conn *);
void queue_call(Conn *, AsyncCall *);
void dequeue_call(Conn *, AsyncCall *);
void start_async_listener(Conn *);

#endif
