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

#include "cccc.h"
#include "internal.h"
#include <fnmatch.h>
#include <signal.h>


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
static void handle_alarm(int sig) {
    (void)sig;
    s_alarm_fired = 1;
    if (s_run) longjmp(s_run->jmp, 2); // 2 = timeout sentinel
}

static void impl_assert(long long cond, long long expr, long long file, long long line) {
    if (!cond) {
        if (!s_run) {
            fprintf(stderr, "$assert called outside a test run at %s:%lld\n",
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
            fprintf(stderr, "$assert_eq called outside a test run at %s:%lld\n",
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
            fprintf(stderr, "$assert_neq called outside a test run at %s:%lld\n",
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
            fprintf(stderr, "$assert_null called outside a test run at %s:%lld\n",
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
            fprintf(stderr, "$assert_not_null called outside a test run at %s:%lld\n",
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
            fprintf(stderr, "$assert_streq called outside a test run at %s:%lld\n",
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
            fprintf(stderr, "$assert_false called outside a test run at %s:%lld\n",
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
        fprintf(stderr, "$assert_fail called outside a test run at %s:%lld\n",
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
        fprintf(stderr, "$assert_fail_msg called outside a test run at %s:%lld\n",
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
            fprintf(stderr, "$assert_gt called outside a test run at %s:%lld\n",
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
            fprintf(stderr, "$assert_lt called outside a test run at %s:%lld\n",
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
            fprintf(stderr, "$assert_ge called outside a test run at %s:%lld\n",
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
            fprintf(stderr, "$assert_le called outside a test run at %s:%lld\n",
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
            fprintf(stderr, "$assert_within called outside a test run at %s:%lld\n",
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
            fprintf(stderr, "$assert_streq_len called outside a test run at %s:%lld\n",
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
            fprintf(stderr, "$assert_mem_eq called outside a test run at %s:%lld\n",
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
            fprintf(stderr, "$assert_float_within called outside a test run at %s:%lld\n",
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
            fprintf(stderr, "$assert_bits called outside a test run at %s:%lld\n",
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
            fprintf(stderr, "$assert_bit_high called outside a test run at %s:%lld\n",
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
            fprintf(stderr, "$assert_bit_low called outside a test run at %s:%lld\n",
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
            fprintf(stderr, "$assert_eq_array called outside a test run at %s:%lld\n",
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
            fprintf(stderr, "$assert_msg called outside a test run at %s:%lld\n",
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
            fprintf(stderr, "$assert_eq_msg called outside a test run at %s:%lld\n",
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
            fprintf(stderr, "$assert_streq_msg called outside a test run at %s:%lld\n",
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
            fprintf(stderr, "$assert_null_msg called outside a test run at %s:%lld\n",
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
            fprintf(stderr, "$assert_not_null_msg called outside a test run at %s:%lld\n",
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
            fprintf(stderr, "$assert_bits_msg called outside a test run at %s:%lld\n",
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
void cc_load_test_runtime(CCCC *vm) {
    cc_register_cfunc(vm, "__cccc_assert",             (void *)impl_assert,              4, 0);
    cc_register_cfunc(vm, "__cccc_assert_false",       (void *)impl_assert_false,         4, 0);
    cc_register_cfunc(vm, "__cccc_assert_fail",        (void *)impl_assert_fail,          2, 0);
    cc_register_cfunc(vm, "__cccc_assert_fail_msg",    (void *)impl_assert_fail_msg,      3, 0);
    cc_register_cfunc(vm, "__cccc_assert_eq",          (void *)impl_assert_eq,            6, 0);
    cc_register_cfunc(vm, "__cccc_assert_neq",         (void *)impl_assert_neq,           6, 0);
    cc_register_cfunc(vm, "__cccc_assert_gt",          (void *)impl_assert_gt,            6, 0);
    cc_register_cfunc(vm, "__cccc_assert_lt",          (void *)impl_assert_lt,            6, 0);
    cc_register_cfunc(vm, "__cccc_assert_ge",          (void *)impl_assert_ge,            6, 0);
    cc_register_cfunc(vm, "__cccc_assert_le",          (void *)impl_assert_le,            6, 0);
    cc_register_cfunc(vm, "__cccc_assert_within",      (void *)impl_assert_within,        8, 0);
    cc_register_cfunc(vm, "__cccc_assert_null",        (void *)impl_assert_null,          4, 0);
    cc_register_cfunc(vm, "__cccc_assert_not_null",    (void *)impl_assert_not_null,      4, 0);
    cc_register_cfunc(vm, "__cccc_assert_streq",       (void *)impl_assert_streq,         6, 0);
    cc_register_cfunc(vm, "__cccc_assert_streq_len",   (void *)impl_assert_streq_len,     7, 0);
    cc_register_cfunc(vm, "__cccc_assert_mem_eq",      (void *)impl_assert_mem_eq,        7, 0);
    cc_register_cfunc(vm, "__cccc_assert_float_within",(void *)impl_assert_float_within,  8, 0);
    cc_register_cfunc(vm, "__cccc_assert_double_within",(void *)impl_assert_double_within,8, 0);
    cc_register_cfunc(vm, "__cccc_assert_bits",        (void *)impl_assert_bits,          8, 0);
    cc_register_cfunc(vm, "__cccc_assert_bit_high",    (void *)impl_assert_bit_high,      6, 0);
    cc_register_cfunc(vm, "__cccc_assert_bit_low",     (void *)impl_assert_bit_low,       6, 0);
    cc_register_cfunc(vm, "__cccc_assert_eq_array",    (void *)impl_assert_eq_array,      8, 0);
    cc_register_cfunc(vm, "__cccc_assert_msg",         (void *)impl_assert_msg,           5, 0);
    cc_register_cfunc(vm, "__cccc_assert_eq_msg",      (void *)impl_assert_eq_msg,        7, 0);
    cc_register_cfunc(vm, "__cccc_assert_streq_msg",   (void *)impl_assert_streq_msg,     7, 0);
    cc_register_cfunc(vm, "__cccc_assert_null_msg",    (void *)impl_assert_null_msg,      5, 0);
    cc_register_cfunc(vm, "__cccc_assert_not_null_msg",(void *)impl_assert_not_null_msg,  5, 0);
    cc_register_cfunc(vm, "__cccc_assert_bits_msg",    (void *)impl_assert_bits_msg,      9, 0);
}

// Preprocess src/tests.h (loaded via the embedded std registry). As a
// side effect, registers all CCCC_ASSERT* macros in vm->compiler.macros so
// they expand correctly when the test file is preprocessed. Returns the
// processed declaration tokens to prepend to the parse stream.
Token *cc_inject_test_header(CCCC *vm) {
    char *src = get_std_header("tests.h");
    Token *toks = tokenize_string(vm, "<tests.h>", src);
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

// Run a single hook function by name (no-op if not found).
static void run_hook(CCCC *vm, Obj *prog, const char *fn_name) {
    Obj *fn = find_fn(prog, fn_name);
    if (fn)
        cc_run_at(vm, (CCCCPc)fn->code_addr, 0, NULL);
}

// Run all matching setup or teardown hooks.
// suite and disp are the current test's context; NULL suite means no active suite.
// once_only=true selects once-per-suite hooks; false selects per-test hooks.
static void run_hooks(CCCC *vm, Obj *prog, TestSetupRecord *setups,
                      bool is_teardown, bool once_only,
                      const char *suite, const char *disp) {
    for (TestSetupRecord *s = setups; s; s = s->next) {
        if (s->is_teardown != is_teardown) continue;
        if (s->once       != once_only)   continue;
        // Suite filter: if the hook targets a specific suite, it must match.
        if (s->suite) {
            if (!suite || strcmp(s->suite, suite) != 0) continue;
        }
        // Name-pattern filter (per-test only).
        if (!once_only && s->name_pat) {
            if (!disp || fnmatch(s->name_pat, disp, 0) != 0) continue;
        }
        run_hook(vm, prog, s->fn_name);
    }
}

// Returns true if any once-setup exists for the given suite.
static bool has_once_setups_for(TestSetupRecord *setups, const char *suite) {
    for (TestSetupRecord *s = setups; s; s = s->next)
        if (!s->is_teardown && s->once && s->suite && strcmp(s->suite, suite) == 0)
            return true;
    return false;
}

// Run hooks in an isolated setjmp context so that CCCC_ASSERT failures are
// caught safely regardless of whether s_run is currently live.  Returns true
// if all hooks completed without failure.  If fail_msg_out is non-NULL and a
// failure occurs, the message (up to 511 chars) is written there.
static bool run_hooks_guarded(CCCC *vm, Obj *prog, TestSetupRecord *setups,
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
static bool run_once_hooks(CCCC *vm, Obj *prog, TestSetupRecord *setups,
                           bool is_teardown, const char *suite) {
    return run_hooks_guarded(vm, prog, setups, is_teardown, true,
                             suite, NULL, NULL);
}

// Thin wrapper used inside cc_run_tests to build ordered/filtered lists without
// copying TestFnRecord.  Pointing at the original record means new fields are
// always visible — no manual sync required (ticket #337).
typedef struct TestListNode {
    TestFnRecord    *rec;
    struct TestListNode *next;
} TestListNode;

int cc_run_tests(CCCC *vm, Obj *prog, const CcTestOptions *opts) {

    // Reverse setup records to declaration order (built by prepending).
    TestSetupRecord *setups = NULL;
    for (TestSetupRecord *s = vm->compiler.test_setups; s; s = s->next) {
        TestSetupRecord *copy = malloc(sizeof(TestSetupRecord));
        *copy      = *s;
        copy->next = setups;
        setups     = copy;
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
            const char *s = r->suite ? r->suite : "";
            if (strcmp(s, opts->suite_filter) != 0)
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

    printf("TAP version 13\n");
    printf("1..%d\n", n);

    int passed = 0;
    int test_num = 0;
    const char *prev_suite = NULL;  // tracks suite changes for TAP comments

    // Stack-allocate state for this run and expose it to the FFI callbacks via
    // the module-level pointer.  Cleared on exit so stray post-run calls are
    // caught by the NULL guard in impl_assert* (ticket #333, #334).
    CCCCTestRunState run;
    s_run = &run;

    bool use_timeout = opts && opts->test_timeout > 0;
    if (use_timeout)
        signal(SIGALRM, handle_alarm);

    // Snapshot the data segment so global state can be restored between tests
    // (ticket #329).  data_ptr only advances during compilation, never during
    // test execution, so one snapshot covers the full segment.
    size_t snap_size  = (size_t)(vm->data_ptr - vm->data_seg);
    char  *base_snap  = malloc(snap_size);
    memcpy(base_snap, vm->data_seg, snap_size);
    // cur_snap is what each test restores to; may be updated by once-setups.
    char  *cur_snap   = base_snap;

    bool stop_early = false;
    for (TestListNode *n2 = filtered; n2 && !stop_early; n2 = n2->next) {
        TestFnRecord *r = n2->rec;
        test_num++;
        const char *disp      = test_display_name(r);
        const char *cur_suite = r->suite;

        // Detect suite transitions.
        bool suite_changed = (cur_suite != prev_suite) &&
                             (cur_suite == NULL || prev_suite == NULL ||
                              strcmp(cur_suite, prev_suite) != 0);

        if (suite_changed) {
            // Once-teardown for the suite we are leaving.
            if (prev_suite) {
                if (!run_once_hooks(vm, prog, setups, true, prev_suite))
                    printf("# once-teardown for suite \"%s\" failed\n", prev_suite);
            }

            // Discard any suite-specific snapshot taken by once-setup.
            if (cur_snap != base_snap) {
                free(cur_snap);
                cur_snap = base_snap;
            }

            // Emit TAP suite comment.
            if (cur_suite)
                printf("# Suite: %s\n", cur_suite);
            else
                printf("# Suite: (none)\n");
            prev_suite = cur_suite;

            // Once-setup for the suite we are entering.
            if (cur_suite && has_once_setups_for(setups, cur_suite)) {
                memcpy(vm->data_seg, base_snap, snap_size);
                if (!run_once_hooks(vm, prog, setups, false, cur_suite))
                    printf("# once-setup for suite \"%s\" failed\n", cur_suite);
                // Snapshot the post-once-setup state so it persists across tests.
                cur_snap = malloc(snap_size);
                memcpy(cur_snap, vm->data_seg, snap_size);
            }
        }

        // Negative test: result is precomputed at compile time, no execution.
        if (r->error_pat) {
            if (r->neg_passed == 1) {
                printf("ok %d - %s\n", test_num, disp);
                passed++;
            } else {
                const char *reason = (r->neg_passed == 0)
                    ? "expected compilation error but code compiled successfully"
                    : r->neg_actual;
                printf("not ok %d - %s\n", test_num, disp);
                printf("  ---\n  message: %s\n  ...\n", reason);
                if (opts && opts->fail_fast) stop_early = true;
            }
            continue;
        }

        Obj *fn = find_fn(prog, r->name);
        if (!fn) {
            printf("not ok %d - %s # SKIP (not found in compiled output)\n",
                   test_num, disp);
            if (opts && opts->fail_fast) stop_early = true;
            continue;
        }

        // Restore global state before each positive test (ticket #329).
        memcpy(vm->data_seg, cur_snap, snap_size);

        run.failed    = 0;
        run.timed_out = 0;
        run.fail_msg[0] = '\0';
        s_alarm_fired   = 0;

        if (use_timeout) alarm((unsigned)opts->test_timeout);

        // Use cc_run_at to avoid mutating text_seg[0] (ticket #332).
        // Setup and test share one setjmp context: if setup fails the test is
        // skipped but teardown still runs below (ticket #331).
        int jval = setjmp(run.jmp);
        if (jval == 0) {
            run_hooks(vm, prog, setups, false, false, cur_suite, disp);
            cc_run_at(vm, (CCCCPc)fn->code_addr, 0, NULL);
        } else if (jval == 2) {
            run.timed_out = 1;
        }

        if (use_timeout) alarm(0); // cancel pending alarm

        // Teardown always runs after setup+test, even on failure.  Skip only
        // on timeout because the VM state is unknown after SIGALRM.
        if (!run.timed_out) {
            char td_fail[512] = {0};
            bool td_ok = run_hooks_guarded(vm, prog, setups, true, false,
                                           cur_suite, disp, td_fail);
            if (!td_ok && !run.failed) {
                run.failed = 1;
                strncpy(run.fail_msg, td_fail, sizeof(run.fail_msg) - 1);
            }
        }

        if (run.timed_out) {
            printf("not ok %d - %s # TIMEOUT\n", test_num, disp);
            if (opts && opts->fail_fast) stop_early = true;
        } else if (!run.failed) {
            printf("ok %d - %s\n", test_num, disp);
            passed++;
        } else {
            printf("not ok %d - %s\n", test_num, disp);
            if (run.fail_msg[0])
                printf("  ---\n  message: %s\n  ...\n", run.fail_msg);
            if (opts && opts->fail_fast) stop_early = true;
        }
    }

    // Once-teardown for the final active suite.
    if (prev_suite && !stop_early) {
        if (!run_once_hooks(vm, prog, setups, true, prev_suite))
            printf("# once-teardown for suite \"%s\" failed\n", prev_suite);
    }

    if (cur_snap != base_snap) free(cur_snap);
    free(base_snap);

    if (use_timeout)
        signal(SIGALRM, SIG_DFL);

    s_run = NULL;

    for (TestListNode *n2 = ordered,  *nx; n2; n2 = nx) { nx = n2->next; free(n2); }
    for (TestListNode *n2 = filtered, *nx; n2; n2 = nx) { nx = n2->next; free(n2); }
    for (TestSetupRecord *s = setups, *nx; s; s = nx) { nx = s->next; free(s); }

    return (passed == n) ? 0 : 1;
}
