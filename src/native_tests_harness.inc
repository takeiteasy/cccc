/* CCCC native test runtime harness for `--testing -c=native` */

#include <fnmatch.h>
#include <inttypes.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
  jmp_buf jmp;
  int failed;
  int timed_out;
  char fail_msg[512];
} __cccc_run_t;
static __cccc_run_t *__s_run = NULL;
static volatile int __s_alarm_fired = 0;
static void __cccc_alarm_hdl(int s) {
  (void)s;
  __s_alarm_fired = 1;
  if (__s_run)
    longjmp(__s_run->jmp, 2);
}
static void __cccc_fail(const char *file, int line, const char *msg) {
  if (!__s_run) {
    fprintf(stderr, "ASSERT at %s:%d: %s\n", file, line, msg);
    abort();
  }
  snprintf(__s_run->fail_msg, sizeof(__s_run->fail_msg), "%s", msg);
  __s_run->failed = 1;
  longjmp(__s_run->jmp, 1);
}
void __cccc_assert(int c, const char *e, const char *f, int l) {
  if (!c) {
    char b[512];
    snprintf(b, sizeof(b), "%s (%s:%d)", e, f, l);
    __cccc_fail(f, l, b);
  }
}
void __cccc_assert_false(int c, const char *e, const char *f, int l) {
  if (c) {
    char b[512];
    snprintf(b, sizeof(b), "!%s (%s:%d)", e, f, l);
    __cccc_fail(f, l, b);
  }
}
void __cccc_assert_fail(const char *f, int l) {
  char b[512];
  snprintf(b, sizeof(b), "forced failure (%s:%d)", f, l);
  __cccc_fail(f, l, b);
}
void __cccc_assert_fail_msg(const char *msg, const char *f, int l) {
  char b[512];
  snprintf(b, sizeof(b), "%s (%s:%d)", msg, f, l);
  __cccc_fail(f, l, b);
}
void __cccc_assert_eq(long long a, long long b, const char *as, const char *bs,
                      const char *f, int l) {
  if (a != b) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s != %s (%lld != %lld) (%s:%d)", as, bs, a, b,
             f, l);
    __cccc_fail(f, l, buf);
  }
}
void __cccc_assert_neq(long long a, long long b, const char *as, const char *bs,
                       const char *f, int l) {
  if (a == b) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s == %s (both %lld) (%s:%d)", as, bs, a, f, l);
    __cccc_fail(f, l, buf);
  }
}
void __cccc_assert_gt(long long a, long long b, const char *as, const char *bs,
                      const char *f, int l) {
  if (!(a > b)) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s <= %s (%lld <= %lld) (%s:%d)", as, bs, a, b,
             f, l);
    __cccc_fail(f, l, buf);
  }
}
void __cccc_assert_lt(long long a, long long b, const char *as, const char *bs,
                      const char *f, int l) {
  if (!(a < b)) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s >= %s (%lld >= %lld) (%s:%d)", as, bs, a, b,
             f, l);
    __cccc_fail(f, l, buf);
  }
}
void __cccc_assert_ge(long long a, long long b, const char *as, const char *bs,
                      const char *f, int l) {
  if (!(a >= b)) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s < %s (%lld < %lld) (%s:%d)", as, bs, a, b, f,
             l);
    __cccc_fail(f, l, buf);
  }
}
void __cccc_assert_le(long long a, long long b, const char *as, const char *bs,
                      const char *f, int l) {
  if (!(a <= b)) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s > %s (%lld > %lld) (%s:%d)", as, bs, a, b, f,
             l);
    __cccc_fail(f, l, buf);
  }
}
void __cccc_assert_within(long long delta, long long expected, long long actual,
                          const char *ds, const char *es, const char *as,
                          const char *f, int l) {
  long long d = expected - actual;
  if (d < 0)
    d = -d;
  if (d > delta) {
    char buf[512];
    snprintf(buf, sizeof(buf), "|%s-%s|=%lld>%s(%lld)(%s:%d)", es, as, d, ds,
             delta, f, l);
    __cccc_fail(f, l, buf);
  }
}
void __cccc_assert_null(const void *p, const char *ps, const char *f, int l) {
  if (p) {
    char b[512];
    snprintf(b, sizeof(b), "%s is not null (%s:%d)", ps, f, l);
    __cccc_fail(f, l, b);
  }
}
void __cccc_assert_not_null(const void *p, const char *ps, const char *f,
                            int l) {
  if (!p) {
    char b[512];
    snprintf(b, sizeof(b), "%s is null (%s:%d)", ps, f, l);
    __cccc_fail(f, l, b);
  }
}
void __cccc_assert_streq(const char *a, const char *b, const char *as,
                         const char *bs, const char *f, int l) {
  if ((!a || !b) || strcmp(a, b) != 0) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s != %s (\"%s\" != \"%s\") (%s:%d)", as, bs,
             a ? a : "(null)", b ? b : "(null)", f, l);
    __cccc_fail(f, l, buf);
  }
}
void __cccc_assert_streq_len(const char *a, const char *b, long long len,
                             const char *as, const char *bs, const char *f,
                             int l) {
  if (strncmp(a, b, (size_t)len) != 0) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s != %s (first %lld chars differ) (%s:%d)", as,
             bs, len, f, l);
    __cccc_fail(f, l, buf);
  }
}
void __cccc_assert_mem_eq(const void *expected, const void *actual,
                          long long len, const char *es, const char *as,
                          const char *f, int l) {
  if (memcmp(expected, actual, (size_t)len) != 0) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s != %s (%lld bytes differ) (%s:%d)", es, as,
             len, f, l);
    __cccc_fail(f, l, buf);
  }
}
void __cccc_assert_float_within(double delta, double expected, double actual,
                                const char *ds, const char *es, const char *as,
                                const char *f, int l) {
  double d = expected - actual;
  if (d < 0)
    d = -d;
  if (d > delta) {
    char buf[512];
    snprintf(buf, sizeof(buf), "|%s-%s|=%g>%s(%g)(%s:%d)", es, as, d, ds, delta,
             f, l);
    __cccc_fail(f, l, buf);
  }
}
void __cccc_assert_double_within(double delta, double expected, double actual,
                                 const char *ds, const char *es, const char *as,
                                 const char *f, int l) {
  __cccc_assert_float_within(delta, expected, actual, ds, es, as, f, l);
}
void __cccc_assert_bits(long long mask, long long expected, long long actual,
                        const char *ms, const char *es, const char *as,
                        const char *f, int l) {
  if ((actual & mask) != (expected & mask)) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s&%s=0x%llx!=%s&%s=0x%llx(%s:%d)", as, ms,
             (unsigned long long)(actual & mask), es, ms,
             (unsigned long long)(expected & mask), f, l);
    __cccc_fail(f, l, buf);
  }
}
void __cccc_assert_bit_high(int bit, long long actual, const char *bs,
                            const char *as, const char *f, int l) {
  if (!(actual & (1LL << bit))) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s bit %d of %s is low (%s:%d)", bs, bit, as, f,
             l);
    __cccc_fail(f, l, buf);
  }
}
void __cccc_assert_bit_low(int bit, long long actual, const char *bs,
                           const char *as, const char *f, int l) {
  if (actual & (1LL << bit)) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s bit %d of %s is high (%s:%d)", bs, bit, as,
             f, l);
    __cccc_fail(f, l, buf);
  }
}
void __cccc_assert_eq_array(const void *expected, const void *actual,
                            long long elem_size, long long count,
                            const char *es, const char *as, const char *f,
                            int l) {
  size_t total = (size_t)elem_size * (size_t)count;
  if (memcmp(expected, actual, total) != 0) {
    char buf[512];
    snprintf(buf, sizeof(buf),
             "%s[0..%lld]!=%s[0..%lld](%zu bytes differ)(%s:%d)", es, count - 1,
             as, count - 1, total, f, l);
    __cccc_fail(f, l, buf);
  }
}
void __cccc_assert_msg(int c, const char *e, const char *msg, const char *f,
                       int l) {
  if (!c) {
    char b[512];
    snprintf(b, sizeof(b), "%s (%s:%d) - %s", e, f, l, msg);
    __cccc_fail(f, l, b);
  }
}
void __cccc_assert_eq_msg(long long a, long long b, const char *as,
                          const char *bs, const char *msg, const char *f,
                          int l) {
  if (a != b) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s != %s (%lld != %lld) (%s:%d) - %s", as, bs,
             a, b, f, l, msg);
    __cccc_fail(f, l, buf);
  }
}
void __cccc_assert_streq_msg(const char *a, const char *b, const char *as,
                             const char *bs, const char *msg, const char *f,
                             int l) {
  if (strcmp(a, b) != 0) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s != %s (\"%s\" != \"%s\") (%s:%d) - %s", as,
             bs, a, b, f, l, msg);
    __cccc_fail(f, l, buf);
  }
}
void __cccc_assert_null_msg(const void *p, const char *ps, const char *msg,
                            const char *f, int l) {
  if (p) {
    char b[512];
    snprintf(b, sizeof(b), "%s is not null (%s:%d) - %s", ps, f, l, msg);
    __cccc_fail(f, l, b);
  }
}
void __cccc_assert_not_null_msg(const void *p, const char *ps, const char *msg,
                                const char *f, int l) {
  if (!p) {
    char b[512];
    snprintf(b, sizeof(b), "%s is null (%s:%d) - %s", ps, f, l, msg);
    __cccc_fail(f, l, b);
  }
}
void __cccc_assert_bits_msg(long long mask, long long expected,
                            long long actual, const char *ms, const char *es,
                            const char *as, const char *msg, const char *f,
                            int l) {
  if ((actual & mask) != (expected & mask)) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s&%s=0x%llx!=%s&%s=0x%llx(%s:%d)-%s", as, ms,
             (unsigned long long)(actual & mask), es, ms,
             (unsigned long long)(expected & mask), f, l, msg);
    __cccc_fail(f, l, buf);
  }
}
typedef struct {
  int passed;
  int timed_out;
  char fail_msg[512];
  int64_t ret_int;
  double ret_float;
  char ret_str[256];
} __cccc_result;

static int __cccc_json_first = 1;
static void __cccc_report(int tn, const char *disp, int fmt,
                          const __cccc_result *r, int *passed, int *failed,
                          int *timeouts) {
  if (r->timed_out)
    (*timeouts)++;
  else if (r->passed)
    (*passed)++;
  else
    (*failed)++;
  if (fmt == 0) {
    if (r->timed_out)
      printf("not ok %d - %s # TIMEOUT\n", tn, disp);
    else if (r->passed)
      printf("ok %d - %s\n", tn, disp);
    else
      printf("not ok %d - %s\n  ---\n  message: %s\n  ...\n", tn, disp,
             r->fail_msg);
  } else if (fmt == 1) {
    if (r->timed_out)
      printf("  TIMEOUT  %s\n", disp);
    else if (r->passed)
      printf("  PASS     %s\n", disp);
    else
      printf("  FAIL     %s\n           %s\n", disp, r->fail_msg);
  } else {
    if (!__cccc_json_first)
      printf(",\n");
    __cccc_json_first = 0;
    printf("{\"test\":\"%s\",\"passed\":%s", disp,
           r->passed && !r->timed_out ? "true" : "false");
    if (r->timed_out)
      printf(",\"timeout\":true");
    if (!r->passed && !r->timed_out)
      printf(",\"message\":\"%s\"", r->fail_msg);
    printf("}");
  }
}
