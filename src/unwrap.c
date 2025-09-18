// SPDX-License-Identifier: MIT
// Copyright (c) 2023 Calvin Rose and contributors
// Copyright (c) 2025 Joshua Krusell
//
// Some of the following functions are derived from Janet's capi.c
// Original code: https://github.com/janet-lang/janet
//
// Modified for improved D-Bus error messages

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>

#include "unwrap.h"

#define ERRCTX_MAX_SIG_LEN 16

typedef struct {
  const char *sig[2];
  size_t len[2];
  size_t depth;
  int32_t argc;
} ErrorContext;

static JANET_THREAD_LOCAL ErrorContext g_errctx;

void dbus_errctx_reset(void) {
  g_errctx = (ErrorContext) { 0 };
}

void dbus_errctx_inc(void) {
  g_errctx.argc++;
}

void dbus_errctx_set(const char *sig, size_t len) {
  int idx           = ++g_errctx.depth > 1;
  g_errctx.sig[idx] = sig;
  g_errctx.len[idx] = len;
}

void dbus_errctx_exit(void) {
  if (g_errctx.depth > 0)
    g_errctx.depth--;
}

static char *format_errctx(char *buf, const char *sig, size_t len) {
  if (len == 0 || !sig)
    return NULL;

  // Reserve 3 characters for overflow '...'
  size_t n = len > (ERRCTX_MAX_SIG_LEN - 4) ? (ERRCTX_MAX_SIG_LEN - 4) : len;

  // Note: janet_panicf does not support '%.*s'
  memcpy(buf, sig, n);
  buf[n] = '\0';

  if (n < len) {
    memset(buf + n, '.', 3);
    buf[ERRCTX_MAX_SIG_LEN - 1] = '\0';
  }

  return buf;
}

static void dbus_type_error(const char *janet_type, Janet x) {
  char sigbuf[ERRCTX_MAX_SIG_LEN];
  if (!(format_errctx(sigbuf, g_errctx.sig[0], g_errctx.len[0]))) {
    janet_panicf("bad argument #%d (missing D-Bus type), expected %s, got %v",
                 g_errctx.argc, janet_type, x);
  }

  if (g_errctx.depth > 1) {
    char childbuf[ERRCTX_MAX_SIG_LEN];
    if ((format_errctx(childbuf, g_errctx.sig[1], g_errctx.len[1]))) {
      janet_panicf("bad argument #%d to D-Bus type '%s' (within '%s'), "
                   "expected %s, got %v",
                   g_errctx.argc, childbuf, sigbuf, janet_type, x);
    }
  }

  janet_panicf("bad argument #%d to D-Bus type '%s', expected %s, got %v",
               g_errctx.argc, sigbuf, janet_type, x);
}

bool getboolean(Janet x) {
  if (!janet_checktype(x, JANET_BOOLEAN)) {
    dbus_type_error("boolean", x);
  }

  return janet_unwrap_boolean(x);
}

double getnumber(Janet x) {
  if (!janet_checktype(x, JANET_NUMBER)) {
    dbus_type_error("number", x);
  }

  return janet_unwrap_number(x);
}

const char *getcstring(Janet x) {
  if (!janet_checktype(x, JANET_STRING)) {
    dbus_type_error("string", x);
  }

  return janet_getcbytes(&x, 0);
}

static bool checkuint8(Janet x) {
  if (!janet_checktype(x, JANET_NUMBER))
    return false;

  double n = janet_unwrap_number(x);
  return (n >= 0 && n <= UINT8_MAX && n == (uint8_t) n);
}

uint8_t getuinteger8(Janet x) {
  if (!checkuint8(x)) {
    dbus_type_error("8 bit unsigned integer", x);
  }

  return (uint8_t) janet_unwrap_number(x);
}

int16_t getinteger16(Janet x) {
  if (!janet_checkint16(x)) {
    dbus_type_error("16 bit signed integer", x);
  }

  return (int16_t) janet_unwrap_number(x);
}

uint16_t getuinteger16(Janet x) {
  if (!janet_checkuint16(x)) {
    dbus_type_error("16 bit unsigned integer", x);
  }

  return (uint16_t) janet_unwrap_number(x);
}

int32_t getinteger(Janet x) {
  if (!janet_checkint(x)) {
    dbus_type_error("32 bit signed integer", x);
  }

  return janet_unwrap_integer(x);
}

uint32_t getuinteger(Janet x) {
  if (!janet_checkuint(x)) {
    dbus_type_error("32 bit unsigned integer", x);
  }

  return (uint32_t) janet_unwrap_number(x);
}

int64_t getinteger64(Janet x) {
#ifdef JANET_INT_TYPES
  return janet_unwrap_s64(x);
#else
  if (!janet_checkint64(x)) {
    dbus_type_error("64 bit signed integer", x);
  }

  return (int64_t) janet_unwrap_number(x);
#endif
}

uint64_t getuinteger64(Janet x) {
#ifdef JANET_INT_TYPES
  return janet_unwrap_u64(x);
#else
  if (!janet_checkuint64(x)) {
    dbus_type_error("64 bit unsigned", x);
  }

  return (uint64_t) janet_unwrap_number(x);
#endif
}

const Janet *gettuple(Janet x) {
  if (!janet_checktype(x, JANET_TUPLE)) {
    dbus_type_error("tuple", x);
  }

  return janet_unwrap_tuple(x);
}

JanetByteView getbytes(Janet x) {
  JanetByteView view;
  if (!janet_bytes_view(x, &view.bytes, &view.len)) {
    dbus_type_error("bytes", x);
  }

  return view;
}

JanetView getindexed(Janet x) {
  JanetView view;
  if (!janet_indexed_view(x, &view.items, &view.len)) {
    dbus_type_error("indexed", x);
  }

  return view;
}

JanetDictView getdictionary(Janet x) {
  JanetDictView view;
  if (!janet_dictionary_view(x, &view.kvs, &view.len, &view.cap)) {
    dbus_type_error("dictionary", x);
  }

  return view;
}

int getfd(Janet x) {
  if (!(janet_checktype(x, JANET_ABSTRACT))) {
    dbus_type_error(":core/file or :core/stream", x);
  }

  void *p                     = janet_unwrap_abstract(x);
  const JanetAbstractType *at = janet_abstract_type(p);
  if (at == &janet_file_type) {
    JanetFile *file = p;
    if (file->flags & JANET_FILE_CLOSED) {
      janet_panic("bad argument to D-Bus type 'h', file is closed");
    }

    return fileno(file->file);
  }

  if (at == &janet_stream_type) {
    JanetStream *stream = p;
    if (stream->flags & JANET_STREAM_CLOSED) {
      janet_panic("bad argument to D-Bus type 'h', stream is closed");
    }

    return stream->handle;
  }

  dbus_type_error(":core/file or :core/stream", x);

  // Unreachable
  return -1;
}
