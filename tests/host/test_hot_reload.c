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

// Host-side test for hot code reload: drives a REPL session through
// cc_repl_eval()/cc_redefine_function() and checks that redefined functions
// are reached by direct calls, function pointers, FFI callbacks and calls made
// from inside running guest code.

#include "internal.h"

#include <stdio.h>
#include <string.h>

static int failed = 0;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL (%s:%d): ", __FILE__, __LINE__);             \
            fprintf(stderr, __VA_ARGS__);                                      \
            fprintf(stderr, "\n");                                             \
            failed = 1;                                                        \
        }                                                                      \
    } while (0)

static VirtualMachine vm;

static long long eval(const char *expr) {
    long long v  = -999999;
    int       rc = cc_repl_eval(&vm, expr, &v);
    CHECK(rc == 0, "eval failed: %s", expr);
    return v;
}

static void decl(const char *src) {
    CHECK(cc_redefine_function(&vm, src) == 0, "definition failed: %s", src);
}

static void decl_fails(const char *src) {
    CHECK(cc_redefine_function(&vm, src) != 0, "expected failure: %s", src);
}

// Called from guest code (test_mid_recursion) to reload `rec` while a frame of
// it is live.
static long long hook_reload_rec(void) {
    decl("int rec(int n){ if (n == 0) return 0; return 100 + rec(n - 1); }");
    return 0;
}

static void test_basic(void) {
    decl("int f(int x){ return x + 1; }");
    decl("int caller(int x){ return f(x) * 2; }");
    CHECK(eval("f(1)") == 2, "f(1) before reload");
    CHECK(eval("caller(1)") == 4, "caller(1) before reload");

    decl("int f(int x){ return x + 100; }");
    CHECK(eval("f(1)") == 101, "f(1) after reload");
    CHECK(eval("caller(1)") == 202,
          "caller compiled before the reload reaches the new body");

    // Reloaded twice: the original entry reaches the newest body.
    decl("int f(int x){ return x + 1000; }");
    CHECK(eval("f(1)") == 1001, "f(1) after second reload");
    CHECK(eval("caller(1)") == 2002, "caller after second reload");
}

static void test_prototype_then_definition(void) {
    decl("int proto(int);");
    decl("int proto(int x){ return x + 7; }");
    CHECK(eval("proto(1)") == 8, "definition after a prototype line");
}

static void test_statics(void) {
    decl("int counter(void){ static int n = 0; return ++n; }");
    CHECK(eval("counter()") == 1, "counter 1");
    CHECK(eval("counter()") == 2, "counter 2");

    decl("int counter(void){ static int n = 0; static int extra = 50; "
         "n += 10; return n + extra; }");
    CHECK(eval("counter()") == 62, "n survives the reload; extra starts fresh");
    CHECK(eval("counter()") == 72, "n keeps counting");

    decl("int shadowed(void){ static int a = 1; { static int a = 2; "
         "a += 20; } return ++a; }");
    CHECK(eval("shadowed()") == 2, "shadowed 1");
    decl("int shadowed(void){ static int a = 100; { static int a = 200; "
         "a += 20; } return ++a; }");
    CHECK(eval("shadowed()") == 3, "same-named statics pair up in order");
}

static void test_function_pointers(void) {
    decl("int target(void){ return 1; }");
    decl("int (*fp)(void);");
    eval("fp = target");
    CHECK(eval("fp()") == 1, "pointer before reload");
    decl("int target(void){ return 2; }");
    CHECK(eval("fp()") == 2, "pointer captured before the reload runs it");
    CHECK(eval("fp == target") == 1, "pointer identity survives the reload");
    eval("fp = target");
    CHECK(eval("fp()") == 2, "pointer taken after the reload");
}

static void test_runtime_pointer_assignment(void) {
    decl("int pa(void){ return 1; }");
    decl("int pb(void){ return 2; }");
    decl("int (*pg)(void) = pa;");
    eval("pg = pb");
    decl("int unrelated = 0;");
    CHECK(eval("pg()") == 2, "a later unit does not reset the pointer");
}

static void test_ffi_callback(void) {
    decl("void qsort(void *, unsigned long, unsigned long, "
         "int (*)(const void *, const void *));");
    decl("int arr[4] = {3, 1, 2, 0};");
    decl("int cmp(const void *a, const void *b){ "
         "return *(const int *)a - *(const int *)b; }");
    eval("(qsort(arr, 4, 4, cmp), 0)");
    CHECK(eval("arr[0]") == 0 && eval("arr[3]") == 3, "ascending sort");

    decl("int cmp(const void *a, const void *b){ "
         "return *(const int *)b - *(const int *)a; }");
    eval("(qsort(arr, 4, 4, cmp), 0)");
    CHECK(eval("arr[0]") == 3 && eval("arr[3]") == 0,
          "qsort calls the reloaded comparator");
}

static void test_rejections(void) {
    decl("int sig(int x){ return x; }");
    decl_fails("long sig(int x){ return x; }");
    decl_fails("int sig(int x, int y){ return x; }");
    decl_fails("static int sig(int x){ return x; }");
    CHECK(eval("sig(5)") == 5, "original untouched after rejected reloads");

    decl_fails("int sig(int x){ return x +; }");
    CHECK(eval("sig(6)") == 6, "original callable after a syntax error");
    decl("int sig(int x){ return x * 2; }");
    CHECK(eval("sig(6)") == 12, "reload after a failed attempt");

    decl("int var = 1;");
    decl_fails("int var = 2;");
    CHECK(eval("var") == 1, "variables are not reloadable");
    decl_fails("1 + 2");
}

static void test_static_inline(void) {
    decl("static inline int si(void){ return 1; }");
    CHECK(eval("si()") == 1, "static inline before reload");
    decl("static inline int si(void){ return 2; }");
    CHECK(eval("si()") == 2, "static inline after reload");
}

static void test_mid_recursion(void) {
    decl("int hook_reload_rec(void);");
    decl("int rec(int n){ if (n == 0) return 0; if (n == 3) "
         "hook_reload_rec(); return 1 + rec(n - 1); }");
    // Frames for n = 5, 4, 3 finish in the old body (+1 each); the call made
    // by n = 3 after the reload runs the new one (100 + 100 + 0).
    CHECK(eval("rec(5)") == 203, "frames already running finish in old body");
    CHECK(eval("rec(2)") == 200, "new calls run the new body");
}

int main(void) {
    cc_init(&vm, 0);
    vm.compiler.compiler_family = CCCC_COMPILER_FAMILY_GCC;
    init_mode_macros(&vm);
    register_stdlib_functions(&vm);
    cc_register_cfunc(&vm, "hook_reload_rec", (void *)hook_reload_rec, 0, 0);
    cc_repl_init(&vm);

    test_basic();
    test_prototype_then_definition();
    test_statics();
    test_function_pointers();
    test_runtime_pointer_assignment();
    test_ffi_callback();
    test_rejections();
    test_static_inline();
    test_mid_recursion();

    cc_destroy(&vm);
    if (!failed)
        printf("PASS: hot reload\n");
    return failed;
}
