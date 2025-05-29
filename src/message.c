#include <stdbool.h>
#include <string.h>

#include "common.h"

#define INIT_PARSER(msg, signature) \
  ((Parser) { \
    (msg), \
    (signature), \
    (signature), \
  })

typedef struct {
  sd_bus_message *msg;
  const char *signature;
  const char *cursor;
} Parser;

int gc_sdbus_message(void *, size_t);
const JanetAbstractType dbus_message_type = {
    .name = "sdbus/message",
    .gc = gc_sdbus_message,
    JANET_ATEND_GC
};

int gc_sdbus_message(void *data, size_t len) {
  UNUSED(len);

  sd_bus_message_unrefp((sd_bus_message **) data);
  *((sd_bus_message **) data) = NULL;

  return 0;
}

JANET_FN(cfun_message_unref, "(sdbus/message-unref msg)", "Deallocate a D-Bus message.") {
  janet_fixarity(argc, 1);
  sd_bus_message **msg_ptr = (sd_bus_message **) janet_getabstract(argv, 0, &dbus_message_type);

  sd_bus_message_unrefp(msg_ptr);
  *msg_ptr = NULL;

  return janet_wrap_nil();
}

JANET_FN(cfun_message_new_method_call,
         "(sdbus/message-new-method-call bus destination path interface member)",
         "Create a new D-Bus method call message.") {
  janet_fixarity(argc, 5);

  sd_bus_message *msg = NULL;

  sd_bus **bus_ptr = (sd_bus **) janet_getabstract(argv, 0, &dbus_bus_type);
  const char *destination = janet_getcstring(argv, 1);
  const char *path = janet_getcstring(argv, 2);
  const char *interface = janet_getcstring(argv, 3);
  const char *member = janet_getcstring(argv, 4);

  int rv = sd_bus_message_new_method_call(*bus_ptr, &msg, destination, path, interface, member);
  if (rv < 0) {
    sd_bus_message_unref(msg);
    janet_panicf("failed to create message: %s", strerror(-rv));
  }

  sd_bus_message **msg_ptr = (sd_bus_message **) janet_abstract(&dbus_message_type, sizeof(sd_bus_message *));
  *msg_ptr = msg;

  return janet_wrap_abstract(msg_ptr);
}

bool is_basic_type(int ch) {
  return !strchr("bnqiuxtdsogh", ch);
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

JANET_FN(cfun_message_append,
         "(sdbus/message-append msg signature args)",
         "Append arguments to a D-Bus message.") {
  janet_arity(argc, 3, -1);

  sd_bus_message **msg_ptr = (sd_bus_message **) janet_getabstract(argv, 0, &dbus_message_type);
  const char *signature = janet_getcstring(argv, 1);

  append_data(*msg_ptr, signature, argv + 2, argc - 2);

  return janet_wrap_nil();
}

void append_data(sd_bus_message *msg, const char *signature, Janet *args, int32_t n) {
  Parser p = INIT_PARSER(msg, signature);
  for (int32_t i = 0; i < n; i++)
    append_complete_type(&p, args[i]);
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
    return 1 + find_complete_type(signature++);
  else if (ch == '(')
    return match(signature, '(', ')');
  else
    return 1;
}

void append_dict_type(Parser *p, Janet arg) {
  JanetTable *tbl = janet_gettable(&arg, 0);

  size_t end;
  if ((end = match(p->signature, '{', '}')) < 2)
    janet_panicf("Invalid dictionary signature: %.*s", end, p->signature);

  // Omit matching braces to extract key and value types
  char *dict_sig = (char *) janet_scalloc(end - 1, sizeof(char));
  memcpy(dict_sig, p->signature + 1, end - 2);

  p->cursor += end;

  if (!is_basic_type(*dict_sig))
    janet_panicf("Dict signature key must be a basic type: %c", *dict_sig);

  Parser dict_parser = INIT_PARSER(p->msg, dict_sig);
  for (int32_t i = 0; i < tbl->count; i++) {
    CALL_SD_BUS_FUNC(sd_bus_message_open_container, p->msg, SD_BUS_TYPE_DICT_ENTRY, dict_sig);

    // Append entry key - basic type
    append_basic_type(&dict_parser, tbl->data[i].key);

    // Append entry value - complete type
    next(&dict_parser);
    append_complete_type(&dict_parser, tbl->data[i].value);

    CALL_SD_BUS_FUNC(sd_bus_message_close_container, p->msg);
  }
}

void append_array_type(Parser *p, Janet arg) {
  JanetArray *array = janet_getarray(&arg, 0);

  if (cursor(p) == '{')
    return append_dict_type(p, arg);

  size_t end = find_complete_type(p->signature);
  char *array_sig = (char *) janet_scalloc(end + 1, sizeof(char));
  memcpy(array_sig, p->signature, end);

  p->cursor += end;

  CALL_SD_BUS_FUNC(sd_bus_message_open_container, p->msg, SD_BUS_TYPE_ARRAY, array_sig);

  Parser array_parser = INIT_PARSER(p->msg, array_sig);
  for (int32_t i = 0; i < array->count; i++) {
    append_complete_type(&array_parser, array->data[i]);
    reset(&array_parser);
  }

  CALL_SD_BUS_FUNC(sd_bus_message_close_container, p->msg);
  janet_sfree(array_sig);
}

void append_struct_type(Parser *p, Janet arg) {
  const Janet *tuple = janet_gettuple(&arg, 0);
  int32_t length = janet_tuple_length(tuple);

  CALL_SD_BUS_FUNC(sd_bus_message_open_container, p->msg, SD_BUS_TYPE_STRUCT, NULL);

  int ch;
  for (int32_t i = 0; i < length; i++) {
    if ((ch = next(p)) == -1 || ch == ')')
      janet_panicf("Mismatched lengths in struct signature and arguments");

    append_complete_type(p, tuple[i]);
  }

  if (next(p) != ')')
    janet_panicf("Missing terminating ')' in struct signature");

  CALL_SD_BUS_FUNC(sd_bus_message_close_container, p->msg);
}

void append_variant_type(Parser *p, Janet arg) {
  const Janet *tuple = janet_gettuple(&arg, 0);
  if (janet_tuple_length(tuple) != 2)
    janet_panicf("Variant type expects exactly 2 arguments");

  const char *variant_signature = janet_getcstring(tuple, 0);
  const Janet variant_arg = tuple[1];

  Parser variant_parser = {.msg = p->msg, .signature = variant_signature,};

  CALL_SD_BUS_FUNC(sd_bus_message_open_container, p->msg, SD_BUS_TYPE_VARIANT, variant_signature);
  append_complete_type(&variant_parser, variant_arg);
  CALL_SD_BUS_FUNC(sd_bus_message_close_container, p->msg);
}

void append_basic_type(Parser *p, Janet arg) {
  switch (cursor(p)) {
  case 'b': // boolean
    CALL_SD_BUS_FUNC(sd_bus_message_append, p->msg, "b", janet_getboolean(&arg, 0));
    break;
  case 'n': // int16_t
    CALL_SD_BUS_FUNC(sd_bus_message_append, p->msg, "n", janet_getinteger16(&arg, 0));
    break;
  case 'q': // uint16_t
    CALL_SD_BUS_FUNC(sd_bus_message_append, p->msg, "q", janet_getuinteger16(&arg, 0));
    break;
  case 'i': // int30_t
    CALL_SD_BUS_FUNC(sd_bus_message_append, p->msg, "i", janet_getinteger(&arg, 0));
    break;
  case 'u': // uint30_t
    CALL_SD_BUS_FUNC(sd_bus_message_append, p->msg, "u", janet_getuinteger(&arg, 0));
    break;
  case 'x': // int64_t
    CALL_SD_BUS_FUNC(sd_bus_message_append, p->msg, "x", janet_getinteger64(&arg, 0));
    break;
  case 't': // uint64_t
    CALL_SD_BUS_FUNC(sd_bus_message_append, p->msg, "t", janet_getuinteger64(&arg, 0));
    break;
  case 'd': // double
    CALL_SD_BUS_FUNC(sd_bus_message_append, p->msg, "d", janet_getnumber(&arg, 0));
    break;
  case 's': // string
  case 'o': // object path
  case 'g': // signature
    CALL_SD_BUS_FUNC(sd_bus_message_append, p->msg, "s", janet_getcstring(&arg, 0));
    break;
  case 'h': // File descriptor
    CALL_SD_BUS_FUNC(sd_bus_message_append, p->msg, "h", janet_getinteger(&arg, 0));
    break;
  }
}

JANET_FN(cfun_message_dump,
         "(sdbus/message-dump msg)",
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
    JANET_REG("message-append", cfun_message_append),
    JANET_REG("message-dump", cfun_message_dump),
    JANET_REG_END
};
