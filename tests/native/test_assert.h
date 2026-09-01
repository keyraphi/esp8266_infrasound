#pragma once
#include <cstdio>
#include <cstring>

inline int g_checks = 0;
inline int g_failures = 0;

#define CHECK(cond)                                                        \
  do {                                                                     \
    ++g_checks;                                                            \
    if (!(cond)) {                                                         \
      ++g_failures;                                                        \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
    }                                                                      \
  } while (0)

#define CHECK_EQ(a, b)                                                     \
  do {                                                                     \
    ++g_checks;                                                            \
    if (!((a) == (b))) {                                                   \
      ++g_failures;                                                        \
      std::printf("FAIL %s:%d: %s == %s (got %lld, want %lld)\n",          \
                  __FILE__, __LINE__, #a, #b,                              \
                  (long long)(a), (long long)(b));                         \
    }                                                                      \
  } while (0)

#define CHECK_STR_EQ(a, b)                                                 \
  do {                                                                     \
    ++g_checks;                                                            \
    if (std::strcmp((a), (b)) != 0) {                                      \
      ++g_failures;                                                        \
      std::printf("FAIL %s:%d: \"%s\" != \"%s\"\n",                        \
                  __FILE__, __LINE__, (a), (b));                           \
    }                                                                      \
  } while (0)
