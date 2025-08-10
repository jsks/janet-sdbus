// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Joshua Krusell

#ifndef _JANET_SDBUS_ASYNC_H
#define _JANET_SDBUS_ASYNC_H

#include <stdbool.h>

#include "common.h"

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
