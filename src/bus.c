// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Joshua Krusell

#include "common.h"

// -------------------------------------------------------------------
// D-Bus bus abstract type
// -------------------------------------------------------------------

static int dbus_bus_gc(void *, size_t);
static int dbus_bus_gcmark(void *, size_t);
static int dbus_bus_get(void *, Janet, Janet *);
static void dbus_bus_tostring(void *, JanetBuffer *);
static Janet dbus_bus_next(void *, Janet);
const JanetAbstractType dbus_bus_type = { .name      = "sdbus/bus",
                                          .gc        = dbus_bus_gc,
                                          .gcmark    = dbus_bus_gcmark,
                                          .get       = dbus_bus_get,
                                          .put       = NULL,
                                          .marshal   = NULL,
                                          .unmarshal = NULL,
                                          .tostring  = dbus_bus_tostring,
                                          .compare   = NULL,
                                          .hash      = NULL,
                                          .next      = dbus_bus_next,
                                          JANET_ATEND_NEXT };

JANET_CFUN(cfun_close_bus);
static JanetMethod dbus_bus_methods[] = {
  { "close", cfun_close_bus },
  { NULL,    NULL           }
};

static int dbus_bus_gc(void *p, size_t size) {
  UNUSED(size);
  Conn *conn = (Conn *) p;

  if (conn->stream) {
    janet_stream_close(conn->stream);
    conn->stream = NULL;
  }

  sd_bus_flush_close_unref(conn->bus);

  return 0;
}

static int dbus_bus_gcmark(void *p, size_t size) {
  UNUSED(size);

  Conn *conn = (Conn *) p;
  if (conn->stream)
    janet_mark(janet_wrap_abstract(conn->stream));

  if (conn->listener)
    janet_mark(janet_wrap_fiber(conn->listener));

  return 0;
}

static void dbus_bus_tostring(void *p, JanetBuffer *buffer) {
  const char *name = NULL;
  CALL_SD_BUS_FUNC(sd_bus_get_unique_name, ((Conn *) p)->bus, &name);

  janet_buffer_push_cstring(buffer, name);
}

static int dbus_bus_get(void *p, Janet key, Janet *out) {
  UNUSED(p);
  if (!janet_checktype(key, JANET_KEYWORD))
    return 0;

  return janet_getmethod(janet_unwrap_keyword(key), dbus_bus_methods, out);
}

static Janet dbus_bus_next(void *p, Janet key) {
  UNUSED(p);
  return janet_nextmethod(dbus_bus_methods, key);
}

// -------------------------------------------------------------------
// Exported wrapper functions
// -------------------------------------------------------------------

#define OPEN_BUS_CORE(CALL)                                                    \
  do {                                                                         \
    Conn *conn = janet_abstract(&dbus_bus_type, sizeof(Conn));                 \
    memset(conn, 0, sizeof(Conn));                                             \
                                                                               \
    CALL;                                                                      \
                                                                               \
    return janet_wrap_abstract(conn);                                          \
  } while (0)

#define OPEN_BUS0(fun) OPEN_BUS_CORE(CALL_SD_BUS_FUNC(fun, &conn->bus))

#define OPEN_BUS1(fun, arg)                                                    \
  OPEN_BUS_CORE(CALL_SD_BUS_FUNC(fun, &conn->bus, arg))

JANET_FN(cfun_open_user_bus, "(sdbus/open-user-bus)",
         "Open a user D-Bus connection.") {
  UNUSED(argv);
  janet_fixarity(argc, 0);

  OPEN_BUS0(sd_bus_open_user);
}

JANET_FN(cfun_open_system_bus, "(sdbus/open-system-bus)",
         "Open a system D-Bus connection.") {
  UNUSED(argv);
  janet_fixarity(argc, 0);

  OPEN_BUS0(sd_bus_open_system);
}

JANET_FN(cfun_open_user_machine, "(sdbus/open-user-machine)",
         "Open a user D-Bus connection for a specific machine.") {
  janet_fixarity(argc, 1);

  const char *machine = janet_getcstring(argv, 0);
  OPEN_BUS1(sd_bus_open_user_machine, machine);
}

JANET_FN(cfun_open_system_machine, "(sdbus/open-system-machine machine)",
         "Open a system D-Bus connection for a specific machine.") {
  janet_fixarity(argc, 1);

  const char *machine = janet_getcstring(argv, 0);
  OPEN_BUS1(sd_bus_open_system_machine, machine);
}

JANET_FN(cfun_open_system_remote, "(sdbus/open-system-remote machine)",
         "Open a system D-Bus connection for a remote machine.") {
  janet_fixarity(argc, 1);

  const char *host = janet_getcstring(argv, 0);
  OPEN_BUS1(sd_bus_open_system_remote, host);
}

// todo: documentation flushes outgoing, does not block for incoming
JANET_FN(cfun_close_bus, "(sdbus/close-bus bus)", "Close a D-Bus connection.") {
  janet_fixarity(argc, 1);

  Conn *conn = janet_getabstract(argv, 0, &dbus_bus_type);
  if (conn->stream) {
    janet_stream_close(conn->stream);
    conn->stream = NULL;
  }

  sd_bus_flush_close_unref(conn->bus);
  conn->bus = NULL;

  return janet_wrap_nil();
}

JANET_FN(cfun_bus_is_open, "(sdbus/bus-is-open bus)",
         "Check if a D-Bus connection is open.") {
  janet_fixarity(argc, 1);

  Conn *conn = janet_getabstract(argv, 0, &dbus_bus_type);
  int check  = CALL_SD_BUS_FUNC(sd_bus_is_open, conn->bus);

  return janet_wrap_boolean(check);
}

JANET_FN(cfun_get_unique_name, "(sdbus/get-unique-name bus)",
         "Get the unique name of a D-Bus connection.") {
  janet_fixarity(argc, 1);

  Conn *conn = janet_getabstract(argv, 0, &dbus_bus_type);

  const char *name = NULL;
  CALL_SD_BUS_FUNC(sd_bus_get_unique_name, conn->bus, &name);

  return janet_cstringv(name);
}

JANET_FN(
    cfun_set_allow_interactive_authorization,
    "(sdbus/set-allow-interactive-authorization bus allow)",
    "Set whether to allow interactive authorization on a D-Bus connection.") {
  janet_fixarity(argc, 2);

  Conn *conn = janet_getabstract(argv, 0, &dbus_bus_type);
  int allow  = janet_getboolean(argv, 1);

  CALL_SD_BUS_FUNC(sd_bus_set_allow_interactive_authorization, conn->bus,
                   allow);

  return janet_wrap_nil();
}

JANET_FN(cfun_list_names, "(sdbus/list-names bus)",
         "List registered names on a D-Bus connection.") {
  janet_fixarity(argc, 1);

  Conn *conn = janet_getabstract(argv, 0, &dbus_bus_type);

  char **acquired = NULL;
  CALL_SD_BUS_FUNC(sd_bus_list_names, conn->bus, &acquired, NULL);

  JanetArray *list = janet_array(1);
  for (char **p = acquired; *p; p++) {
    Janet name = janet_cstringv(*p);
    janet_free(*p);

    // TODO: janet_array_push may panic leading to a memory leak
    janet_array_push(list, name);
  }

  janet_free(acquired);
  return janet_wrap_array(list);
}

JANET_FN(cfun_send, "(sdbus/send bus msg)", "Send a D-Bus message.") {
  janet_fixarity(argc, 2);

  Conn *conn               = janet_getabstract(argv, 0, &dbus_bus_type);
  sd_bus_message **msg_ptr = janet_getabstract(argv, 1, &dbus_message_type);

  CALL_SD_BUS_FUNC(sd_bus_send, conn->bus, *msg_ptr, NULL);

  return janet_wrap_nil();
}

JanetRegExt cfuns_bus[] = {
  JANET_REG("open-user-bus", cfun_open_user_bus),
  JANET_REG("open-system-bus", cfun_open_system_bus),
  JANET_REG("open-user-machine", cfun_open_user_machine),
  JANET_REG("open-system-machine", cfun_open_system_machine),
  JANET_REG("open-system-remote", cfun_open_system_remote),
  JANET_REG("close-bus", cfun_close_bus),
  JANET_REG("bus-is-open", cfun_bus_is_open),
  JANET_REG("get-unique-name", cfun_get_unique_name),
  JANET_REG("set-allow-interactive-authorization",
            cfun_set_allow_interactive_authorization),
  JANET_REG("list-names", cfun_list_names),
  JANET_REG("send", cfun_send),
  JANET_REG_END
};
