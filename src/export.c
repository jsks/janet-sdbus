// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Joshua Krusell

#include "common.h"

#define cstr(s) ((char *) janet_unwrap_string(s))

#define FREE_EXPORT_STATE(state)                                               \
  do {                                                                         \
    janet_gcunroot(state->bus);                                                \
    janet_gcunroot(state->members);                                            \
    janet_free(state->vtable);                                                 \
    janet_free(state);                                                         \
  } while (0)

typedef struct {
  sd_bus_vtable *vtable;
  Janet bus;
  Janet members;
} ExportState;

static ExportState *init_export_state(Conn *conn, sd_bus_vtable *vtable,
                                      Janet members) {
  ExportState *state;
  if (!(state = janet_malloc(sizeof(ExportState))))
    JANET_OUT_OF_MEMORY;

  *state = (ExportState) { .vtable  = vtable,
                           .bus     = janet_wrap_abstract(conn),
                           .members = members };

  janet_gcroot(state->bus);
  janet_gcroot(state->members);

  return state;
}

static Janet dict_symget(Janet dict, const char *key) {
  JanetDictView view;
  janet_dictionary_view(dict, &view.kvs, &view.len, &view.cap);

  Janet sym   = janet_ckeywordv(key);
  Janet value = janet_dictionary_get(view.kvs, view.cap, sym);
  if (janet_checktype(value, JANET_NIL))
    janet_panicf("Missing required field: %s", key);

  return value;
}

static uint64_t sd_bus_flags(JanetKeyword keys) {
  int32_t len   = janet_string_length(keys);
  uint64_t mask = 0;

  if (len == 0)
    return mask;

  for (int32_t i = 0; i < len; i++) {
    switch (keys[i]) {
      case 'd':
        mask |= SD_BUS_VTABLE_DEPRECATED;
        break;
      case 'h':
        mask |= SD_BUS_VTABLE_HIDDEN;
        break;
      case 's':
        mask |= SD_BUS_VTABLE_SENSITIVE;
        break;
      case 'n':
        mask |= SD_BUS_VTABLE_METHOD_NO_REPLY;
        break;
      case 'r':
        mask |= SD_BUS_VTABLE_PROPERTY_CONST;
        break;
      case 'e':
        mask |= SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE;
        break;
      case 'i':
        mask |= SD_BUS_VTABLE_PROPERTY_EMITS_INVALIDATION;
        break;
      case 'x':
        mask |= SD_BUS_VTABLE_PROPERTY_EXPLICIT;
        break;
      case 'w': // Property writeable flag
        break;
      default:
        janet_panicf("Unknown export flag: %c", keys[i]);
        break;
    }
  }

  return mask;
}

static int method_handler(sd_bus_message *msg, void *userdata,
                          sd_bus_error *ret_error) {
  const char *member = sd_bus_message_get_member(msg);
  ExportState *state = userdata;
  JanetDictView env;
  janet_dictionary_view(state->members, &env.kvs, &env.len, &env.cap);

  sd_bus_message **msg_ptr =
      janet_abstract(&dbus_message_type, sizeof(sd_bus_message *));
  *msg_ptr = sd_bus_message_ref(msg);

  janet_gcroot(janet_wrap_abstract(msg_ptr));

  Janet out, argv[] = { state->bus, janet_wrap_abstract(msg_ptr) };
  Janet method =
      janet_dictionary_get(env.kvs, env.cap, janet_ckeywordv(member));

  JanetFunction *f = janet_unwrap_function(dict_symget(method, "function"));

  // Method function may yield to the event-loop and return
  // JANET_SIGNAL_EVENT so let the fiber take care of sending the dbus
  // reply or any possible error messages. Alternatively, we could
  // block and wait on the method function fiber using a channel.
  JanetSignal signal = janet_pcall(f, 2, argv, &out, NULL);

  janet_gcunroot(janet_wrap_abstract(msg_ptr));

  if (signal == JANET_SIGNAL_ERROR) {
    return sd_bus_error_setf(ret_error, "org.janet.error",
                             "internal method error: %s",
                             (char *) janet_to_string(out));
  }

  return 1;
}

static int property_handler_core(const char *property, const char *method,
                                 sd_bus_message *msg, void *userdata,
                                 sd_bus_error *ret_error) {
  ExportState *state = userdata;
  JanetDictView env;
  janet_dictionary_view(state->members, &env.kvs, &env.len, &env.cap);

  Janet prop =
      janet_dictionary_get(env.kvs, env.cap, janet_ckeywordv(property));

  // When getting a property, `msg` is the reply, otherwise when
  // setting a property it is the value payload.
  sd_bus_message **msg_ptr =
      janet_abstract(&dbus_message_type, sizeof(sd_bus_message *));
  *msg_ptr = sd_bus_message_ref(msg);

  janet_gcroot(janet_wrap_abstract(msg_ptr));

  Janet out, argv[] = { prop, janet_wrap_abstract(msg_ptr) };
  JanetFunction *f = janet_unwrap_function(dict_symget(prop, method));

  // Normally we'd invoke an object method with `janet_mcall`;
  // however, the present function is run from inside the async
  // listener scheduled on the event-loop.
  JanetSignal signal = janet_pcall(f, 2, argv, &out, NULL);

  janet_gcunroot(janet_wrap_abstract(msg_ptr));

  if (signal == JANET_SIGNAL_ERROR) {
    return sd_bus_error_setf(ret_error, "org.janet.error",
                             "internal property error: %s",
                             (char *) janet_to_string(out));
  }

  return janet_checktype(out, JANET_NIL);
}

static int property_getter(sd_bus *bus, const char *path, const char *interface,
                           const char *property, sd_bus_message *msg,
                           void *userdata, sd_bus_error *ret_error) {
  UNUSED(bus);
  UNUSED(path);
  UNUSED(interface);

  property_handler_core(property, "getter", msg, userdata, ret_error);

  return 0;
}

static int property_setter(sd_bus *bus, const char *path, const char *interface,
                           const char *property, sd_bus_message *msg,
                           void *userdata, sd_bus_error *ret_error) {
  UNUSED(bus);
  UNUSED(path);
  UNUSED(interface);

  property_handler_core(property, "setter", msg, userdata, ret_error);

  return 0;
}

static int property_setter_with_signal(sd_bus *bus, const char *path,
                                       const char *interface,
                                       const char *property,
                                       sd_bus_message *msg, void *userdata,
                                       sd_bus_error *ret_error) {
  int rv = property_handler_core(property, "setter", msg, userdata, ret_error);
  if (!rv)
    CALL_SD_BUS_FUNC(sd_bus_emit_properties_changed, bus, path, interface,
                     property, NULL);

  return 0;
}

static void destroy_export_callback(void *userdata) {
  ExportState *state = userdata;

  FREE_EXPORT_STATE(state);
}

static sd_bus_vtable create_vtable_method(const char *name, Janet entry) {
  const char *in  = cstr(dict_symget(entry, "sig-in")),
             *out = cstr(dict_symget(entry, "sig-out"));

  Janet fun = dict_symget(entry, "function");
  if (!janet_checktype(fun, JANET_FUNCTION))
    janet_panicf("Expected function for method: %s", name);

  Janet flags       = dict_symget(entry, "flags");
  JanetKeyword keys = janet_unwrap_keyword(flags);
  uint64_t mask     = sd_bus_flags(keys);

  return (sd_bus_vtable) SD_BUS_METHOD(name, in, out, method_handler, mask);
}

static sd_bus_vtable create_vtable_property(const char *name, Janet entry) {
  const char *sig = cstr(dict_symget(entry, "sig"));

  Janet flags       = dict_symget(entry, "flags");
  JanetKeyword keys = janet_unwrap_keyword(flags);
  uint64_t mask     = sd_bus_flags(keys);

  bool writable = janet_unwrap_boolean(dict_symget(entry, "writable"));
  sd_bus_vtable property;
  if (writable && mask & (SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE |
                          SD_BUS_VTABLE_PROPERTY_EMITS_INVALIDATION))
    property = (sd_bus_vtable) SD_BUS_WRITABLE_PROPERTY(
        name, sig, property_getter, property_setter_with_signal, 0, mask);
  else if (writable)
    property = (sd_bus_vtable) SD_BUS_WRITABLE_PROPERTY(
        name, sig, property_getter, property_setter, 0, mask);
  else
    property =
        (sd_bus_vtable) SD_BUS_PROPERTY(name, sig, property_getter, 0, mask);

  return property;
}

static sd_bus_vtable create_vtable_signal(const char *name, Janet entry) {
  const char *sig = cstr(dict_symget(entry, "sig"));

  Janet flags       = dict_symget(entry, "flags");
  JanetKeyword keys = janet_unwrap_keyword(flags);
  uint64_t mask     = sd_bus_flags(keys);

  return (sd_bus_vtable) SD_BUS_SIGNAL(name, sig, mask);
}

static sd_bus_vtable *create_vtable(size_t len, JanetDictView dict) {
  sd_bus_vtable vtable[len];
  vtable[0] = (sd_bus_vtable) SD_BUS_VTABLE_START(0);

  const JanetKV *kv = NULL;
  size_t i          = 0;
  while ((kv = janet_dictionary_next(dict.kvs, dict.cap, kv))) {
    if (i >= len - 1)
      janet_panicf("Too many members for D-Bus interface: %d", len);

    const char *member = cstr(kv->key);
    if (sd_bus_member_name_is_valid(member) == 0)
      janet_panicf("Invalid D-Bus method name: %s", member);

    Janet type = dict_symget(kv->value, "type");
    if (janet_symeq(type, "method"))
      vtable[++i] = create_vtable_method(member, kv->value);
    else if (janet_symeq(type, "property"))
      vtable[++i] = create_vtable_property(member, kv->value);
    else if (janet_symeq(type, "signal"))
      vtable[++i] = create_vtable_signal(member, kv->value);
    else
      janet_panicf("Unknown D-Bus member type: %s", cstr(type));
  }

  vtable[len - 1] = (sd_bus_vtable) SD_BUS_VTABLE_END;

  // This is dumb, but janet_panic* will longjmp leading to a memory
  // leak for vtable.
  sd_bus_vtable *copy;
  if (!(copy = janet_malloc(len * sizeof(sd_bus_vtable))))
    JANET_OUT_OF_MEMORY;
  memcpy(copy, vtable, len * sizeof(sd_bus_vtable));

  return copy;
}

JANET_FN(cfun_export, "(sdbus/export bus path interface env)",
         "Register a D-Bus interface with the given bus.") {
  janet_fixarity(argc, 4);

  Conn *conn            = janet_getabstract(argv, 0, &dbus_bus_type);
  const char *path      = janet_getcstring(argv, 1);
  const char *interface = janet_getcstring(argv, 2);
  JanetDictView env     = janet_getdictionary(argv, 3);

  if (sd_bus_object_path_is_valid(path) == 0)
    janet_panicf("Invalid D-Bus object path: %s", path);

  if (sd_bus_interface_name_is_valid(interface) == 0)
    janet_panicf("Invalid D-Bus interface name: %s", interface);

  if (env.len == 0)
    janet_panicf("No members to register for interface: %s", interface);

  sd_bus_vtable *vtable = create_vtable(env.len + 2, env);
  ExportState *state    = init_export_state(conn, vtable, argv[3]);

  sd_bus_slot **slot_ptr =
      janet_abstract(&dbus_slot_type, sizeof(sd_bus_slot *));
  *slot_ptr = NULL;

  int rv = sd_bus_add_object_vtable(conn->bus, slot_ptr, path, interface,
                                    vtable, state);
  sd_bus_slot_set_floating(*slot_ptr, 1);

  if (rv < 0) {
    FREE_EXPORT_STATE(state);
    janet_panicf("failed to register D-Bus interface: %s", strerror(-rv));
  }

  sd_bus_slot_set_destroy_callback(*slot_ptr, destroy_export_callback);

  return janet_wrap_abstract(slot_ptr);
}

JanetRegExt cfuns_export[] = { JANET_REG("export", cfun_export),
                               JANET_REG_END };
