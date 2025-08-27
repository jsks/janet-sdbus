// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Joshua Krusell

#include "async.h"

#define janet_fstringv(cstr, ...)                                              \
  janet_wrap_string(janet_formatc(cstr, __VA_ARGS__))

AsyncCall *create_async_call(JanetChannel *ch) {
  AsyncCall *call;
  if (!(call = janet_malloc(sizeof(AsyncCall))))
    JANET_OUT_OF_MEMORY;

  call->chan  = ch;
  call->slot  = janet_abstract(&dbus_slot_type, sizeof(sd_bus_slot *));
  *call->slot = NULL;

  return call;
}

bool is_listener_closeable(Conn *conn) {
  return !conn->gc && conn->listener && conn->subscribers == 0 && !conn->queue;
}

void queue_call(AsyncCall **head, AsyncCall *call) {
  call->prev = NULL;
  call->next = *head;

  if (*head)
    (*head)->prev = call;

  *head = call;
}

void dequeue_call(AsyncCall **head, AsyncCall *call) {
  if (call->prev)
    call->prev->next = call->next;
  else
    *head = call->next;

  if (call->next)
    call->next->prev = call->prev;
}

static void closeall_pending_calls(Conn *conn, Janet msg) {
  if (!conn->queue || conn->gc)
    return;

  Janet status     = janet_ckeywordv("error");
  JanetTuple tuple = janet_tuple_n((Janet[]) { status, msg }, 2);

  AsyncCall *p = conn->queue;
  do {
    janet_channel_give(p->chan, janet_wrap_tuple(tuple));

    sd_bus_slot_unrefp(p->slot);
    *p->slot = NULL;
  } while ((p = p->next));
}

static void bus_process_driver(JanetFiber *fiber, JanetAsyncEvent event) {
  Conn *conn = *(Conn **) fiber->ev_state;

  switch (event) {
    case JANET_ASYNC_EVENT_READ: {
      int rv;
      while ((rv = sd_bus_process(conn->bus, NULL)) > 0) {
      }

      if (rv < 0) {
        Janet msg = janet_fstringv("sd_bus_process: %s", strerror(-rv));
        closeall_pending_calls(conn, msg);

        CANCEL_LISTENER(conn, msg);
        return;
      }

      break;
    }

    case JANET_ASYNC_EVENT_HUP:
    case JANET_ASYNC_EVENT_ERR: {
      Janet msg = janet_cstringv("D-Bus connection error");
      closeall_pending_calls(conn, msg);

      CANCEL_LISTENER(conn, msg);
      return;
    }

    case JANET_ASYNC_EVENT_CLOSE: {
      Janet msg = janet_cstringv("D-Bus connection closed");
      closeall_pending_calls(conn, msg);

      END_LISTENER(conn);
    }
    case JANET_ASYNC_EVENT_DEINIT:
      return;

    default:
      break;
  }

  // Ideally we would keep the listener fiber up and avoid restarting
  // on the next call; however, a running event-loop task in Janet
  // blocks program exit.
  if (is_listener_closeable(conn))
    END_LISTENER(conn);
}

void start_async_listener(Conn *conn) {
  if (conn->listener && janet_fiber_status(conn->listener) != JANET_STATUS_DEAD)
    return;

  // todo: check closed flag for stream
  if (!conn->stream) {
    int fd       = CALL_SD_BUS_FUNC(sd_bus_get_fd, conn->bus);
    conn->stream = janet_stream(
        fd, JANET_STREAM_READABLE | JANET_STREAM_NOT_CLOSEABLE, NULL);
  }

  JanetFunction *thunk = janet_thunk_delay(janet_wrap_nil());
  conn->listener       = janet_fiber(thunk, 64, 0, NULL);

  // Ugly, but janet_async_end will call free on fiber->ev_state which will
  // otherwise clobber our bus object
  Conn **state;
  if (!(state = janet_malloc(sizeof(Conn *))))
    JANET_OUT_OF_MEMORY;
  *state = conn;

  janet_async_start_fiber(conn->listener, conn->stream, JANET_ASYNC_LISTEN_BOTH,
                          bus_process_driver, state);
}
