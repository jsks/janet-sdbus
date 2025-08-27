// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Joshua Krusell

#ifndef _JANET_SDBUS_ASYNC_H
#define _JANET_SDBUS_ASYNC_H

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

typedef struct AsyncCall {
  Janet cookie;
  sd_bus_slot **slot;
  JanetChannel *chan;
  struct AsyncCall *next, *prev;
} AsyncCall;

AsyncCall *create_async_call(JanetChannel *);
bool is_listener_closeable(Conn *);
void queue_call(AsyncCall **, AsyncCall *);
void dequeue_call(AsyncCall **, AsyncCall *);
void start_async_listener(Conn *);

#endif
