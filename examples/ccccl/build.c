// Build script for the ccccl example. Run from this directory with:
//
//     cccc --build build.c                 # build + check every example
//     cccc --build build.c --build-target=fib   # just one native binary
//
// Every example is built two ways, and both must produce identical output:
//
//   - native   — one `cccc --compile=native` invocation (a CcccExecutable
//                target): the comptime pass lowers the `.lisp` and the whole
//                program is compiled and linked in a single step, no
//                intermediate file. This is the path the plain `Makefile`
//                could not express — a build target could not use cccc as its
//                compiler.
//   - generated — `cccc -c=generated` writes an inspectable `build/NAME.gen.c`,
//                then the system `cc` links it against the hand-written
//                `NAME_main.c` (library examples) and the runtime. The
//                portability proof: nothing but a C compiler past that point.
//
// `check` runs each built binary and diffs its output against `NAME.expected`.

#include <stdio.h>

// Library examples: the .lisp defines functions only; examples/NAME_main.c
// drives them. Program examples: the .lisp also runs toplevel forms, cccc
// synthesizes main(), no host TU.
static const char *LIB_EXAMPLES[] = {
    "append", "lambda_head", "mutual", "reverse", "fib", "adder", "letsum"};
static const char *PROG_EXAMPLES[] = {"hello"};

// The comptime pass: src/ccccl_comptime.c is the emitter; the reader / IR /
// lowering modules are ordinary .h/.c pairs it pulls in with `#include
// @comptime`. All appear on the cccc command line; none is linked into a
// binary. runtime/ccccl_rt.c is the only file that reaches the final link.
static const char *COMPTIME_SRC[]  = {"src/ccccl_comptime.c", "src/ccccl_ir.c",
                                      "src/ccccl_form.c", "src/ccccl_lower.c"};
static const char *COMPTIME_HDRS[] = {
    "include/ccccl/ccccl_form.h", "include/ccccl/ccccl_ir.h",
    "include/ccccl/ccccl_lower.h", "runtime/ccccl_rt.h",
    "runtime/ccccl_rt_internal.h"};

// Declare the inputs whose edits must invalidate a CcccExecutable target:
// the comptime modules' headers, the runtime headers, and the .lisp itself
// (read via -D at comptime — no #include, so no depfile would ever see it).
static void add_comptime_inputs(BuildTarget *t, const char *lisp) {
    for (int i = 0; i < (int)(sizeof(COMPTIME_HDRS) / sizeof(*COMPTIME_HDRS));
         i++)
        AddInput(t, COMPTIME_HDRS[i]);
    AddInput(t, lisp);
}

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *check =
        RunCustom(ctx, "check", "echo 'ccccl: all examples ok'");

    int n_lib  = (int)(sizeof(LIB_EXAMPLES) / sizeof(*LIB_EXAMPLES));
    int n_prog = (int)(sizeof(PROG_EXAMPLES) / sizeof(*PROG_EXAMPLES));

    for (int e = 0; e < n_lib + n_prog; e++) {
        const char *name =
            e < n_lib ? LIB_EXAMPLES[e] : PROG_EXAMPLES[e - n_lib];
        int  is_lib = e < n_lib;

        char lisp[128], lispdef[160], mainc[128], genc[128], cmd[1024];
        snprintf(lisp, sizeof(lisp), "examples/%s.lisp", name);
        snprintf(lispdef, sizeof(lispdef), "\"examples/%s.lisp\"", name);
        snprintf(mainc, sizeof(mainc), "examples/%s_main.c", name);
        snprintf(genc, sizeof(genc), "build/%s.gen.c", name);

        // --- native: one whole-program `cccc --compile=native` ---
        char nativename[64];
        snprintf(nativename, sizeof(nativename), "%s", name);
        BuildTarget *nat = CcccExecutable(ctx, nativename);
        for (int i = 0; i < (int)(sizeof(COMPTIME_SRC) / sizeof(*COMPTIME_SRC));
             i++)
            AddSource(nat, COMPTIME_SRC[i]);
        AddSource(nat, "runtime/ccccl_rt.c");
        if (is_lib)
            AddSource(nat, mainc);
        AddInclude(nat, "include/ccccl");
        AddInclude(nat, "runtime");
        AddDefine(nat, "CCCCL_LISP_PATH", lispdef);
        add_comptime_inputs(nat, lisp);

        char checknat[80];
        snprintf(checknat, sizeof(checknat), "check-%s-native", name);
        snprintf(cmd, sizeof(cmd), "%s | diff - examples/%s.expected",
                 TargetOutput(nat), name);
        BuildTarget *cn = RunCustom(ctx, checknat, cmd);
        DependsOn(cn, nat);
        DependsOn(check, cn);

        // --- generated: `cccc -c=generated` then a plain `cc` link ---
        snprintf(cmd, sizeof(cmd),
                 "mkdir -p build && %s -c=generated src/ccccl_comptime.c "
                 "src/ccccl_ir.c src/ccccl_form.c src/ccccl_lower.c "
                 "-Iinclude/ccccl -Iruntime -DCCCCL_LISP_PATH='%s' -o %s",
                 CcccPath(ctx), lispdef, genc);
        char genname[80];
        snprintf(genname, sizeof(genname), "%s.gen", name);
        BuildTarget *gen = RunCustom(ctx, genname, cmd);
        DeclareOutput(gen, genc);
        for (int i = 0; i < (int)(sizeof(COMPTIME_SRC) / sizeof(*COMPTIME_SRC));
             i++)
            AddInput(gen, COMPTIME_SRC[i]);
        add_comptime_inputs(gen, lisp);

        char genexe[80];
        snprintf(genexe, sizeof(genexe), "%s-generated", name);
        BuildTarget *ge = Executable(ctx, genexe);
        AddSourcesGlobDeferred(ge, genc);
        AddSource(ge, "runtime/ccccl_rt.c");
        if (is_lib)
            AddSource(ge, mainc);
        AddInclude(ge, "runtime");
        DependsOn(ge, gen);

        char checkgen[80];
        snprintf(checkgen, sizeof(checkgen), "check-%s-generated", name);
        snprintf(cmd, sizeof(cmd), "%s | diff - examples/%s.expected",
                 TargetOutput(ge), name);
        BuildTarget *cg = RunCustom(ctx, checkgen, cmd);
        DependsOn(cg, ge);
        DependsOn(check, cg);
    }

    return BuildDefault(ctx);
}
