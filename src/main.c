// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Joshua Krusell

#include "common.h"

int check_sd_bus_return(const char *function, int rv) {
  if (rv < 0)
    janet_panicf("failed to call %s: %s", function, strerror(-rv));

  return rv;
}

JANET_MODULE_ENTRY(JanetTable *env) {
  janet_cfuns_ext(env, "sdbus", cfuns_bus);
  janet_cfuns_ext(env, "sdbus", cfuns_call);
  janet_cfuns_ext(env, "sdbus", cfuns_export);
  janet_cfuns_ext(env, "sdbus", cfuns_message);
  janet_cfuns_ext(env, "sdbus", cfuns_slot);
}
