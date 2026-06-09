/*
 JCC: JIT C Compiler

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

#include "jcc.h"
#include "internal.h"


// Per-test failure state; longjmp target lives in cc_run_tests.
static jmp_buf s_test_jmp;
static int     s_test_failed;
static char    s_fail_msg[512];

static void impl_assert(long long cond, long long expr, long long file, long long line) {
    if (!cond) {
        snprintf(s_fail_msg, sizeof(s_fail_msg), "%s (%s:%lld)",
                 (char *)expr, (char *)file, line);
        s_test_failed = 1;
        longjmp(s_test_jmp, 1);
    }
}

static void impl_assert_eq(long long a, long long b,
                           long long as, long long bs,
                           long long file, long long line) {
    if (a != b) {
        snprintf(s_fail_msg, sizeof(s_fail_msg),
                 "%s != %s (%lld != %lld) (%s:%lld)",
                 (char *)as, (char *)bs, a, b, (char *)file, line);
        s_test_failed = 1;
        longjmp(s_test_jmp, 1);
    }
}

static void impl_assert_neq(long long a, long long b,
                            long long as, long long bs,
                            long long file, long long line) {
    if (a == b) {
        snprintf(s_fail_msg, sizeof(s_fail_msg),
                 "%s == %s (both %lld) (%s:%lld)",
                 (char *)as, (char *)bs, a, (char *)file, line);
        s_test_failed = 1;
        longjmp(s_test_jmp, 1);
    }
}

static void impl_assert_null(long long p, long long ps,
                             long long file, long long line) {
    if (p != 0) {
        snprintf(s_fail_msg, sizeof(s_fail_msg), "%s is not null (%s:%lld)",
                 (char *)ps, (char *)file, line);
        s_test_failed = 1;
        longjmp(s_test_jmp, 1);
    }
}

static void impl_assert_not_null(long long p, long long ps,
                                 long long file, long long line) {
    if (p == 0) {
        snprintf(s_fail_msg, sizeof(s_fail_msg), "%s is null (%s:%lld)",
                 (char *)ps, (char *)file, line);
        s_test_failed = 1;
        longjmp(s_test_jmp, 1);
    }
}

static void impl_assert_streq(long long a, long long b,
                              long long as, long long bs,
                              long long file, long long line) {
    if (strcmp((char *)a, (char *)b) != 0) {
        snprintf(s_fail_msg, sizeof(s_fail_msg),
                 "%s != %s (\"%s\" != \"%s\") (%s:%lld)",
                 (char *)as, (char *)bs, (char *)a, (char *)b,
                 (char *)file, line);
        s_test_failed = 1;
        longjmp(s_test_jmp, 1);
    }
}

// Register native assertion functions so the compiler can emit FFI calls to
// them. Must be called before cc_compile.
void cc_load_test_runtime(JCC *vm) {
    cc_register_cfunc(vm, "__jcc_assert",         (void *)impl_assert,         4, 0);
    cc_register_cfunc(vm, "__jcc_assert_eq",      (void *)impl_assert_eq,      6, 0);
    cc_register_cfunc(vm, "__jcc_assert_neq",     (void *)impl_assert_neq,     6, 0);
    cc_register_cfunc(vm, "__jcc_assert_null",    (void *)impl_assert_null,    4, 0);
    cc_register_cfunc(vm, "__jcc_assert_not_null",(void *)impl_assert_not_null,4, 0);
    cc_register_cfunc(vm, "__jcc_assert_streq",   (void *)impl_assert_streq,   6, 0);
}

// Preprocess src/tests.h (loaded via the embedded std registry). As a
// side effect, registers all JCC_ASSERT* macros in vm->compiler.macros so
// they expand correctly when the test file is preprocessed. Returns the
// processed declaration tokens to prepend to the parse stream.
Token *cc_inject_test_header(JCC *vm) {
    char *src = get_std_header("tests.h");
    Token *toks = tokenize_string(vm, "<tests.h>", src);
    return preprocess(vm, toks);
}

int cc_run_tests(JCC *vm, Obj *prog) {

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

        s_test_failed = 0;
        s_fail_msg[0] = '\0';
        vm->text_seg[0] = fn->code_addr;

        if (setjmp(s_test_jmp) == 0)
            cc_run(vm, 0, NULL);

        if (!s_test_failed) {
            printf("ok %d - %s\n", test_num, r->name);
            passed++;
        } else {
            printf("not ok %d - %s\n", test_num, r->name);
            if (s_fail_msg[0])
                printf("  ---\n  message: %s\n  ...\n", s_fail_msg);
        }
    }

    for (TestFnRecord *r = ordered, *next; r; r = next) {
        next = r->next;
        free(r);
    }

    return (passed == n) ? 0 : 1;
}
