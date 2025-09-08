// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Joshua Krusell

#include "common.h"

#define MESSAGE_PEEK(msg, type, contents)                                      \
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

// State struct when parsing signatures and appending data
typedef struct {
  sd_bus_message *msg;
  const char *cursor;
} Parser;

static int gc_sdbus_message(void *, size_t);
const JanetAbstractType dbus_message_type = { .name = "sdbus/message",
                                              .gc   = gc_sdbus_message,
                                              JANET_ATEND_GC };

static int gc_sdbus_message(void *data, size_t len) {
  UNUSED(len);

  sd_bus_message_unrefp((sd_bus_message **) data);
  *((sd_bus_message **) data) = NULL;

  return 0;
}

static bool is_basic_type(int ch) {
  // TODO: byte and file descriptor types
  return strchr("bnqiuxtdsog", ch) != NULL;
}

static int skip(Parser *p, size_t skip) {
  p->cursor += skip;
  return *p->cursor;
}

static int next(Parser *p) {
  return *(++p->cursor);
}

static int peek(Parser *p) {
  return *(p->cursor + 1);
}

static int cursor(Parser *p) {
  return *p->cursor;
}

// Returns offset of matching 'close' character in str
static size_t match(const char *str, int open, int close) {
  int depth     = 1;
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

// Returns offset to the start of the first complete type or
// dictionary entry in signature. Used to find the member type for
// arrays.
static size_t find_subtype(const char *signature) {
  int ch;
  if (!(ch = *signature))
    janet_panicf("Missing array signature");

  if (ch == 'a')
    return 1 + find_subtype(signature + 1);
  else if (ch == '{')
    return match(signature, '{', '}');
  else if (ch == '(')
    return match(signature, '(', ')');
  else
    return 0;
}

static void append_complete_type(Parser *, Janet);
static void append_basic_type(Parser *, Janet);
static void append_variant_type(Parser *, Janet);
static void append_struct_type(Parser *, Janet);
static void append_array_type(Parser *, Janet);
static void append_dict_type(Parser *, Janet);

static void append_data(sd_bus_message *msg, const char *signature, Janet *args,
                        int32_t n) {
  Parser p = { msg, signature };
  for (int32_t i = 0; i < n; i++) {
    if (!cursor(&p))
      janet_panicf("Excessive arguments for signature: %s", signature);

    append_complete_type(&p, args[i]);
    next(&p);
  }

  if (cursor(&p))
    janet_panicf("Arguments missing for signature: %s", signature);
}

static void append_complete_type(Parser *p, Janet arg) {
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

static void append_dict_type(Parser *p, Janet arg) {
  size_t end;
  if ((end = match(p->cursor, '{', '}')) < 3)
    janet_panicf("Incomplete dictionary signature: %s", p->cursor);

  // Opening/closing braces must be included for sd_bus_message_open_container
  char *dict_sig = janet_scalloc(end + 2, sizeof(char));
  memcpy(dict_sig, p->cursor, end + 1);

  skip(p, end);

  if (!is_basic_type(*(dict_sig + 1)))
    janet_panicf("Dict signature key must be a basic type: %s", dict_sig);

  CALL_SD_BUS_FUNC(sd_bus_message_open_container, p->msg, SD_BUS_TYPE_ARRAY,
                   dict_sig);

  // Strip opening/closing braces
  memmove(dict_sig, dict_sig + 1, end - 1);
  dict_sig[end - 1] = '\0';

  JanetTable *tbl = janet_gettable(&arg, 0);
  if (tbl->count == 0)
    janet_panic("Empty table: missing dictionary arguments");

  Parser dict_parser = { p->msg, dict_sig };
  for (int32_t i = 0; i < tbl->count; i++) {
    CALL_SD_BUS_FUNC(sd_bus_message_open_container, p->msg,
                     SD_BUS_TYPE_DICT_ENTRY, dict_sig);

    // Append entry key - basic type
    append_basic_type(&dict_parser, tbl->data[i].key);

    // Append entry value - complete type
    next(&dict_parser);
    append_complete_type(&dict_parser, tbl->data[i].value);

    // Reset cursor back to 'key' type
    dict_parser.cursor = dict_sig;

    CALL_SD_BUS_FUNC(sd_bus_message_close_container, p->msg);
  }

  CALL_SD_BUS_FUNC(sd_bus_message_close_container, p->msg);
  janet_sfree(dict_sig);
}

static void append_array_type(Parser *p, Janet arg) {
  if (peek(p) == '{') {
    next(p);
    append_dict_type(p, arg);
    return;
  }

  size_t end      = find_subtype(p->cursor);
  char *array_sig = janet_scalloc(end + 1, sizeof(char));
  memcpy(array_sig, p->cursor + 1, end);

  skip(p, end);

  CALL_SD_BUS_FUNC(sd_bus_message_open_container, p->msg, SD_BUS_TYPE_ARRAY,
                   array_sig);

  JanetArray *array = janet_getarray(&arg, 0);
  if (array->count == 0)
    janet_panic("Empty array: missing array arguments");

  Parser array_parser = { p->msg, array_sig };
  for (int32_t i = 0; i < array->count; i++) {
    append_complete_type(&array_parser, array->data[i]);

    // Reset - container types may move our cursor
    array_parser.cursor = array_sig;
  }

  CALL_SD_BUS_FUNC(sd_bus_message_close_container, p->msg);
  janet_sfree(array_sig);
}

static void append_struct_type(Parser *p, Janet arg) {
  size_t end = 0;
  if ((end = match(p->cursor, '(', ')')) < 2)
    janet_panicf("Missing struct signature contents: %s", p->cursor);

  // Exclude opening/closing parentheses
  char *struct_sig = janet_scalloc(end, sizeof(char));
  memcpy(struct_sig, p->cursor + 1, end - 1);

  skip(p, end);

  CALL_SD_BUS_FUNC(sd_bus_message_open_container, p->msg, SD_BUS_TYPE_STRUCT,
                   struct_sig);

  const Janet *tuple = janet_gettuple(&arg, 0);
  int32_t length;
  if ((length = janet_tuple_length(tuple)) == 0)
    janet_panic("Empty tuple: missing struct arguments");

  Parser struct_parser = { p->msg, struct_sig };
  for (int32_t i = 0; i < length; i++) {
    if (!cursor(&struct_parser))
      janet_panicf("Excessive arguments for struct signature: %s", struct_sig);

    append_complete_type(&struct_parser, tuple[i]);
    next(&struct_parser);
  }

  if (cursor(&struct_parser))
    janet_panicf("Arguments missing for struct signature: %s", struct_sig);

  CALL_SD_BUS_FUNC(sd_bus_message_close_container, p->msg);
  janet_sfree(struct_sig);
}

static void append_variant_type(Parser *p, Janet arg) {
  const Janet *tuple = janet_gettuple(&arg, 0);
  if (janet_tuple_length(tuple) != 2)
    janet_panicf("Variant type expects exactly 2 arguments");

  const char *variant_sig = janet_getcstring(tuple, 0);
  const Janet variant_arg = tuple[1];
  Parser variant_parser   = { p->msg, variant_sig };

  CALL_SD_BUS_FUNC(sd_bus_message_open_container, p->msg, SD_BUS_TYPE_VARIANT,
                   variant_sig);
  append_complete_type(&variant_parser, variant_arg);
  CALL_SD_BUS_FUNC(sd_bus_message_close_container, p->msg);
}

static void append_basic_type(Parser *p, Janet arg) {
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

static Janet read_basic_type(sd_bus_message *, char);
static Janet read_variant_type(sd_bus_message *, const char *);
static Janet read_struct_type(sd_bus_message *, const char *);
static Janet read_array_type(sd_bus_message *, const char *);
static Janet read_dict_type(sd_bus_message *, const char *);

// Returns 1 on success, 0 on end of message
static int read_complete_type(sd_bus_message *msg, Janet *obj) {
  char type;                    // Next type in message
  const char *signature = NULL; // Signature of contents if container

  if (MESSAGE_PEEK(msg, &type, &signature) == 0)
    return 0;

  if (is_basic_type(type))
    *obj = read_basic_type(msg, type);
  else if (type == SD_BUS_TYPE_VARIANT)
    *obj = read_variant_type(msg, signature);
  else if (type == SD_BUS_TYPE_STRUCT)
    *obj = read_struct_type(msg, signature);
  else if (type == 'a' && *signature == '{')
    *obj = read_dict_type(msg, signature);
  else if (type == 'a')
    *obj = read_array_type(msg, signature);
  else
    janet_panicf("Unsupported message type: %c", type);

  return 1;
}

static Janet read_variant_type(sd_bus_message *msg, const char *signature) {
  CALL_SD_BUS_FUNC(sd_bus_message_enter_container, msg, SD_BUS_TYPE_VARIANT,
                   signature);

  Janet obj;
  if (read_complete_type(msg, &obj) == 0)
    janet_panic("Unexpected end of variant type");

  CALL_SD_BUS_FUNC(sd_bus_message_exit_container, msg);

  JanetTuple tuple = TUPLE(janet_cstringv(signature), obj);
  return janet_wrap_tuple(tuple);
}

static Janet read_struct_type(sd_bus_message *msg, const char *signature) {
  JanetArray *array = janet_array(1);

  CALL_SD_BUS_FUNC(sd_bus_message_enter_container, msg, SD_BUS_TYPE_STRUCT,
                   signature);

  Janet obj;
  while (read_complete_type(msg, &obj) > 0)
    janet_array_push(array, obj);

  CALL_SD_BUS_FUNC(sd_bus_message_exit_container, msg);

  JanetTuple tuple = janet_tuple_n(array->data, array->count);
  return janet_wrap_tuple(tuple);
}

static Janet read_dict_type(sd_bus_message *msg, const char *signature) {
  JanetTable *tbl = janet_table(1);

  CALL_SD_BUS_FUNC(sd_bus_message_enter_container, msg, SD_BUS_TYPE_ARRAY,
                   signature);

  char type;
  const char *dict_sig = NULL;
  while (MESSAGE_PEEK(msg, &type, &dict_sig) > 0) {
    CALL_SD_BUS_FUNC(sd_bus_message_enter_container, msg,
                     SD_BUS_TYPE_DICT_ENTRY, dict_sig);

    Janet key = read_basic_type(msg, dict_sig[0]);
    Janet value;
    if (read_complete_type(msg, &value) == 0)
      janet_panic("Unexpected end of dictionary type");

    janet_table_put(tbl, key, value);

    CALL_SD_BUS_FUNC(sd_bus_message_exit_container, msg);
  }

  CALL_SD_BUS_FUNC(sd_bus_message_exit_container, msg);
  return janet_wrap_table(tbl);
}

static Janet read_array_type(sd_bus_message *msg, const char *signature) {
  JanetArray *array = janet_array(1);

  CALL_SD_BUS_FUNC(sd_bus_message_enter_container, msg, SD_BUS_TYPE_ARRAY,
                   signature);

  Janet obj;
  while (read_complete_type(msg, &obj) > 0)
    janet_array_push(array, obj);

  CALL_SD_BUS_FUNC(sd_bus_message_exit_container, msg);
  return janet_wrap_array(array);
}

static Janet read_basic_type(sd_bus_message *msg, char type) {
  switch (type) {
    case 'b': // boolean
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
    case 's': // string
      DBUS_TO_JANET_STR('s');
    case 'o': // object path
      DBUS_TO_JANET_STR('o');
    case 'g': // signature
      DBUS_TO_JANET_STR('g');
  }

  // Unreachable
  janet_panic("Unsupported basic type");
}

JANET_FN(
    cfun_message_new_method_call,
    "(sdbus/message-new-method-call bus destination path interface member)",
    "Create a new D-Bus method call message.") {
  janet_fixarity(argc, 5);

  Conn *conn              = janet_getabstract(argv, 0, &dbus_bus_type);
  const char *destination = janet_getcstring(argv, 1);
  const char *path        = janet_getcstring(argv, 2);
  const char *interface   = janet_getcstring(argv, 3);
  const char *member      = janet_getcstring(argv, 4);

  sd_bus_message *msg = NULL;
  CALL_SD_BUS_FUNC(sd_bus_message_new_method_call, conn->bus, &msg, destination,
                   path, interface, member);

  sd_bus_message **msg_ptr =
      janet_abstract(&dbus_message_type, sizeof(sd_bus_message *));
  *msg_ptr = msg;

  return janet_wrap_abstract(msg_ptr);
}

JANET_FN(cfun_message_new_method_return,
         "(sdbus/message-new-method-return call)",
         "Create a new D-Bus message object in response to a method call.") {
  janet_fixarity(argc, 1);

  sd_bus_message **msg_ptr = janet_getabstract(argv, 0, &dbus_message_type);

  sd_bus_message *reply = NULL;
  CALL_SD_BUS_FUNC(sd_bus_message_new_method_return, *msg_ptr, &reply);

  sd_bus_message **reply_ptr =
      janet_abstract(&dbus_message_type, sizeof(sd_bus_message *));
  *reply_ptr = reply;

  return janet_wrap_abstract(reply_ptr);
}

JANET_FN(cfun_message_new_method_error,
         "(sdbus/message-new-method-error call name message)",
         "Create a new D-Bus message object in response to a method\n"
         "call with an error.") {
  janet_fixarity(argc, 3);

  sd_bus_message **call = janet_getabstract(argv, 0, &dbus_message_type);
  const char *name      = janet_getcstring(argv, 1);
  const char *message   = janet_getcstring(argv, 2);

  sd_bus_error error    = SD_BUS_ERROR_MAKE_CONST(name, message);
  sd_bus_message *reply = NULL;
  CALL_SD_BUS_FUNC(sd_bus_message_new_method_error, *call, &reply, &error);

  sd_bus_message **reply_ptr =
      janet_abstract(&dbus_message_type, sizeof(sd_bus_message *));
  *reply_ptr = reply;

  return janet_wrap_abstract(reply_ptr);
}

JANET_FN(cfun_message_send, "(sdbus/message-send msg)",
         "Send a D-Bus message.") {
  janet_fixarity(argc, 1);

  sd_bus_message **msg_ptr = janet_getabstract(argv, 0, &dbus_message_type);
  CALL_SD_BUS_FUNC(sd_bus_message_send, *msg_ptr);

  return janet_wrap_nil();
}

#define MESSAGE_GET(target)                                                    \
  janet_fixarity(argc, 1);                                                     \
  sd_bus_message **msg_ptr = janet_getabstract(argv, 0, &dbus_message_type);   \
  const char *target       = sd_bus_message_get_##target(*msg_ptr);            \
  return janet_cstringv(target);

JANET_FN(cfun_message_get_destination, "(sdbus/message-get-destination msg)",
         "Get the destination of a D-Bus message."){ MESSAGE_GET(destination) }

JANET_FN(cfun_message_get_path, "(sdbus/message-get-path msg)",
         "Get the object path of a D-Bus message."){ MESSAGE_GET(path) }

JANET_FN(cfun_message_get_interface, "(sdbus/message-get-interface msg)",
         "Get the interface of a D-Bus message."){ MESSAGE_GET(interface) }

JANET_FN(cfun_message_get_member, "(sdbus/message-get-member msg)",
         "Get the member of a D-Bus message."){ MESSAGE_GET(member) }

JANET_FN(cfun_message_get_sender, "(sdbus/message-get-sender msg)",
         "Get the sender of a D-Bus message."){ MESSAGE_GET(sender) }

JANET_FN(cfun_message_unref, "(sdbus/message-unref msg)",
         "Deallocate a D-Bus message.") {
  janet_fixarity(argc, 1);
  sd_bus_message **msg_ptr = janet_getabstract(argv, 0, &dbus_message_type);

  sd_bus_message_unrefp(msg_ptr);
  *msg_ptr = NULL;

  return janet_wrap_nil();
}

JANET_FN(cfun_message_append, "(sdbus/message-append msg signature & args)",
         "Append arguments to a D-Bus message.") {
  janet_arity(argc, 3, -1);

  sd_bus_message **msg_ptr = janet_getabstract(argv, 0, &dbus_message_type);
  const char *signature    = janet_getcstring(argv, 1);

  append_data(*msg_ptr, signature, argv + 2, argc - 2);

  return janet_wrap_nil();
}

JANET_FN(cfun_message_read, "(sdbus/message-read msg)",
         "Read a single complete type from a D-Bus message."
         "Returns nil upon end of message.") {
  janet_fixarity(argc, 1);

  sd_bus_message **msg_ptr = janet_getabstract(argv, 0, &dbus_message_type);

  // Follow Janet's file/read and return nil on eof
  Janet obj;
  if (read_complete_type(*msg_ptr, &obj) == 0)
    return janet_wrap_nil();

  return obj;
}

JANET_FN(cfun_message_read_all, "(sdbus/message-read-all msg)",
         "Read all contents of a D-Bus message."
         "If `msg` contains multiple complete types"
         "returns an array, else a single value or nil if"
         "`msg` is empty.") {
  janet_fixarity(argc, 1);

  sd_bus_message **msg_ptr = janet_getabstract(argv, 0, &dbus_message_type);

  JanetArray *array = janet_array(1);
  Janet obj;
  while (read_complete_type(*msg_ptr, &obj) > 0)
    janet_array_push(array, obj);

  return (array->count < 2) ? janet_array_pop(array) : janet_wrap_array(array);
}

JANET_FN(cfun_message_seal, "(sdbus/message-seal msg)", "Seal a message.") {
  janet_fixarity(argc, 1);

  sd_bus_message **msg_ptr = janet_getabstract(argv, 0, &dbus_message_type);

  CALL_SD_BUS_FUNC(sd_bus_message_seal, *msg_ptr, 0, 0);
  return janet_wrap_nil();
}

JANET_FN(cfun_message_dump, "(sdbus/message-dump msg &opt f)",
         "Dump a D-Bus message to file."
         "If `f` is not provided, dumps to stdout.") {
  janet_arity(argc, 1, 2);

  sd_bus_message **msg_ptr = janet_getabstract(argv, 0, &dbus_message_type);

  JanetFile dflt = { 0 };
  JanetFile *f   = janet_optabstract(argv, argc, 1, &janet_file_type, &dflt);

  if (f->file && (f->flags & JANET_FILE_CLOSED))
    janet_panic("Cannot dump message to a closed file");

  sd_bus_message_dump(*msg_ptr, f->file, SD_BUS_MESSAGE_DUMP_WITH_HEADER);
  CALL_SD_BUS_FUNC(sd_bus_message_rewind, *msg_ptr, true);

  return janet_wrap_nil();
}

JanetRegExt cfuns_message[] = {
  JANET_REG("message-unref", cfun_message_unref),
  JANET_REG("message-new-method-call", cfun_message_new_method_call),
  JANET_REG("message-new-method-return", cfun_message_new_method_return),
  JANET_REG("message-new-method-error", cfun_message_new_method_error),
  JANET_REG("message-send", cfun_message_send),
  JANET_REG("message-get-destination", cfun_message_get_destination),
  JANET_REG("message-get-path", cfun_message_get_path),
  JANET_REG("message-get-interface", cfun_message_get_interface),
  JANET_REG("message-get-member", cfun_message_get_member),
  JANET_REG("message-get-sender", cfun_message_get_sender),
  JANET_REG("message-seal", cfun_message_seal),
  JANET_REG("message-append", cfun_message_append),
  JANET_REG("message-read", cfun_message_read),
  JANET_REG("message-read-all", cfun_message_read_all),
  JANET_REG("message-dump", cfun_message_dump),
  JANET_REG_END
};
