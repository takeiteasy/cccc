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
    CHECK(cc_debug_reload(&vm, p) == 0, "reload again");
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

    if (!failed)
        printf("PASS: debugger reload\n");
    return failed;
}
