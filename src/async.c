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

AsyncPending *create_async_pending(JanetChannel *ch) {
  AsyncPending *pending;
  if (!(pending = janet_malloc(sizeof(AsyncPending))))
    JANET_OUT_OF_MEMORY;

  pending->chan  = ch;
  pending->slot  = janet_abstract(&dbus_slot_type, sizeof(sd_bus_slot *));
  *pending->slot = NULL;

  return pending;
}

void queue_pending(AsyncPending **head, AsyncPending *pending) {
  pending->prev = NULL;
  pending->next = *head;

  if (*head)
    (*head)->prev = pending;

  *head = pending;
}

void dequeue_pending(AsyncPending **head, AsyncPending *pending) {
  if (!head || !*head)
    return;

  if (pending->prev)
    pending->prev->next = pending->next;
  else
    *head = pending->next;

  if (pending->next)
    pending->next->prev = pending->prev;
}

static void closeall_pending(Conn *conn, Janet status, Janet msg) {
  if (!conn->queue)
    return;

  Janet tuple = janet_wrap_tuple(TUPLE(status, msg));

  AsyncPending *p = conn->queue;
  while (p) {
    AsyncPending *next = p->next;

    janet_channel_give(p->chan, tuple);

    sd_bus_slot_unrefp(p->slot);
    *p->slot = NULL;

    p = next;
  }

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
      Janet status = janet_ckeywordv("error"),
            msg    = janet_cstringv("D-Bus connection error");
      closeall_pending(conn, status, msg);

      CANCEL_LISTENER(fiber, msg);
      return;
    }

    case JANET_ASYNC_EVENT_CLOSE: {
      Janet status = janet_ckeywordv("close"),
            msg    = janet_cstringv("D-Bus connection closed");
      closeall_pending(conn, status, msg);

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
  if (timer_fd == -1)
    janet_panicf("failed to call timerfd_create: %s", strerror(errno));

  conn->timer =
      janet_poll(conn, timer_fd, JANET_STREAM_READABLE, timer_callback);

  int bus_fd       = CALL_SD_BUS_FUNC(sd_bus_get_fd, conn->bus);
  uint32_t flags   = getevents(conn->bus);
  conn->bus_stream = janet_poll(
      conn, bus_fd, flags | JANET_STREAM_NOT_CLOSEABLE, bus_callback);
}
