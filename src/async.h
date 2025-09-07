// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Joshua Krusell

#ifndef _JANET_SDBUS_ASYNC_H
#define _JANET_SDBUS_ASYNC_H

#define _POSIX_C_SOURCE 199309L

#include <errno.h>
#include <poll.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

#include "common.h"

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

typedef struct AsyncCall {
  sd_bus_slot **slot;
  JanetChannel *chan;
  struct AsyncCall *next, *prev;
} AsyncCall;

AsyncCall *create_async_call(JanetChannel *);
void queue_call(AsyncCall **, AsyncCall *);
void dequeue_call(AsyncCall **, AsyncCall *);
void start_async_listener(Conn *);
void init_async(Conn *);
void settimeout(Conn *);

#endif
