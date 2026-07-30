/*
 CCCC: Comprehensiev C Compensation Compiler

 Copyright (C) 2025 George Watson

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "internal.h"

#include <fnmatch.h>
#include <inttypes.h>
#include <regex.h>
#include <signal.h>
#include <string.h>
#include <sys/time.h>
#ifdef _POSIX_VERSION
#include <sys/wait.h>
#include <unistd.h>
#endif


// Per-run failure state allocated on the stack inside cc_run_tests.
// s_run is set to point at the current run's state before each test loop and
// cleared afterward.  A NULL check in the impl_assert* functions guards against
// calls that arrive outside a test run (e.g. from a [[cccc::macro]] during the
// comptime pass) -- ticket #334.
typedef struct {
    jmp_buf jmp;
    int     failed;
    int     timed_out;
    char    fail_msg[512];
} CCCCTestRunState;

static CCCCTestRunState *s_run = NULL;

static volatile sig_atomic_t s_alarm_fired = 0;
#ifdef _POSIX_VERSION
static volatile pid_t s_fork_child_pid = 0;
#endif
static void handle_alarm(int sig) {
    (void)sig;
    s_alarm_fired = 1;
#ifdef _POSIX_VERSION
    if (s_fork_child_pid > 0) {
        kill(s_fork_child_pid, SIGKILL);
        return; // waitpid in parent will get EINTR
    }
#endif
    if (s_run) longjmp(s_run->jmp, 2); // 2 = timeout sentinel
}

static void impl_assert(long long cond, long long expr, long long file, long long line) {
    if (!cond) {
        if (!s_run) {
            fprintf(stderr, "Assert called outside a test run at %s:%lld\n",
                    (char *)file, line);
            return;
        }
        snprintf(s_run->fail_msg, sizeof(s_run->fail_msg), "%s (%s:%lld)",
                 (char *)expr, (char *)file, line);
        s_run->failed = 1;
        longjmp(s_run->jmp, 1);
    }
}

static void impl_assert_eq(long long a, long long b,
                           long long as, long long bs,
                           long long file, long long line) {
    if (a != b) {
        if (!s_run) {
            fprintf(stderr, "AssertEq called outside a test run at %s:%lld\n",
                    (char *)file, line);
            return;
        }
        snprintf(s_run->fail_msg, sizeof(s_run->fail_msg),
                 "%s != %s (%lld != %lld) (%s:%lld)",
                 (char *)as, (char *)bs, a, b, (char *)file, line);
        s_run->failed = 1;
        longjmp(s_run->jmp, 1);
    }
}

static void impl_assert_neq(long long a, long long b,
                            long long as, long long bs,
                            long long file, long long line) {
    if (a == b) {
        if (!s_run) {
            fprintf(stderr, "AssertNeq called outside a test run at %s:%lld\n",
                    (char *)file, line);
            return;
        }
        snprintf(s_run->fail_msg, sizeof(s_run->fail_msg),
                 "%s == %s (both %lld) (%s:%lld)",
                 (char *)as, (char *)bs, a, (char *)file, line);
        s_run->failed = 1;
        longjmp(s_run->jmp, 1);
    }
}

static void impl_assert_null(long long p, long long ps,
                             long long file, long long line) {
    if (p != 0) {
        if (!s_run) {
            fprintf(stderr, "AssertNull called outside a test run at %s:%lld\n",
                    (char *)file, line);
            return;
        }
        snprintf(s_run->fail_msg, sizeof(s_run->fail_msg), "%s is not null (%s:%lld)",
                 (char *)ps, (char *)file, line);
        s_run->failed = 1;
        longjmp(s_run->jmp, 1);
    }
}

static void impl_assert_not_null(long long p, long long ps,
                                 long long file, long long line) {
    if (p == 0) {
        if (!s_run) {
            fprintf(stderr, "AssertNotNull called outside a test run at %s:%lld\n",
                    (char *)file, line);
            return;
        }
        snprintf(s_run->fail_msg, sizeof(s_run->fail_msg), "%s is null (%s:%lld)",
                 (char *)ps, (char *)file, line);
        s_run->failed = 1;
        longjmp(s_run->jmp, 1);
    }
}

static void impl_assert_streq(long long a, long long b,
                              long long as, long long bs,
                              long long file, long long line) {
    if (strcmp((char *)a, (char *)b) != 0) {
        if (!s_run) {
            fprintf(stderr, "AssertStrEq called outside a test run at %s:%lld\n",
                    (char *)file, line);
            return;
        }
        snprintf(s_run->fail_msg, sizeof(s_run->fail_msg),
                 "%s != %s (\"%s\" != \"%s\") (%s:%lld)",
                 (char *)as, (char *)bs, (char *)a, (char *)b,
                 (char *)file, line);
        s_run->failed = 1;
        longjmp(s_run->jmp, 1);
    }
}

static void impl_assert_false(long long cond, long long expr,
                              long long file, long long line) {
    if (cond) {
        if (!s_run) {
            fprintf(stderr, "AssertFalse called outside a test run at %s:%lld\n",
                    (char *)file, line);
            return;
        }
        snprintf(s_run->fail_msg, sizeof(s_run->fail_msg),
                 "!%s (%s:%lld)", (char *)expr, (char *)file, line);
        s_run->failed = 1;
        longjmp(s_run->jmp, 1);
    }
}

static void impl_assert_fail(long long file, long long line) {
    if (!s_run) {
        fprintf(stderr, "AssertFail called outside a test run at %s:%lld\n",
                (char *)file, line);
        return;
    }
    snprintf(s_run->fail_msg, sizeof(s_run->fail_msg),
             "forced failure (%s:%lld)", (char *)file, line);
    s_run->failed = 1;
    longjmp(s_run->jmp, 1);
}

static void impl_assert_fail_msg(long long msg, long long file, long long line) {
    if (!s_run) {
        fprintf(stderr, "AssertFailMsg called outside a test run at %s:%lld\n",
                (char *)file, line);
        return;
    }
    snprintf(s_run->fail_msg, sizeof(s_run->fail_msg),
             "%s (%s:%lld)", (char *)msg, (char *)file, line);
    s_run->failed = 1;
    longjmp(s_run->jmp, 1);
}

static void impl_assert_gt(long long a, long long b,
                           long long as, long long bs,
                           long long file, long long line) {
    if (!(a > b)) {
        if (!s_run) {
            fprintf(stderr, "AssertGt called outside a test run at %s:%lld\n",
                    (char *)file, line);
            return;
        }
        snprintf(s_run->fail_msg, sizeof(s_run->fail_msg),
                 "%s <= %s (%lld <= %lld) (%s:%lld)",
                 (char *)as, (char *)bs, a, b, (char *)file, line);
        s_run->failed = 1;
        longjmp(s_run->jmp, 1);
    }
}

static void impl_assert_lt(long long a, long long b,
                           long long as, long long bs,
                           long long file, long long line) {
    if (!(a < b)) {
        if (!s_run) {
            fprintf(stderr, "AssertLt called outside a test run at %s:%lld\n",
                    (char *)file, line);
            return;
        }
        snprintf(s_run->fail_msg, sizeof(s_run->fail_msg),
                 "%s >= %s (%lld >= %lld) (%s:%lld)",
                 (char *)as, (char *)bs, a, b, (char *)file, line);
        s_run->failed = 1;
        longjmp(s_run->jmp, 1);
    }
}

static void impl_assert_ge(long long a, long long b,
                           long long as, long long bs,
                           long long file, long long line) {
    if (!(a >= b)) {
        if (!s_run) {
            fprintf(stderr, "AssertGe called outside a test run at %s:%lld\n",
                    (char *)file, line);
            return;
        }
        snprintf(s_run->fail_msg, sizeof(s_run->fail_msg),
                 "%s < %s (%lld < %lld) (%s:%lld)",
                 (char *)as, (char *)bs, a, b, (char *)file, line);
        s_run->failed = 1;
        longjmp(s_run->jmp, 1);
    }
}

static void impl_assert_le(long long a, long long b,
                           long long as, long long bs,
                           long long file, long long line) {
    if (!(a <= b)) {
        if (!s_run) {
            fprintf(stderr, "AssertLe called outside a test run at %s:%lld\n",
                    (char *)file, line);
            return;
        }
        snprintf(s_run->fail_msg, sizeof(s_run->fail_msg),
                 "%s > %s (%lld > %lld) (%s:%lld)",
                 (char *)as, (char *)bs, a, b, (char *)file, line);
        s_run->failed = 1;
        longjmp(s_run->jmp, 1);
    }
}

static void impl_assert_within(long long delta, long long expected, long long actual,
                               long long ds, long long es, long long as,
                               long long file, long long line) {
    long long diff = expected - actual;
    if (diff < 0) diff = -diff;
    if (diff > delta) {
        if (!s_run) {
            fprintf(stderr, "AssertWithin called outside a test run at %s:%lld\n",
                    (char *)file, line);
            return;
        }
        snprintf(s_run->fail_msg, sizeof(s_run->fail_msg),
                 "%s |%s - %s| = %lld > %s (%lld) (%s:%lld)",
                 (char *)as, (char *)es, (char *)as, diff, (char *)ds, delta,
                 (char *)file, line);
        s_run->failed = 1;
        longjmp(s_run->jmp, 1);
    }
}

static void impl_assert_streq_len(long long a, long long b, long long len,
                                  long long as, long long bs,
                                  long long file, long long line) {
    if (strncmp((char *)a, (char *)b, (size_t)len) != 0) {
        if (!s_run) {
            fprintf(stderr, "AssertStrEqLen called outside a test run at %s:%lld\n",
                    (char *)file, line);
            return;
        }
        snprintf(s_run->fail_msg, sizeof(s_run->fail_msg),
                 "%s != %s (first %lld chars differ) (%s:%lld)",
                 (char *)as, (char *)bs, len, (char *)file, line);
        s_run->failed = 1;
        longjmp(s_run->jmp, 1);
    }
}

static void impl_assert_mem_eq(long long expected, long long actual, long long len,
                               long long es, long long as,
                               long long file, long long line) {
    if (memcmp((void *)expected, (void *)actual, (size_t)len) != 0) {
        if (!s_run) {
            fprintf(stderr, "AssertMemEq called outside a test run at %s:%lld\n",
                    (char *)file, line);
            return;
        }
        snprintf(s_run->fail_msg, sizeof(s_run->fail_msg),
                 "%s != %s (%lld bytes differ) (%s:%lld)",
                 (char *)es, (char *)as, len, (char *)file, line);
        s_run->failed = 1;
        longjmp(s_run->jmp, 1);
    }
}

static void impl_assert_float_within(double delta, double expected, double actual,
                                     long long ds, long long es, long long as,
                                     long long file, long long line) {
    double diff = expected - actual;
    if (diff < 0) diff = -diff;
    if (diff > delta) {
        if (!s_run) {
            fprintf(stderr, "AssertFloatWithin called outside a test run at %s:%lld\n",
                    (char *)file, line);
            return;
        }
        snprintf(s_run->fail_msg, sizeof(s_run->fail_msg),
                 "%s |%s - %s| = %g > %s (%g) (%s:%lld)",
                 (char *)as, (char *)es, (char *)as, diff,
                 (char *)ds, delta, (char *)file, line);
        s_run->failed = 1;
        longjmp(s_run->jmp, 1);
    }
}

static void impl_assert_double_within(double delta, double expected, double actual,
                                      long long ds, long long es, long long as,
                                      long long file, long long line) {
    // Identical to impl_assert_float_within — the C ABI for doubles is the same.
    impl_assert_float_within(delta, expected, actual, ds, es, as, file, line);
}

static void impl_assert_bits(long long mask, long long expected, long long actual,
                             long long ms, long long es, long long as,
                             long long file, long long line) {
    if ((actual & mask) != (expected & mask)) {
        if (!s_run) {
            fprintf(stderr, "AssertBits called outside a test run at %s:%lld\n",
                    (char *)file, line);
            return;
        }
        snprintf(s_run->fail_msg, sizeof(s_run->fail_msg),
                 "%s & %s = 0x%llx != %s & %s = 0x%llx (%s:%lld)",
                 (char *)as, (char *)ms, (unsigned long long)(actual & mask),
                 (char *)es, (char *)ms, (unsigned long long)(expected & mask),
                 (char *)file, line);
        s_run->failed = 1;
        longjmp(s_run->jmp, 1);
    }
}

static void impl_assert_bit_high(long long bit, long long actual,
                                 long long bs, long long as,
                                 long long file, long long line) {
    if (!(actual & (1LL << bit))) {
        if (!s_run) {
            fprintf(stderr, "AssertBitHigh called outside a test run at %s:%lld\n",
                    (char *)file, line);
            return;
        }
        snprintf(s_run->fail_msg, sizeof(s_run->fail_msg),
                 "%s bit %lld of %s is low (%s:%lld)",
                 (char *)bs, bit, (char *)as, (char *)file, line);
        s_run->failed = 1;
        longjmp(s_run->jmp, 1);
    }
}

static void impl_assert_bit_low(long long bit, long long actual,
                                long long bs, long long as,
                                long long file, long long line) {
    if (actual & (1LL << bit)) {
        if (!s_run) {
            fprintf(stderr, "AssertBitLow called outside a test run at %s:%lld\n",
                    (char *)file, line);
            return;
        }
        snprintf(s_run->fail_msg, sizeof(s_run->fail_msg),
                 "%s bit %lld of %s is high (%s:%lld)",
                 (char *)bs, bit, (char *)as, (char *)file, line);
        s_run->failed = 1;
        longjmp(s_run->jmp, 1);
    }
}

static void impl_assert_eq_array(long long expected, long long actual,
                                 long long elem_size, long long count,
                                 long long es, long long as,
                                 long long file, long long line) {
    size_t total = (size_t)elem_size * (size_t)count;
    if (memcmp((void *)expected, (void *)actual, total) != 0) {
        if (!s_run) {
            fprintf(stderr, "AssertArrayEq called outside a test run at %s:%lld\n",
                    (char *)file, line);
            return;
        }
        snprintf(s_run->fail_msg, sizeof(s_run->fail_msg),
                 "%s[0..%lld] != %s[0..%lld] (%lld bytes differ) (%s:%lld)",
                 (char *)es, count - 1, (char *)as, count - 1,
                 (long long)total, (char *)file, line);
        s_run->failed = 1;
        longjmp(s_run->jmp, 1);
    }
}

// Message-appending variants — append msg to the fail message after the main diagnostic.

static void impl_assert_msg(long long cond, long long expr, long long msg,
                            long long file, long long line) {
    if (!cond) {
        if (!s_run) {
            fprintf(stderr, "AssertMsg called outside a test run at %s:%lld\n",
                    (char *)file, line);
            return;
        }
        snprintf(s_run->fail_msg, sizeof(s_run->fail_msg),
                 "%s (%s:%lld) - %s", (char *)expr, (char *)file, line, (char *)msg);
        s_run->failed = 1;
        longjmp(s_run->jmp, 1);
    }
}

static void impl_assert_eq_msg(long long a, long long b,
                               long long as, long long bs, long long msg,
                               long long file, long long line) {
    if (a != b) {
        if (!s_run) {
            fprintf(stderr, "AssertEqMsg called outside a test run at %s:%lld\n",
                    (char *)file, line);
            return;
        }
        snprintf(s_run->fail_msg, sizeof(s_run->fail_msg),
                 "%s != %s (%lld != %lld) (%s:%lld) - %s",
                 (char *)as, (char *)bs, a, b, (char *)file, line, (char *)msg);
        s_run->failed = 1;
        longjmp(s_run->jmp, 1);
    }
}

static void impl_assert_streq_msg(long long a, long long b,
                                  long long as, long long bs, long long msg,
                                  long long file, long long line) {
    if (strcmp((char *)a, (char *)b) != 0) {
        if (!s_run) {
            fprintf(stderr, "AssertStrEqMsg called outside a test run at %s:%lld\n",
                    (char *)file, line);
            return;
        }
        snprintf(s_run->fail_msg, sizeof(s_run->fail_msg),
                 "%s != %s (\"%s\" != \"%s\") (%s:%lld) - %s",
                 (char *)as, (char *)bs, (char *)a, (char *)b,
                 (char *)file, line, (char *)msg);
        s_run->failed = 1;
        longjmp(s_run->jmp, 1);
    }
}

static void impl_assert_null_msg(long long p, long long ps, long long msg,
                                 long long file, long long line) {
    if (p != 0) {
        if (!s_run) {
            fprintf(stderr, "AssertNullMsg called outside a test run at %s:%lld\n",
                    (char *)file, line);
            return;
        }
        snprintf(s_run->fail_msg, sizeof(s_run->fail_msg),
                 "%s is not null (%s:%lld) - %s",
                 (char *)ps, (char *)file, line, (char *)msg);
        s_run->failed = 1;
        longjmp(s_run->jmp, 1);
    }
}

static void impl_assert_not_null_msg(long long p, long long ps, long long msg,
                                     long long file, long long line) {
    if (p == 0) {
        if (!s_run) {
            fprintf(stderr, "AssertNotNullMsg called outside a test run at %s:%lld\n",
                    (char *)file, line);
            return;
        }
        snprintf(s_run->fail_msg, sizeof(s_run->fail_msg),
                 "%s is null (%s:%lld) - %s",
                 (char *)ps, (char *)file, line, (char *)msg);
        s_run->failed = 1;
        longjmp(s_run->jmp, 1);
    }
}

static void impl_assert_bits_msg(long long mask, long long expected, long long actual,
                                 long long ms, long long es, long long as, long long msg,
                                 long long file, long long line) {
    if ((actual & mask) != (expected & mask)) {
        if (!s_run) {
            fprintf(stderr, "AssertBitsMsg called outside a test run at %s:%lld\n",
                    (char *)file, line);
            return;
        }
        snprintf(s_run->fail_msg, sizeof(s_run->fail_msg),
                 "%s & %s = 0x%llx != %s & %s = 0x%llx (%s:%lld) - %s",
                 (char *)as, (char *)ms, (unsigned long long)(actual & mask),
                 (char *)es, (char *)ms, (unsigned long long)(expected & mask),
                 (char *)file, line, (char *)msg);
        s_run->failed = 1;
        longjmp(s_run->jmp, 1);
    }
}

// Register native assertion functions so the compiler can emit FFI calls to
// them. Must be called before cc_compile (but after cc_execute_inline_macros
// to avoid registering these symbols during the comptime pass -- ticket #334).
void cc_load_test_runtime(VirtualMachine *vm) {
    cc_register_cfunc(vm, "__builtin_assert",             (void *)impl_assert,              4, 0);
    cc_register_cfunc(vm, "__builtin_assert_false",       (void *)impl_assert_false,         4, 0);
    cc_register_cfunc(vm, "__builtin_assert_fail",        (void *)impl_assert_fail,          2, 0);
    cc_register_cfunc(vm, "__builtin_assert_fail_msg",    (void *)impl_assert_fail_msg,      3, 0);
    cc_register_cfunc(vm, "__builtin_assert_eq",          (void *)impl_assert_eq,            6, 0);
    cc_register_cfunc(vm, "__builtin_assert_neq",         (void *)impl_assert_neq,           6, 0);
    cc_register_cfunc(vm, "__builtin_assert_gt",          (void *)impl_assert_gt,            6, 0);
    cc_register_cfunc(vm, "__builtin_assert_lt",          (void *)impl_assert_lt,            6, 0);
    cc_register_cfunc(vm, "__builtin_assert_ge",          (void *)impl_assert_ge,            6, 0);
    cc_register_cfunc(vm, "__builtin_assert_le",          (void *)impl_assert_le,            6, 0);
    cc_register_cfunc(vm, "__builtin_assert_within",      (void *)impl_assert_within,        8, 0);
    cc_register_cfunc(vm, "__builtin_assert_null",        (void *)impl_assert_null,          4, 0);
    cc_register_cfunc(vm, "__builtin_assert_not_null",    (void *)impl_assert_not_null,      4, 0);
    cc_register_cfunc(vm, "__builtin_assert_streq",       (void *)impl_assert_streq,         6, 0);
    cc_register_cfunc(vm, "__builtin_assert_streq_len",   (void *)impl_assert_streq_len,     7, 0);
    cc_register_cfunc(vm, "__builtin_assert_mem_eq",      (void *)impl_assert_mem_eq,        7, 0);
    cc_register_cfunc(vm, "__builtin_assert_float_within",(void *)impl_assert_float_within,  8, 0);
    cc_register_cfunc(vm, "__builtin_assert_double_within",(void *)impl_assert_double_within,8, 0);
    cc_register_cfunc(vm, "__builtin_assert_bits",        (void *)impl_assert_bits,          8, 0);
    cc_register_cfunc(vm, "__builtin_assert_bit_high",    (void *)impl_assert_bit_high,      6, 0);
    cc_register_cfunc(vm, "__builtin_assert_bit_low",     (void *)impl_assert_bit_low,       6, 0);
    cc_register_cfunc(vm, "__builtin_assert_eq_array",    (void *)impl_assert_eq_array,      8, 0);
    cc_register_cfunc(vm, "__builtin_assert_msg",         (void *)impl_assert_msg,           5, 0);
    cc_register_cfunc(vm, "__builtin_assert_eq_msg",      (void *)impl_assert_eq_msg,        7, 0);
    cc_register_cfunc(vm, "__builtin_assert_streq_msg",   (void *)impl_assert_streq_msg,     7, 0);
    cc_register_cfunc(vm, "__builtin_assert_null_msg",    (void *)impl_assert_null_msg,      5, 0);
    cc_register_cfunc(vm, "__builtin_assert_not_null_msg",(void *)impl_assert_not_null_msg,  5, 0);
    cc_register_cfunc(vm, "__builtin_assert_bits_msg",    (void *)impl_assert_bits_msg,      9, 0);
}

// Preprocess src/testing.h (loaded via the embedded std registry). As a
// side effect, registers all CCCC_ASSERT* macros in vm->compiler.macros so
// they expand correctly when the test file is preprocessed. Returns the
// processed declaration tokens to prepend to the parse stream.
Token *cc_inject_test_header(VirtualMachine *vm) {
    char *src = get_std_header("testing.h");
    if (!src)
        error("could not load embedded testing.h — run `make bootstrap` (or `sh tools/regen_stdlib.sh <cccc>`) to regenerate src/std.c");
    Token *toks = tokenize_string(vm, "<testing.h>", src);
    return preprocess(vm, toks);
}

// Returns the display name for a test (name = "..." or C function name).
static const char *test_display_name(const TestFnRecord *r) {
    return r->display_name ? r->display_name : r->name;
}

// Find a compiled function object by C name.
static Obj *find_fn(Obj *prog, const char *name) {
    for (Obj *o = prog; o; o = o->next)
        if (o->is_function && o->name && strcmp(o->name, name) == 0)
            return o;
    return NULL;
}

// Returns true if `suite` matches `filter` for --test-suite filtering.
// Matching rules (filter is never NULL when this is called):
//   - Exact match: suite == filter
//   - Prefix match: suite starts with filter followed by '/' (sub-suite selection)
//   - Glob match: filter contains glob metacharacters (*?[), use fnmatch
// Hook suite matching: exact by default; when a hook has inherit=true it uses
// suite_matches so the hook covers the named suite and all sub-suites (#515).
static bool suite_matches(const char *suite, const char *filter) {
    const char *s = suite ? suite : "";
    if (strpbrk(filter, "*?["))
        return fnmatch(filter, s, 0) == 0;
    size_t n = strlen(filter);
    return strncmp(s, filter, n) == 0 && (s[n] == '\0' || s[n] == '/');
}

// Run a single hook function by name (no-op if not found).
static void run_hook(VirtualMachine *vm, Obj *prog, const char *fn_name) {
    Obj *fn = find_fn(prog, fn_name);
    if (fn)
        cc_run_at(vm, (Pc)fn->code_addr, 0, NULL);
}

// Run all matching setup or teardown hooks.
// suite and disp are the current test's context; NULL suite means no active suite.
// once_only=true selects once-per-suite hooks; false selects per-test hooks.
static void run_hooks(VirtualMachine *vm, Obj *prog, TestSetupRecord *setups,
                      bool is_teardown, bool once_only,
                      const char *suite, const char *disp) {
    for (TestSetupRecord *s = setups; s; s = s->next) {
        if (s->is_teardown != is_teardown) continue;
        if (s->once       != once_only)   continue;
        // Suite filter: if the hook targets a specific suite, it must match.
        // With inherit=true, use hierarchical suite_matches; otherwise exact.
        if (s->suite) {
            if (!suite) continue;
            if (s->inherit ? !suite_matches(suite, s->suite)
                           : strcmp(s->suite, suite) != 0) continue;
        }
        // Name-pattern filter.
        // For per-test (once_only=false): run if name_pat matches or no name_pat.
        // For once-only (once_only=true): name_pat-only hooks are handled
        // separately by the first-match / end-of-tests logic in cc_run_tests,
        // so skip them here (they have already fired or will fire later).
        if (s->name_pat) {
            if (once_only) continue; // handled separately for once-hooks
            if (!disp || fnmatch(s->name_pat, disp, 0) != 0) continue;
        }
        run_hook(vm, prog, s->fn_name);
    }
}

// Run hooks in an isolated setjmp context so that CCCC_ASSERT failures are
// caught safely regardless of whether s_run is currently live.  Returns true
// if all hooks completed without failure.  If fail_msg_out is non-NULL and a
// failure occurs, the message (up to 511 chars) is written there.
static bool run_hooks_guarded(VirtualMachine *vm, Obj *prog, TestSetupRecord *setups,
                              bool is_teardown, bool once_only,
                              const char *suite, const char *disp,
                              char fail_msg_out[512]) {
    CCCCTestRunState guard;
    guard.failed    = 0;
    guard.timed_out = 0;
    guard.fail_msg[0] = '\0';

    CCCCTestRunState *saved = s_run;
    s_run = &guard;
    int jval = setjmp(guard.jmp);
    if (jval == 0)
        run_hooks(vm, prog, setups, is_teardown, once_only, suite, disp);
    else
        guard.failed = 1;
    s_run = saved;

    if (guard.failed && fail_msg_out)
        strncpy(fail_msg_out, guard.fail_msg, 511);

    return !guard.failed;
}

// Convenience wrapper for suite-boundary once-hooks.
static bool run_once_hooks(VirtualMachine *vm, Obj *prog, TestSetupRecord *setups,
                           bool is_teardown, const char *suite) {
    return run_hooks_guarded(vm, prog, setups, is_teardown, true,
                             suite, NULL, NULL);
}

// Streaming once-suite-hook state (#515). Tracks one once-suite-setup hook
// through its open (setup fired) → closed (teardown fired) lifecycle.
typedef struct {
    TestSetupRecord *hook; // points into the local setups copy
    bool  open;            // setup has fired; teardown has not
    char *snap;            // data-segment snapshot taken right after setup ran
} OnceSuiteHook;

// Returns the innermost open once-suite-hook's snapshot, or fallback
// (cur_snap / base_snap) when no suite hooks are active.
static char *once_suite_snap(OnceSuiteHook *hs, int n, char *fallback) {
    for (int i = n - 1; i >= 0; i--)
        if (hs[i].open) return hs[i].snap;
    return fallback;
}

// Escapes a string for JSON output (writes to buf, returns buf).
static const char *json_esc(const char *s, char *buf, size_t size) {
    size_t i = 0, j = 0;
    while (s[i] && j + 6 < size) {
        if (s[i] == '\\') { buf[j++] = '\\'; buf[j++] = '\\'; i++; }
        else if (s[i] == '"') { buf[j++] = '\\'; buf[j++] = '"'; i++; }
        else if (s[i] == '\n') { buf[j++] = '\\'; buf[j++] = 'n'; i++; }
        else if (s[i] == '\r') { buf[j++] = '\\'; buf[j++] = 'r'; i++; }
        else if (s[i] == '\t') { buf[j++] = '\\'; buf[j++] = 't'; i++; }
        else { buf[j++] = s[i]; i++; }
    }
    buf[j] = '\0';
    return buf;
}

static void emit_test_result(CcTestFormat fmt, const char *name,
                              const char *suite, const char *status,
                              const char *message, bool *json_first,
                              int test_num) {
    char ebuf[4096];
    switch (fmt) {
    case TEST_FORMAT_TAP:
        if (strcmp(status, "skip") == 0) {
            printf("not ok %d - %s # SKIP (not found in compiled output)\n",
                   test_num, name);
        } else if (strcmp(status, "timeout") == 0) {
            printf("not ok %d - %s # TIMEOUT\n", test_num, name);
        } else if (message) {
            printf("not ok %d - %s\n", test_num, name);
            printf("  ---\n  message: %s\n  ...\n", message);
        } else {
            printf("ok %d - %s\n", test_num, name);
        }
        break;
    case TEST_FORMAT_PLAIN: {
        const char *prefix;
        if (strcmp(status, "pass") == 0)       prefix = "  \xe2\x9c\x93";
        else if (strcmp(status, "neg_pass") == 0) prefix = "  \xe2\x9c\x93";
        else if (strcmp(status, "skip") == 0)  prefix = "  -";
        else                                   prefix = "  \xe2\x9c\x97";
        if (message)
            printf("%s %s (%s)\n", prefix, name, message);
        else if (strcmp(status, "neg_pass") == 0)
            printf("%s %s (correctly rejected)\n", prefix, name);
        else
            printf("%s %s\n", prefix, name);
        break;
    }
    case TEST_FORMAT_JSON:
        if (!*json_first) printf(",\n");
        *json_first = false;
        printf("  {\"name\":\"%s\"", json_esc(name, ebuf, sizeof(ebuf)));
        if (suite)
            printf(",\"suite\":\"%s\"", json_esc(suite, ebuf, sizeof(ebuf)));
        printf(",\"status\":\"%s\"", status);
        if (message)
            printf(",\"message\":\"%s\"", json_esc(message, ebuf, sizeof(ebuf)));
        printf("}");
        break;
    }
}

// Thin wrapper used inside cc_run_tests to build ordered/filtered lists without
// copying TestFnRecord.  Pointing at the original record means new fields are
// always visible — no manual sync required (ticket #337).
typedef struct TestListNode {
    TestFnRecord    *rec;
    struct TestListNode *next;
} TestListNode;

// drain_pipe: read all data from fd until EOF, return malloc'd NUL-terminated
// string. The caller must free the returned buffer. Returns "" (not NULL) on
// empty or error so pattern checks always have a valid string to match against.
#ifdef _POSIX_VERSION
static char *drain_pipe(int fd) {
    char tmp[4096];
    size_t total = 0;
    char *buf = malloc(1);
    buf[0] = '\0';
    ssize_t n;
    while ((n = read(fd, tmp, sizeof(tmp))) > 0) {
        buf = realloc(buf, total + (size_t)n + 1);
        memcpy(buf + total, tmp, (size_t)n);
        total += (size_t)n;
    }
    buf[total] = '\0';
    return buf;
}

// check_output_pattern: compile POSIX ERE and search buf.
// negate=false: FAIL if pattern does NOT match.
// negate=true:  FAIL if pattern DOES match.
// Returns true on success (assertion passes), false on failure.
// On failure writes a message into fail_msg[512].
static bool check_output_pattern(const char *pat, const char *buf,
                                  bool negate, const char *label,
                                  char fail_msg[512]) {
    if (!pat) return true;
    if (!buf) buf = "";
    regex_t re;
    int rc = regcomp(&re, pat, REG_EXTENDED);
    if (rc != 0) {
        char errbuf[128];
        regerror(rc, &re, errbuf, sizeof(errbuf));
        snprintf(fail_msg, 512, "%s \"%s\" (regex error: %s)", label, pat, errbuf);
        return false;
    }
    int matched = (regexec(&re, buf, 0, NULL, 0) == 0);
    regfree(&re);
    if (!negate && !matched) {
        snprintf(fail_msg, 512, "%s \"%s\"", label, pat);
        return false;
    }
    if (negate && matched) {
        snprintf(fail_msg, 512, "%s \"%s\"", label, pat);
        return false;
    }
    return true;
}
#endif

int cc_run_tests(VirtualMachine *vm, Obj *prog, const CcTestOptions *opts) {

    // Reverse setup records to declaration order (built by prepending).
    TestSetupRecord *setups = NULL;
    for (TestSetupRecord *s = vm->compiler.test_setups; s; s = s->next) {
        TestSetupRecord *copy = malloc(sizeof(TestSetupRecord));
        *copy      = *s;
        copy->next = setups;
        setups     = copy;
    }

    // Build streaming once-suite-hook state array (#515).
    int n_once_suite = 0;
    for (TestSetupRecord *s = setups; s; s = s->next)
        if (!s->is_teardown && s->once && s->suite) n_once_suite++;
    OnceSuiteHook *once_suite = calloc(n_once_suite > 0 ? n_once_suite : 1,
                                       sizeof(OnceSuiteHook));
    {
        int idx = 0;
        for (TestSetupRecord *s = setups; s; s = s->next)
            if (!s->is_teardown && s->once && s->suite)
                once_suite[idx++].hook = s;
    }

    // Reverse test records to run in declaration order (test_fns is built by prepending).
    TestListNode *ordered = NULL;
    for (TestFnRecord *r = vm->compiler.test_fns; r; r = r->next) {
        TestListNode *node = malloc(sizeof(TestListNode));
        node->rec  = r;
        node->next = ordered;
        ordered    = node;
    }

    // Apply filters to build the active list.
    TestListNode *filtered = NULL;
    TestListNode **tail = &filtered;
    int n = 0;
    for (TestListNode *n2 = ordered; n2; n2 = n2->next) {
        TestFnRecord *r = n2->rec;
        const char *disp = test_display_name(r);
        if (opts && opts->suite_filter) {
            if (!suite_matches(r->suite, opts->suite_filter))
                continue;
        }
        if (opts && opts->test_glob) {
            // Match against the display name so name = "..." aliases are filterable.
            if (fnmatch(opts->test_glob, disp, 0) != 0)
                continue;
        }
        TestListNode *node = malloc(sizeof(TestListNode));
        node->rec  = r;
        node->next = NULL;
        *tail = node;
        tail  = &node->next;
        n++;
    }

    // --list-tests: enumerate without running.
    if (opts && opts->list_only) {
        printf("# Tests (%d total):\n", n);
        for (TestListNode *n2 = filtered; n2; n2 = n2->next) {
            TestFnRecord *r = n2->rec;
            const char *disp = test_display_name(r);
            if (r->suite)
                printf("%-40s [suite: %s]\n", disp, r->suite);
            else
                printf("%s\n", disp);
        }
        for (TestListNode *n2 = ordered,  *nx; n2; n2 = nx) { nx = n2->next; free(n2); }
        for (TestListNode *n2 = filtered, *nx; n2; n2 = nx) { nx = n2->next; free(n2); }
        for (TestSetupRecord *s = setups, *nx; s; s = nx) { nx = s->next; free(s); }
        return 0;
    }

    CcTestFormat fmt = opts ? opts->format : TEST_FORMAT_TAP;

    switch (fmt) {
    case TEST_FORMAT_TAP:
        printf("TAP version 13\n");
        printf("1..%d\n", n);
        break;
    case TEST_FORMAT_PLAIN:
        if (n == 0)
            printf("No tests found.\n");
        else
            printf("Running %d test%s...\n", n, n == 1 ? "" : "s");
        break;
    case TEST_FORMAT_JSON:
        printf("[\n");
        break;
    }

    int passed = 0;
    int test_num = 0;
    int total_failures = 0;
    int total_skipped = 0;
    int total_timeouts = 0;
    int total_neg_pass = 0;
    int total_neg_fail = 0;
    int total_neg = 0;
    const char *prev_suite = NULL;
    bool json_first = true;
    bool any_timeout_possible = false;

    CCCCTestRunState run;
    s_run = &run;

    // Register SIGALRM handler for timeouts (always, since per-test timeouts
    // may be set on individual [[cccc::test]] attributes).
    signal(SIGALRM, handle_alarm);

    // Helper: arm/cancel timer with millisecond precision via setitimer.
    // Called with ms <= 0 to cancel, ms > 0 to set a one-shot timer.
    // Defined here (C89-compatible) so the lambda-like usage reads inline.
    long global_timeout_ms = (opts && opts->test_timeout > 0) ? opts->test_timeout * 1000L : 0;
    #define SET_TIMEOUT(ms) do {                                    \
        long _t = (ms);                                             \
        if (_t > 0) {                                               \
            struct itimerval _itv;                                  \
            _itv.it_interval.tv_sec  = 0;                           \
            _itv.it_interval.tv_usec = 0;                           \
            _itv.it_value.tv_sec     = _t / 1000;                   \
            _itv.it_value.tv_usec    = (_t % 1000) * 1000;          \
            setitimer(ITIMER_REAL, &_itv, NULL);                    \
        } else {                                                    \
            struct itimerval _zero = {{0,0},{0,0}};                 \
            setitimer(ITIMER_REAL, &_zero, NULL);                   \
        }                                                           \
    } while (0)

    size_t snap_size  = (size_t)(vm->data_ptr - vm->data_seg);
    char  *base_snap  = malloc(snap_size);
    memcpy(base_snap, vm->data_seg, snap_size);
    char  *cur_snap   = base_snap;

    // Per-test flags (#356, #612): capture the base compiled config.  Safety,
    // optimisation, warning, and -f pass flags are baked into codegen, so any
    // test that requests a different config triggers a lazy recompile.
    uint32_t base_vm_flags        = vm->flags;
    int      base_opt_lvl         = vm->compiler.opt_level;
    uint64_t base_warnings        = vm->compiler.warnings;
    uint64_t base_warn_errs       = vm->compiler.warning_errors;
    bool     base_warn_ae         = vm->warnings_as_errors;
    uint32_t base_f_enable        = vm->compiler.opt_f_enable;
    uint32_t base_f_disable       = vm->compiler.opt_f_disable;
    int      base_ffi_allow_count = vm->ffi_allow_count;
    uint32_t cur_vm_flags         = base_vm_flags;
    int      cur_opt_lvl          = base_opt_lvl;
    uint64_t cur_warnings         = base_warnings;
    uint64_t cur_warn_errs        = base_warn_errs;
    bool     cur_warn_ae          = base_warn_ae;
    uint32_t cur_f_enable         = base_f_enable;
    uint32_t cur_f_disable        = base_f_disable;
    int      cur_ffi_allow_count  = base_ffi_allow_count;

    bool stop_early = false;
    for (TestListNode *n2 = filtered; n2 && !stop_early; n2 = n2->next) {
        TestFnRecord *r = n2->rec;
        test_num++;
        const char *disp      = test_display_name(r);
        const char *cur_suite = r->suite;

        // Streaming once-suite-hook close pass (#515): close innermost-first any
        // hook whose subtree no longer covers this test.  "Subtree" uses
        // suite_matches regardless of the hook's inherit flag, so a hook on "a"
        // stays open across a "a/b" dip, preventing re-entry double-fire.
        for (int _i = n_once_suite - 1; _i >= 0; _i--) {
            if (!once_suite[_i].open) continue;
            const char *hs = once_suite[_i].hook->suite;
            if (suite_matches(cur_suite ? cur_suite : "", hs)) continue;
            if (!run_once_hooks(vm, prog, setups, true, hs)) {
                if (fmt == TEST_FORMAT_TAP)
                    printf("# once-teardown for suite \"%s\" failed\n", hs);
                else if (fmt == TEST_FORMAT_PLAIN)
                    printf("  ! once-teardown for suite \"%s\" failed\n", hs);
            }
            once_suite[_i].open = false;
            free(once_suite[_i].snap);
            once_suite[_i].snap = NULL;
        }

        // Suite-change header (purely cosmetic; hook lifecycle is now streaming).
        bool suite_changed = (cur_suite != prev_suite) &&
                             (cur_suite == NULL || prev_suite == NULL ||
                              strcmp(cur_suite, prev_suite) != 0);
        if (suite_changed) {
            if (fmt == TEST_FORMAT_TAP) {
                if (cur_suite)
                    printf("# Suite: %s\n", cur_suite);
                else
                    printf("# Suite: (none)\n");
            } else if (fmt == TEST_FORMAT_PLAIN) {
                printf("── %s ──\n", cur_suite ? cur_suite : "(no suite)");
            }
            prev_suite = cur_suite;
        }

        // Streaming once-suite-hook open pass (#515): open outermost-first any
        // hook not yet fired that covers this test.
        for (int _i = 0; _i < n_once_suite; _i++) {
            if (once_suite[_i].hook->once_fired) continue;
            const char *hs = once_suite[_i].hook->suite;
            bool covers = once_suite[_i].hook->inherit
                              ? suite_matches(cur_suite ? cur_suite : "", hs)
                              : (cur_suite && strcmp(cur_suite, hs) == 0);
            if (!covers) continue;
            // Restore from current top-of-stack before firing this setup.
            memcpy(vm->data_seg, once_suite_snap(once_suite, _i, cur_snap), snap_size);
            run_hook(vm, prog, once_suite[_i].hook->fn_name);
            once_suite[_i].snap = malloc(snap_size);
            memcpy(once_suite[_i].snap, vm->data_seg, snap_size);
            once_suite[_i].open            = true;
            once_suite[_i].hook->once_fired = true;
        }

        if (r->error_pat || r->expect_compile_error) {
            if (r->neg_passed == 1) {
                emit_test_result(fmt, disp, cur_suite, "neg_pass", NULL, &json_first, test_num);
                passed++;
                total_neg_pass++;
                total_neg++;
            } else {
                const char *reason = (r->neg_passed == 0)
                    ? "expected compilation error but code compiled successfully"
                    : r->neg_actual;
                emit_test_result(fmt, disp, cur_suite, "neg_fail", reason, &json_first, test_num);
                total_neg_fail++;
                total_neg++;
                if (opts && opts->fail_fast) stop_early = true;
            }
            continue;
        }

        // --- Lazy recompile for per-test flags (#356, #612) ---
        // flags= on the test attribute may specify a different codegen config
        // (safety level, optimisation level, individual checks, warning flags,
        // optimisation-pass enables/disables).  Since those are baked in at
        // cc_compile() time, we recompile the whole program whenever the
        // required config differs from what is currently compiled.
        // Adjacent tests sharing the same flags share one compile; the unflagged
        // base-config compile is also reused across consecutive unflagged tests.
#ifdef _POSIX_VERSION
        char *captured_compile_stderr = NULL;
#endif
        {
            uint32_t req_flags;
            int      req_opt;
            uint64_t req_warnings;
            uint64_t req_warn_errs;
            bool     req_warn_ae;
            uint32_t req_f_enable;
            uint32_t req_f_disable;

            bool has_any_flags = r->test_flags_mask || r->test_opt_set
                              || r->test_warn_mask   || r->test_warn_errors_mask
                              || r->test_warn_as_errors_set || r->test_f_set
                              || r->test_ffi_allow_count > 0;

            if (has_any_flags) {
                req_flags     = (base_vm_flags & ~r->test_flags_mask) | r->test_flags_or;
                req_opt       = r->test_opt_set ? r->test_opt_level : base_opt_lvl;
                req_warnings  = (base_warnings  & ~r->test_warn_mask)        | r->test_warn_or;
                req_warn_errs = (base_warn_errs & ~r->test_warn_errors_mask) | r->test_warn_errors_or;
                req_warn_ae   = r->test_warn_as_errors_set ? r->test_warn_as_errors : base_warn_ae;
                req_f_enable  = (base_f_enable  & ~r->test_f_disable) | r->test_f_enable;
                req_f_disable = (base_f_disable & ~r->test_f_enable)  | r->test_f_disable;
            } else {
                req_flags     = base_vm_flags;
                req_opt       = base_opt_lvl;
                req_warnings  = base_warnings;
                req_warn_errs = base_warn_errs;
                req_warn_ae   = base_warn_ae;
                req_f_enable  = base_f_enable;
                req_f_disable = base_f_disable;
            }

            // Determine whether the per-test ffi-allow list differs from current.
            int req_ffi_allow_count = r->test_ffi_allow_count;
            bool ffi_allow_changed  = (req_ffi_allow_count !=
                                       cur_ffi_allow_count - base_ffi_allow_count);
            if (!ffi_allow_changed && req_ffi_allow_count > 0) {
                for (int i = 0; i < req_ffi_allow_count && !ffi_allow_changed; i++) {
                    if (strcmp(r->test_ffi_allow[i],
                               vm->ffi_allow_list[base_ffi_allow_count + i]) != 0)
                        ffi_allow_changed = true;
                }
            }

            bool needs_recompile = (req_flags    != cur_vm_flags)
                                || (req_opt       != cur_opt_lvl)
                                || (req_warnings  != cur_warnings)
                                || (req_warn_errs != cur_warn_errs)
                                || (req_warn_ae   != cur_warn_ae)
                                || (req_f_enable  != cur_f_enable)
                                || (req_f_disable != cur_f_disable)
                                || ffi_allow_changed;

            if (needs_recompile) {
                // Discard any per-once-hook snapshot that references the old compile.
                if (cur_snap != base_snap) { free(cur_snap); cur_snap = base_snap; }
                // Zero-clear and reset the data segment so gen() re-emits all
                // globals from scratch (avoids stale bytes for zero-init globals).
                memset(vm->data_seg, 0, snap_size);
                vm->data_ptr = vm->data_seg;

                vm->flags                        = req_flags;
                vm->compiler.opt_level           = req_opt;
                vm->compiler.ffp_contract_fma    = (req_flags & CCCC_FMA) != 0;
                vm->ffi_errors_fatal             = (req_flags & CCCC_FFI_ERRORS_FATAL) ? 1 : 0;
                vm->compiler.warnings            = req_warnings;
                vm->compiler.warning_errors      = req_warn_errs;
                vm->warnings_as_errors           = req_warn_ae;
                vm->compiler.opt_f_enable        = req_f_enable;
                vm->compiler.opt_f_disable       = req_f_disable;
                // Apply per-test ffi-allow: truncate to base then add new entries.
                if (ffi_allow_changed) {
                    for (int i = base_ffi_allow_count; i < vm->ffi_allow_count; i++)
                        free(vm->ffi_allow_list[i]);
                    vm->ffi_allow_count = base_ffi_allow_count;
                    for (int i = 0; i < req_ffi_allow_count; i++)
                        cc_ffi_allow(vm, r->test_ffi_allow[i]);
                    cur_ffi_allow_count = base_ffi_allow_count + req_ffi_allow_count;
                }

                // Capture compile-time stderr when the test asserts on it (#621).
                // This covers warnings/errors emitted by cc_compile() during the
                // recompile, which happen before the runtime fd-redirect below.
                // Do NOT redirect here for exit_code= tests — those use a fork path
                // that comes later and must not inherit a hijacked stderr.
#ifdef _POSIX_VERSION
                bool   needs_compile_capture = (r->expect_stderr || r->reject_stderr)
                                              && r->expect_exit_code < 0;
                int    saved_compile_err = -1;
                int    compile_pipe[2]   = {-1, -1};
                if (needs_compile_capture) {
                    if (pipe(compile_pipe) == 0) {
                        saved_compile_err = dup(STDERR_FILENO);
                        fflush(stderr);
                        dup2(compile_pipe[1], STDERR_FILENO);
                        close(compile_pipe[1]); compile_pipe[1] = -1;
                    } else {
                        needs_compile_capture = false;
                    }
                }
#endif
                cc_compile(vm, prog);
#ifdef _POSIX_VERSION
                if (needs_compile_capture) {
                    fflush(stderr);
                    if (saved_compile_err >= 0) {
                        dup2(saved_compile_err, STDERR_FILENO);
                        close(saved_compile_err);
                    }
                    captured_compile_stderr = drain_pipe(compile_pipe[0]);
                    if (compile_pipe[0] >= 0) close(compile_pipe[0]);
                }
#endif

                // Refresh the base snapshot from the newly initialised data segment.
                memcpy(base_snap, vm->data_seg, snap_size);
                cur_snap = base_snap;

                cur_vm_flags  = req_flags;
                cur_opt_lvl   = req_opt;
                cur_warnings  = req_warnings;
                cur_warn_errs = req_warn_errs;
                cur_warn_ae   = req_warn_ae;
                cur_f_enable  = req_f_enable;
                cur_f_disable = req_f_disable;
            }
        }

#ifdef _POSIX_VERSION
        if (r->expect_exit_code >= 0) {
            Obj *fn = find_fn(prog, r->name);
            if (!fn) {
                emit_test_result(fmt, disp, cur_suite, "skip", NULL, &json_first, test_num);
                total_skipped++;
                continue;
            }

            memcpy(vm->data_seg, once_suite_snap(once_suite, n_once_suite, cur_snap), snap_size);
            run_hooks(vm, prog, setups, false, false, cur_suite, disp);

            fflush(stdout);
            fflush(stderr);
            s_alarm_fired = 0;

            pid_t pid = fork();
            if (pid == 0) {
                signal(SIGALRM, SIG_DFL);
                s_fork_child_pid = 0;
                // Prevent leaks -atExit instrumentation (handed down via
                // MallocStackLogging / DYLD_INSERT_LIBRARIES) from hanging
                // in the child when the test calls exit(2) or crashes.
                unsetenv("MallocStackLogging");
                unsetenv("MallocStackLoggingNoCompact");
                unsetenv("DYLD_INSERT_LIBRARIES");
                int rc = cc_run_at(vm, (Pc)fn->code_addr, 0, NULL);
                _exit((unsigned char)rc); /* -1 (VM error) → 255, matching non-testing exit convention */
            }

            s_fork_child_pid = pid;
            long effective_ms = r->timeout_ms > 0 ? r->timeout_ms : global_timeout_ms;
            SET_TIMEOUT(effective_ms);
            if (effective_ms > 0) any_timeout_possible = true;

            int wstatus = 0;
            pid_t waited;
            do {
                waited = waitpid(pid, &wstatus, 0);
            } while (waited < 0 && errno == EINTR && !s_alarm_fired);

            SET_TIMEOUT(0);
            s_fork_child_pid = 0;

            bool fork_timed_out = (waited < 0 && s_alarm_fired);
            if (fork_timed_out) {
                waitpid(pid, NULL, 0);
                emit_test_result(fmt, disp, cur_suite, "timeout", NULL, &json_first, test_num);
                total_timeouts++;
                if (opts && opts->fail_fast) stop_early = true;
            } else {
                int actual_code = -1;
                if (WIFEXITED(wstatus))        actual_code = WEXITSTATUS(wstatus);
                else if (WIFSIGNALED(wstatus)) actual_code = 128 + WTERMSIG(wstatus);

                if (actual_code == r->expect_exit_code) {
                    emit_test_result(fmt, disp, cur_suite, "pass", NULL, &json_first, test_num);
                    passed++;
                } else {
                    char fail_msg[128];
                    snprintf(fail_msg, sizeof(fail_msg),
                             "expected exit_code %d, got %d",
                             r->expect_exit_code, actual_code);
                    emit_test_result(fmt, disp, cur_suite, "fail", fail_msg, &json_first, test_num);
                    total_failures++;
                    if (opts && opts->fail_fast) stop_early = true;
                }

                if (!fork_timed_out) {
                    char td_fail[512] = {0};
                    run_hooks_guarded(vm, prog, setups, true, false, cur_suite, disp, td_fail);
                }
            }
            continue;
        }
#else
        if (r->expect_exit_code >= 0) {
            emit_test_result(fmt, disp, cur_suite, "skip",
                             "exit_code= requires POSIX fork", &json_first, test_num);
            total_skipped++;
            continue;
        }
#endif

        Obj *fn = find_fn(prog, r->name);
        if (!fn) {
            emit_test_result(fmt, disp, cur_suite, "skip", NULL, &json_first, test_num);
            total_skipped++;
            if (opts && opts->fail_fast) stop_early = true;
            continue;
        }

        memcpy(vm->data_seg, once_suite_snap(once_suite, n_once_suite, cur_snap), snap_size);

        run.failed    = 0;
        run.timed_out = 0;
        run.fail_msg[0] = '\0';
        s_alarm_fired   = 0;
        int64_t ret_int   = 0;
        double  ret_float = 0.0;

        // For RET_STRUCT: allocate a capture buffer before setjmp so the bytes
        // can be copied out of the rotating return-buffer pool immediately after
        // cc_run_at (before teardown hooks clobber the pool).
        char *ret_struct_buf  = NULL;
        int   ret_struct_size = 0;
        if (r->ret_kind == RET_STRUCT &&
            fn->ty && fn->ty->return_ty &&
            fn->ty->return_ty->size > 0) {
            ret_struct_size = fn->ty->return_ty->size;
            ret_struct_buf  = calloc(1, ret_struct_size);
        }

        // Per-test timeout: individual timeout_ms overrides the global value.
        long effective_ms = r->timeout_ms > 0 ? r->timeout_ms : global_timeout_ms;
        SET_TIMEOUT(effective_ms);
        if (effective_ms > 0) any_timeout_possible = true;

        // Fire once-setup hooks with name_pat on first match (before setjmp,
        // since they need to update the data segment snapshot).
        bool namepat_once_fired = false;
        for (TestSetupRecord *s = setups; s; s = s->next) {
            if (s->is_teardown || !s->once || s->once_fired) continue;
            if (s->name_pat && disp && fnmatch(s->name_pat, disp, 0) == 0) {
                run_hook(vm, prog, s->fn_name);
                s->once_fired = true;
                namepat_once_fired = true;
            }
        }
        if (namepat_once_fired) {
            // Re-capture the top-of-stack snapshot to include name_pat side-effects.
            char *ts = once_suite_snap(once_suite, n_once_suite, NULL);
            if (ts) {
                memcpy(ts, vm->data_seg, snap_size);
            } else {
                if (cur_snap == base_snap) cur_snap = malloc(snap_size);
                memcpy(cur_snap, vm->data_seg, snap_size);
            }
        }

        // Per-test stdout/stderr capture (#614). Redirect fds to pipes before
        // setjmp so the captured span covers setup hooks + the test function.
        // Not supported for exit_code= tests (handled above via fork).
#ifdef _POSIX_VERSION
        bool needs_capture = r->expect_stdout || r->reject_stdout ||
                             r->expect_stderr || r->reject_stderr;
        int saved_stdout = -1, saved_stderr = -1;
        int pipe_out[2], pipe_err[2];
        pipe_out[0] = pipe_out[1] = pipe_err[0] = pipe_err[1] = -1;
        char *captured_stdout = NULL, *captured_stderr = NULL;
        if (needs_capture) {
            if (pipe(pipe_out) == 0 && pipe(pipe_err) == 0) {
                saved_stdout = dup(STDOUT_FILENO);
                saved_stderr = dup(STDERR_FILENO);
                fflush(stdout); fflush(stderr);
                dup2(pipe_out[1], STDOUT_FILENO); close(pipe_out[1]); pipe_out[1] = -1;
                dup2(pipe_err[1], STDERR_FILENO); close(pipe_err[1]); pipe_err[1] = -1;
            } else {
                needs_capture = false;
            }
        }
#endif

        int jval = setjmp(run.jmp);
        if (jval == 0) {
            run_hooks(vm, prog, setups, false, false, cur_suite, disp);
            cc_run_at(vm, (Pc)fn->code_addr, 0, NULL);
            // Capture return values before teardown hooks clobber the registers.
            ret_int   = (int64_t)vm->regs[REG_A0];
            ret_float = cccc_freg_get_f64(vm, FREG_A0);
            // For structs: REG_A0 holds the address of the returned struct in
            // the rotating return-buffer pool.  Copy it out immediately before
            // teardown hooks can rotate the pool and overwrite the data.
            if (ret_struct_buf && ret_struct_size > 0) {
                void *st_ptr = (void *)(uintptr_t)(uint64_t)ret_int;
                if (st_ptr) memcpy(ret_struct_buf, st_ptr, ret_struct_size);
            }
        } else if (jval == 2) {
            run.timed_out = 1;
        }

        SET_TIMEOUT(0);

        // Restore stdout/stderr and drain captured output.
#ifdef _POSIX_VERSION
        if (needs_capture) {
            fflush(stdout); fflush(stderr);
            if (saved_stdout >= 0) { dup2(saved_stdout, STDOUT_FILENO); close(saved_stdout); }
            if (saved_stderr >= 0) { dup2(saved_stderr, STDERR_FILENO); close(saved_stderr); }
            captured_stdout = drain_pipe(pipe_out[0]);
            captured_stderr = drain_pipe(pipe_err[0]);
            if (pipe_out[0] >= 0) close(pipe_out[0]);
            if (pipe_err[0] >= 0) close(pipe_err[0]);
        }
        // Prepend compile-phase stderr (#621) so pattern checks see both phases.
        if (captured_compile_stderr && captured_compile_stderr[0]) {
            size_t clen = strlen(captured_compile_stderr);
            const char *rpart = captured_stderr ? captured_stderr : "";
            size_t rlen = strlen(rpart);
            char *merged = malloc(clen + rlen + 1);
            memcpy(merged, captured_compile_stderr, clen);
            memcpy(merged + clen, rpart, rlen + 1);
            free(captured_stderr);
            captured_stderr = merged;
        }
        free(captured_compile_stderr);
        captured_compile_stderr = NULL;
#endif

        if (!run.timed_out) {
            char td_fail[512] = {0};
            bool td_ok = run_hooks_guarded(vm, prog, setups, true, false,
                                           cur_suite, disp, td_fail);
            if (!td_ok && !run.failed) {
                run.failed = 1;
                strncpy(run.fail_msg, td_fail, sizeof(run.fail_msg) - 1);
            }
            if (!run.failed && r->ret_kind != RET_NONE) {
                bool ret_ok = false;
                switch (r->ret_kind) {
                case RET_INT:
                    ret_ok = apply_cmp_op_i64(r->ret_op, ret_int, r->ret_expect.ret_int);
                    if (!ret_ok)
                        snprintf(run.fail_msg, sizeof(run.fail_msg),
                                 "expected return value %s %" PRId64 ", got %" PRId64,
                                 cmp_op_str(r->ret_op),
                                 r->ret_expect.ret_int, ret_int);
                    break;
                case RET_FLOAT: {
                    double eps = (r->ret_epsilon > 0.0) ? r->ret_epsilon : 1e-9;
                    ret_ok = apply_cmp_op_f64(r->ret_op, ret_float,
                                              r->ret_expect.ret_float, eps);
                    if (!ret_ok)
                        snprintf(run.fail_msg, sizeof(run.fail_msg),
                                 "expected return value %s %g, got %g",
                                 cmp_op_str(r->ret_op),
                                 r->ret_expect.ret_float, ret_float);
                    break;
                }
                case RET_STR: {
                    char *got = (char *)(intptr_t)ret_int;
                    const char *exp = r->ret_expect.ret_str;
                    int cmp = (got && exp) ? strcmp(got, exp)
                                          : (got ? 1 : (exp ? -1 : 0));
                    ret_ok = apply_cmp_op_i64(r->ret_op, (int64_t)cmp, 0);
                    if (!ret_ok)
                        snprintf(run.fail_msg, sizeof(run.fail_msg),
                                 "expected return string %s \"%s\", got \"%s\"",
                                 cmp_op_str(r->ret_op),
                                 exp ? exp : "(null)", got ? got : "(null)");
                    break;
                }
                case RET_STRUCT: {
                    Type *st = (fn->ty && fn->ty->return_ty) ? fn->ty->return_ty : NULL;
                    if (!ret_struct_buf || !st || !st->members) {
                        ret_ok = true; // can't compare; skip silently
                        break;
                    }
                    // Build failure "got" string and compare field-by-field.
                    // For != the whole struct must differ (all-equal → mismatch).
                    char got_str[512] = "{";
                    bool all_eq = true;
                    bool first_field = true;
                    double eps = (r->ret_epsilon > 0.0) ? r->ret_epsilon : 1e-9;
                    for (Member *m = st->members; m; m = m->next) {
                        if (!m->name) continue; // anonymous / padding pseudo-member

                        // Find the matching expected field (NULL → expect 0)
                        const TestRetField *ef = NULL;
                        for (const TestRetField *f = r->ret_expect.ret_fields; f; f = f->next) {
                            if (f->name && m->name &&
                                m->name->len == strlen(f->name) &&
                                strncmp(m->name->loc, f->name, m->name->len) == 0) {
                                ef = f;
                                break;
                            }
                        }

                        // Read actual value from the captured buffer.
                        bool field_eq = true;
                        char field_got[64] = {0};
                        const char *field_delim = first_field ? "" : ", ";

                        if (m->ty->kind == TY_FLOAT) {
                            float fv = 0.0f;
                            memcpy(&fv, ret_struct_buf + m->offset, sizeof(fv));
                            double actual = (double)fv;
                            double expect = ef ? ef->val.f : 0.0;
                            if (ef && ef->kind == RET_INT) expect = (double)ef->val.i;
                            field_eq = (actual - expect < eps && expect - actual < eps);
                            snprintf(field_got, sizeof(field_got), "%s.%.*s = %g",
                                     field_delim, (int)m->name->len, m->name->loc, actual);
                        } else if (m->ty->kind == TY_DOUBLE) {
                            double actual = 0.0;
                            memcpy(&actual, ret_struct_buf + m->offset, sizeof(actual));
                            double expect = ef ? ef->val.f : 0.0;
                            if (ef && ef->kind == RET_INT) expect = (double)ef->val.i;
                            field_eq = (actual - expect < eps && expect - actual < eps);
                            snprintf(field_got, sizeof(field_got), "%s.%.*s = %g",
                                     field_delim, (int)m->name->len, m->name->loc, actual);
                        } else if (m->ty->kind == TY_PTR &&
                                   ef && ef->kind == RET_STR) {
                            // char* field compared with strcmp
                            uintptr_t ptr_val = 0;
                            memcpy(&ptr_val, ret_struct_buf + m->offset,
                                   sizeof(uintptr_t));
                            char *actual = (char *)ptr_val;
                            const char *expect = ef->val.s;
                            int cmp = (actual && expect) ? strcmp(actual, expect)
                                                        : (actual ? 1 : (expect ? -1 : 0));
                            field_eq = (cmp == 0);
                            snprintf(field_got, sizeof(field_got), "%s.%.*s = \"%s\"",
                                     field_delim, (int)m->name->len, m->name->loc,
                                     actual ? actual : "(null)");
                        } else {
                            // Integer (any size up to 8 bytes)
                            int64_t actual = 0;
                            int sz = m->ty->size < 8 ? m->ty->size : 8;
                            if (m->ty->is_unsigned) {
                                uint64_t uv = 0;
                                memcpy(&uv, ret_struct_buf + m->offset, sz);
                                actual = (int64_t)uv;
                            } else {
                                // Sign-extend for signed types < 8 bytes
                                uint64_t uv = 0;
                                memcpy(&uv, ret_struct_buf + m->offset, sz);
                                int shift = (8 - sz) * 8;
                                if (shift > 0 && shift < 64)
                                    actual = (int64_t)((int64_t)(uv << shift) >> shift);
                                else
                                    actual = (int64_t)uv;
                            }
                            int64_t expect = ef ? ef->val.i : 0;
                            if (ef && ef->kind == RET_FLOAT)
                                expect = (int64_t)ef->val.f;
                            field_eq = (actual == expect);
                            snprintf(field_got, sizeof(field_got), "%s.%.*s = %" PRId64,
                                     field_delim, (int)m->name->len, m->name->loc, actual);
                        }

                        // Append to got_str
                        strncat(got_str, field_got,
                                sizeof(got_str) - strlen(got_str) - 1);
                        if (!field_eq) all_eq = false;
                        first_field = false;
                    }
                    strncat(got_str, "}", sizeof(got_str) - strlen(got_str) - 1);

                    // For = : all fields must be equal.
                    // For !=: at least one field must differ (not all equal).
                    if (r->ret_op == CMP_EQ)
                        ret_ok = all_eq;
                    else  // CMP_NE
                        ret_ok = !all_eq;

                    if (!ret_ok)
                        snprintf(run.fail_msg, sizeof(run.fail_msg),
                                 "expected return value %s %s, got %s",
                                 cmp_op_str(r->ret_op),
                                 r->ret_struct_text ? r->ret_struct_text : "(struct)",
                                 got_str);
                    break;
                }
                default:
                    ret_ok = true;
                    break;
                }
                if (!ret_ok) run.failed = 1;
            }
        }

        free(ret_struct_buf);

        // Per-test output pattern checks (#614).
#ifdef _POSIX_VERSION
        if (!run.timed_out && needs_capture && !run.failed) {
            if (!check_output_pattern(r->expect_stderr, captured_stderr, false,
                                      "expected stderr to match", run.fail_msg))
                run.failed = 1;
            else if (!check_output_pattern(r->reject_stderr, captured_stderr, true,
                                      "expected stderr not to match", run.fail_msg))
                run.failed = 1;
            else if (!check_output_pattern(r->expect_stdout, captured_stdout, false,
                                      "expected stdout to match", run.fail_msg))
                run.failed = 1;
            else if (!check_output_pattern(r->reject_stdout, captured_stdout, true,
                                      "expected stdout not to match", run.fail_msg))
                run.failed = 1;
        }
        free(captured_stdout);
        free(captured_stderr);
#endif

        if (run.timed_out) {
            emit_test_result(fmt, disp, cur_suite, "timeout", NULL, &json_first, test_num);
            total_timeouts++;
            if (opts && opts->fail_fast) stop_early = true;
        } else if (!run.failed) {
            emit_test_result(fmt, disp, cur_suite, "pass", NULL, &json_first, test_num);
            passed++;
        } else {
            const char *msg = run.fail_msg[0] ? run.fail_msg : "unknown error";
            emit_test_result(fmt, disp, cur_suite, "fail", msg, &json_first, test_num);
            total_failures++;
            if (opts && opts->fail_fast) stop_early = true;
        }
    }

    // Close all still-open once-suite hooks innermost-first (covers both normal
    // end-of-run and stop_early — teardowns always fire on exit).
    for (int _i = n_once_suite - 1; _i >= 0; _i--) {
        if (!once_suite[_i].open) continue;
        const char *hs = once_suite[_i].hook->suite;
        if (!run_once_hooks(vm, prog, setups, true, hs)) {
            if (fmt == TEST_FORMAT_TAP)
                printf("# once-teardown for suite \"%s\" failed\n", hs);
            else if (fmt == TEST_FORMAT_PLAIN)
                printf("  ! once-teardown for suite \"%s\" failed\n", hs);
        }
        once_suite[_i].open = false;
        free(once_suite[_i].snap);
        once_suite[_i].snap = NULL;
    }

    // Fire once-teardown hooks with name_pat after all tests complete
    for (TestSetupRecord *s = setups; s; s = s->next) {
        if (!s->is_teardown || !s->once || s->once_fired || !s->name_pat)
            continue;
        run_hook(vm, prog, s->fn_name);
        s->once_fired = true;
    }

    switch (fmt) {
    case TEST_FORMAT_TAP:
        break;
    case TEST_FORMAT_PLAIN:
        if (n > 0)
            printf("\n");
        printf("=======================\n");
        printf("Test Results Summary\n");
        printf("=======================\n");
        printf("Total:          %d\n", n);
        printf("Passed:         %d\n", passed);
        if (total_neg > 0)
            printf("Negative tests: %d (correctly rejected: %d, failed: %d)\n",
                   total_neg, total_neg_pass, total_neg_fail);
        if (total_failures > 0)
            printf("Failed:         %d\n", total_failures);
        if (total_skipped > 0)
            printf("Skipped:        %d\n", total_skipped);
        if (total_timeouts > 0)
            printf("Timed out:      %d\n", total_timeouts);
        break;
    case TEST_FORMAT_JSON:
        printf("\n]\n");
        break;
    }

    // Restore the base compiled config if the last test used a per-test one.
    if (cur_vm_flags != base_vm_flags || cur_opt_lvl != base_opt_lvl
        || cur_ffi_allow_count != base_ffi_allow_count) {
        // Restore per-test ffi-allow list to base state first.
        for (int i = base_ffi_allow_count; i < vm->ffi_allow_count; i++)
            free(vm->ffi_allow_list[i]);
        vm->ffi_allow_count = base_ffi_allow_count;
        memset(vm->data_seg, 0, snap_size);
        vm->data_ptr           = vm->data_seg;
        vm->flags                     = base_vm_flags;
        vm->compiler.opt_level        = base_opt_lvl;
        vm->compiler.ffp_contract_fma = (base_vm_flags & CCCC_FMA) != 0;
        vm->ffi_errors_fatal          = (base_vm_flags & CCCC_FFI_ERRORS_FATAL) ? 1 : 0;
        cc_compile(vm, prog);
    }

    if (cur_snap != base_snap) free(cur_snap);
    free(base_snap);

    for (int _i = 0; _i < n_once_suite; _i++)
        if (once_suite[_i].snap) free(once_suite[_i].snap);
    free(once_suite);

    if (any_timeout_possible)
        signal(SIGALRM, SIG_DFL);

    s_run = NULL;

    for (TestListNode *n2 = ordered,  *nx; n2; n2 = nx) { nx = n2->next; free(n2); }
    for (TestListNode *n2 = filtered, *nx; n2; n2 = nx) { nx = n2->next; free(n2); }
    for (TestSetupRecord *s = setups, *nx; s; s = nx) { nx = s->next; free(s); }

    #undef SET_TIMEOUT
    return (passed == n) ? 0 : 1;
}
