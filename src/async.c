// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Joshua Krusell

#include "common.h"

#define janet_fstringv(cstr, ...)                                              \
  janet_wrap_string(janet_formatc(cstr, __VA_ARGS__))

void settimeout(Conn *);
void setevents(Conn *);

static void process_bus(Conn *conn) {
  int rv;
  while ((rv = sd_bus_process(conn->bus, NULL)) > 0) {
  }

  if (rv < 0)
    janet_panicf("failed to call sd_bus_process: %s", strerror(-rv));

  setevents(conn);
  settimeout(conn);
}

static uint32_t getevents(sd_bus *bus) {
  int events = CALL_SD_BUS_FUNC(sd_bus_get_events, bus);

  uint32_t flags = 0;
  if (events & POLLIN)
    flags |= JANET_STREAM_READABLE;
  if (events & POLLOUT)
    flags |= JANET_STREAM_WRITABLE;

  return flags;
}

void setevents(Conn *conn) {
  uint32_t newflags       = getevents(conn->bus);
  conn->bus_stream->flags = (conn->bus_stream->flags &
                             ~(JANET_STREAM_READABLE | JANET_STREAM_WRITABLE)) |
                            newflags;

  janet_stream_edge_triggered(conn->bus_stream);
}

void settimeout(Conn *conn) {
  uint64_t usec = 0;
  CALL_SD_BUS_FUNC(sd_bus_get_timeout, conn->bus, &usec);

  if (usec == 0) {
    process_bus(conn);
    return;
  }

  struct itimerspec new_value = { 0 };
  if (usec != UINT64_MAX) {
    new_value.it_value.tv_sec  = usec / 1000000;
    new_value.it_value.tv_nsec = (usec % 1000000) * 1000;
  }

  if (timerfd_settime(conn->timer->handle, TFD_TIMER_ABSTIME, &new_value,
                      NULL) == -1)
    janet_panicf("timerfd_settime: %s", strerror(errno));
}

AsyncCall *create_async_call(JanetChannel *ch) {
  AsyncCall *call;
  if (!(call = janet_malloc(sizeof(AsyncCall))))
    JANET_OUT_OF_MEMORY;

  call->chan  = ch;
  call->slot  = janet_abstract(&dbus_slot_type, sizeof(sd_bus_slot *));
  *call->slot = NULL;

  return call;
}

void queue_call(AsyncCall **head, AsyncCall *call) {
  call->prev = NULL;
  call->next = *head;

  if (*head)
    (*head)->prev = call;

  *head = call;
}

void dequeue_call(AsyncCall **head, AsyncCall *call) {
  if (!head)
    return;

  if (call->prev)
    call->prev->next = call->next;
  else
    *head = call->next;

  if (call->next)
    call->next->prev = call->prev;
}

static void closeall_pending_calls(Conn *conn, Janet msg) {
  if (!conn->queue)
    return;

  Janet status     = janet_ckeywordv("error");
  JanetTuple tuple = janet_tuple_n((Janet[]) { status, msg }, 2);

  AsyncCall *p = conn->queue;
  do {
    janet_channel_give(p->chan, janet_wrap_tuple(tuple));

    sd_bus_slot_unrefp(p->slot);
    *p->slot = NULL;
  } while ((p = p->next));

  conn->queue = NULL;
}

static void timer_callback(JanetFiber *fiber, JanetAsyncEvent event) {
  Conn *conn = *(Conn **) fiber->ev_state;

  switch (event) {
    case JANET_ASYNC_EVENT_READ: {
      uint64_t expirations;
      int rv = read(conn->timer->handle, &expirations, sizeof(uint64_t));
      if (rv == -1 && errno == EBADF)
        janet_panic("Timer file descriptor unexpectedly closed");

      process_bus(conn);
      break;
    }

    case JANET_ASYNC_EVENT_CLOSE:
      END_LISTENER(fiber);
      break;

    default:
      break;
  }
}

static void bus_callback(JanetFiber *fiber, JanetAsyncEvent event) {
  Conn *conn = *(Conn **) fiber->ev_state;

  switch (event) {
    case JANET_ASYNC_EVENT_WRITE:
    case JANET_ASYNC_EVENT_READ:
      process_bus(conn);
      break;

    case JANET_ASYNC_EVENT_HUP:
    case JANET_ASYNC_EVENT_ERR: {
      Janet msg = janet_cstringv("D-Bus connection error");
      closeall_pending_calls(conn, msg);

      CANCEL_LISTENER(fiber, msg);
      return;
    }

    case JANET_ASYNC_EVENT_CLOSE: {
      Janet msg = janet_cstringv("D-Bus connection closed");
      closeall_pending_calls(conn, msg);

      END_LISTENER(fiber);
      return;
    }

    default:
      break;
  }
}

static JanetStream *janet_poll(Conn *conn, int fd, uint32_t flags,
                               JanetEVCallback callback) {
  JanetStream *stream = janet_stream(fd, flags, NULL);

  JanetFunction *thunk = janet_thunk_delay(janet_wrap_nil());
  JanetFiber *fiber    = janet_fiber(thunk, 64, 0, NULL);

  Conn **state;
  if (!(state = janet_malloc(sizeof(Conn *))))
    JANET_OUT_OF_MEMORY;
  *state = conn;

  janet_async_start_fiber(fiber, stream, JANET_ASYNC_LISTEN_BOTH, callback,
                          state);
  return stream;
}

void init_async(Conn *conn) {
  int timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
  conn->timer =
      janet_poll(conn, timer_fd, JANET_STREAM_READABLE, timer_callback);

  int bus_fd       = CALL_SD_BUS_FUNC(sd_bus_get_fd, conn->bus);
  uint32_t flags   = getevents(conn->bus);
  conn->bus_stream = janet_poll(
      conn, bus_fd, flags | JANET_STREAM_NOT_CLOSEABLE, bus_callback);
}
