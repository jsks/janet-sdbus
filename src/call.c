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

static sd_bus_message *message_copy(sd_bus *bus, sd_bus_message *msg,
                                    uint8_t type) {
  sd_bus_message *new;
  CALL_SD_BUS_FUNC(sd_bus_message_new, bus, &new, type);

  CALL_SD_BUS_FUNC(sd_bus_message_rewind, msg, true);
  CALL_SD_BUS_FUNC(sd_bus_message_copy, new, msg, true);
  CALL_SD_BUS_FUNC(sd_bus_message_rewind, msg, true);

  CALL_SD_BUS_FUNC(sd_bus_message_seal, new, 0, 0);

  return new;
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
      if (call->kind == Call)
        dequeue_call(&conn->queue, call);
    /* fallthrough */
    case SD_BUS_MESSAGE_METHOD_CALL: {
      sd_bus_message **msg_ptr =
          janet_abstract(&dbus_message_type, sizeof(sd_bus_message *));

      *msg_ptr = (call->kind == Call) ? sd_bus_message_ref(reply)
                                      : message_copy(conn->bus, reply, type);

      CHAN_PUSH(call->chan, janet_ckeywordv("ok"),
                janet_wrap_abstract(msg_ptr));
      break;
    }
    case SD_BUS_MESSAGE_SIGNAL: {
      sd_bus_message **msg_ptr =
          janet_abstract(&dbus_message_type, sizeof(sd_bus_message *));
      *msg_ptr = sd_bus_message_ref(reply);

      CHAN_PUSH(call->chan, janet_ckeywordv("ok"),
                janet_wrap_abstract(msg_ptr));
      break;
    }

    case SD_BUS_MESSAGE_METHOD_ERROR: {
      if (call->kind == Call)
        dequeue_call(&conn->queue, call);

      sd_bus_error *error = (sd_bus_error *) sd_bus_message_get_error(reply);
      JanetString str     = format_error(error);

      CHAN_PUSH(call->chan, janet_ckeywordv("error"), janet_wrap_string(str));
      break;
    }

    default:
      break;
  }

  return 0;
}

JANET_FN(
    cfun_call_async, "(sdbus/call-async bus message chan &opt timeout)",
    "Call a D-Bus method asynchronously with an optional timeout in "
    "microseconds. Returns a bus slot that may be passed to `sdbus/cancel` to "
    "cancel the pending call.\n\n"
    "The reply message from the asynchronous call will be written to "
    "the channel, `chan`, together with a status value as a tuple, `[status "
    "reply]`. Status will be one of :ok, :error, or :close --- the last "
    "of which indicating that the D-Bus connection was closed while the "
    "call was pending.") {
  janet_arity(argc, 3, 4);

  Conn *conn               = janet_getabstract(argv, 0, &dbus_bus_type);
  sd_bus_message **msg_ptr = janet_getabstract(argv, 1, &dbus_message_type);
  JanetChannel *ch         = janet_getabstract(argv, 2, &janet_channel_type);
  uint64_t timeout         = janet_optinteger64(argv, argc, 3, 0);

  AsyncState *state = init_callback_state(conn, ch);
  state->call->kind = Call;

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

JANET_FN(
    cfun_match_async, "(sdbus/match-async bus rule chan)",
    "Subscribe to D-Bus messages that match a rule string. Returns a bus slot "
    "that may be passed to `sdbus/cancel` to unsubscribe.\n\n"
    "The rule string must conform to the D-Bus specification on Match Rules. "
    "Refer to the spec for valid keys.\n\n"
    "Matching messages are written to the channel, `chan`, together with a "
    "status value as a tuple, `[status msg]`. Status will be one of :ok, "
    ":error, or :close --- the last of which indicates that the D-Bus "
    "connection has been closed.\n\n") {
  janet_fixarity(argc, 3);

  Conn *conn        = janet_getabstract(argv, 0, &dbus_bus_type);
  const char *match = janet_getcstring(argv, 1);
  JanetChannel *ch  = janet_getabstract(argv, 2, &janet_channel_type);

  AsyncState *state = init_callback_state(conn, ch);
  state->call->kind = Match;

  int rv =
      sd_bus_add_match_async(conn->bus, state->call->slot, match,
                             message_handler, signal_install_handler, state);

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
                             JANET_REG("match-async", cfun_match_async),
                             JANET_REG_END };
