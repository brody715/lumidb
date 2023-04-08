#pragma once

#ifndef MAIN_DEBUG
// uncomment it if not use main in this source file
// #define TEST_NO_MAIN
#include "acutest.h"
#endif

#ifdef MAIN_DEBUG
#define TEST_CHECK(expr)                                                       \
  do {                                                                         \
    int _ = expr;                                                              \
  } while (0)
#define TEST_EXCEPTION(expr, excep)                                            \
  do {                                                                         \
    int _ = expr;                                                              \
  } while (0)
#define TEST_CASE(name)
#define TEST_MSG(...)
#endif

#define TEST_FUNC(funcName)                                                    \
  { #funcName, funcName }