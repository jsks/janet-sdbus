// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Joshua Krusell

#ifndef _JANET_SDBUS_UNWRAP_H
#define _JANET_SDBUS_UNWRAP_H

#include <stdbool.h>
#include <stdint.h>

#include <janet.h>

void dbus_errctx_reset(void);
void dbus_errctx_inc(void);
void dbus_errctx_set(const char *, size_t);
void dbus_errctx_exit(void);

bool getboolean(Janet);
double getnumber(Janet);
const char *getcstring(Janet);

uint8_t getuinteger8(Janet);
int16_t getinteger16(Janet);
uint16_t getuinteger16(Janet);
int32_t getinteger(Janet);
uint32_t getuinteger(Janet);
uint64_t getuinteger64(Janet);
int64_t getinteger64(Janet);

const Janet *gettuple(Janet);
JanetByteView getbytes(Janet);
JanetView getindexed(Janet);
JanetDictView getdictionary(Janet);

int getfd(Janet);

#endif
