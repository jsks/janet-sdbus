// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Joshua Krusell

#include "common.h"

#define FREE_CALL_STATE(state)                                                 \
  do {                                                                         \
    janet_free(state->call);                                                   \
    janet_free(state);                                                         \
  } while (0)

typedef struct {
  Conn *conn;
  AsyncCall *call;
} AsyncState;

static JanetString format_error(sd_bus_error *error) {
  const char *fmt =
      (error->message) ? "D-Bus error: %s: %s" : "D-Bus error: %s";
  JanetString str = janet_formatc(fmt, error->name, error->message);

  return str;
}

static AsyncState *init_callback_state(Conn *conn, JanetChannel *ch) {
  AsyncCall *call = create_async_call(ch);

  AsyncState *state;
  if (!(state = janet_malloc(sizeof(AsyncState))))
    JANET_OUT_OF_MEMORY;
  *state = (AsyncState) { .conn = conn, .call = call };

  return state;
}

static void destroy_call_callback(void *userdata) {
  AsyncState *state = userdata;
  dequeue_call(&state->conn->queue, state->call);

  FREE_CALL_STATE(state);
}

static int signal_install_handler(sd_bus_message *msg, void *userdata,
                                  sd_bus_error *ret_error) {
  UNUSED(ret_error);

  AsyncState *state = userdata;
  AsyncCall *call   = state->call;

  if (sd_bus_message_is_method_error(msg, NULL)) {
    sd_bus_error *error = (sd_bus_error *) sd_bus_message_get_error(msg);
    JanetString str     = format_error(error);

    CHAN_PUSH(call->chan, janet_ckeywordv("error"), janet_wrap_string(str));
  }

  return 0;
}

static int message_handler(sd_bus_message *reply, void *userdata,
                           sd_bus_error *ret_error) {
  UNUSED(ret_error);

  AsyncState *state = userdata;
  Conn *conn        = state->conn;
  AsyncCall *call   = state->call;

  uint8_t type;
  sd_bus_message_get_type(reply, &type);

  switch (type) {
    case SD_BUS_MESSAGE_METHOD_RETURN:
      dequeue_call(&conn->queue, call);
    /* fallthrough */
    case SD_BUS_MESSAGE_SIGNAL: {
      sd_bus_message **msg_ptr =
          janet_abstract(&dbus_message_type, sizeof(sd_bus_message *));
      *msg_ptr = sd_bus_message_ref(reply);

      CHAN_PUSH(call->chan, janet_ckeywordv("ok"),
                janet_wrap_abstract(msg_ptr));
      break;
    }

    case SD_BUS_MESSAGE_METHOD_ERROR: {
      dequeue_call(&conn->queue, call);

      sd_bus_error *error = (sd_bus_error *) sd_bus_message_get_error(reply);
      JanetString str     = format_error(error);

      CHAN_PUSH(call->chan, janet_ckeywordv("error"), janet_wrap_string(str));
    }

    default:
      break;
  }

  return 0;
}

JANET_FN(cfun_call_async, "(sdbus/call-async bus message chan)",
         "Call a D-Bus method asynchronously. Returns an object\n"
         "representing the pending call that can be passed to\n"
         "`sdbus/cancel`.\n\n"
         "`message` must be a well-formed `:sdbus/message` object.\n\n"
         "The result from the asynchronous call will be written to the\n"
         "channel, `chan`. Result will be a tuple containing a status\n"
         "and value, either\n `[:error value]` indicating an error or\n"
         "`[:ok value]`. \n\n") {
  janet_arity(argc, 3, 4);

  Conn *conn               = janet_getabstract(argv, 0, &dbus_bus_type);
  sd_bus_message **msg_ptr = janet_getabstract(argv, 1, &dbus_message_type);
  JanetChannel *ch         = janet_getabstract(argv, 2, &janet_channel_type);
  uint64_t timeout         = janet_optinteger64(argv, argc, 3, 0);

  AsyncState *state = init_callback_state(conn, ch);

  int rv = sd_bus_call_async(conn->bus, state->call->slot, *msg_ptr,
                             message_handler, state, timeout);
  if (rv < 0) {
    FREE_CALL_STATE(state);
    janet_panicf("failed to call sd_bus_call_async: %s", strerror(-rv));
  }

  sd_bus_slot_set_floating(*state->call->slot, 1);

  queue_call(&conn->queue, state->call);
  sd_bus_slot_set_destroy_callback(*state->call->slot, destroy_call_callback);

  settimeout(conn);

  return janet_wrap_abstract(state->call->slot);
}

JANET_FN(cfun_match_signal, "(sdbus/match-signal bus match-rule chan)",
         "Subscribe to a D-Bus signal with the given match-rule.") {
  janet_fixarity(argc, 3);

  Conn *conn       = janet_getabstract(argv, 0, &dbus_bus_type);
  const char *rule = janet_getcstring(argv, 1);
  JanetChannel *ch = janet_getabstract(argv, 2, &janet_channel_type);

  const char *prefix = *rule ? "type='signal'," : "type='signal'";
  size_t prefix_len = strlen(prefix), rule_len = strlen(rule),
         len = prefix_len + rule_len + 1;

  char *match;
  if (!(match = janet_malloc(len * sizeof(char))))
    JANET_OUT_OF_MEMORY;

  memcpy(match, prefix, prefix_len);
  memcpy(match, rule, rule_len + 1);

  AsyncState *state = init_callback_state(conn, ch);

  int rv =
      sd_bus_add_match_async(conn->bus, state->call->slot, match,
                             message_handler, signal_install_handler, state);
  janet_free(match);

  if (rv < 0) {
    FREE_CALL_STATE(state);
    janet_panicf("failed to call sd_bus_add_match_async: %s", strerror(-rv));
  }

  sd_bus_slot_set_floating(*state->call->slot, 1);

  queue_call(&conn->queue, state->call);
  sd_bus_slot_set_destroy_callback(*state->call->slot, destroy_call_callback);

  return janet_wrap_abstract(state->call->slot);
}

JanetRegExt cfuns_call[] = { JANET_REG("call-async", cfun_call_async),
                             JANET_REG("match-signal", cfun_match_signal),
                             JANET_REG_END };
