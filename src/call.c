// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Joshua Krusell

#include "async.h"
#include "common.h"

#define FREE_CALL_STATE(state)                                                 \
  do {                                                                         \
    janet_free(state->call);                                                   \
    janet_free(state);                                                         \
  } while (0)

typedef struct {
  Conn *conn;
  AsyncCall *call;
} AsyncCallbackState;

// -------------------------------------------------------------------
// Abstract types
// -------------------------------------------------------------------

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

static void destroy_call_callback(void *userdata) {
  AsyncCallbackState *state = userdata;

  dequeue_call(&state->conn->queue, state->call);
  if (is_listener_closeable(state->conn))
    END_LISTENER(state->conn);

  FREE_CALL_STATE(state);
}

static int message_handler(sd_bus_message *reply, void *userdata,
                           sd_bus_error *ret_error) {
  UNUSED(ret_error);

  AsyncCallbackState *state = userdata;
  Conn *conn                = state->conn;
  AsyncCall *call           = state->call;

  dequeue_call(&conn->queue, call);

  Janet status, value;
  if (sd_bus_message_is_method_error(reply, NULL)) {
    sd_bus_error *error = (sd_bus_error *) sd_bus_message_get_error(reply);
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

// -------------------------------------------------------------------
// Exported wrapper functions
// -------------------------------------------------------------------

JANET_FN(cfun_call_async, "(sdbus/call-async bus message chan)",
         "Call a D-Bus method asynchronously. Returns an object\n"
         "representing the pending call that can be passed to\n"
         "`sdbus/cancel`.\n\n"
         "`message` must be a well-formed `:sdbus/message` object.\n\n"
         "The result from the asynchronous call will be written to the\n"
         "channel, `chan`. Result will be a tuple containing a status\n"
         "and value, either\n `[:error value]` indicating an error or\n"
         "`[:ok value]`. \n\n") {
  janet_fixarity(argc, 3);

  Conn *conn               = janet_getabstract(argv, 0, &dbus_bus_type);
  sd_bus_message **msg_ptr = janet_getabstract(argv, 1, &dbus_message_type);
  JanetChannel *ch         = janet_getabstract(argv, 2, &janet_channel_type);

  AsyncCall *call = create_async_call(ch);

  // TODO: free if sd_bus_call_async or sd_bus_message_get_cookie fails
  AsyncCallbackState *state;
  if (!(state = janet_malloc(sizeof(AsyncCallbackState))))
    JANET_OUT_OF_MEMORY;
  *state = (AsyncCallbackState) { .conn = conn, .call = call };

  int rv = sd_bus_call_async(conn->bus, call->slot, *msg_ptr, message_handler,
                             state, 0);
  if (rv < 0) {
    FREE_CALL_STATE(state);
    janet_panicf("failed to call sd_bus_call_async: %s", strerror(-rv));
  }

  sd_bus_slot_set_floating(*call->slot, 1);

  uint64_t cookie;
  sd_bus_message_get_cookie(*msg_ptr, &cookie);
  call->cookie = janet_wrap_u64(cookie);

  queue_call(&conn->queue, call);
  sd_bus_slot_set_destroy_callback(*call->slot, destroy_call_callback);
  start_async_listener(conn);

  return janet_wrap_abstract(call->slot);
}

JanetRegExt cfuns_call[] = { JANET_REG("call-async", cfun_call_async),
                             JANET_REG_END };
