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

#include <stdbool.h>
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
    bool        skip_cleanups;
    int        *disarm; // zeroed after the op is scheduled
} plan;

static void arm(int frame, const char *ret_expr) {
    plan.frame    = frame;
    plan.ret_expr = ret_expr;
    plan.fire     = 1;
    plan.rc       = -1;
    plan.skip_cleanups = false;
    plan.disarm        = NULL;
}

static long long hook(void) {
    plan.frames_seen = cc_debug_frame_count(&vm);
    if (plan.fire <= 0)
        return 0;
    plan.fire--;
    bool run = !plan.skip_cleanups;
    plan.rc  = plan.ret_expr
                   ? cc_debug_return(&vm, plan.frame, plan.ret_expr, run)
                   : cc_debug_restart(&vm, plan.frame, run);
    if (plan.disarm)
        *plan.disarm = 0;
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
    int rc_restart           = cc_debug_restart(&vm, 0, true);
    int rc_return            = cc_debug_return(&vm, 0, "1", true);
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
            cc_debug_restart(&vm, 0, true);
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

static long long cfix_hook(void) {
    if (cc_debug_fix(&vm, "fxc", "int fxc(void){ return 9; }") == 0)
        cc_debug_return(&vm, 0, "0", true);
    return 0;
}

static int host_log;

static void host_rec(int *p) {
    host_log += *p;
}

// Guest [[cleanup]] handlers log their variable into lg[]; hook() pops a frame
// while the variables are live.
static void test_cleanup(void) {
    decl("int lg[16]; int ln = 0; int armed = 0;");
    decl("void rec(int *p){ lg[ln++] = *p; }");
    decl("void rech(int *p){ lg[ln++] = *p; hook(); }");
    decl("void bad(int *p){ if (armed) lg[0] = 1 / (armed - 1); }");
    long long armed = eval("(long long)&armed");
    long long ln    = eval("(long long)&ln");
    int      *lgp   = (int *)eval("(long long)lg");
#define RESET() (*(int *)ln = 0)
#define LOG(i)  lgp[i]

    decl("int vlog = 0; void recv(void *p){ vlog++; }");
    decl("int vla(int n){ __attribute__((cleanup(recv))) int a[n]; hook();"
         " return 0; }");
    long long vlog = eval("(long long)&vlog");
    arm(0, "0");
    eval("vla(3)");
    CHECK(*(int *)vlog == 1, "VLA handler ran: %d", *(int *)vlog);

    // Frame 0, two nested scopes: innermost declared first out.
    decl("int nest(void){ __attribute__((cleanup(rec))) int a = 1;"
         " { __attribute__((cleanup(rec))) int b = 2; hook(); } return 7; }");
    plan.fire = 0;
    RESET();
    CHECK(eval("nest()") == 7, "no op");
    CHECK(*(int *)ln == 2 && LOG(0) == 2 && LOG(1) == 1,
          "natural exit order: %d %d %d", *(int *)ln, LOG(0), LOG(1));
    RESET();
    arm(0, "9");
    CHECK(eval("nest()") == 9, "return pops the frame");
    CHECK(plan.rc == 0, "return scheduled");
    CHECK(*(int *)ln == 2 && LOG(0) == 2 && LOG(1) == 1,
          "return runs both handlers, innermost first: %d %d %d", *(int *)ln,
          LOG(0), LOG(1));

    // Several frames: the innermost frame's handlers run first.
    decl("int cin(void){ __attribute__((cleanup(rec))) int c = 3; hook();"
         " return 1; }");
    decl("int cout(void){ __attribute__((cleanup(rec))) int d = 4;"
         " return cin() + 100; }");
    RESET();
    arm(1, "5");
    CHECK(eval("cout()") == 5, "return from the outer frame");
    CHECK(*(int *)ln == 2 && LOG(0) == 3 && LOG(1) == 4,
          "inner frame first: %d %d %d", *(int *)ln, LOG(0), LOG(1));

    // Variables not yet declared, or already out of scope, owe nothing.
    decl("int late(void){ __attribute__((cleanup(rec))) int a = 1; hook();"
         " __attribute__((cleanup(rec))) int b = 2; return b; }");
    RESET();
    arm(0, "0");
    eval("late()");
    CHECK(*(int *)ln == 1 && LOG(0) == 1, "only the declared variable: %d",
          *(int *)ln);
    decl("int gone(void){ { __attribute__((cleanup(rec))) int a = 1; }"
         " hook(); return 0; }");
    RESET();
    arm(0, "0");
    eval("gone()");
    CHECK(*(int *)ln == 1, "a closed scope's handler ran once: %d", *(int *)ln);

    // A pause inside a variable's initializer owes nothing for that variable.
    decl("int mkv(void){ hook(); return 5; }");
    decl("int ini(void){ __attribute__((cleanup(rec))) int a = mkv();"
         " return a; }");
    RESET();
    arm(1, "0");
    eval("ini()");
    CHECK(*(int *)ln == 0, "initializer in progress: %d", *(int *)ln);
    // A pause inside a handler: the ones already started or run are done.
    decl("int midh(void){ __attribute__((cleanup(rec))) int a = 1;"
         " __attribute__((cleanup(rech))) int b = 2; return 0; }");
    RESET();
    arm(1, "0");
    eval("midh()");
    CHECK(*(int *)ln == 2 && LOG(0) == 2 && LOG(1) == 1,
          "natural exit, paused in the first handler: %d %d %d", *(int *)ln,
          LOG(0), LOG(1));
    decl("int early(int x){ __attribute__((cleanup(rec))) int a = 1;"
         " if (x) { __attribute__((cleanup(rech))) int b = 2; return 5; }"
         " return 6; }");
    RESET();
    arm(1, "0");
    eval("early(1)");
    CHECK(*(int *)ln == 2 && LOG(0) == 2 && LOG(1) == 1,
          "return path, paused in the first handler: %d %d %d", *(int *)ln,
          LOG(0), LOG(1));
    plan.fire = 0;
    RESET();
    CHECK(eval("early(0)") == 6, "later code is still owed its handler");
    CHECK(*(int *)ln == 1, "fall-through after an early exit: %d", *(int *)ln);
    decl("int brk(void){ for (int i = 0; i < 2; i++) {"
         " __attribute__((cleanup(rec))) int a = 1;"
         " __attribute__((cleanup(rech))) int b = 2; break; } return 0; }");
    RESET();
    arm(1, "0");
    eval("brk()");
    CHECK(*(int *)ln == 2 && LOG(0) == 2 && LOG(1) == 1,
          "break path, paused in the first handler: %d %d %d", *(int *)ln,
          LOG(0), LOG(1));

    // restart abandons the frame, so its handlers run too.
    decl("int rs(void){ __attribute__((cleanup(rec))) int a = 1; hook();"
         " return ln; }");
    RESET();
    arm(0, NULL);
    CHECK(eval("rs()") == 1, "restart ran the abandoned frame's handler");
    CHECK(*(int *)ln == 2, "and the re-entered body its own: %d", *(int *)ln);

    // -n skips them.
    RESET();
    arm(0, "9");
    plan.skip_cleanups = true;
    CHECK(eval("nest()") == 9, "return -n pops the frame");
    CHECK(*(int *)ln == 0, "handlers skipped: %d", *(int *)ln);

    // A failing handler rejects the op and leaves the frame running.
    decl("int bd(void){ __attribute__((cleanup(bad))) int a = 1; hook();"
         " return 7; }");
    RESET();
    *(int *)armed = 1;
    arm(0, "9");
    plan.disarm = (int *)armed;
    CHECK(eval("bd()") == 7, "frame not popped after a handler failure");
    CHECK(plan.rc != 0, "op rejected");
    CHECK(*(int *)armed == 0, "hook disarmed the handler");

    // A host (FFI) handler.
    decl("void host_rec(int *p);");
    decl("int hc(void){ __attribute__((cleanup(host_rec))) int a = 7; hook();"
         " return 0; }");
    host_log = 0;
    arm(0, "0");
    eval("hc()");
    CHECK(host_log == 7, "host handler ran once: %d", host_log);

    // The popped frame runs the body it was paused in, even after a fix.
    decl("int cfix_hook(void);");
    decl("int fxc(void){ __attribute__((cleanup(rec))) int a = 1;"
         " cfix_hook(); return 5; }");
    RESET();
    CHECK(eval("fxc()") == 0, "popped through the fix");
    CHECK(*(int *)ln == 1 && LOG(0) == 1, "old body's handler ran: %d",
          *(int *)ln);
    CHECK(eval("fxc()") == 9, "later calls run the new body");
#undef RESET
#undef LOG
}

static Obj *find_fn(const char *name) {
    for (Obj *o = vm.compiler.globals; o; o = o->next)
        if (o->is_function && o->body && o->name && !strcmp(o->name, name))
            return o;
    return NULL;
}

static void test_fix_editor(void) {
    decl("int src_a(int x){ return x + 1; }");

    size_t      len = 0;
    const char *src = cc_function_source(find_fn("src_a"), &len);
    CHECK(src && len == strlen("int src_a(int x){ return x + 1; }") &&
              !strncmp(src, "int src_a(int x){ return x + 1; }", len),
          "the definition's source text is recorded");

    CHECK(cc_debug_fix_edit(&vm, "src_a", "sed -i.bak -e 's/+ 1/+ 7/'") == 0,
          "edit through an editor command");
    CHECK(eval("src_a(1)") == 8, "the edited body runs");
    src = cc_function_source(find_fn("src_a"), &len);
    CHECK(src && !strncmp(src, "int src_a(int x){ return x + 7; }", len),
          "an edited function keeps its source");

    CHECK(cc_debug_fix_edit(&vm, "src_a", "true") != 0,
          "an unchanged file does nothing");
    CHECK(cc_debug_fix_edit(&vm, "src_a", "sed -i.bak -e 's/+ 7/+/'") != 0,
          "a body that does not compile is rejected");
    CHECK(cc_debug_fix_edit(&vm, "src_a", "false") != 0,
          "a failing editor cancels");
    CHECK(eval("src_a(1)") == 8, "function intact after rejected edits");

    CHECK(cc_debug_fix(&vm, "src_a",
                       "int src_a(int x){ return x + __STDC__; }") == 0,
          "macros expand in fix source");
    CHECK(eval("src_a(41)") == 42, "expanded macro value");
    CHECK(cc_debug_fix(&vm, "src_a",
                       "#define Q 1\nint src_a(int x){ return x; }") != 0,
          "directives are rejected");
}

// Declares `int name(int x)` over four lines and reports the pcs of lines 3
// and 2.
static void decl_fn(const char *name, Pc *line3, Pc *line2) {
    char src[128];
    snprintf(src, sizeof(src), "int %s(int x){\n  int y = x;\n  return y;\n}",
             name);
    decl(src);
    File *f = last_compiled_file();
    if (line3)
        *line3 = cc_find_pc_for_source(&vm, f, 3);
    if (line2)
        *line2 = cc_find_pc_for_source(&vm, f, 2);
}

static int cond_bp(Pc pc, const char *cond) {
    int i = cc_add_breakpoint(&vm, pc);
    CHECK(i >= 0, "breakpoint set");
    vm.dbg.breakpoints[i].condition = strdup(cond);
    return i;
}

static void test_reclaim(void) {
    int dead0 = vm.compiler.num_dead_marks; // wrappers pinned by earlier tests
    decl("const char *rc_sp;");
    eval("1 + 2");
    Pc    t0 = vm.text_ptr;
    char *d0 = vm.data_ptr;
    for (int i = 0; i < 1000; i++)
        eval("1 + 2");
    CHECK(vm.text_ptr == t0 && vm.data_ptr == d0,
          "one-shot wrappers are reclaimed: text %u -> %u", t0, vm.text_ptr);

    eval("(rc_sp = \"abc\", 0)");
    CHECK(vm.text_ptr == t0, "text of a string-literal wrapper reclaimed");
    CHECK(vm.data_ptr > d0, "the string literal's data is kept");
    CHECK(eval("rc_sp[1]") == 'b', "the string literal is intact");

    t0 = vm.text_ptr;
    d0 = vm.data_ptr;
    CHECK(cc_repl_eval(&vm, "1 +", NULL) != 0, "bad expression rejected");
    CHECK(vm.text_ptr == t0 && vm.data_ptr == d0, "failed compile reclaimed");

    // Conditions: shared per function, released in any order.
    Pc line2, line3;
    Pc a_in = 0, b_in = 0, c_in = 0;
    decl_fn("rc_a", &a_in, NULL);
    decl_fn("rc_b", &b_in, NULL);
    decl_fn("rc_c", &c_in, NULL);
    decl_fn("rc_d", &line3, &line2);
    Pc  base = vm.text_ptr;

    int a    = cond_bp(a_in, "x > 99");
    int b    = cond_bp(b_in, "x > 99");
    int c    = cond_bp(c_in, "x > 98");
    int d1   = cond_bp(line2, "x > 99");
    int d2   = cond_bp(line3, "x > 99");
    eval("rc_a(1)");
    char *dbase = vm.data_ptr;
    eval("rc_b(1)");
    eval("rc_c(1)");
    eval("rc_d(1)");
    CHECK(vm.text_ptr > base, "conditions compiled");
    CHECK(vm.dbg.num_conds == 4, "one wrapper per function and text: %d",
          vm.dbg.num_conds);
    CHECK(vm.dbg.breakpoints[d1].cond_fn &&
              vm.dbg.breakpoints[d1].cond_fn == vm.dbg.breakpoints[d2].cond_fn,
          "the same condition in one function is shared");

    cc_remove_breakpoint(&vm, a);
    cc_remove_breakpoint(&vm, c);
    cc_remove_breakpoint(&vm, d1);
    CHECK(vm.dbg.num_conds == 2, "shared wrapper outlives one breakpoint: %d",
          vm.dbg.num_conds);
    CHECK(eval("rc_d(1)") == 1, "condition still evaluates");
    cc_remove_breakpoint(&vm, d2);
    cc_remove_breakpoint(&vm, b);
    CHECK(vm.dbg.num_conds == 0, "all conditions dropped");
    CHECK(vm.text_ptr == base && vm.data_ptr == dbase,
          "segments back at the baseline: text %u -> %u", base, vm.text_ptr);
    CHECK(vm.compiler.num_dead_marks == dead0, "no wrapper left waiting: %d",
          vm.compiler.num_dead_marks - dead0);

    // A condition still works where an earlier wrapper's code was.
    int e = cond_bp(a_in, "x == 7");
    CHECK(eval("rc_a(1)") == 1, "false condition");
    cc_remove_breakpoint(&vm, e);
}

// A returned frame's &local tag must not make a condition on a later frame at
// the same stack depth look dangling.
static void test_condition_stale_tag(void) {
    decl("int st_g(int x){\n  int *p = &x;\n  return *p;\n}");
    decl("int st_f(int x){\n  int a[4] = {1, 2, 3, 4};\n  int y = a[x];\n"
         "  return y;\n}");
    File *f     = last_compiled_file();
    Pc    line3 = cc_find_pc_for_source(&vm, f, 3);
    Pc    line4 = cc_find_pc_for_source(&vm, f, 4);

    int   hit   = cond_bp(line3, "x == 2 && a[x] == 3");
    int   miss  = cond_bp(line4, "x == 3");
    eval("st_g(2)");
    CHECK(eval("st_f(2)") == 3, "st_f(2)");
    CHECK(vm.dbg.breakpoints[hit].hit_count == 1,
          "true condition fires after a stale tag: %d",
          vm.dbg.breakpoints[hit].hit_count);
    CHECK(vm.dbg.breakpoints[miss].hit_count == 0, "false condition is silent");
    cc_remove_breakpoint(&vm, hit);
    cc_remove_breakpoint(&vm, miss);
}

// Steps over the instructions in [from, to) and reports whether they end
// exactly on `to`.
static bool text_walk_aligned(Pc from, Pc to) {
    Pc pc = from;
    while (pc < to) {
        int words = cc_instr_words((int)vm.text_seg[pc]);
        if (words <= 0)
            return false;
        pc += (Pc)words;
    }
    return pc == to;
}

static bool source_map_sorted(void) {
    for (int i = 1; i < vm.dbg.source_map_count; i++)
        if (vm.dbg.source_map[i].pc_offset < vm.dbg.source_map[i - 1].pc_offset)
            return false;
    return true;
}

// Text freed below live code is reused by later conditions.
static void test_holes(void) {
    int dead0  = vm.compiler.num_dead_marks;
    int holes0 = vm.compiler.num_text_holes;
    Pc  a_in, b_in, c_in;
    decl_fn("ho_a", &a_in, NULL);
    decl_fn("ho_b", &b_in, NULL);
    decl_fn("ho_c", &c_in, NULL);

    int a = cond_bp(a_in, "x > 99 && x < 200");
    eval("ho_a(1)");
    Obj *aw = vm.dbg.breakpoints[a].cond_fn;
    CHECK(aw != NULL, "condition A compiled");
    Pc a_code = (Pc)aw->code_addr;
    Pc a_end  = (Pc)aw->code_end_addr;

    decl("int ho_live(int x){ return x + 1; }");
    Obj *live = find_fn("ho_live");
    CHECK(live && (Pc)live->code_addr == a_end, "live code sits right above A");
    Pc    top     = vm.text_ptr;
    File *lf      = last_compiled_file();
    Pc    live_pc = cc_find_pc_for_source(&vm, lf, 1);

    cc_remove_breakpoint(&vm, a);
    CHECK(vm.text_ptr == top, "live code above the wrapper stays");
    CHECK(vm.compiler.num_text_holes == holes0 + 1, "A's text is a hole: %d",
          vm.compiler.num_text_holes);
    CHECK(vm.compiler.num_dead_marks == dead0, "no wrapper left waiting");
    CHECK(vm.text_seg[a_code] == HOLE && vm.text_seg[a_end - 1] == HOLE,
          "the hole is filled with HOLE words");
    CHECK(text_walk_aligned(a_code, a_end), "a hole can be walked");

    // A smaller condition fits in a hole (the evaluation wrapper that ran A
    // was freed below A, so there may be an earlier one).
    int b = cond_bp(b_in, "x == 1");
    eval("ho_b(1)");
    Obj *bw = vm.dbg.breakpoints[b].cond_fn;
    CHECK(bw && (Pc)bw->code_addr < top, "B reuses freed text: %lld vs %u",
          bw ? bw->code_addr : -1, top);
    CHECK(vm.text_ptr == top, "text top unchanged by a hole compile");
    Pc b_end = bw ? (Pc)bw->code_end_addr : 0;
    for (int i = 0; i < vm.compiler.num_text_holes; i++)
        if (vm.compiler.text_holes[i].start == b_end)
            CHECK(text_walk_aligned((Pc)bw->code_addr,
                                    vm.compiler.text_holes[i].end),
                  "a partly filled hole can be walked");
    CHECK(eval("ho_b(1)") == 1, "program runs with B in the hole");
    CHECK(vm.dbg.breakpoints[b].hit_count == 2,
          "B evaluates true in the hole: %d", vm.dbg.breakpoints[b].hit_count);
    CHECK(source_map_sorted(), "source map stays sorted");
    CHECK(cc_find_pc_for_source(&vm, lf, 1) == live_pc,
          "live code keeps its line");

    // A larger condition does not fit and goes to the top.
    int c = cond_bp(c_in, "x > 99 && x < 200 && x != 150 && x != 151");
    eval("ho_c(1)");
    Obj *cw = vm.dbg.breakpoints[c].cond_fn;
    CHECK(cw && (Pc)cw->code_addr > top, "C does not fit and goes on top");

    // Holes next to each other merge, and the top takes them back.
    cc_remove_breakpoint(&vm, b);
    cc_remove_breakpoint(&vm, c);
    CHECK(vm.text_ptr == top, "top wrapper released: %u vs %u", vm.text_ptr,
          top);
    CHECK(source_map_sorted(), "source map sorted after releases");

    // A wrapper released while conditions run waits until they are done.
    int e = cond_bp(a_in, "x == 3");
    eval("ho_a(1)");
    int holes1        = vm.compiler.num_text_holes;
    vm.dbg.cond_depth = 1;
    cc_remove_breakpoint(&vm, e);
    CHECK(vm.compiler.num_text_holes == holes1 &&
              vm.compiler.num_dead_marks == dead0 + 1,
          "release deferred while a condition runs");
    vm.dbg.cond_depth = 0;
    cc_code_retry_dead(&vm);
    CHECK(vm.compiler.num_dead_marks == dead0, "deferred release done");
}

// The symbol table grows past its initial capacity.
static void test_many_symbols(void) {
    enum { N = 5000 };
    char *src = malloc((size_t)N * 32);
    char *p   = src;
    for (int i = 0; i < N; i++)
        p += sprintf(p, "int sym_many_%d;", i);
    decl(src);
    free(src);
    CHECK(vm.dbg.num_debug_symbols >= N, "symbols recorded: %d",
          vm.dbg.num_debug_symbols);
    CHECK(cc_lookup_symbol(&vm, "sym_many_0") != NULL, "first symbol found");
    CHECK(cc_lookup_symbol(&vm, "sym_many_4999") != NULL, "last symbol found");
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
    cc_register_cfunc(&vm, "cfix_hook", (void *)cfix_hook, 0, 0);
    cc_register_cfunc(&vm, "host_rec", (void *)host_rec, 1, 0);
    cc_repl_init(&vm);

    test_holes();
    test_return();
    test_return_locals();
    test_return_types();
    test_restart();
    test_rejections();
    test_session();
    test_cleanup();
    test_fix();
    test_fix_editor();
    test_reclaim();
    test_condition_stale_tag();
    test_many_symbols();

    cc_destroy(&vm);
    if (!failed)
        printf("PASS: debugger frames\n");
    return failed;
}
