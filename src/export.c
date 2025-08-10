// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Joshua Krusell

#include "async.h"
#include "common.h"

typedef struct {
  Conn *conn;
  Janet methods;
} ExportCallbackState;

static inline Janet struct_symget(JanetStruct st, const char *key) {
  Janet sym   = janet_ckeywordv(key);
  Janet value = janet_struct_rawget(st, sym);
  if (janet_checktype(value, JANET_NIL))
    janet_panicf("Missing required field: %s", key);

  return value;
}

static int method_handler(sd_bus_message *msg, void *userdata,
                          sd_bus_error *ret_error) {
  const char *member         = sd_bus_message_get_member(msg);
  ExportCallbackState *state = userdata;
  JanetDictView env;
  janet_dictionary_view(state->methods, &env.kvs, &env.len, &env.cap);

  sd_bus_message **msg_ptr =
      janet_abstract(&dbus_message_type, sizeof(sd_bus_message *));
  *msg_ptr = sd_bus_message_ref(msg);

  janet_gcroot(janet_wrap_abstract(msg_ptr));

  Janet out, argv[] = { janet_wrap_abstract(msg_ptr) };
  Janet method =
            janet_dictionary_get(env.kvs, env.cap, janet_ckeywordv(member)),
        fun = struct_symget(janet_unwrap_struct(method), "fun");

  JanetSignal sig =
      janet_pcall(janet_unwrap_function(fun), 1, argv, &out, NULL);

  janet_gcunroot(janet_wrap_abstract(msg_ptr));

  if (sig == JANET_SIGNAL_ERROR)
    return sd_bus_error_setf(ret_error, "org.janet.error",
                             "Failed to call method: %s", member);

  sd_bus_message **reply = janet_unwrap_abstract(out);
  sd_bus_message_send(*reply);

  return 0;
}

static void destroy_export_callback(void *userdata) {
  ExportCallbackState *state = userdata;

  state->conn->subscribers--;
  janet_gcunroot(state->methods);

  janet_free(state);
}

static sd_bus_vtable create_vtable_method(JanetString name, JanetStruct entry) {
  Janet in  = struct_symget(entry, "sig-in"),
        out = struct_symget(entry, "sig-out");

  Janet fun = struct_symget(entry, "fun");
  if (!janet_checktype(fun, JANET_FUNCTION))
    janet_panicf("Expected function for method: %s", name);

  return (sd_bus_vtable) SD_BUS_METHOD(
      (char *) name, (char *) janet_unwrap_string(in),
      (char *) janet_unwrap_string(out), method_handler, 0);
  // return (sd_bus_vtable) SD_BUS_METHOD("example", "i", "i", method_handler,
  // 0);
}

static sd_bus_vtable *create_vtable(size_t len, JanetDictView dict) {
  sd_bus_vtable *vtable;
  if (!(vtable = janet_calloc(len, sizeof(sd_bus_vtable))))
    JANET_OUT_OF_MEMORY;

  vtable[0] = (sd_bus_vtable) SD_BUS_VTABLE_START(0);

  const JanetKV *kv = NULL;
  size_t i          = 0;
  while ((kv = janet_dictionary_next(dict.kvs, dict.cap, kv))) {
    if (i >= len - 1)
      janet_panicf("Too many methods for D-Bus interface: %d", len);

    JanetString member = janet_unwrap_string(kv->key);
    JanetStruct entry  = janet_unwrap_struct(kv->value);

    vtable[++i] = create_vtable_method(member, entry);
  }

  vtable[len - 1] = (sd_bus_vtable) SD_BUS_VTABLE_END;
  return vtable;
}

JANET_FN(cfun_export, "(sdbus/export bus path interface env)",
         "Register a D-Bus service with the given bus.") {
  janet_fixarity(argc, 4);

  Conn *conn            = janet_getabstract(argv, 0, &dbus_bus_type);
  const char *path      = janet_getcstring(argv, 1);
  const char *interface = janet_getcstring(argv, 2);
  JanetDictView dict    = janet_getdictionary(argv, 3);

  if (dict.len == 0)
    janet_panicf("No methods to register for interface: %s", interface);

  sd_bus_vtable *vtable = create_vtable(dict.len + 2, dict);

  ExportCallbackState *state;
  if (!(state = janet_malloc(sizeof(ExportCallbackState))))
    JANET_OUT_OF_MEMORY;
  *state = (ExportCallbackState) { .conn = conn, .methods = argv[3] };
  janet_gcroot(state->methods);

  sd_bus_slot **slot_ptr =
      janet_abstract(&dbus_slot_type, sizeof(sd_bus_slot *));
  *slot_ptr = NULL;

  int rv = sd_bus_add_object_vtable(conn->bus, slot_ptr, path, interface,
                                    vtable, state);
  if (rv < 0) {
    janet_gcunroot(state->methods);
    janet_free(vtable);
    janet_free(state);
    janet_panicf("Failed to register D-Bus service: %s", strerror(-rv));
  }

  sd_bus_slot_set_destroy_callback(*slot_ptr, destroy_export_callback);
  start_async_listener(conn);
  conn->subscribers++;

  return janet_wrap_abstract(slot_ptr);
}

JanetRegExt cfuns_export[] = { JANET_REG("export", cfun_export),
                               JANET_REG_END };
