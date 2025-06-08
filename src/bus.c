// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Joshua Krusell

#include "common.h"

// -------------------------------------------------------------------
// D-Bus bus abstract type
// -------------------------------------------------------------------

static int dbus_bus_gc(void *, size_t);
static int dbus_bus_get(void *, Janet, Janet *);
static void dbus_bus_tostring(void *, JanetBuffer *);
static Janet dbus_bus_next(void *, Janet);
const JanetAbstractType dbus_bus_type = { .name      = "sdbus/bus",
                                          .gc        = dbus_bus_gc,
                                          .gcmark    = NULL,
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

  sd_bus_flush_close_unrefp((sd_bus **) p);
  *((sd_bus **) p) = NULL;

  return 0;
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

static void dbus_bus_tostring(void *p, JanetBuffer *buffer) {
  const char *name = NULL;
  CALL_SD_BUS_FUNC(sd_bus_get_unique_name, *(sd_bus **) p, &name);

  janet_buffer_push_cstring(buffer, name);
}

// -------------------------------------------------------------------
// Exported wrapper functions
// -------------------------------------------------------------------

JANET_FN(cfun_open_user_bus, "(sdbus/open-user-bus)",
         "Open a user D-Bus connection.") {
  UNUSED(argv);
  janet_fixarity(argc, 0);

  sd_bus **bus_ptr = janet_abstract(&dbus_bus_type, sizeof(sd_bus *));
  CALL_SD_BUS_FUNC(sd_bus_open_user, bus_ptr);

  return janet_wrap_abstract(bus_ptr);
}

JANET_FN(cfun_open_system_bus, "(sdbus/open-system-bus)",
         "Open a system D-Bus connection.") {
  UNUSED(argv);
  janet_fixarity(argc, 0);

  sd_bus **bus_ptr = janet_abstract(&dbus_bus_type, sizeof(sd_bus *));
  CALL_SD_BUS_FUNC(sd_bus_open_system, bus_ptr);

  return janet_wrap_abstract(bus_ptr);
}

JANET_FN(cfun_close_bus, "(sdbus/close-bus bus)", "Close a D-Bus connection.") {
  janet_fixarity(argc, 1);

  sd_bus **bus_ptr = janet_getabstract(argv, 0, &dbus_bus_type);
  sd_bus_flush(*bus_ptr);
  sd_bus_close(*bus_ptr);

  return janet_wrap_nil();
}

JANET_FN(cfun_bus_is_open, "(sdbus/bus-is-open bus)",
         "Check if a D-Bus connection is open.") {
  janet_fixarity(argc, 1);

  sd_bus **bus_ptr = janet_getabstract(argv, 0, &dbus_bus_type);
  int check        = CALL_SD_BUS_FUNC(sd_bus_is_open, *bus_ptr);

  return janet_wrap_boolean(check);
}

JANET_FN(cfun_get_unique_name, "(sdbus/get-unique-name bus)",
         "Get the unique name of a D-Bus connection.") {
  janet_fixarity(argc, 1);

  sd_bus **bus_ptr = janet_getabstract(argv, 0, &dbus_bus_type);

  const char *name = NULL;
  CALL_SD_BUS_FUNC(sd_bus_get_unique_name, *bus_ptr, &name);

  return janet_cstringv(name);
}

JANET_FN(
    cfun_set_allow_interactive_authorization,
    "(sdbus/set-allow-interactive-authorization bus allow)",
    "Set whether to allow interactive authorization on a D-Bus connection.") {
  janet_fixarity(argc, 2);

  sd_bus **bus_ptr = janet_getabstract(argv, 0, &dbus_bus_type);
  int allow        = janet_getboolean(argv, 1);

  CALL_SD_BUS_FUNC(sd_bus_set_allow_interactive_authorization, *bus_ptr, allow);

  return janet_wrap_nil();
}

JANET_FN(cfun_list_names, "(sdbus/list-names bus)",
         "List registered names on a D-Bus connection.") {
  janet_fixarity(argc, 1);

  sd_bus **bus_ptr = janet_getabstract(argv, 0, &dbus_bus_type);

  char **acquired = NULL;
  CALL_SD_BUS_FUNC(sd_bus_list_names, *bus_ptr, &acquired, NULL);

  JanetArray *list = janet_array(1);
  for (char **p = acquired; *p; p++) {
    Janet name = janet_cstringv(*p);
    free(*p);

    // TODO: janet_array_push may panic leading to a memory leak
    janet_array_push(list, name);
  }

  free(acquired);
  return janet_wrap_array(list);
}

JANET_FN(cfun_call, "(sdbus/call bus message)", "Call a D-Bus method.") {
  janet_fixarity(argc, 2);

  sd_bus **bus_ptr         = janet_getabstract(argv, 0, &dbus_bus_type);
  sd_bus_message **msg_ptr = janet_getabstract(argv, 1, &dbus_message_type);

  // Initialize via an abstract type so that Janet's GC can clean up
  // on panic.
  sd_bus_error *error = janet_abstract(&dbus_error_type, sizeof(sd_bus_error));
  *error              = SD_BUS_ERROR_NULL;

  sd_bus_message **reply =
      janet_abstract(&dbus_message_type, sizeof(sd_bus_message *));

  int rv = sd_bus_call(*bus_ptr, *msg_ptr, 0, error, reply);
  if (rv < 0) {
    if (sd_bus_error_get_errno(error)) {
      const char *fmt = (error->message) ? "D-Bus method call failed: %s: %s"
                                         : "D-Bus method call failed: %s";

      JanetString str = janet_formatc(fmt, error->name, error->message);
      sd_bus_error_free(error);
      janet_panics(str);
    }

    janet_panicf("D-Bus method call failed: %s", strerror(-rv));
  }

  return janet_wrap_abstract(reply);
}

JanetRegExt cfuns_bus[] = { JANET_REG("open-user-bus", cfun_open_user_bus),
                            JANET_REG("open-system-bus", cfun_open_system_bus),
                            JANET_REG("close-bus", cfun_close_bus),
                            JANET_REG("bus-is-open", cfun_bus_is_open),
                            JANET_REG("get-unique-name", cfun_get_unique_name),
                            JANET_REG("set-allow-interactive-authorization",
                                      cfun_set_allow_interactive_authorization),
                            JANET_REG("list-names", cfun_list_names),
                            JANET_REG("call", cfun_call),
                            JANET_REG_END };
