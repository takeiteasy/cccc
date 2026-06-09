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


// Per-run failure state allocated on the stack inside cc_run_tests.
// s_run is set to point at the current run's state before each test loop and
// cleared afterward.  A NULL check in the impl_assert* functions guards against
// calls that arrive outside a test run (e.g. from a [[cccc::macro]] during the
// comptime pass) -- ticket #334.
typedef struct {
    jmp_buf jmp;
    int     failed;
    char    fail_msg[512];
} CCCCTestRunState;

static CCCCTestRunState *s_run = NULL;

static void impl_assert(long long cond, long long expr, long long file, long long line) {
    if (!cond) {
        if (!s_run) {
            fprintf(stderr, "CCCC_ASSERT called outside a test run at %s:%lld\n",
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
            fprintf(stderr, "CCCC_ASSERT_EQ called outside a test run at %s:%lld\n",
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
            fprintf(stderr, "CCCC_ASSERT_NEQ called outside a test run at %s:%lld\n",
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
            fprintf(stderr, "CCCC_ASSERT_NULL called outside a test run at %s:%lld\n",
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
            fprintf(stderr, "CCCC_ASSERT_NOT_NULL called outside a test run at %s:%lld\n",
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
            fprintf(stderr, "CCCC_ASSERT_STREQ called outside a test run at %s:%lld\n",
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

// Register native assertion functions so the compiler can emit FFI calls to
// them. Must be called before cc_compile (but after cc_execute_inline_macros
// to avoid registering these symbols during the comptime pass -- ticket #334).
void cc_load_test_runtime(CCCC *vm) {
    cc_register_cfunc(vm, "__cccc_assert",         (void *)impl_assert,         4, 0);
    cc_register_cfunc(vm, "__cccc_assert_eq",      (void *)impl_assert_eq,      6, 0);
    cc_register_cfunc(vm, "__cccc_assert_neq",     (void *)impl_assert_neq,     6, 0);
    cc_register_cfunc(vm, "__cccc_assert_null",    (void *)impl_assert_null,    4, 0);
    cc_register_cfunc(vm, "__cccc_assert_not_null",(void *)impl_assert_not_null,4, 0);
    cc_register_cfunc(vm, "__cccc_assert_streq",   (void *)impl_assert_streq,   6, 0);
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

int cc_run_tests(CCCC *vm, Obj *prog) {

    // Count tests and reverse the list to run in declaration order.
    // test_fns is built by prepending, so it is in reverse order.
    int n = 0;
    TestFnRecord *ordered = NULL;
    for (TestFnRecord *r = vm->compiler.test_fns; r; r = r->next) {
        TestFnRecord *copy = malloc(sizeof(TestFnRecord));
        copy->name = r->name;
        copy->next = ordered;
        ordered = copy;
        n++;
    }

    printf("TAP version 13\n");
    printf("1..%d\n", n);

    int passed = 0;
    int test_num = 0;

    // Stack-allocate state for this run and expose it to the FFI callbacks via
    // the module-level pointer.  Cleared on exit so stray post-run calls are
    // caught by the NULL guard in impl_assert* (ticket #333, #334).
    CCCCTestRunState run;
    s_run = &run;

    for (TestFnRecord *r = ordered; r; r = r->next) {
        test_num++;

        Obj *fn = NULL;
        for (Obj *o = prog; o; o = o->next) {
            if (o->is_function && o->name && strcmp(o->name, r->name) == 0) {
                fn = o;
                break;
            }
        }

        if (!fn) {
            printf("not ok %d - %s # SKIP (not found in compiled output)\n",
                   test_num, r->name);
            continue;
        }

        run.failed = 0;
        run.fail_msg[0] = '\0';

        // Use cc_run_at to avoid mutating text_seg[0] (ticket #332).
        if (setjmp(run.jmp) == 0)
            cc_run_at(vm, (CCCCPc)fn->code_addr, 0, NULL);

        if (!run.failed) {
            printf("ok %d - %s\n", test_num, r->name);
            passed++;
        } else {
            printf("not ok %d - %s\n", test_num, r->name);
            if (run.fail_msg[0])
                printf("  ---\n  message: %s\n  ...\n", run.fail_msg);
        }
    }

    s_run = NULL;

    for (TestFnRecord *r = ordered, *next; r; r = next) {
        next = r->next;
        free(r);
    }

    return (passed == n) ? 0 : 1;
}
