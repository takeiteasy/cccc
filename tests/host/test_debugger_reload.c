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

// Host-side test for cc_debug_reload: a file loaded into a REPL session is
// edited on disk and reloaded. Runs under CFI, stack canaries and
// dangling-pointer detection so a desynced stack or stale frame tag fails.

#include "internal.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

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
static char           dir[512];

static long long eval(const char *expr) {
    long long v  = -999999;
    int       rc = cc_repl_eval(&vm, expr, &v);
    CHECK(rc == 0, "eval failed: %s", expr);
    return v;
}

static char *path_of(const char *name) {
    static char paths[8][640];
    static int  next;
    char       *p = paths[next++ % 8];
    snprintf(p, 640, "%s/%s", dir, name);
    return p;
}

static char *write_file(const char *name, const char *text) {
    char *path = path_of(name);
    FILE *f    = fopen(path, "w");
    CHECK(f != NULL, "write %s", path);
    if (f) {
        fputs(text, f);
        fclose(f);
    }
    return path;
}

static int input_file_count(void) {
    int n = 0;
    while (vm.compiler.input_files && vm.compiler.input_files[n])
        n++;
    return n;
}

static void load(const char *path) {
    CHECK(cc_repl_load_file(&vm, path) == 0, "load %s", path);
}

static void test_changed_and_kept(void) {
    char *p = write_file("a.c", "int counter = 5;\n"
                                "int bump(void) { return ++counter; }\n"
                                "int stable(int x) { return x * 2; }\n"
                                "int twice(int x) { return stable(x); }\n"
                                "int gone(void) { return 1; }\n");
    load(p);
    CHECK(eval("bump()") == 6, "initial bump");

    Pc  stable_pc = cc_find_function_entry(&vm, "stable");
    int keep_bp = cc_add_breakpoint(&vm, cc_find_function_entry(&vm, "twice"));
    int drop_bp = cc_add_breakpoint(&vm, cc_find_function_entry(&vm, "bump"));
    Pc  keep_pc = vm.dbg.breakpoints[keep_bp].pc;
    write_file("a.c", "int counter = 1000;\n"
                      "int bump(void) { counter += 10; return counter; }\n"
                      "int stable(int x) { return x * 2; }\n"
                      "int twice(int x) { return stable(x); }\n"
                      "int added(void) { return 7; }\n"
                      "int newglobal = 3;\n");
    CHECK(cc_debug_reload(&vm, p) == 0, "reload");
    CHECK(vm.dbg.breakpoints[keep_bp].enabled &&
              vm.dbg.breakpoints[keep_bp].pc == keep_pc,
          "breakpoint in an unchanged function stays");
    CHECK(!vm.dbg.breakpoints[drop_bp].enabled,
          "breakpoint in a replaced body is dropped");
    cc_remove_breakpoint(&vm, keep_bp);
    CHECK(eval("bump()") == 16, "changed body runs, counter keeps its value");
    CHECK(eval("counter") == 16, "global keeps its value");
    CHECK(eval("twice(4)") == 8, "unchanged function still works");
    CHECK(cc_find_function_entry(&vm, "stable") == stable_pc,
          "unchanged function keeps its entry");
    CHECK(eval("added()") == 7, "new function is callable");
    CHECK(eval("newglobal") == 3, "new global is initialized");
    CHECK(eval("gone()") == 1, "removed function stays");

    // A second reload of the same text changes nothing.
    int files = input_file_count();
    CHECK(cc_debug_reload(&vm, p) == 0, "reload again");
    for (int i = 0; i < 5; i++)
        cc_debug_reload(&vm, p);
    CHECK(input_file_count() == files, "reloads reuse the input_files entry");
    CHECK(eval("bump()") == 26, "second reload keeps state");
    CHECK(eval("twice(5)") == 10, "unchanged after two reloads");
}

static void test_refusals(void) {
    char *p = write_file("b.c", "struct P { int x; };\n"
                                "int gv = 4;\n"
                                "int getx(struct P *p) { return p->x; }\n"
                                "int sig(int a) { return a; }\n");
    load(p);

    write_file("b.c", "struct P { int x; int y; };\n"
                      "int gv = 4;\n"
                      "int getx(struct P *p) { return p->x; }\n"
                      "int sig(int a) { return a; }\n");
    CHECK(cc_debug_reload(&vm, p) != 0, "struct layout change refused");

    write_file("b.c", "struct P { int x; };\n"
                      "long gv = 4;\n"
                      "int getx(struct P *p) { return p->x; }\n"
                      "int sig(int a) { return a; }\n");
    CHECK(cc_debug_reload(&vm, p) != 0, "global type change refused");

    write_file("b.c", "struct P { int x; };\n"
                      "int gv = 4;\n"
                      "int getx(struct P *p) { return p->x; }\n"
                      "long sig(int a, int b) { return a + b; }\n");
    CHECK(cc_debug_reload(&vm, p) != 0, "signature change refused");

    write_file("b.c", "int nonsense(\n");
    CHECK(cc_debug_reload(&vm, p) != 0, "parse error refused");

    CHECK(eval("sig(9)") == 9 && eval("gv") == 4, "session unchanged");

    CHECK(cc_debug_reload(&vm, path_of("nothere.c")) != 0,
          "missing file refused");
    char *other = write_file("other.c", "int other(void) { return 1; }\n");
    CHECK(cc_debug_reload(&vm, other) != 0, "foreign file refused");
}

// A program compiled from a file, which reloads itself from a host hook
// while it runs. main is the running frame and stays in its old body.
static const char *hook_path;

static long long hook(void) {
    write_file("h.h", "#ifndef H_H\n#define H_H\n#define K 4\n#endif\n");
    write_file("c.c",
               "#include \"h.h\"\n"
               "static int hits;\n"
               "int reload_hook(void);\n"
               "int count(void) { static int local; return (++hits + ++local) "
               "* 10; }\n"
               "int k(void) { return K; }\n"
               "int plain(void) { return 1; }\n"
               "int main(void) {\n"
               "  int a = count();\n"
               "  int b = k();\n"
               "  reload_hook();\n"
               "  int c = count();\n"
               "  int d = k();\n"
               "  return a + b * 100 + c * 1000 + d * 10000 + plain();\n"
               "}\n");
    CHECK(cc_debug_reload(&vm, hook_path) == 0,
          "reload from a running program");
    return 0;
}

static void test_running_program(void) {
    write_file("h.h", "#ifndef H_H\n#define H_H\n#define K 3\n#endif\n");
    hook_path =
        write_file("c.c", "#include \"h.h\"\n"
                          "static int hits;\n"
                          "int reload_hook(void);\n"
                          "int count(void) { static int local; return "
                          "++hits + ++local; }\n"
                          "int k(void) { return K; }\n"
                          "int plain(void) { return 1; }\n"
                          "int main(void) {\n"
                          "  int a = count();\n"
                          "  int b = k();\n"
                          "  reload_hook();\n"
                          "  int c = count();\n"
                          "  int d = k();\n"
                          "  return a + b * 100 + c * 1000 + d * 10000 + "
                          "plain();\n"
                          "}\n");

    cc_init(&vm, CCCC_CFI | CCCC_STACK_CANARIES | CCCC_DANGLING_DETECT);
    vm.compiler.compiler_family = CCCC_COMPILER_FAMILY_GCC;
    init_mode_macros(&vm);
    register_stdlib_functions(&vm);
    cc_register_cfunc(&vm, "reload_hook", (void *)hook, 0, 0);

    Token *tok = cc_preprocess(&vm, hook_path);
    CHECK(tok != NULL, "preprocess");
    Obj *prog   = cc_parse(&vm, tok);
    Obj *merged = cc_link_progs(&vm, &prog, 1);
    cc_expand_macros(&vm, merged);
    cc_compile(&vm, merged);

    // a = 2, b = 3, then count() = (2 + 2) * 10, k() = 4, plain() = 1.
    int rc = cc_run_at(&vm, (Pc)vm.text_seg[0], 0, NULL);
    CHECK(rc == 2 + 300 + 40000 + 40000 + 1, "program result %d", rc);
    cc_destroy(&vm);
}

// The comptime code is regenerated for each expansion, so a reload or fix can
// call a live comptime function. A reload that changes or adds comptime code
// builds the comptime program again: statics and variables that still match
// keep their values, and the functions that call comptime code expand again.
static const char *ct_path;

static void write_ct(const char *comptime, const char *f, const char *g,
                     const char *extra) {
    char text[2048];
    snprintf(text, sizeof(text),
             "%s\n"
             "int f(void) { return %s; }\n"
             "int g(void) { return %s; }\n"
             "%s\n"
             "int reload_hook(void);\n"
             "int main(void) { reload_hook(); return f() + g(); }\n",
             comptime, f, g, extra);
    write_file("ct.c", text);
}

#define VAL(n)                                                                 \
    "@comptime Node *val(void) { return MakeIntLiteral(" #n "); }\n"

static long long ct_hook(void) {
    const char *val42 = VAL(42);
    write_ct(val42, "val()", "2", "");
    CHECK(cc_debug_reload(&vm, ct_path) == 0, "unchanged comptime reloads");
    write_ct(val42, "val()", "3", "");
    CHECK(cc_debug_reload(&vm, ct_path) == 0, "reloads again");

    write_ct(val42, "val() + 1", "3", "");
    CHECK(cc_debug_reload(&vm, ct_path) == 0, "new comptime call expands");
    CHECK(eval("f()") == 43, "f is val() + 1");
    CHECK(cc_redefine_function(&vm, "int f(void) { return val() + 5; }") == 0,
          "fix with a comptime call expands");
    CHECK(eval("f()") == 47, "f is val() + 5");
    CHECK(cc_redefine_function(&vm, "int f(void) { return val() + ; }") != 0,
          "fix with a parse error refused");

    write_ct(VAL(7), "val()", "3", "");
    CHECK(cc_debug_reload(&vm, ct_path) == 0, "changed comptime reloads");
    CHECK(eval("f()") == 7, "f expands the new body");
    CHECK(eval("g()") == 3, "g does not call comptime");

    write_ct(val42, "val()", "3", "");
    CHECK(cc_debug_reload(&vm, ct_path) == 0, "comptime changes back");
    CHECK(eval("f()") == 42, "an unchanged caller expands again");

    char both[512];
    snprintf(both, sizeof(both),
             "%s@comptime Node *extra(void) { return MakeIntLiteral(1); }",
             val42);
    write_ct(both, "val() + extra()", "3", "");
    CHECK(cc_debug_reload(&vm, ct_path) == 0, "new comptime function");
    CHECK(eval("f()") == 43, "f calls the new function");

    char tick[512];
    snprintf(tick, sizeof(tick),
             "%s@comptime Node *tick(void) { static int n = 10; "
             "return MakeIntLiteral(n++); }",
             val42);
    write_ct(tick, "tick()", "3", "");
    CHECK(cc_debug_reload(&vm, ct_path) == 0, "new comptime static");
    CHECK(eval("f()") == 10, "a new static is initialized");
    snprintf(tick, sizeof(tick),
             "%s@comptime Node *tick(void) { static int n = 10; n += 5; "
             "return MakeIntLiteral(n); }",
             val42);
    write_ct(tick, "tick()", "3", "");
    CHECK(cc_debug_reload(&vm, ct_path) == 0, "changed comptime static");
    CHECK(eval("f()") == 16, "a static keeps its value: %lld", eval("f()"));

    char var[512];
    snprintf(var, sizeof(var),
             "%s[[cccc::comptime]] int base = 100;\n"
             "@comptime Node *getbase(void) { return "
             "MakeIntLiteral(GetComptimeInt(\"base\")); }",
             val42);
    write_ct(var, "getbase()", "3", "");
    CHECK(cc_debug_reload(&vm, ct_path) == 0, "new comptime variable");
    CHECK(eval("f()") == 100, "a new variable is initialized");
    snprintf(var, sizeof(var),
             "%s[[cccc::comptime]] int base = 200;\n"
             "@comptime Node *getbase(void) { return "
             "MakeIntLiteral(GetComptimeInt(\"base\")); }",
             val42);
    write_ct(var, "getbase()", "3", "");
    CHECK(cc_debug_reload(&vm, ct_path) == 0, "changed comptime variable");
    CHECK(eval("f()") == 200, "an edited variable is initialized again");

    snprintf(var, sizeof(var),
             "%s[[cccc::comptime]] int th = 42;\n"
             "@comptime Node *thp(void) { return GetComptimePtr(\"th\"); }",
             val42);
    write_ct(var, "*(int *)thp()", "3", "");
    CHECK(cc_debug_reload(&vm, ct_path) == 0, "comptime pointer reloads");
    CHECK(eval("f()") == 42, "a new variable has a shadow");

    char gen[1024];
    snprintf(gen, sizeof(gen),
             "%s@comptime void mk(void) { Obj *fn = MakeFunction(\"gen_v\", "
             "GetType(\"int\")); WithFn(fn) { FunctionSetBody(fn, MakeReturn("
             "MakeIntLiteral(%%d))); } PublishNode(fn); }\n",
             val42);
    char gen7[1024], gen8[1024], genbad[1024];
    snprintf(gen7, sizeof(gen7), gen, 7);
    snprintf(gen8, sizeof(gen8), gen, 8);
    snprintf(genbad, sizeof(genbad),
             "%s@comptime void mk(void) { Obj *fn = MakeFunction(\"gen_v\", "
             "GetType(\"long\")); WithFn(fn) { FunctionSetBody(fn, MakeReturn("
             "MakeIntLiteral(1))); } PublishNode(fn); }\n",
             val42);
    write_ct(gen7, "val()", "3", "mk();");
    CHECK(cc_debug_reload(&vm, ct_path) == 0, "file-scope call runs");
    CHECK(eval("gen_v()") == 7, "a generated function is added");
    write_ct(gen8, "val()", "3", "mk();");
    CHECK(cc_debug_reload(&vm, ct_path) == 0, "file-scope call runs again");
    CHECK(eval("gen_v()") == 8, "a generated function is redefined");
    write_ct(genbad, "val()", "3", "mk();");
    CHECK(cc_debug_reload(&vm, ct_path) != 0, "generated signature change");
    CHECK(eval("gen_v()") == 8, "the generated function is as it was");
    write_ct(gen8, "val()", "3", "");
    CHECK(cc_debug_reload(&vm, ct_path) == 0, "call dropped");
    CHECK(eval("gen_v()") == 8, "the old generated function is kept");
    write_ct(val42, "val()", "3 +", "");
    CHECK(cc_debug_reload(&vm, ct_path) != 0, "parse error refused");
    CHECK(vm.error_count == 0, "diagnostics are cleared");
    snprintf(both, sizeof(both),
             "%s@comptime Node *bad(void) { return +; }", val42);
    write_ct(both, "val()", "3", "");
    CHECK(cc_debug_reload(&vm, ct_path) != 0, "broken comptime refused");
    CHECK(vm.error_count == 0, "diagnostics are cleared");
    CHECK(eval("f()") == 42, "the session is as it was");

    write_ct(val42, "val() + 1", "4", "");
    CHECK(cc_debug_reload(&vm, ct_path) == 0, "reload after refusals");
    CHECK(cc_redefine_function(&vm, "int g(void) { return val() + 5; }") == 0,
          "fix after refusals");
    return 0;
}

static void test_comptime(void) {
    ct_path = write_file("ct.c", "");
    write_ct("@comptime Node *val(void) { return MakeIntLiteral(42); }",
             "val()", "1", "");

    cc_init(&vm, CCCC_CFI | CCCC_STACK_CANARIES | CCCC_DANGLING_DETECT);
    vm.collect_errors           = true;
    vm.compiler.compiler_family = CCCC_COMPILER_FAMILY_GCC;
    init_mode_macros(&vm);
    register_stdlib_functions(&vm);
    cc_register_cfunc(&vm, "reload_hook", (void *)ct_hook, 0, 0);

    Token *tok = cc_preprocess(&vm, ct_path);
    CHECK(tok != NULL, "preprocess");
    Obj *prog   = cc_parse(&vm, tok);
    Obj *merged = cc_link_progs(&vm, &prog, 1);
    cc_expand_macros(&vm, merged);
    cc_compile(&vm, merged);

    int rc = cc_run_at(&vm, (Pc)vm.text_seg[0], 0, NULL);
    CHECK(rc == (42 + 1) + (42 + 5), "program result %d", rc);
    cc_destroy(&vm);
}

// Comptime globals keep their values between expansions, a function pointer
// held in one follows the regenerated code, and the paused frames below the
// expansion keep their locals.
static const char *state_path;

static long long state_hook(void) {
    CHECK(cc_redefine_function(&vm, "int b(void) { return next(); }") == 0,
          "first fix expands");
    CHECK(cc_redefine_function(&vm, "int b(void) { return next(); }") == 0,
          "second fix expands");
    CHECK(cc_redefine_function(&vm, "int c(void) { return bad(); }") != 0,
          "fix whose expansion fails refused");
    CHECK(cc_redefine_function(&vm, "int c(void) { return viaptr(); }") == 0,
          "fix through a function pointer table expands");
    CHECK(cc_redefine_function(&vm, "int d(void) { return next() +; }") != 0,
          "failed fix refused");
    CHECK(cc_redefine_function(&vm, "int d(void) { return next(") != 0,
          "truncated comptime call refused");
    CHECK(cc_redefine_function(&vm, "int d(void) { return deep(") != 0,
          "truncated call refused");
    CHECK(cc_redefine_function(&vm, "int c(void) { return viaptr(); }") == 0,
          "session survives truncated input");
    return 0;
}

static void test_comptime_state(void) {
    state_path = write_file(
        "state.c",
        "@comptime Node *next(void) { static int n = 100; "
        "return MakeIntLiteral(n++); }\n"
        "@comptime Node *one(void) { return MakeIntLiteral(1); }\n"
        "@comptime Node *bad(void) { return 0; }\n"
        "@comptime Node *viaptr(void) { static Node *(*t[1])(void) = { one }; "
        "return t[0](); }\n"
        "int a(void) { return next(); }\n"
        "int b(void) { return 0; }\n"
        "int c(void) { return 0; }\n"
        "int reload_hook(void);\n"
        "int deep(int x) { int y = x * 2; reload_hook(); return y + x; }\n"
        "int main(void) {\n"
        "  int keep = 7;\n"
        "  int r = deep(5);\n"
        "  return a() + b() + c() + r + keep;\n"
        "}\n");

    cc_init(&vm, CCCC_CFI | CCCC_STACK_CANARIES | CCCC_DANGLING_DETECT);
    vm.collect_errors           = true;
    vm.compiler.compiler_family = CCCC_COMPILER_FAMILY_GCC;
    init_mode_macros(&vm);
    register_stdlib_functions(&vm);
    cc_register_cfunc(&vm, "reload_hook", (void *)state_hook, 0, 0);

    Token *tok = cc_preprocess(&vm, state_path);
    CHECK(tok != NULL, "preprocess");
    Obj *prog   = cc_parse(&vm, tok);
    Obj *merged = cc_link_progs(&vm, &prog, 1);
    cc_expand_macros(&vm, merged);
    cc_compile(&vm, merged);

    // a = 100, b = 101 then 102, c = 1, deep(5) = 15, keep = 7.
    int rc = cc_run_at(&vm, (Pc)vm.text_seg[0], 0, NULL);
    CHECK(rc == 100 + 102 + 1 + 15 + 7, "program result %d", rc);
    cc_destroy(&vm);
}

int main(void) {
    snprintf(dir, sizeof(dir), "%s/cccc-reload-XXXXXX",
             getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    if (!mkdtemp(dir)) {
        fprintf(stderr, "FAIL: mkdtemp\n");
        return 1;
    }
    cc_init(&vm, CCCC_ENABLE_DEBUGGER | CCCC_CFI | CCCC_STACK_CANARIES |
                     CCCC_DANGLING_DETECT);
    vm.compiler.compiler_family = CCCC_COMPILER_FAMILY_GCC;
    init_mode_macros(&vm);
    register_stdlib_functions(&vm);
    cc_repl_init(&vm);

    test_changed_and_kept();
    test_refusals();
    cc_destroy(&vm);
    test_running_program();
    test_comptime();
    test_comptime_state();

    if (!failed)
        printf("PASS: debugger reload\n");
    return failed;
}
