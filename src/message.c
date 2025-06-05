// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Joshua Krusell

#include <stdbool.h>
#include <string.h>

#include "common.h"

#define INIT_PARSER(msg, signature)                                            \
  ((Parser) {                                                                  \
      (msg),                                                                   \
      (signature),                                                             \
      (signature),                                                             \
  })

#define MESSAGE_PEEK(msg, type, contents) \
  CALL_SD_BUS_FUNC(sd_bus_message_peek_type, (msg), (type), (contents))

#define DBUS_TO_JANET_NUM(janet_type, c_type, dbus_type)                       \
  do {                                                                         \
    c_type x;                                                                  \
    CALL_SD_BUS_FUNC(sd_bus_message_read_basic, msg, dbus_type, &x);           \
    return janet_wrap_##janet_type(x);                                         \
  } while (0)

#define DBUS_TO_JANET_STR(dbus_type)                                           \
  do {                                                                         \
    const char *x;                                                             \
    CALL_SD_BUS_FUNC(sd_bus_message_read_basic, msg, dbus_type, &x);           \
    return janet_cstringv(x);                                                  \
  } while (0)

typedef struct {
  sd_bus_message *msg;
  const char *signature;
  const char *cursor;
} Parser;

int gc_sdbus_message(void *, size_t);
const JanetAbstractType dbus_message_type = { .name = "sdbus/message",
                                              .gc = gc_sdbus_message,
                                              JANET_ATEND_GC };

int gc_sdbus_message(void *data, size_t len) {
  UNUSED(len);

  sd_bus_message_unrefp((sd_bus_message **) data);
  *((sd_bus_message **) data) = NULL;

  return 0;
}

JANET_FN(cfun_message_unref, "(sdbus/message-unref msg)",
         "Deallocate a D-Bus message.") {
  janet_fixarity(argc, 1);
  sd_bus_message **msg_ptr = janet_getabstract(argv, 0, &dbus_message_type);

  sd_bus_message_unrefp(msg_ptr);
  *msg_ptr = NULL;

  return janet_wrap_nil();
}

JANET_FN(
    cfun_message_new_method_call,
    "(sdbus/message-new-method-call bus destination path interface member)",
    "Create a new D-Bus method call message.") {
  janet_fixarity(argc, 5);

  sd_bus_message *msg = NULL;

  sd_bus **bus_ptr = janet_getabstract(argv, 0, &dbus_bus_type);
  const char *destination = janet_getcstring(argv, 1);
  const char *path = janet_getcstring(argv, 2);
  const char *interface = janet_getcstring(argv, 3);
  const char *member = janet_getcstring(argv, 4);

  int rv = sd_bus_message_new_method_call(*bus_ptr, &msg, destination, path,
                                          interface, member);
  if (rv < 0) {
    sd_bus_message_unref(msg);
    janet_panicf("failed to create message: %s", strerror(-rv));
  }

  sd_bus_message **msg_ptr = (sd_bus_message **) janet_abstract(
      &dbus_message_type, sizeof(sd_bus_message *));
  *msg_ptr = msg;

  return janet_wrap_abstract(msg_ptr);
}

JANET_FN(cfun_message_seal, "(sdbus/message-seal)", "Seal a message") {
  janet_fixarity(argc, 1);

  sd_bus_message **msg_ptr = janet_getabstract(argv, 0, &dbus_message_type);

  CALL_SD_BUS_FUNC(sd_bus_message_seal, *msg_ptr, 0, 0);
  return janet_wrap_nil();
}

bool is_basic_type(int ch) {
  return strchr("bnqiuxtdsog", ch) != NULL;
}

int peek(Parser *p) {
  return (p->cursor && *p->cursor) ? *(p->cursor + 1) : -1;
}

int next(Parser *p) {
  return (p->cursor && *p->cursor) ? *++p->cursor : -1;
}

int cursor(Parser *p) {
  return (p->cursor) ? *p->cursor : -1;
}

void reset(Parser *p) {
  p->cursor = p->signature;
}

void append_complete_type(Parser *, Janet);
void append_data(sd_bus_message *, const char *, Janet *, int32_t);
void append_basic_type(Parser *, Janet);
void append_variant_type(Parser *, Janet);
void append_struct_type(Parser *, Janet);
void append_array_type(Parser *, Janet);
void append_dict_type(Parser *, Janet);

JANET_FN(cfun_message_append, "(sdbus/message-append msg signature & args)",
         "Append arguments to a D-Bus message.") {
  janet_arity(argc, 3, -1);

  sd_bus_message **msg_ptr = janet_getabstract(argv, 0, &dbus_message_type);
  const char *signature = janet_getcstring(argv, 1);

  append_data(*msg_ptr, signature, argv + 2, argc - 2);

  return janet_wrap_nil();
}

void append_data(sd_bus_message *msg, const char *signature, Janet *args,
                 int32_t n) {
  Parser p = INIT_PARSER(msg, signature);
  for (int32_t i = 0; i < n; i++) {
    append_complete_type(&p, args[i]);
    next(&p);
  }
}

void append_complete_type(Parser *p, Janet arg) {
  int ch = cursor(p);
  if (is_basic_type(ch))
    append_basic_type(p, arg);
  else if (ch == 'v')
    append_variant_type(p, arg);
  else if (ch == '(')
    append_struct_type(p, arg);
  else if (ch == 'a')
    append_array_type(p, arg);
  else
    janet_panicf("Unsupported argument type: %c", ch);
}

size_t match(const char *str, int open, int close) {
  int depth = 1;
  size_t length = 0;

  int ch;
  while ((ch = *++str)) {
    if (ch == open)
      depth++;
    else if (ch == close)
      depth--;

    length++;
    if (depth == 0)
      return length;
  }

  janet_panicf("Unmatched %c in signature", open);
}

size_t find_complete_type(const char *signature) {
  int ch;
  if (!(ch = *signature))
    janet_panicf("Missing array signature");

  if (ch == 'a')
    return 1 + find_complete_type(signature + 1);
  else if (ch == '{')
    return match(signature, '{', '}');
  else if (ch == '(')
    return match(signature, '(', ')');
  else
    return 1;
}

void append_dict_type(Parser *p, Janet arg) {
  size_t end;
  if ((end = match(p->cursor, '{', '}')) < 3)
    janet_panicf("Incomplete dictionary signature: %s", p->cursor);

  char *dict_sig = janet_scalloc(end + 2, sizeof(char));
  memcpy(dict_sig, p->cursor, end + 1);

  if (!is_basic_type(*(dict_sig + 1)))
    janet_panicf("Dict signature key must be a basic type: %s", dict_sig);

  CALL_SD_BUS_FUNC(sd_bus_message_open_container, p->msg, SD_BUS_TYPE_ARRAY, dict_sig);

  // Strip opening/closing braces
  memmove(dict_sig, dict_sig + 1, end - 1);
  dict_sig[end - 1] = '\0';

  JanetTable *tbl = janet_gettable(&arg, 0);
  if (tbl->count == 0)
    janet_panic("Missing arguments");

  Parser dict_parser = INIT_PARSER(p->msg, dict_sig);
  for (int32_t i = 0; i < tbl->count; i++) {
    CALL_SD_BUS_FUNC(sd_bus_message_open_container, p->msg,
                     SD_BUS_TYPE_DICT_ENTRY, dict_sig);

    // Append entry key - basic type
    append_basic_type(&dict_parser, tbl->data[i].key);

    // Append entry value - complete type
    next(&dict_parser);
    append_complete_type(&dict_parser, tbl->data[i].value);

    // Reset cursor back to 'key'
    dict_parser.cursor = dict_sig;

    CALL_SD_BUS_FUNC(sd_bus_message_close_container, p->msg);
  }

  CALL_SD_BUS_FUNC(sd_bus_message_close_container, p->msg);
  janet_sfree(dict_sig);
}

void append_array_type(Parser *p, Janet arg) {
  if (*(p->cursor + 1) == '{') {
    p->cursor++;
    return append_dict_type(p, arg);
  }

  JanetArray *array = janet_getarray(&arg, 0);

  size_t end = find_complete_type(p->cursor);
  char *array_sig = janet_scalloc(end + 1, sizeof(char));
  memcpy(array_sig, p->cursor + 1, end);

  p->cursor += end;

  CALL_SD_BUS_FUNC(sd_bus_message_open_container, p->msg, SD_BUS_TYPE_ARRAY,
                   array_sig);

  Parser array_parser = INIT_PARSER(p->msg, array_sig);
  for (int32_t i = 0; i < array->count; i++) {
    append_complete_type(&array_parser, array->data[i]);

    // Reset - container types may move our cursor
    array_parser.cursor = array_parser.signature;
  }

  CALL_SD_BUS_FUNC(sd_bus_message_close_container, p->msg);
  janet_sfree(array_sig);
}

void append_struct_type(Parser *p, Janet arg) {
  const Janet *tuple = janet_gettuple(&arg, 0);
  int32_t length = janet_tuple_length(tuple);

  size_t end = 0;
  if ((end = match(p->cursor, '(', ')')) < 2)
    janet_panicf("Missing struct signature contents: %s", p->cursor);

  char *struct_sig = janet_scalloc(end - 1, sizeof(char));
  memcpy(struct_sig, p->cursor + 1, end - 1);

  p->cursor += end;

  CALL_SD_BUS_FUNC(sd_bus_message_open_container, p->msg, SD_BUS_TYPE_STRUCT, struct_sig);

  Parser struct_parser = INIT_PARSER(p->msg, struct_sig);
  for (int32_t i = 0; i < length; i++) {
    if (!*struct_parser.cursor)
      janet_panic("Mismatched lengths in struct signature and arguments");

    append_complete_type(&struct_parser, tuple[i]);
    next(&struct_parser);
  }

  CALL_SD_BUS_FUNC(sd_bus_message_close_container, p->msg);
}

void append_variant_type(Parser *p, Janet arg) {
  const Janet *tuple = janet_gettuple(&arg, 0);
  if (janet_tuple_length(tuple) != 2)
    janet_panicf("Variant type expects exactly 2 arguments");

  const char *variant_signature = janet_getcstring(tuple, 0);
  const Janet variant_arg = tuple[1];

  Parser variant_parser = INIT_PARSER(p->msg, variant_signature);

  CALL_SD_BUS_FUNC(sd_bus_message_open_container, p->msg, SD_BUS_TYPE_VARIANT,
                   variant_signature);
  append_complete_type(&variant_parser, variant_arg);
  CALL_SD_BUS_FUNC(sd_bus_message_close_container, p->msg);
}

void append_basic_type(Parser *p, Janet arg) {
  switch (cursor(p)) {
    case 'b': // boolean
      CALL_SD_BUS_FUNC(sd_bus_message_append, p->msg, "b",
                       janet_getboolean(&arg, 0));
      break;
    case 'n': // int16_t
      CALL_SD_BUS_FUNC(sd_bus_message_append, p->msg, "n",
                       janet_getinteger16(&arg, 0));
      break;
    case 'q': // uint16_t
      CALL_SD_BUS_FUNC(sd_bus_message_append, p->msg, "q",
                       janet_getuinteger16(&arg, 0));
      break;
    case 'i': // int32_t
      CALL_SD_BUS_FUNC(sd_bus_message_append, p->msg, "i",
                       janet_getinteger(&arg, 0));
      break;
    case 'u': // uint32_t
      CALL_SD_BUS_FUNC(sd_bus_message_append, p->msg, "u",
                       janet_getuinteger(&arg, 0));
      break;
    case 'x': // int64_t
      CALL_SD_BUS_FUNC(sd_bus_message_append, p->msg, "x",
                       janet_getinteger64(&arg, 0));
      break;
    case 't': // uint64_t
      CALL_SD_BUS_FUNC(sd_bus_message_append, p->msg, "t",
                       janet_getuinteger64(&arg, 0));
      break;
    case 'd': // double
      CALL_SD_BUS_FUNC(sd_bus_message_append, p->msg, "d",
                       janet_getnumber(&arg, 0));
      break;
    case 's': // string
    case 'o': // object path
    case 'g': // signature
      CALL_SD_BUS_FUNC(sd_bus_message_append, p->msg, "s",
                       janet_getcstring(&arg, 0));
      break;
  }
}

Janet read_complete_type(sd_bus_message *, char, const char *);
Janet read_basic_type(sd_bus_message *, char);
Janet read_variant_type(sd_bus_message *, const char *);
Janet read_struct_type(sd_bus_message *, const char *);
Janet read_array_type(sd_bus_message *, const char *);
Janet read_dict_type(sd_bus_message *, const char *);

JANET_FN(cfun_message_read, "(sdbus/message-read msg &opt what)",
         "Read a single complete type from a D-Bus message") {
  janet_fixarity(argc, 1);

  sd_bus_message **msg_ptr = janet_getabstract(argv, 0, &dbus_message_type);
  char type;
  const char *contents = NULL;

  // Follow file/read and return nil on eof
  if (MESSAGE_PEEK(*msg_ptr, &type, &contents) == 0)
    return janet_wrap_nil();

  return read_complete_type(*msg_ptr, type, contents);
}

JANET_FN(cfun_message_read_all, "(sdbus/message-read-all msg)",
         "Read all contents of a D-Bus message and return them as a tuple.") {
  janet_fixarity(argc, 1);

  sd_bus_message **msg_ptr = janet_getabstract(argv, 0, &dbus_message_type);
  char type;
  const char *contents = NULL;

  JanetArray *array = janet_array(1);
  while (MESSAGE_PEEK(*msg_ptr, &type, &contents) > 0)
    janet_array_push(array, read_complete_type(*msg_ptr, type, contents));

  return janet_wrap_array(array);
}

Janet read_complete_type(sd_bus_message *msg, char type, const char *contents) {
  if (is_basic_type(type))
    return read_basic_type(msg, type);
  else if (type == SD_BUS_TYPE_VARIANT)
    return read_variant_type(msg, contents);
  else if (type == SD_BUS_TYPE_STRUCT)
    return read_struct_type(msg, contents);
  else if (type == 'a' && *contents == '{')
    return read_dict_type(msg, contents);
  else if (type == 'a')
    return read_array_type(msg, contents);
  else
    janet_panicf("Unsupported message type: %c", type);
}

Janet read_variant_type(sd_bus_message *msg, const char *contents) {
  char type;
  const char *subcontents = NULL;

  CALL_SD_BUS_FUNC(sd_bus_message_enter_container, msg, SD_BUS_TYPE_VARIANT,
                   contents);
  if (MESSAGE_PEEK(msg, &type, &subcontents) == 0)
    janet_panicf("Unexpected end of variant type");

  Janet obj = read_complete_type(msg, type, subcontents);
  CALL_SD_BUS_FUNC(sd_bus_message_exit_container, msg);

  Janet sig = janet_cstringv(contents);
  JanetTuple tuple = janet_tuple_n((const Janet[]) { sig, obj }, 2);

  return janet_wrap_tuple(tuple);
}

Janet read_struct_type(sd_bus_message *msg, const char *contents) {
  JanetArray *array = janet_array(1);
  char type;
  const char *subcontents = NULL;

  CALL_SD_BUS_FUNC(sd_bus_message_enter_container, msg, SD_BUS_TYPE_STRUCT,
                   contents);

  while (MESSAGE_PEEK(msg, &type, &subcontents) > 0) {
    Janet obj = read_complete_type(msg, type, subcontents);
    janet_array_push(array, obj);
  }

  CALL_SD_BUS_FUNC(sd_bus_message_exit_container, msg);

  return janet_wrap_tuple(janet_tuple_n(array->data, array->count));
}

Janet read_dict_type(sd_bus_message *msg, const char *contents) {
  JanetTable *tbl = janet_table(1);
  char type;
  const char *subcontents = NULL;

  CALL_SD_BUS_FUNC(sd_bus_message_enter_container, msg, SD_BUS_TYPE_ARRAY,
                   contents);

  while (MESSAGE_PEEK(msg, &type, &subcontents) > 0) {
    CALL_SD_BUS_FUNC(sd_bus_message_enter_container, msg,
                     SD_BUS_TYPE_DICT_ENTRY, subcontents);
    Janet key = read_basic_type(msg, subcontents[0]);

    MESSAGE_PEEK(msg, &type, &subcontents);
    Janet value = read_complete_type(msg, type, subcontents);

    CALL_SD_BUS_FUNC(sd_bus_message_exit_container, msg);
    janet_table_put(tbl, key, value);
  }

  CALL_SD_BUS_FUNC(sd_bus_message_exit_container, msg);
  return janet_wrap_table(tbl);
}

Janet read_array_type(sd_bus_message *msg, const char *contents) {
  JanetArray *array = janet_array(1);
  char type;
  const char *subcontents = NULL;

  CALL_SD_BUS_FUNC(sd_bus_message_enter_container, msg, SD_BUS_TYPE_ARRAY,
                   contents);

  while (MESSAGE_PEEK(msg, &type, &subcontents) > 0) {
    Janet obj = read_complete_type(msg, type, subcontents);
    janet_array_push(array, obj);
  }

  CALL_SD_BUS_FUNC(sd_bus_message_exit_container, msg);

  return janet_wrap_array(array);
}

Janet read_basic_type(sd_bus_message *msg, char type) {
  switch (type) {
    case 'b': // Boolean
      DBUS_TO_JANET_NUM(boolean, int, 'b');
    case 'n': // int16_t
      DBUS_TO_JANET_NUM(number, int16_t, 'n');
    case 'q': // uint16_t
      DBUS_TO_JANET_NUM(number, uint16_t, 'q');
    case 'i': // int32_t
      DBUS_TO_JANET_NUM(number, int32_t, 'i');
    case 'u': // uint32_t
      DBUS_TO_JANET_NUM(number, uint32_t, 'u');
    case 'x': // int64_t
      DBUS_TO_JANET_NUM(s64, int64_t, 'x');
    case 't': // uint64_t
      DBUS_TO_JANET_NUM(u64, uint64_t, 't');
    case 'd': // double
      DBUS_TO_JANET_NUM(number, double, 'd');
    case 's':
      DBUS_TO_JANET_STR('s');
    case 'o':
      DBUS_TO_JANET_STR('o');
    case 'g':
      DBUS_TO_JANET_STR('g');
  }

  return janet_wrap_nil();
}

JANET_FN(cfun_message_dump, "(sdbus/message-dump msg)",
         "Dump a D-Bus message to stdout.") {
  janet_fixarity(argc, 1);
  sd_bus_message **msg_ptr = janet_getabstract(argv, 0, &dbus_message_type);

  sd_bus_message_dump(*msg_ptr, NULL, SD_BUS_MESSAGE_DUMP_WITH_HEADER);
  CALL_SD_BUS_FUNC(sd_bus_message_rewind, *msg_ptr, true);

  return janet_wrap_nil();
}

JanetRegExt cfuns_message[] = {
  JANET_REG("message-unref", cfun_message_unref),
  JANET_REG("message-new-method-call", cfun_message_new_method_call),
  JANET_REG("message-seal", cfun_message_seal),
  JANET_REG("message-append", cfun_message_append),
  JANET_REG("message-read", cfun_message_read),
  JANET_REG("message-read-all", cfun_message_read_all),
  JANET_REG("message-dump", cfun_message_dump),
  JANET_REG_END
};
