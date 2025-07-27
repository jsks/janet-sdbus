// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Joshua Krusell

#include "common.h"

#define janet_fstringv(cstr, ...)                                              \
  janet_wrap_string(janet_formatc(cstr, __VA_ARGS__))

typedef struct {
  uint64_t cookie;
  sd_bus_slot *slot;
  JanetChannel *chan;
} Call;

typedef struct {
  Conn *conn;
  Call *call;
} CallbackState;

// -------------------------------------------------------------------
// Abstract types
// -------------------------------------------------------------------

static int dbus_call_gc(void *, size_t);
const JanetAbstractType dbus_call_type = { .name = "sdbus/call",
                                           .gc   = dbus_call_gc,
                                           JANET_ATEND_GC };

static int dbus_call_gc(void *p, size_t size) {
  UNUSED(size);

  Call *call = (Call *) p;
  sd_bus_slot_unref(call->slot);

  return 0;
}

static int dbus_error_gc(void *, size_t);
const JanetAbstractType dbus_error_type = { .name = "sdbus/error",
                                            .gc   = dbus_error_gc,
                                            JANET_ATEND_GC };

static int dbus_error_gc(void *data, size_t size) {
  UNUSED(size);

  sd_bus_error_free((sd_bus_error *) data);
  return 0;
}

// -------------------------------------------------------------------
// Async call functions
// -------------------------------------------------------------------

static JanetString format_error(sd_bus_error *error) {
  const char *fmt =
      (error->message) ? "D-Bus error: %s: %s" : "D-Bus error: %s";
  JanetString str = janet_formatc(fmt, error->name, error->message);

  sd_bus_error_free(error);
  return str;
}

void queue(Conn *conn, Call *call) {
  if (!conn->queue)
    conn->queue = janet_table(1);

  janet_table_put(conn->queue, janet_wrap_u64(call->cookie),
                  janet_wrap_abstract(call));
}

void dequeue(Conn *conn, Call *call) {
  if (!conn->queue)
    return;

  janet_table_remove(conn->queue, janet_wrap_u64(call->cookie));
}

static void destroy_callback(void *userdata) {
  free(userdata);
  return;
}

static int message_handler(sd_bus_message *reply, void *userdata,
                           sd_bus_error *ret_error) {
  UNUSED(ret_error);

  CallbackState *state = userdata;
  Conn *conn           = state->conn;
  Call *call           = state->call;

  dequeue(conn, call);

  if (conn->queue && conn->queue->count == 0) {
    janet_stream_close(conn->stream);
    conn->stream = NULL;
  }

  Janet status, value;
  if (sd_bus_message_is_method_error(reply, NULL)) {
    sd_bus_error *error = sd_bus_message_get_error(reply);
    JanetString str     = format_error(error);

    status = janet_ckeywordv("error");
    value  = janet_wrap_string(str);
  } else {
    status = janet_ckeywordv("ok");

    sd_bus_message **msg_ptr =
        janet_abstract(&dbus_message_type, sizeof(sd_bus_message *));
    *msg_ptr = sd_bus_message_ref(reply);

    value = janet_wrap_abstract(msg_ptr);
  }

  JanetTuple tuple = janet_tuple_n((Janet[]) { status, value }, 2);
  janet_channel_give(call->chan, janet_wrap_tuple(tuple));

  return 0;
}

static void closeall_pending(Conn *conn, Janet msg) {
  if (!conn->queue)
    return;

  Janet status     = janet_ckeywordv("error");
  JanetTuple tuple = janet_tuple_n((Janet[]) { status, msg }, 2);

  const JanetKV *kvs = conn->queue->data, *kv = NULL;
  int32_t cap = conn->queue->capacity;
  while ((kv = janet_dictionary_next(kvs, cap, kv))) {
    Call *call = janet_unwrap_abstract(kv->value);

    sd_bus_slot_unref(call->slot);
    call->slot = NULL;
    janet_channel_give(call->chan, janet_wrap_tuple(tuple));
  }

  janet_table_clear(conn->queue);
}

static void process_driver(JanetFiber *fiber, JanetAsyncEvent event) {
  Conn *conn = *(Conn **) fiber->ev_state;

  switch (event) {
    case JANET_ASYNC_EVENT_READ: {
      int rv;
      while ((rv = sd_bus_process(conn->bus, NULL)) > 0) {
      }

      if (rv < 0) {
        Janet msg = janet_fstringv("sd_bus_process: %s", strerror(-rv));
        closeall_pending(conn, msg);

        janet_cancel(fiber, msg);
        janet_async_end(fiber);
      }

      break;
    }

    case JANET_ASYNC_EVENT_HUP:
    case JANET_ASYNC_EVENT_ERR: {
      Janet msg = janet_cstringv("D-Bus connection error");
      closeall_pending(conn, msg);

      janet_cancel(fiber, msg);
      janet_async_end(fiber);
      break;
    }

    case JANET_ASYNC_EVENT_CLOSE: {
      Janet msg = janet_cstringv("D-Bus connection closed");
      closeall_pending(conn, msg);

      janet_schedule(fiber, janet_wrap_nil());
      janet_async_end(fiber);
      break;
    }

    default:
      break;
  }
}

static void start_listener(Conn *conn) {
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
    janet_panic("Failed to allocate memory for Janet callback state");
  *state = conn;

  janet_async_start_fiber(conn->listener, conn->stream, JANET_ASYNC_LISTEN_READ,
                          process_driver, state);
}

// -------------------------------------------------------------------
// Exported wrapper functions
// -------------------------------------------------------------------

JANET_FN(cfun_call, "(sdbus/call bus message)", "Call a D-Bus method.") {
  janet_fixarity(argc, 2);

  Conn *conn               = janet_getabstract(argv, 0, &dbus_bus_type);
  sd_bus_message **msg_ptr = janet_getabstract(argv, 1, &dbus_message_type);

  // Initialize via an abstract type so that Janet's GC can clean up
  // on panic.
  sd_bus_error *error = janet_abstract(&dbus_error_type, sizeof(sd_bus_error));
  *error              = SD_BUS_ERROR_NULL;

  sd_bus_message **reply =
      janet_abstract(&dbus_message_type, sizeof(sd_bus_message *));

  int rv = sd_bus_call(conn->bus, *msg_ptr, 0, error, reply);
  if (rv < 0) {
    if (sd_bus_error_is_set(error)) {
      JanetString str = format_error(error);
      janet_panics(str);
    }

    janet_panicf("D-Bus method call failed: %s", strerror(-rv));
  }

  return janet_wrap_abstract(reply);
}

JANET_FN(cfun_call_async, "(sdbus/call-async bus msg chan)",
         "Call a D-Bus method asynchronously.") {
  janet_fixarity(argc, 3);

  Conn *conn               = janet_getabstract(argv, 0, &dbus_bus_type);
  sd_bus_message **msg_ptr = janet_getabstract(argv, 1, &dbus_message_type);

  Call *call = janet_abstract(&dbus_call_type, sizeof(Call));
  call->chan = janet_getabstract(argv, 2, &janet_channel_type);

  CallbackState *state;
  if (!(state = janet_malloc(sizeof(CallbackState))))
    janet_panic("Failed to allocate memory for D-Bus callback state");
  state->conn = conn;
  state->call = call;

  CALL_SD_BUS_FUNC(sd_bus_call_async, conn->bus, &call->slot, *msg_ptr,
                   message_handler, state, 0);
  CALL_SD_BUS_FUNC(sd_bus_message_get_cookie, *msg_ptr, &call->cookie);

  queue(conn, call);

  sd_bus_slot_set_destroy_callback(call->slot, destroy_callback);

  if (!conn->listener ||
      janet_fiber_status(conn->listener) == JANET_STATUS_DEAD)
    start_listener(conn);

  Janet out = janet_wrap_abstract(call);
  return out;
}

JANET_FN(cfun_cancel, "(sdbus/cancel call)", "Cancel a D-Bus async call.") {
  janet_fixarity(argc, 1);

  Call *call = janet_getabstract(argv, 0, &dbus_call_type);
  sd_bus_slot_unref(call->slot);
  call->slot = NULL;

  return janet_wrap_nil();
}

JanetRegExt cfuns_call[] = { JANET_REG("call-async", cfun_call_async),
                             JANET_REG("call", cfun_call),
                             JANET_REG("cancel", cfun_cancel), JANET_REG_END };
