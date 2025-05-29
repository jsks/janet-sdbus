#include "common.h"

int dbus_bus_gc(void *, size_t);
int dbus_bus_get(void *, Janet, Janet *);
void dbus_bus_tostring(void *, JanetBuffer *);

const JanetAbstractType dbus_bus_type = {
    .name = "sdbus/bus",
    .gc = dbus_bus_gc,
    .gcmark = NULL,
    .get = dbus_bus_get,
    .put = NULL,
    .marshal = NULL,
    .unmarshal = NULL,
    .tostring = dbus_bus_tostring,
    JANET_ATEND_NEXT
};

JANET_CFUN(cfun_close_bus);

const JanetMethod dbus_bus_methods[] = {
  {"close", cfun_close_bus}
};

int dbus_bus_gc(void *p, size_t size) {
  UNUSED(size);

  sd_bus_flush_close_unrefp((sd_bus **) p);
  *((sd_bus **) p) = NULL;

  return 0;
}

int dbus_bus_get(void *p, Janet key, Janet *out) {
  UNUSED(p);
  if (!janet_checktype(key, JANET_KEYWORD))
    return 0;

  return janet_getmethod(janet_unwrap_keyword(key), dbus_bus_methods, out);
}

void dbus_bus_tostring(void *p, JanetBuffer *buffer) {
  const char *name = NULL;
  CALL_SD_BUS_FUNC(sd_bus_get_unique_name, *(sd_bus **)p, &name);

  janet_buffer_push_cstring(buffer, name);
}

JANET_FN(cfun_get_unique_name, "(sdbus/get-unique-name bus)", "Get the unique name of a D-Bus connection.") {
  janet_fixarity(argc, 1);

  sd_bus **slot = (sd_bus **) janet_getabstract(argv, 0, &dbus_bus_type);

  const char *name = NULL;
  CALL_SD_BUS_FUNC(sd_bus_get_unique_name, *slot, &name);

  return janet_cstringv(name);
}

JANET_FN(cfun_open_user_bus, "(sdbus/open-user-bus)", "Open a user D-Bus connection.") {
  UNUSED(argv);
  janet_fixarity(argc, 0);

  sd_bus **slot = janet_abstract(&dbus_bus_type, sizeof(sd_bus *));
  CALL_SD_BUS_FUNC(sd_bus_open_user, slot);

  return janet_wrap_abstract(slot);
}

JANET_FN(cfun_open_system_bus, "(sdbus/open-system-bus)", "Open a system D-Bus connection.") {
  UNUSED(argv);
  janet_fixarity(argc, 0);

  sd_bus **slot = janet_abstract(&dbus_bus_type, sizeof(sd_bus *));
  CALL_SD_BUS_FUNC(sd_bus_open_system, slot);

  return janet_wrap_abstract(slot);
}

JANET_FN(cfun_set_allow_interactive_authorization,
    "(sdbus/set-allow-interactive-authorization bus allow)",
         "Set whether to allow interactive authorization on a D-Bus connection.") {
  janet_fixarity(argc, 2);

  sd_bus **slot = janet_getabstract(argv, 0, &dbus_bus_type);
  int allow = janet_getboolean(argv, 1);

  CALL_SD_BUS_FUNC(sd_bus_set_allow_interactive_authorization, *slot, allow);

  return janet_wrap_nil();
}

JANET_FN(cfun_close_bus, "(sdbus/close-bus bus)", "Close a D-Bus connection.") {
  janet_fixarity(argc, 1);

  sd_bus **slot = janet_getabstract(argv, 0, &dbus_bus_type);
  sd_bus_flush_close_unref(*slot);
  *slot = NULL;

  return janet_wrap_nil();
}

JANET_FN(cfun_list_names, "(sdbus/list-names bus)",
         "List registered names on a D-Bus connection.") {
  janet_fixarity(argc, 1);

  sd_bus **slot = janet_getabstract(argv, 0, &dbus_bus_type);

  char **acquired = NULL;
  CALL_SD_BUS_FUNC(sd_bus_list_names, *slot, &acquired, NULL);

  JanetArray *list = janet_array(5);
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

  sd_bus **slot = janet_getabstract(argv, 0, &dbus_bus_type);
  sd_bus_message **msg_ptr = janet_getabstract(argv, 1, &dbus_message_type);

  sd_bus_error *error = NULL;
  sd_bus_message *reply = NULL;

  CALL_SD_BUS_FUNC(sd_bus_call, *slot, *msg_ptr, 0, error, &reply);
  sd_bus_message_dump(reply, NULL, SD_BUS_MESSAGE_DUMP_WITH_HEADER);

  return janet_wrap_nil();
}

JanetRegExt cfuns_bus[] = {
    JANET_REG("get-unique-name", cfun_get_unique_name),
    JANET_REG("open-user-bus", cfun_open_user_bus),
    JANET_REG("open-system-bus", cfun_open_system_bus),
    JANET_REG("set-allow-interactive-authorization", cfun_set_allow_interactive_authorization),
    JANET_REG("close-bus", cfun_close_bus),
    JANET_REG("list-names", cfun_list_names),
    JANET_REG("call", cfun_call),
    JANET_REG_END
};
