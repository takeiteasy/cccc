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
#include <string.h>
#include <sys/time.h>


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

    bool stop_early = false;
    for (TestListNode *n2 = filtered; n2 && !stop_early; n2 = n2->next) {
        TestFnRecord *r = n2->rec;
        test_num++;
        const char *disp      = test_display_name(r);
        const char *cur_suite = r->suite;

        bool suite_changed = (cur_suite != prev_suite) &&
                             (cur_suite == NULL || prev_suite == NULL ||
                              strcmp(cur_suite, prev_suite) != 0);

        if (suite_changed) {
            if (prev_suite) {
                if (!run_once_hooks(vm, prog, setups, true, prev_suite)) {
                    if (fmt == TEST_FORMAT_TAP)
                        printf("# once-teardown for suite \"%s\" failed\n", prev_suite);
                    else if (fmt == TEST_FORMAT_PLAIN)
                        printf("  ! once-teardown for suite \"%s\" failed\n", prev_suite);
                }
            }
            if (cur_snap != base_snap) {
                free(cur_snap);
                cur_snap = base_snap;
            }
            if (fmt == TEST_FORMAT_TAP) {
                if (cur_suite)
                    printf("# Suite: %s\n", cur_suite);
                else
                    printf("# Suite: (none)\n");
            } else if (fmt == TEST_FORMAT_PLAIN) {
                printf("── %s ──\n", cur_suite ? cur_suite : "(no suite)");
            }
            prev_suite = cur_suite;

            if (cur_suite && has_once_setups_for(setups, cur_suite)) {
                memcpy(vm->data_seg, base_snap, snap_size);
                if (!run_once_hooks(vm, prog, setups, false, cur_suite)) {
                    if (fmt == TEST_FORMAT_TAP)
                        printf("# once-setup for suite \"%s\" failed\n", cur_suite);
                    else if (fmt == TEST_FORMAT_PLAIN)
                        printf("  ! once-setup for suite \"%s\" failed\n", cur_suite);
                }
                cur_snap = malloc(snap_size);
                memcpy(cur_snap, vm->data_seg, snap_size);
            }
        }

        if (r->error_pat) {
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

        Obj *fn = find_fn(prog, r->name);
        if (!fn) {
            emit_test_result(fmt, disp, cur_suite, "skip", NULL, &json_first, test_num);
            total_skipped++;
            if (opts && opts->fail_fast) stop_early = true;
            continue;
        }

        memcpy(vm->data_seg, cur_snap, snap_size);

        run.failed    = 0;
        run.timed_out = 0;
        run.fail_msg[0] = '\0';
        s_alarm_fired   = 0;
        int ret_val = 0;

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
            // Snapshot after once-setup so subsequent tests see its state
            if (cur_snap == base_snap) {
                cur_snap = malloc(snap_size);
            }
            memcpy(cur_snap, vm->data_seg, snap_size);
        }

        int jval = setjmp(run.jmp);
        if (jval == 0) {
            run_hooks(vm, prog, setups, false, false, cur_suite, disp);
            ret_val = cc_run_at(vm, (CCCCPc)fn->code_addr, 0, NULL);
        } else if (jval == 2) {
            run.timed_out = 1;
        }

        SET_TIMEOUT(0);

        if (!run.timed_out) {
            char td_fail[512] = {0};
            bool td_ok = run_hooks_guarded(vm, prog, setups, true, false,
                                           cur_suite, disp, td_fail);
            if (!td_ok && !run.failed) {
                run.failed = 1;
                strncpy(run.fail_msg, td_fail, sizeof(run.fail_msg) - 1);
            }
            if (!run.failed && r->has_expect_return && ret_val != (int)r->expect_return) {
                run.failed = 1;
                snprintf(run.fail_msg, sizeof(run.fail_msg),
                         "expected return value %ld, got %d", r->expect_return, ret_val);
            }
        }

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

    if (prev_suite && !stop_early) {
        if (!run_once_hooks(vm, prog, setups, true, prev_suite)) {
            if (fmt == TEST_FORMAT_TAP)
                printf("# once-teardown for suite \"%s\" failed\n", prev_suite);
            else if (fmt == TEST_FORMAT_PLAIN)
                printf("  ! once-teardown for suite \"%s\" failed\n", prev_suite);
        }
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

    if (cur_snap != base_snap) free(cur_snap);
    free(base_snap);

    if (any_timeout_possible)
        signal(SIGALRM, SIG_DFL);

    s_run = NULL;

    for (TestListNode *n2 = ordered,  *nx; n2; n2 = nx) { nx = n2->next; free(n2); }
    for (TestListNode *n2 = filtered, *nx; n2; n2 = nx) { nx = n2->next; free(n2); }
    for (TestSetupRecord *s = setups, *nx; s; s = nx) { nx = s->next; free(s); }

    #undef SET_TIMEOUT
    return (passed == n) ? 0 : 1;
}
