// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Joshua Krusell

#include "common.h"

JANET_MODULE_ENTRY(JanetTable *env) {
  janet_cfuns_ext(env, "sdbus", cfuns_bus);
  janet_cfuns_ext(env, "sdbus", cfuns_call);
  janet_cfuns_ext(env, "sdbus", cfuns_export);
  janet_cfuns_ext(env, "sdbus", cfuns_message);
  janet_cfuns_ext(env, "sdbus", cfuns_slot);
}
