#include <unistd.h>

#include <cstdio>

#define TEST_LOG_EXT(svc, name, arg, exttext, ...)                             \
  {                                                                            \
    auto rc = svc->name(arg);                                                  \
    printf("%s: ok: %s" exttext "\n", #name, rc.isOk() ? "true" : "false",     \
           ##__VA_ARGS__);                                                     \
    sleep(1);                                                                  \
  }

#define TEST_LOG(svc, name, arg) TEST_LOG_EXT(svc, name, arg, "")

#define TEST_LOG2_EXT(svc, name, arg1, arg2, exttext, ...)                     \
  {                                                                            \
    auto rc = svc->name(arg1, arg2);                                           \
    printf("%s: ok: %s" exttext "\n", #name, rc.isOk() ? "true" : "false",     \
           ##__VA_ARGS__);                                                     \
    sleep(1);                                                                  \
  }
#define TEST_LOG2(svc, name, arg1, arg2)                                       \
  TEST_LOG2_EXT(svc, name, arg1, arg2, "")
