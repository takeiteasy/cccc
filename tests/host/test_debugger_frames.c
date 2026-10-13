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

// Host-side test for the debugger's frame control: guest code calls a host
// hook, which schedules cc_debug_return()/cc_debug_restart() on a frame of the
// running program. Runs under CFI, stack canaries and dangling-pointer
// detection so a desynced shadow stack, canary or frame epoch fails the test.

#include "internal.h"

#include <stdio.h>
#include <string.h>
#include <signal.h>
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

static long long eval(const char *expr) {
    long long v  = -999999;
    int       rc = cc_repl_eval(&vm, expr, &v);
    CHECK(rc == 0, "eval failed: %s", expr);
    return v;
}

static void decl(const char *src) {
    CHECK(cc_repl_eval(&vm, src, NULL) == 0, "declaration failed: %s", src);
}

// What the next hook invocation does.
static struct {
    int         frame;
    const char *ret_expr; // NULL: restart
    int         fire;     // invocations left that act
    int         rc;
    int         frames_seen;
} plan;

static void arm(int frame, const char *ret_expr) {
    plan.frame    = frame;
    plan.ret_expr = ret_expr;
    plan.fire     = 1;
    plan.rc       = -1;
}

static long long hook(void) {
    plan.frames_seen = cc_debug_frame_count(&vm);
    if (plan.fire <= 0)
        return 0;
    plan.fire--;
    plan.rc = plan.ret_expr ? cc_debug_return(&vm, plan.frame, plan.ret_expr)
                            : cc_debug_restart(&vm, plan.frame);
    return 0;
}

// The debugger REPL reads commands from stdin and writes to stdout. These
// helpers feed it a script for the duration of one eval and capture what it
// printed.
static char        session_out[8192];
static const char *session_script;
static int         session_calls;

static long long session_hook(void) {
    session_calls++;
    if (session_script) {
        session_script = NULL;
        cc_debug_repl(&vm);
    }
    return 0;
}

static long long eval_session(const char *script, const char *expr) {
    FILE *in  = tmpfile();
    FILE *out = tmpfile();
    fputs(script, in);
    rewind(in);
    fflush(stdout);
    int saved_in  = dup(0);
    int saved_out = dup(1);
    dup2(fileno(in), 0);
    dup2(fileno(out), 1);
    clearerr(stdin);

    session_script = script;
    session_calls  = 0;
    long long v    = eval(expr);

    fflush(stdout);
    dup2(saved_in, 0);
    dup2(saved_out, 1);
    close(saved_in);
    close(saved_out);
    rewind(out);
    size_t n       = fread(session_out, 1, sizeof(session_out) - 1, out);
    session_out[n] = '\0';
    fclose(in);
    fclose(out);
    return v;
}

static void test_return(void) {
    decl("int hook(void);");
    decl("int inner(void){ hook(); return 1; }");
    decl("int outer(void){ return inner() + 100; }");

    CHECK(eval("outer() + 0") == 101, "no scheduled op");
    CHECK(plan.frames_seen == 3, "inner, outer and the eval root: %d",
          plan.frames_seen);

    arm(0, "7");
    CHECK(eval("outer()") == 107, "return 7 from the innermost frame");
    CHECK(plan.rc == 0, "return scheduled");

    arm(1, "7");
    CHECK(eval("outer() + 1") == 8, "return 7 from outer pops both frames");

    arm(0, "-1");
    CHECK(eval("outer()") == 99, "negative return value");

    // Later calls still pass CFI, canary and liveness checks.
    plan.fire = 0;
    CHECK(eval("outer()") == 101, "state consistent afterwards");
}

static void test_return_locals(void) {
    decl("int lc(int x){ int y = x * 2; hook(); return y; }");
    plan.fire = 0;
    {
        long long got = eval("lc(5) + 0");
        CHECK(got == 10, "plain call: %lld", got);
    }
    arm(0, "x + 1");
    {
        long long got = eval("lc(5) + 0");
        CHECK(got == 6, "return expression reads a parameter: %lld", got);
    }
    arm(0, "y + 1");
    {
        long long got = eval("lc(5) + 0");
        CHECK(got == 11, "return expression reads a local: %lld", got);
    }
}

static void test_return_types(void) {
    decl("double half(void){ hook(); return 1.0; }");
    arm(0, "2.5f");
    CHECK(eval("(int)(half() * 2)") == 5, "float return converts to double");

    decl("struct P { int a, b; };");
    decl("struct P mk(void){ struct P p = {1, 2}; hook(); return p; }");
    decl("int usep(void){ struct P p = mk(); return p.a * 10 + p.b; }");
    CHECK(eval("usep()") == 12, "struct return untouched");
    arm(0, "(struct P){3, 4}");
    CHECK(eval("usep()") == 34, "struct return through the return buffer");
    CHECK(plan.rc == 0, "struct return scheduled");

    decl("void vfn(void){ hook(); }");
    arm(0, "5");
    CHECK(plan.rc == -1, "nothing scheduled yet");
    long long v = 0;
    CHECK(cc_repl_eval(&vm, "(vfn(), 0)", &v) == 0, "void function runs");
    CHECK(plan.rc != 0, "a void function takes no return value");

    // Bare return leaves the registers alone.
    decl("int bare(void){ hook(); return 1; }");
    arm(0, "");
    eval("bare()");
    CHECK(plan.rc == 0, "bare return scheduled");
}

static void test_restart(void) {
    decl("int g = 1;");

    decl("int f(int x){ x += 1000; hook(); return x + g; }");
    long long gaddr = eval("(long long)&g");
    arm(0, NULL);
    *(int *)gaddr = 10;
    CHECK(eval("f(5)") == 1015, "restart re-enters with the original x");
    CHECK(plan.rc == 0, "restart scheduled");
    plan.fire = 0;
    CHECK(eval("f(5)") == 1015, "second run, no restart");

    decl("double h(double a, int b){ a *= 2; hook(); return a + b; }");
    arm(0, NULL);
    CHECK(eval("(int)h(2.5, 1)") == 6, "float argument restored");

    decl("int vf(int n, ...){ n++; hook(); return n; }");
    arm(0, NULL);
    CHECK(eval("vf(5, 6, 7)") == 6, "variadic frame restored");

    decl("int many(int a, int b, int c, int d, int e, int f2, int g2, int h2, "
         "int i, int j){ a += 100; hook(); return a + i * 10 + j; }");
    arm(0, NULL);
    CHECK(eval("many(1, 2, 3, 4, 5, 6, 7, 8, 9, 10)") == 1 + 100 + 90 + 10,
          "stack-passed arguments survive the restart");

    // Restart a caller's frame from the callee: pops both, re-runs the caller.
    decl("int callee(void){ hook(); return 1; }");
    decl("int calls = 0;");
    decl("int caller(int x){ calls++; return x + callee(); }");
    arm(1, NULL);
    CHECK(eval("caller(10)") == 11, "outer restart runs the caller twice");
    CHECK(eval("calls") == 2, "caller body entered twice");
    plan.fire = 0;
    CHECK(eval("caller(10)") == 11, "state consistent afterwards");
}

static void test_rejections(void) {
    decl("int rj(void){ hook(); return 3; }");

    arm(99, NULL);
    CHECK(eval("rj()") == 3, "out-of-range restart leaves the call alone");
    CHECK(plan.rc != 0, "out-of-range frame rejected");
    arm(99, "1");
    eval("rj()");
    CHECK(plan.rc != 0, "out-of-range return rejected");

    arm(0, "undefined_name + 1");
    CHECK(eval("rj()") == 3, "uncompilable value leaves the call alone");
    CHECK(plan.rc != 0, "bad return expression rejected");

    vm.dbg.frame_records_on = false;
    arm(0, NULL);
    CHECK(eval("rj()") == 3, "no snapshot: call completes normally");
    CHECK(plan.rc != 0, "restart without a snapshot rejected");
    vm.dbg.frame_records_on = true;

    // Both calls are rejected the way the REPL rejects them after a host
    // fault.
    vm.dbg.host_fault_signal = SIGSEGV;
    int rc_restart           = cc_debug_restart(&vm, 0);
    int rc_return            = cc_debug_return(&vm, 0, "1");
    vm.dbg.host_fault_signal = 0;
    CHECK(rc_restart != 0 && rc_return != 0,
          "frame ops rejected after a host fault");
}

static void test_session(void) {
    decl("int session_hook(void);");
    decl("int sg = 1;");
    decl("int leaf(int x){ int y = x * 2; session_hook(); return y + sg; }");
    decl("int mid(int a){ int t = a + 1; return leaf(t) + 100; }");

    CHECK(eval_session("bt\nup\nprint t\nprint a\ndown\nprint y\n"
                       "return y + 1\nc\n",
                       "mid(4) + 0") == 111,
          "return from the REPL");
    CHECK(strstr(session_out, "#0  leaf") && strstr(session_out, "#1  mid"),
          "bt lists the frames:\n%s", session_out);
    CHECK(strstr(session_out, "t = (int) 5") &&
              strstr(session_out, "a = (int) 4"),
          "print reads the selected frame's locals:\n%s", session_out);
    CHECK(strstr(session_out, "y = (int) 10"), "frame 0 locals:\n%s",
          session_out);

    CHECK(eval_session("restart\nc\n", "mid(4) + 0") == 111,
          "restart from the REPL");
    CHECK(session_calls == 2, "leaf ran twice: %d", session_calls);

    CHECK(eval_session("up\nrestart\nc\n", "mid(4) + 0") == 111,
          "restart of an outer frame from the REPL");
    CHECK(session_calls == 2, "leaf ran again from the restarted mid: %d",
          session_calls);
}

static int fix_rc    = -1;
static int fix_armed = 0;

static long long fix_hook(void) {
    if (fix_armed) {
        fix_armed = 0;
        fix_rc =
            cc_debug_fix(&vm, "fixme", "int fixme(int x){ return x * 3; }");
        if (fix_rc == 0)
            cc_debug_restart(&vm, 0);
    }
    return 0;
}

static File *last_compiled_file(void) {
    return vm.dbg.source_map[vm.dbg.source_map_count - 1].file;
}

static void test_fix(void) {
    decl("int fix_hook(void);");
    decl("int fixme(int x){ fix_hook(); return x + 1; }");
    CHECK(eval("fixme(5) + 0") == 6, "unfixed call");

    // Fix a function from inside its own running call, then re-enter it.
    fix_armed = 1;
    CHECK(eval("fixme(5) + 0") == 15, "restart runs the new body");
    CHECK(fix_rc == 0, "fix succeeded");
    CHECK(eval("fixme(2) + 0") == 6, "later calls run the new body");

    // Source lines of REPL-compiled code are indexed.
    decl("int bpf(int x){\n  int y = x + 1;\n  return y;\n}");
    File *f     = last_compiled_file();
    Pc    line3 = cc_find_pc_for_source(&vm, f, 3);
    CHECK(line3 != CCCC_INVALID_PC, "line of REPL code found");
    const char *name = cc_pc_to_name(&vm, line3);
    CHECK(name && !strcmp(name, "bpf"), "line maps into bpf: %s",
          name ? name : "(null)");

    // Breakpoints follow a fix: a function breakpoint moves to the new body, a
    // line breakpoint whose line is gone is dropped.
    int line_bp                          = cc_add_breakpoint(&vm, line3);
    vm.dbg.breakpoints[line_bp].src_file = f;
    vm.dbg.breakpoints[line_bp].src_line = 3;
    int fn_bp = cc_add_breakpoint(&vm, cc_find_function_entry(&vm, "bpf"));
    vm.dbg.breakpoints[fn_bp].func = strdup("bpf");
    Pc old_entry                   = vm.dbg.breakpoints[fn_bp].pc;

    CHECK(cc_debug_fix(&vm, "bpf", "int bpf(int x){\n  return x;\n}") == 0,
          "fix bpf");
    CHECK(!vm.dbg.breakpoints[line_bp].enabled, "line breakpoint dropped");
    CHECK(vm.dbg.breakpoints[fn_bp].enabled &&
              vm.dbg.breakpoints[fn_bp].pc != old_entry &&
              vm.dbg.breakpoints[fn_bp].pc ==
                  cc_find_function_entry(&vm, "bpf"),
          "function breakpoint moved to the new body");
    cc_remove_breakpoint(&vm, fn_bp);
    CHECK(cc_find_pc_for_source(&vm, f, 3) == CCCC_INVALID_PC,
          "the replaced body's lines are no longer indexed");

    CHECK(eval("bpf(7)") == 7, "fixed function runs");

    CHECK(cc_debug_fix(&vm, "no_such_fn",
                       "int no_such_fn(void){ return 1; }") != 0,
          "fix of an unknown function rejected");
    CHECK(cc_debug_fix(&vm, "bpf", "int other(void){ return 1; }") != 0,
          "source that does not define the function rejected");
    CHECK(cc_debug_fix(&vm, "bpf", "int bpf(int x){ return x +; }") != 0,
          "syntax error rejected");
    CHECK(eval("bpf(8)") == 8, "function intact after rejected fixes");
}

int main(void) {
    cc_init(&vm, CCCC_ENABLE_DEBUGGER | CCCC_CFI | CCCC_STACK_CANARIES |
                     CCCC_DANGLING_DETECT);
    vm.compiler.compiler_family = CCCC_COMPILER_FAMILY_GCC;
    init_mode_macros(&vm);
    register_stdlib_functions(&vm);
    cc_register_cfunc(&vm, "hook", (void *)hook, 0, 0);
    cc_register_cfunc(&vm, "session_hook", (void *)session_hook, 0, 0);
    cc_register_cfunc(&vm, "fix_hook", (void *)fix_hook, 0, 0);
    cc_repl_init(&vm);

    test_return();
    test_return_locals();
    test_return_types();
    test_restart();
    test_rejections();
    test_session();
    test_fix();

    cc_destroy(&vm);
    if (!failed)
        printf("PASS: debugger frames\n");
    return failed;
}
