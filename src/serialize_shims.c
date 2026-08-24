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

// Serialization: native-accessor/threads/uchar/posix-compat/
// canonical-const accessor shims for -c=native output (#1150).
#include "./serialize_internal.h"

// Public API: Serialize entire program to C source
// #904: CCCC's own polyfill headers (stdio.h/errno.h/getopt.h in src/std.c)
// define stdout/stderr/stdin/errno/optarg/optind/opterr/optopt as macros
// that expand to a call into an internal accessor shim (__cccc_stdout(),
// etc -- see src/stdlib/stdio.c and src/stdlib/posix_io.c) so they reflect
// the real host state instead of being inert, always-zero/NULL guest
// globals (#736). That macro expansion happens during preprocessing,
// before this backend ever runs, so the AST already contains a call to
// e.g. __cccc_stdout() with no record that it started life as `stdout`.
// Under -c=native the #901 fix correctly declines to serialize a
// prototype for these (they're declared in CCCC's own header, not the
// primary file, so #901's from_include check excludes them) -- but with
// no prototype AND no definition, the generated call is entirely
// undeclared and the downstream compiler rejects it outright. Define each
// shim actually used in terms of the real symbol instead: the auto-capture
// mechanism (this same function, just above) has already re-emitted the
// real #include that provides it, since that's the only way the macro
// which expands to this shim call could have been reached in the first
// place.
static const struct {
    const char *name;
    const char *def;
} native_accessor_shims[] = {
    {"__cccc_stdin", "static FILE *__cccc_stdin(void) { return stdin; }\n"},
    {"__cccc_stdout", "static FILE *__cccc_stdout(void) { return stdout; }\n"},
    {"__cccc_stderr", "static FILE *__cccc_stderr(void) { return stderr; }\n"},
    {"__cccc_errno_ptr",
     "static int *__cccc_errno_ptr(void) { return &errno; }\n"},
    {"__cccc_optarg_ptr",
     "static char **__cccc_optarg_ptr(void) { return &optarg; }\n"},
    {"__cccc_optind_ptr",
     "static int *__cccc_optind_ptr(void) { return &optind; }\n"},
    {"__cccc_opterr_ptr",
     "static int *__cccc_opterr_ptr(void) { return &opterr; }\n"},
    {"__cccc_optopt_ptr",
     "static int *__cccc_optopt_ptr(void) { return &optopt; }\n"},
    // #1021: include/math.h's isnan/isinf/signbit/fpclassify are themselves
    // #defined as `_Generic((x), float: __cccc_isnan_f, default:
    // __cccc_isnan_d)(x)` etc -- a shim body that read the plain macro name
    // would, once math.h's replayed #include brings that definition into
    // scope, expand right back into a call to itself (infinite recursion),
    // the same trap FLT_ROUNDS sits in below. __builtin_{isnan,isinf,
    // signbit} are portable clang/gcc intrinsics with no such indirection.
    // __builtin_fpclassify takes the FP_* class codes as arguments and
    // returns whichever one matches. Every call site comparing against
    // FP_INFINITE/FP_NAN/FP_NORMAL/FP_SUBNORMAL/FP_ZERO was already
    // constant-folded to CCCC's OWN numeric values (include/math.h:23-27)
    // at guest compile time, baked into the emitted TU as plain integer
    // literals -- so the shim must return CCCC's numbering regardless of
    // which <math.h> the shim's own text ends up seeing (confirmed the two
    // can genuinely differ: real macOS FP_ZERO is 3, not CCCC's 5).
    // Spelled as literals here, not the FP_* macro names, so this stays
    // correct even on a platform where a real host <math.h>'s FP_* values
    // don't match CCCC's own.
    {"__cccc_isnan_f",
     "static int __cccc_isnan_f(float x) { return __builtin_isnan(x); }\n"},
    {"__cccc_isnan_d",
     "static int __cccc_isnan_d(double x) { return __builtin_isnan(x); }\n"},
    {"__cccc_isinf_f",
     "static int __cccc_isinf_f(float x) { return __builtin_isinf(x); }\n"},
    {"__cccc_isinf_d",
     "static int __cccc_isinf_d(double x) { return __builtin_isinf(x); }\n"},
    {"__cccc_signbit_f",
     "static int __cccc_signbit_f(float x) { return __builtin_signbit(x); }\n"},
    {"__cccc_signbit_d", "static int __cccc_signbit_d(double x) { return "
                         "__builtin_signbit(x); }\n"},
    {"__cccc_fpclassify_f", "static int __cccc_fpclassify_f(float x) { return "
                            "__builtin_fpclassify(2, 1, 3, 4, 5, x); }\n"},
    {"__cccc_fpclassify_d", "static int __cccc_fpclassify_d(double x) { return "
                            "__builtin_fpclassify(2, 1, 3, 4, 5, x); }\n"},
    // #1021: include/float.h:73 defines `FLT_ROUNDS` itself as a call to
    // this shim (`#define FLT_ROUNDS (__cccc_flt_rounds())`) -- so a body
    // reading FLT_ROUNDS would textually expand right back into a call to
    // itself (infinite recursion) once float.h's replayed #include is in
    // scope. Signature matches float.h's own
    // `extern int __cccc_flt_rounds(void);` (:72), not src/stdlib/fenv.c's
    // VM-side long long version.
    //
    // #1071: this used to call __builtin_flt_rounds(), which clang
    // implements but GCC 13 does not ("implicit declaration of function
    // '__builtin_flt_rounds'", an undefined-symbol link error) -- not "the
    // portable clang/gcc intrinsic" it was previously documented as.
    // Replaced with the exact fegetround()-based mapping
    // src/stdlib/fenv.c's own __cccc_flt_rounds() already uses on the VM
    // side, so both paths agree by construction. The #include <fenv.h>
    // here follows the __cccc_iseqsig_{f,d} precedent just above (legal
    // mid-file, harmless if repeated thanks to the header's own include
    // guard) -- confirmed it resolves to the real host <fenv.h> under real
    // GCC too (angle-bracket #include from this synthetic shim body, found
    // at -I position 0, so include/fenv.h's own #include_next hand-off
    // resumes the search at position 1 and reaches /usr/include/fenv.h;
    // this is a different shape from #1070's still-open gap, which is a
    // *quoted* #include issued from another CCCC-owned header). The
    // switch is over the *host's* FE_* (host compiler, host header); the
    // returned 0/1/2/3/-1 are CCCC's own fixed encoding, spelled as bare
    // literals rather than any host macro name, since guest comparisons
    // against FLT_ROUNDS were already folded against that encoding at
    // guest compile time -- same asymmetry the __cccc_fpclassify_* shims
    // above already document for FP_*.
    {"__cccc_flt_rounds", "#include <fenv.h>\n"
                          "static int __cccc_flt_rounds(void) {\n"
                          "    switch (fegetround()) {\n"
                          "    case FE_TOWARDZERO: return 0;\n"
                          "    case FE_TONEAREST:  return 1;\n"
                          "    case FE_UPWARD:     return 2;\n"
                          "    case FE_DOWNWARD:   return 3;\n"
                          "    default:            return -1;\n"
                          "    }\n"
                          "}\n"},
    // #1052: issignaling(x)/iseqsig(x,y) (include/math.h:530-541) are
    // CCCC-internal _Generic-dispatched macros with no real libc/libm
    // symbol behind them -- same shape as isnan/isinf/etc above, needing a
    // synthesized definition here too. The bit-pattern logic mirrors
    // cccc_issignaling_{f,d}/cccc_iseqsig_{f,d} (src/stdlib/math.c) exactly:
    // a signaling NaN is identified by its raw bit pattern, not via
    // isnan()/arithmetic, either of which would quiet it before it could be
    // observed. iseqsig's own shim inlines that same bit-pattern check
    // rather than calling __cccc_issignaling_{f,d} -- a program can use
    // iseqsig() without ever calling issignaling() directly, in which case
    // this loop (keyed off is_used) would never emit that other shim's own
    // definition, leaving an undefined reference to a name math.h only
    // declares, not defines. feraiseexcept()/FE_INVALID need <fenv.h>,
    // which -- unlike stdin/stdout/errno/optarg's already-guaranteed
    // headers above -- iseqsig()'s own call site has no guarantee already
    // reached; #include it directly in the shim text (legal mid-file,
    // harmless if repeated thanks to the header's own include guard).
    {"__cccc_issignaling_f",
     "static int __cccc_issignaling_f(float x) {\n"
     "    union { float f; unsigned int u; } __v; __v.f = x;\n"
     "    unsigned int u = __v.u;\n"
     "    return ((u & 0x7F800000U) == 0x7F800000U) && (u & 0x003FFFFFU) != 0 "
     "&& !(u & 0x00400000U);\n"
     "}\n"},
    {"__cccc_issignaling_d",
     "static int __cccc_issignaling_d(double x) {\n"
     "    union { double d; unsigned long long u; } __v; __v.d = x;\n"
     "    unsigned long long u = __v.u;\n"
     "    return ((u & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL) && (u "
     "& 0x0007FFFFFFFFFFFFULL) != 0 && !(u & 0x0008000000000000ULL);\n"
     "}\n"},
    {"__cccc_iseqsig_f", "#include <fenv.h>\n"
                         "static int __cccc_iseqsig_f(float x, float y) {\n"
                         "    union { float f; unsigned int u; } __vx, __vy; "
                         "__vx.f = x; __vy.f = y;\n"
                         "    unsigned int ux = __vx.u, uy = __vy.u;\n"
                         "    int sx = ((ux & 0x7F800000U) == 0x7F800000U) && "
                         "(ux & 0x003FFFFFU) != 0 && !(ux & 0x00400000U);\n"
                         "    int sy = ((uy & 0x7F800000U) == 0x7F800000U) && "
                         "(uy & 0x003FFFFFU) != 0 && !(uy & 0x00400000U);\n"
                         "    if (sx || sy) feraiseexcept(FE_INVALID);\n"
                         "    return x == y;\n"
                         "}\n"},
    {"__cccc_iseqsig_d",
     "#include <fenv.h>\n"
     "static int __cccc_iseqsig_d(double x, double y) {\n"
     "    union { double d; unsigned long long u; } __vx, __vy; __vx.d = x; "
     "__vy.d = y;\n"
     "    unsigned long long ux = __vx.u, uy = __vy.u;\n"
     "    int sx = ((ux & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL) && "
     "(ux & 0x0007FFFFFFFFFFFFULL) != 0 && !(ux & 0x0008000000000000ULL);\n"
     "    int sy = ((uy & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL) && "
     "(uy & 0x0007FFFFFFFFFFFFULL) != 0 && !(uy & 0x0008000000000000ULL);\n"
     "    if (sx || sy) feraiseexcept(FE_INVALID);\n"
     "    return x == y;\n"
     "}\n"},
    // #1069: include/stdlib.h defines MB_CUR_MAX itself as a call to this
    // shim (`#define MB_CUR_MAX (__cccc_mb_cur_max())`) -- same
    // infinite-recursion trap as FLT_ROUNDS/isnan/etc above, since a
    // replayed `#include <stdlib.h>` is what brings that macro into scope
    // in the first place. Unlike those, this shim does NOT resolve the
    // trap by re-#include-ing <stdlib.h> a second time: a first attempt at
    // exactly that (giving stdlib.h its own #include_next hand-off, same
    // shape as stdio.h/errno.h/fenv.h/math.h) chased the real host's own
    // <stdlib.h> chain deep enough to hit an unrelated, pre-existing class
    // of the SAME -I./include shadowing hazard #1054 first documented for
    // setjmp.h -- e.g. real macOS's own <_stdlib.h> pulls in <sys/time.h>,
    // and CCCC's own bundled (non-hand-off) copy of THAT header
    // unconditionally #includes CCCC's own top-level time.h, defining a
    // `clock_t` that later collides with the real one once sys/types.h's
    // own chain reaches it too ("typedef redefinition"). That hand-off
    // has no clean stopping point (fixing it would mean auditing every
    // header transitively reachable from <stdlib.h> on every supported
    // host), so instead this shim spells the host's own internal
    // accessor directly, verified against the real headers on both hosts:
    // glibc declares `extern size_t __ctype_get_mb_cur_max(void);`
    // (/usr/include/stdlib.h, MB_CUR_MAX's own macro expansion); macOS
    // declares `extern int __mb_cur_max;`, a plain global
    // (/usr/include/_stdlib.h). src/stdlib/stdlib.c's wrap_mb_cur_max
    // (the VM-side shim) instead just reads the VM's own host libc's
    // MB_CUR_MAX macro directly -- no -I./include shadowing exists there,
    // since it's compiled by the real host cc as part of CCCC itself, not
    // reached through this serializer's own replay machinery.
    {"__cccc_mb_cur_max",
#if defined(__linux__)
     "extern size_t __ctype_get_mb_cur_max(void);\n"
     "static size_t __cccc_mb_cur_max(void) { return __ctype_get_mb_cur_max(); "
     "}\n"
#else
     "extern int __mb_cur_max;\n"
     "static size_t __cccc_mb_cur_max(void) { return (size_t)__mb_cur_max; }\n"
#endif
    },
    // #1139: include/unistd.h defines `environ` itself as
    // `#define environ (*__cccc_environ_ptr())`, so a guest read/write of
    // `environ` reaches the output as a call to this otherwise-undeclared
    // accessor, same trap as every other entry in this table. The leading
    // `#undef environ` is load-bearing, not decorative: if CCCC's own
    // unistd.h is in scope when this shim text is compiled (the
    // -I./include replay-forwarding case), `extern char **environ;`
    // would otherwise itself expand through that macro into
    // `extern char **(*__cccc_environ_ptr());` -- nonsense syntax, the
    // same infinite-recursion-shaped trap FLT_ROUNDS/isnan/MB_CUR_MAX
    // above each need their own workaround for. Plain
    // `extern char **environ;` is the correct declaration for a Darwin
    // *executable* (which is exactly what -c=native emits) -- the
    // `_NSGetEnviron()` indirection is only required inside a dylib.
    {"__cccc_environ_ptr",
     "#undef environ\n"
     "extern char **environ;\n"
     "static char ***__cccc_environ_ptr(void) { return &environ; }\n"},
};

void serialize_native_accessor_shims(FILE *f, Obj *prog) {
    bool any = false;
    for (size_t i = 0;
         i < sizeof(native_accessor_shims) / sizeof(native_accessor_shims[0]);
         i++) {
        for (Obj *obj = prog; obj; obj = obj->next) {
            if (!obj->is_function || !obj->is_used)
                continue;
            if (strcmp(obj->name, native_accessor_shims[i].name) != 0)
                continue;
            fprintf(f, "%s", native_accessor_shims[i].def);
            any = true;
            break;
        }
    }
    if (any)
        fprintf(f, "\n");
}

// #1088: real definitions for the C11 <threads.h> family (thrd_*/mtx_*/
// cnd_*/tss_*/call_once) under -c=native. <threads.h> is on
// is_cccc_supplied_only_header() (preprocess.c) -- its own #include is
// suppressed and its types (mtx_t/cnd_t/thrd_t/tss_t/etc) are re-derived like
// any other cccc-only header, but until now no *definition* of any of these
// functions existed anywhere reachable from the generated TU: they're VM
// cfuncs (src/stdlib/pthread.c), and a native binary has no VM to call into
// -- every use failed at the host linker with an undefined symbol.
//
// Deliberately a self-contained shim written over the real host <pthread.h>
// (already replayed via the #1022-widened auto-capture gate,
// preprocess.c:5304), NOT a #include_next hand-off onto a real host
// <threads.h> the way include/pthread.h itself hands off -- two reasons,
// both load-bearing (user sign-off): (1) CCCC's own thrd_error/thrd_timedout/
// thrd_busy/thrd_nomem encoding (include/threads.h) does not match glibc's,
// and those values are folded to bare integer literals at guest compile
// time, so any comparison other than `!= thrd_success` would silently change
// meaning once glibc's own enum reached the output -- the same FP_*/isnan
// asymmetry native_accessor_shims's own comment documents above; (2) Darwin
// has no <threads.h> at all, so a hand-off would leave macOS permanently
// unsupported. A self-contained shim closes both platforms in one change,
// consulting the host's own <threads.h> on neither.
//
// Each function below is a near-verbatim port of its VM cfunc counterpart in
// src/stdlib/pthread.c (named in each comment), minus the GIL save/release
// dance and the --thread-safety lock-order bookkeeping -- both meaningless
// without a VM -- but NOT a verbatim port of the VM's lazy mtx_t/cnd_t
// handle allocation: ensure_mtx/ensure_cond (pthread.c:991/463) are
// check-then-malloc-then-store, safe only because the GIL serializes every
// VM cfunc call. Two real threads racing that check under -c=native's actual
// parallelism could each allocate a host mutex and store its own, silently
// locking two different mutexes -- a wrong answer, not a crash, and the
// wrong side of this batch's own "works on the VM -> correct natively" bar.
// __cccc_ensure_mtx/__cccc_ensure_cnd below use a real atomic
// compare-exchange on the ->__handle field instead, so exactly one raced
// allocation wins and every other caller adopts it.
// #1141 generalized this from a threads.h-only helper (originally
// threads_shim_fn_is_used) to also serve serialize_uchar_shims below --
// same "declared-only, cccc-only-header-sourced, actually used" test,
// just parameterized on which header's declaration it must trace back to.
static bool shim_fn_is_used(VirtualMachine *vm, Obj *prog, const char *name,
                            const char *header_basename) {
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (!obj->is_function || !obj->is_used || obj->body)
            continue;
        if (strcmp(obj->name, name) != 0)
            continue;
        Token *t = obj->tok;
        if (!t || !t->file)
            continue;
        if (!cc_file_is_cccc_only(vm, t->file->name))
            continue;
        if (!path_basename_is(t->file->name, header_basename))
            continue;
        return true;
    }
    return false;
}

// #1145: sibling to bundled_shim_fn_is_used() below for a *tag* (struct/
// union) rather than a function -- gates emission of the
// struct-in6_pktinfo shim below on whether the guest program actually
// parsed the tag from CCCC's bundled `header_basename` at all (so the
// shim never references a type from a header the replayed #include never
// pulled in). Deliberately does not require is_used: an unused struct
// tag record still proves the header was captured, which is the only
// question this gate needs answered.
static bool bundled_tag_is_declared(VirtualMachine *vm, const char *tag_name,
                                    const char *header_basename) {
    size_t tag_name_len = strlen(tag_name);
    for (TypeNameRecord *rec = vm->compiler.type_names; rec; rec = rec->next) {
        if (!rec->is_tag || !rec->from_include || !rec->file_path)
            continue;
        // rec->name is a raw (name_len, loc) slice into the source buffer,
        // not a null-terminated string (record_type_name(), src/
        // parse_core.c) -- strcmp() here would read past it into whatever
        // follows in that buffer.
        if (!rec->name || (size_t)rec->name_len != tag_name_len ||
            strncmp(rec->name, tag_name, tag_name_len) != 0)
            continue;
        if (!cc_file_is_cccc_bundled(vm, rec->file_path))
            continue;
        if (!path_basename_is(rec->file_path, header_basename))
            continue;
        return true;
    }
    return false;
}

// #1140: sibling to shim_fn_is_used above for headers that are real host
// headers CCCC merely bundles a copy of (poll.h/sched.h/netdb.h), not
// cccc-only ones -- cc_file_is_cccc_only() is false for those (the host
// genuinely has the file), so the declaration's provenance is checked via
// cc_file_is_cccc_bundled() instead, the same "which of CCCC's headers did
// this declaration come from" question serialize_posix_compat_shims' own
// caller (the bodiless-prototype gate) already asks.
static bool bundled_shim_fn_is_used(VirtualMachine *vm, Obj *prog,
                                    const char *name,
                                    const char *header_basename) {
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (!obj->is_function || !obj->is_used || obj->body)
            continue;
        if (strcmp(obj->name, name) != 0)
            continue;
        Token *t = obj->tok;
        if (!t || !t->file)
            continue;
        if (!cc_file_is_cccc_bundled(vm, t->file->name))
            continue;
        if (!path_basename_is(t->file->name, header_basename))
            continue;
        return true;
    }
    return false;
}

// #1146: sibling to bundled_shim_fn_is_used() above with a rename
// side-effect -- finds the guest program's bodiless declaration of `name`
// from CCCC's bundled `header_basename` (same provenance check) and renames
// every reference to `__cccc_native_<name>` so a translating shim (emitted
// under that new name, immediately below) can supply the definition without
// colliding with the real host-declared symbol of the same name. Renaming
// an Obj safely renames every call site through it -- every reference
// resolves through the same Obj*, exactly the invariant
// rename_colliding_static_names() (this file) relies on; unlike that pass's
// own #1103 hazard (renaming an Obj whose *definition* is only ever
// supplied by a replayed #include), this is the safe direction: the
// renamed Obj's definition is supplied right here, and the real host
// symbol under the original name is left completely untouched for the
// shim body to call.
static bool rename_bundled_extern_for_native_shim(VirtualMachine *vm, Obj *prog,
                                                  const char *name,
                                                  const char *header_basename) {
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (!obj->is_function || !obj->is_used || obj->body)
            continue;
        if (strcmp(obj->name, name) != 0)
            continue;
        Token *t = obj->tok;
        if (!t || !t->file)
            continue;
        if (!cc_file_is_cccc_bundled(vm, t->file->name))
            continue;
        if (!path_basename_is(t->file->name, header_basename))
            continue;
        obj->name = arena_format(vm, "__cccc_native_%s", name);
        return true;
    }
    return false;
}

void serialize_threads_shims(FILE *f, VirtualMachine *vm, Obj *prog) {
    // #1088's own --emit-cccc exemption: under --emit-cccc the cccc-only
    // suppression above is exempted (see the include-replay loop's own
    // gate, cc_serialize_program) so `#include <threads.h>` IS replayed,
    // reaching a consumer cccc that already has the real cfuncs registered
    // -- emitting definitions here would shadow them with a second,
    // divergent implementation. Same gating as serialize_synth_setjmp_decls.
    if (vm->compiler.emit_cccc)
        return;

    bool use_thrd_create =
        shim_fn_is_used(vm, prog, "thrd_create", "threads.h");
    bool use_thrd_join = shim_fn_is_used(vm, prog, "thrd_join", "threads.h");
    bool use_thrd_exit = shim_fn_is_used(vm, prog, "thrd_exit", "threads.h");
    bool use_thrd_detach =
        shim_fn_is_used(vm, prog, "thrd_detach", "threads.h");
    bool use_thrd_yield = shim_fn_is_used(vm, prog, "thrd_yield", "threads.h");
    bool use_thrd_sleep = shim_fn_is_used(vm, prog, "thrd_sleep", "threads.h");
    bool use_thrd_current =
        shim_fn_is_used(vm, prog, "thrd_current", "threads.h");
    bool use_thrd_equal = shim_fn_is_used(vm, prog, "thrd_equal", "threads.h");
    bool any_thrd       = use_thrd_create || use_thrd_join || use_thrd_exit ||
                          use_thrd_detach || use_thrd_yield || use_thrd_sleep ||
                          use_thrd_current || use_thrd_equal;

    bool use_mtx_init   = shim_fn_is_used(vm, prog, "mtx_init", "threads.h");
    bool use_mtx_lock   = shim_fn_is_used(vm, prog, "mtx_lock", "threads.h");
    bool use_mtx_trylock =
        shim_fn_is_used(vm, prog, "mtx_trylock", "threads.h");
    bool use_mtx_timedlock =
        shim_fn_is_used(vm, prog, "mtx_timedlock", "threads.h");
    bool use_mtx_unlock = shim_fn_is_used(vm, prog, "mtx_unlock", "threads.h");
    bool use_mtx_destroy =
        shim_fn_is_used(vm, prog, "mtx_destroy", "threads.h");
    bool any_mtx      = use_mtx_init || use_mtx_lock || use_mtx_trylock ||
                        use_mtx_timedlock || use_mtx_unlock || use_mtx_destroy;

    bool use_cnd_init = shim_fn_is_used(vm, prog, "cnd_init", "threads.h");
    bool use_cnd_wait = shim_fn_is_used(vm, prog, "cnd_wait", "threads.h");
    bool use_cnd_signal = shim_fn_is_used(vm, prog, "cnd_signal", "threads.h");
    bool use_cnd_broadcast =
        shim_fn_is_used(vm, prog, "cnd_broadcast", "threads.h");
    bool use_cnd_timedwait =
        shim_fn_is_used(vm, prog, "cnd_timedwait", "threads.h");
    bool use_cnd_destroy =
        shim_fn_is_used(vm, prog, "cnd_destroy", "threads.h");
    bool any_cnd = use_cnd_init || use_cnd_wait || use_cnd_signal ||
                   use_cnd_broadcast || use_cnd_timedwait || use_cnd_destroy;

    bool use_tss_create = shim_fn_is_used(vm, prog, "tss_create", "threads.h");
    bool use_tss_get    = shim_fn_is_used(vm, prog, "tss_get", "threads.h");
    bool use_tss_set    = shim_fn_is_used(vm, prog, "tss_set", "threads.h");
    bool use_tss_delete = shim_fn_is_used(vm, prog, "tss_delete", "threads.h");
    bool any_tss =
        use_tss_create || use_tss_get || use_tss_set || use_tss_delete;

    bool use_call_once = shim_fn_is_used(vm, prog, "call_once", "threads.h");

    if (!any_thrd && !any_mtx && !any_cnd && !any_tss && !use_call_once)
        return;

    // Self-contained #includes rather than trusting the nested-include
    // capture, following the __cccc_iseqsig_* precedent above -- harmless if
    // repeated thanks to each header's own include guard.
    fprintf(f, "#include <pthread.h>\n"
               "#include <time.h>\n"
               "#include <errno.h>\n"
               "#include <stdlib.h>\n");
    // #1054-class hazard: CCCC's own bundled include/sched.h and
    // include/string.h have no #include_next hand-off, so a plain
    // `#include` of either here (under the same -I./include forwarding
    // every other replayed header sees) would re-pull CCCC's own
    // polyfill copies, colliding with the real ones already reached via
    // <pthread.h>'s own hand-off (struct sched_param redefinition,
    // confirmed). sched_yield() is declared directly instead
    // (POSIX-portable, no header needed); memcpy is replaced by the
    // portable __builtin_memcpy below, avoiding <string.h> entirely.
    // call_once's spin-wait (below) also needs sched_yield -- see its own
    // comment for why.
    if (use_thrd_yield || use_call_once)
        fprintf(f, "extern int sched_yield(void);\n");
    // <stdatomic.h> is NOT usable here for the same reason <sched.h>/
    // <string.h> aren't above, but for a stricter cause: it's on
    // is_compiler_owned_header() (preprocess.c), so force_cccc makes
    // search_include_paths() resolve a plain #include to CCCC's own
    // macro-based polyfill unconditionally -- even under
    // --use-system-headers -- rather than ever reaching the real host
    // <stdatomic.h>. CCCC's own copy expands atomic_compare_exchange_strong
    // to __builtin_compare_and_swap, a CCCC-internal builtin absent on a
    // real clang/gcc ("use of undeclared identifier", confirmed). The
    // call_once shim below uses the plain __atomic_compare_exchange_n
    // builtin on a pointer-to-plain-int instead (like __cccc_ensure_mtx's
    // ->__handle CAS above), reached by casting away once_flag's own
    // _Atomic qualifier -- the same reason that cast is needed here as for
    // the VM-side wrap_call_once (src/stdlib/pthread.c): passing a pointer
    // to an _Atomic-qualified type straight to the GCC/clang __atomic_*
    // builtins is rejected outright ("address argument to atomic operation
    // must be a pointer to integer or pointer"), since the compiler treats
    // that argument shape as a request for the C11 stdatomic API instead.

    if (any_thrd) {
        // thrd_t is re-derived as CCCC's own `pthread_t` polyfill
        // (include/pthread.h: `typedef void *pthread_t;`), i.e. a plain
        // void*, while the real host pthread_t is `unsigned long` on glibc
        // and an opaque pointer on Darwin -- both exactly pointer-sized, but
        // not the same *type*, so a plain cast is not portable. The shims
        // below round-trip through __builtin_memcpy instead of a cast
        // (avoiding a <string.h> dependency -- see the sched_yield comment
        // below for why that header can't just be #include-d here); the
        // _Static_assert makes the sizing assumption checked rather than
        // silently assumed.
        fprintf(f,
                "_Static_assert(sizeof(pthread_t) <= sizeof(void *),\n"
                "               \"cccc: host pthread_t must fit in a "
                "pointer-sized thrd_t\");\n"
                "struct __cccc_thrd_args { int (*fn)(void *); void *arg; };\n"
                "static void *__cccc_thrd_trampoline(void *argp) {\n"
                "    struct __cccc_thrd_args *a = "
                "(struct __cccc_thrd_args *)argp;\n"
                "    int rc = a->fn(a->arg);\n"
                "    free(a);\n"
                "    return (void *)(long)rc;\n"
                "}\n");
    }

    if (any_mtx || any_cnd) {
        // Port of ensure_mtx (src/stdlib/pthread.c:991-1014), with the
        // lazy-allocation race closed by an atomic compare-exchange on
        // ->__handle instead of the VM's GIL-only check-then-store (see
        // this function's own comment above). mtx_recursive (1, CCCC's own
        // enum, include/threads.h) is remapped to the real host
        // PTHREAD_MUTEX_RECURSIVE -- forwarding ->__type straight through
        // would be wrong, since CCCC's C11 enum and the host's pthread
        // mutex-type constants don't share a numbering.
        fprintf(f, "static pthread_mutex_t *__cccc_ensure_mtx(mtx_t *mtx) {\n"
                   "    if (!mtx) return NULL;\n"
                   "    void *h = __atomic_load_n(&mtx->__handle, "
                   "__ATOMIC_ACQUIRE);\n"
                   "    if (h) return (pthread_mutex_t *)h;\n"
                   "    pthread_mutex_t *host = malloc(sizeof(*host));\n"
                   "    if (!host) return NULL;\n"
                   "    pthread_mutexattr_t attr;\n"
                   "    pthread_mutexattr_init(&attr);\n"
                   "    if (mtx->__type == 1)\n"
                   "        pthread_mutexattr_settype(&attr, "
                   "PTHREAD_MUTEX_RECURSIVE);\n"
                   "    if (pthread_mutex_init(host, &attr) != 0) {\n"
                   "        pthread_mutexattr_destroy(&attr);\n"
                   "        free(host);\n"
                   "        return NULL;\n"
                   "    }\n"
                   "    pthread_mutexattr_destroy(&attr);\n"
                   "    void *expected = NULL;\n"
                   "    if (!__atomic_compare_exchange_n(&mtx->__handle, "
                   "&expected, host, 0,\n"
                   "                                      __ATOMIC_ACQ_REL, "
                   "__ATOMIC_ACQUIRE)) {\n"
                   "        pthread_mutex_destroy(host);\n"
                   "        free(host);\n"
                   "        return (pthread_mutex_t *)expected;\n"
                   "    }\n"
                   "    mtx->__state = 1;\n"
                   "    return host;\n"
                   "}\n");
    }
    if (any_cnd) {
        // Port of ensure_cond (src/stdlib/pthread.c:463-478); same
        // atomic-compare-exchange race closure as __cccc_ensure_mtx above.
        fprintf(f, "static pthread_cond_t *__cccc_ensure_cnd(cnd_t *cond) {\n"
                   "    if (!cond) return NULL;\n"
                   "    void *h = __atomic_load_n(&cond->__handle, "
                   "__ATOMIC_ACQUIRE);\n"
                   "    if (h) return (pthread_cond_t *)h;\n"
                   "    pthread_cond_t *host = malloc(sizeof(*host));\n"
                   "    if (!host) return NULL;\n"
                   "    if (pthread_cond_init(host, NULL) != 0) {\n"
                   "        free(host);\n"
                   "        return NULL;\n"
                   "    }\n"
                   "    void *expected = NULL;\n"
                   "    if (!__atomic_compare_exchange_n(&cond->__handle, "
                   "&expected, host, 0,\n"
                   "                                      __ATOMIC_ACQ_REL, "
                   "__ATOMIC_ACQUIRE)) {\n"
                   "        pthread_cond_destroy(host);\n"
                   "        free(host);\n"
                   "        return (pthread_cond_t *)expected;\n"
                   "    }\n"
                   "    cond->__state = 1;\n"
                   "    return host;\n"
                   "}\n");
    }

    // ---- Thread lifecycle (port of pthread.c:923-981) ----
    if (use_thrd_create)
        fprintf(f,
                "int thrd_create(thrd_t *thr, thrd_start_t func, void *arg) {\n"
                "    struct __cccc_thrd_args *a = malloc(sizeof(*a));\n"
                "    if (!a) return ENOMEM;\n"
                "    a->fn = func;\n"
                "    a->arg = arg;\n"
                "    pthread_t host;\n"
                "    int rc = pthread_create(&host, NULL, "
                "__cccc_thrd_trampoline, a);\n"
                "    if (rc != 0) {\n"
                "        free(a);\n"
                "        return rc == ENOMEM ? ENOMEM : 1;\n"
                "    }\n"
                "    thrd_t out = 0;\n"
                "    __builtin_memcpy(&out, &host, sizeof(host));\n"
                "    *thr = out;\n"
                "    return 0;\n"
                "}\n");
    if (use_thrd_join)
        fprintf(f, "int thrd_join(thrd_t thr, int *res) {\n"
                   "    pthread_t host;\n"
                   "    __builtin_memcpy(&host, &thr, sizeof(host));\n"
                   "    void *retval = NULL;\n"
                   "    if (pthread_join(host, &retval) != 0) return 1;\n"
                   "    if (res) *res = (int)(long)retval;\n"
                   "    return 0;\n"
                   "}\n");
    if (use_thrd_exit)
        // Matches thrd_create's trampoline encoding: returning `rc` from the
        // thread function is equivalent (POSIX) to pthread_exit() with that
        // same value, so thrd_join's narrowing agrees regardless of which
        // path a thread actually exits through.
        fprintf(f, "_Noreturn void thrd_exit(int res) {\n"
                   "    pthread_exit((void *)(long)res);\n"
                   "}\n");
    if (use_thrd_detach)
        fprintf(f, "int thrd_detach(thrd_t thr) {\n"
                   "    pthread_t host;\n"
                   "    __builtin_memcpy(&host, &thr, sizeof(host));\n"
                   "    return pthread_detach(host) == 0 ? 0 : "
                   "1;\n"
                   "}\n");
    if (use_thrd_yield)
        fprintf(f, "void thrd_yield(void) { sched_yield(); }\n");
    if (use_thrd_sleep)
        fprintf(f, "int thrd_sleep(const struct timespec *duration, struct "
                   "timespec *remaining) {\n"
                   "    if (!duration) return -2;\n"
                   "    int rc = nanosleep(duration, remaining);\n"
                   "    if (rc == 0) return 0;\n"
                   "    return errno == EINTR ? -1 : -2;\n"
                   "}\n");
    if (use_thrd_current)
        fprintf(f, "thrd_t thrd_current(void) {\n"
                   "    pthread_t self = pthread_self();\n"
                   "    thrd_t out = 0;\n"
                   "    __builtin_memcpy(&out, &self, sizeof(self));\n"
                   "    return out;\n"
                   "}\n");
    if (use_thrd_equal)
        fprintf(f, "int thrd_equal(thrd_t a, thrd_t b) {\n"
                   "    pthread_t pa, pb;\n"
                   "    __builtin_memcpy(&pa, &a, sizeof(pa));\n"
                   "    __builtin_memcpy(&pb, &b, sizeof(pb));\n"
                   "    return pthread_equal(pa, pb) != 0;\n"
                   "}\n");

    // ---- Mutex (port of pthread.c:1016-1130) ----
    if (use_mtx_init)
        fprintf(f,
                "int mtx_init(mtx_t *mtx, int type) {\n"
                "    if (!mtx) return 1;\n"
                "    if (__atomic_load_n(&mtx->__handle, __ATOMIC_ACQUIRE))\n"
                "        return 1;\n"
                "    mtx->__type = type;\n"
                "    return __cccc_ensure_mtx(mtx) ? 0 : "
                "1;\n"
                "}\n");
    if (use_mtx_lock)
        fprintf(f, "int mtx_lock(mtx_t *mtx) {\n"
                   "    pthread_mutex_t *host = __cccc_ensure_mtx(mtx);\n"
                   "    if (!host) return 1;\n"
                   "    return pthread_mutex_lock(host) == 0 ? 0 : "
                   "1;\n"
                   "}\n");
    if (use_mtx_trylock)
        fprintf(f, "int mtx_trylock(mtx_t *mtx) {\n"
                   "    pthread_mutex_t *host = __cccc_ensure_mtx(mtx);\n"
                   "    if (!host) return 1;\n"
                   "    int rc = pthread_mutex_trylock(host);\n"
                   "    if (rc == 0) return 0;\n"
                   "    return rc == EBUSY ? EBUSY : 1;\n"
                   "}\n");
    if (use_mtx_timedlock)
        // #824 note: this is not new lossy emulation -- it is byte-for-byte
        // the same __linux__ / trylock-poll split the VM's own
        // wrap_mtx_timedlock already ships (pthread.c:1067-1105), matching
        // existing CCCC behaviour rather than inventing a new one. macOS has
        // no pthread_mutex_timedlock at all.
        //
        // The macOS branch's own clock_gettime(CLOCK_REALTIME, ...) used to
        // declare its own local `extern int clock_gettime(int, struct
        // timespec *);` rather than reach a real declaration via #include:
        // <time.h> is NOT on this function's own #include list above (nor
        // was it usable if it were -- same #1054-class hazard as <sched.h>/
        // <string.h>, documented in man/HEADERS.md's own pthread_native_1022
        // writeup as the reason CCCC never gave <time.h> itself a full
        // #include_next hand-off: the cascade has no clean stopping point).
        // That local extern was itself wrong on macOS (clockid_t is a real
        // enum type there, not plain int, so it disagreed with the
        // system's own declaration) -- invisible pre-#1143 only because a
        // user `-I./include` shadowed the real host <pthread.h> chain (and
        // everything it transitively reaches, including <time.h>) outright.
        // #1143 demotes CCCC's own bundled include dirs to `-idirafter`, so
        // pthread.h's real #include_next hand-off (#1022) now reliably
        // reaches the host's own clock_gettime declaration on macOS through
        // the same <pthread.h> chain this function already requires --
        // confirmed directly (`cc -idirafter ./include` resolves it with no
        // separate #include needed) -- so the local extern is dropped
        // rather than fixed to match the host's own clockid_t spelling,
        // which is Darwin-specific and would need its own translation.
        // CLOCK_REALTIME is still spelled as its own literal value (0 on
        // both glibc and Darwin, confirmed) rather than the macro name, the
        // same "spell CCCC's own fixed values as literals" precedent
        // native_accessor_shims's own FP_*/fpclassify comment documents --
        // this branch never runs on Linux, so only Darwin's value matters.
        fprintf(f,
                "int mtx_timedlock(mtx_t *mtx, const struct timespec *ts) {\n"
                "    pthread_mutex_t *host = __cccc_ensure_mtx(mtx);\n"
                "    if (!host || !ts) return 1;\n"
                "    int rc;\n"
                "#if defined(__linux__)\n"
                "    rc = pthread_mutex_timedlock(host, ts);\n"
                "#else\n"
                "    for (;;) {\n"
                "        rc = pthread_mutex_trylock(host);\n"
                "        if (rc == 0) break;\n"
                "        struct timespec now;\n"
                "        clock_gettime(0 /* CLOCK_REALTIME */, &now);\n"
                "        if (now.tv_sec > ts->tv_sec ||\n"
                "            (now.tv_sec == ts->tv_sec && now.tv_nsec >= "
                "ts->tv_nsec)) {\n"
                "            rc = ETIMEDOUT;\n"
                "            break;\n"
                "        }\n"
                "        struct timespec delay = {0, 1000000};\n"
                "        nanosleep(&delay, NULL);\n"
                "    }\n"
                "#endif\n"
                "    if (rc == 0) return 0;\n"
                "    return rc == ETIMEDOUT ? ETIMEDOUT : 1;\n"
                "}\n");
    if (use_mtx_unlock)
        fprintf(f, "int mtx_unlock(mtx_t *mtx) {\n"
                   "    if (!mtx || !mtx->__handle) return 1;\n"
                   "    return pthread_mutex_unlock((pthread_mutex_t "
                   "*)mtx->__handle) == 0 ? 0 : 1;\n"
                   "}\n");
    if (use_mtx_destroy)
        fprintf(f, "void mtx_destroy(mtx_t *mtx) {\n"
                   "    if (!mtx || !mtx->__handle) return;\n"
                   "    pthread_mutex_destroy((pthread_mutex_t "
                   "*)mtx->__handle);\n"
                   "    free(mtx->__handle);\n"
                   "    mtx->__handle = NULL;\n"
                   "    mtx->__state = 0;\n"
                   "}\n");

    // ---- Condition variable (port of pthread.c:1132-1178) ----
    if (use_cnd_init)
        fprintf(f, "int cnd_init(cnd_t *cond) {\n"
                   "    if (!cond) return 1;\n"
                   "    return __cccc_ensure_cnd(cond) ? 0 : "
                   "1;\n"
                   "}\n");
    if (use_cnd_wait)
        fprintf(f, "int cnd_wait(cnd_t *cond, mtx_t *mtx) {\n"
                   "    pthread_cond_t *c = __cccc_ensure_cnd(cond);\n"
                   "    pthread_mutex_t *m = __cccc_ensure_mtx(mtx);\n"
                   "    if (!c || !m) return 1;\n"
                   "    return pthread_cond_wait(c, m) == 0 ? 0 : "
                   "1;\n"
                   "}\n");
    if (use_cnd_signal)
        fprintf(f, "int cnd_signal(cnd_t *cond) {\n"
                   "    pthread_cond_t *c = __cccc_ensure_cnd(cond);\n"
                   "    if (!c) return 1;\n"
                   "    return pthread_cond_signal(c) == 0 ? 0 : "
                   "1;\n"
                   "}\n");
    if (use_cnd_broadcast)
        fprintf(f, "int cnd_broadcast(cnd_t *cond) {\n"
                   "    pthread_cond_t *c = __cccc_ensure_cnd(cond);\n"
                   "    if (!c) return 1;\n"
                   "    return pthread_cond_broadcast(c) == 0 ? 0 : "
                   "1;\n"
                   "}\n");
    if (use_cnd_timedwait)
        fprintf(f, "int cnd_timedwait(cnd_t *cond, mtx_t *mtx, const struct "
                   "timespec *ts) {\n"
                   "    pthread_cond_t *c = __cccc_ensure_cnd(cond);\n"
                   "    pthread_mutex_t *m = __cccc_ensure_mtx(mtx);\n"
                   "    if (!c || !m || !ts) return 1;\n"
                   "    int rc = pthread_cond_timedwait(c, m, ts);\n"
                   "    if (rc == 0) return 0;\n"
                   "    return rc == ETIMEDOUT ? ETIMEDOUT : 1;\n"
                   "}\n");
    if (use_cnd_destroy)
        fprintf(f, "void cnd_destroy(cnd_t *cond) {\n"
                   "    if (!cond || !cond->__handle) return;\n"
                   "    pthread_cond_destroy((pthread_cond_t "
                   "*)cond->__handle);\n"
                   "    free(cond->__handle);\n"
                   "    cond->__handle = NULL;\n"
                   "    cond->__state = 0;\n"
                   "}\n");

    // ---- Thread-specific storage (port of pthread.c:1180-1195) ----
    // tss_t is re-derived as a plain alias of the host's own pthread_key_t
    // (include/threads.h: `typedef pthread_key_t tss_t;`, and pthread_key_t
    // itself comes from the replayed real <pthread.h>) and tss_dtor_t
    // (`void (*)(void *)`) already matches pthread's own destructor
    // signature exactly -- so these forward straight through, no adapter
    // needed.
    if (use_tss_create)
        fprintf(f, "int tss_create(tss_t *key, tss_dtor_t dtor) {\n"
                   "    return pthread_key_create(key, dtor) == 0 ? "
                   "0 : 1;\n"
                   "}\n");
    if (use_tss_get)
        fprintf(f,
                "void *tss_get(tss_t key) { return pthread_getspecific(key); "
                "}\n");
    if (use_tss_set)
        fprintf(f, "int tss_set(tss_t key, void *val) {\n"
                   "    return pthread_setspecific(key, val) == 0 ? "
                   "0 : 1;\n"
                   "}\n");
    if (use_tss_delete)
        fprintf(f, "void tss_delete(tss_t key) { pthread_key_delete(key); "
                   "}\n");

    // ---- call_once (#1088; see include/threads.h's own comment on why
    // this is a real function now, not a macro) ----
    //
    // Three states, not a plain two-state CAS: 0 (not started) -> 1 (in
    // progress) -> 2 (done). A first attempt used a plain 0->1
    // compare-exchange with no wait for the losing side, mirroring
    // wrap_call_once's own VM-side CAS -- but that's only correct there
    // because the GIL serializes every cfunc call end-to-end: a losing
    // guest thread can't even enter wrap_call_once until the winning
    // thread's own call (guest callback included) has already returned and
    // released the GIL, so the winner's func() is unconditionally done by
    // the time any loser observes the flag. -c=native has no GIL, so a
    // losing thread reaching the two-state version could return, and a
    // caller relying on call_once to have initialized shared state before
    // proceeding (the standard idiom) would race -- caught by stress-
    // running tests/test_threads_call_once_1088.c (occasional non-42 exit
    // out of dozens of runs). The 1 (in-progress) state gives every losing
    // thread something to spin-wait on until the winner stores 2, matching
    // real pthread_once/glibc's own blocking behaviour, which is what
    // C11 programs actually rely on in practice even though 7.26.6.2p2's
    // literal text only promises a happens-before ordering.
    if (use_call_once)
        fprintf(f,
                "void call_once(once_flag *flag, void (*func)(void)) {\n"
                "    int *raw = (int *)flag;\n"
                "    int expected = 0;\n"
                "    if (__atomic_compare_exchange_n(raw, &expected, 1, 0,\n"
                "                                     __ATOMIC_ACQ_REL, "
                "__ATOMIC_ACQUIRE)) {\n"
                "        func();\n"
                "        __atomic_store_n(raw, 2, __ATOMIC_RELEASE);\n"
                "    } else {\n"
                "        while (__atomic_load_n(raw, __ATOMIC_ACQUIRE) != 2)\n"
                "            sched_yield();\n"
                "    }\n"
                "}\n");

    fprintf(f, "\n");
}

// #1141: real definitions for the C11/C23 <uchar.h> multibyte<->UTF-16/32/8
// conversions (mbrtoc16/c16rtomb/mbrtoc32/c32rtomb, mbrtoc8/c8rtomb).
// uchar.h is on is_cccc_supplied_only_header() (preprocess.c) like
// threads.h -- its declarations are re-derived from CCCC's own
// include/uchar.h, but until now no *definition* reached -c=native's
// output for any of the six, since they're VM cfuncs
// (src/stdlib/wide.c) with nothing for a native binary to link against.
//
// Unlike threads.h, glibc's real libc HAS shipped these since 2.16
// (2.36 for the c8 pair) -- #1141's own repro is Darwin-only
// ("Undefined symbols ... _c16rtomb"; confirmed test_suite_strings.c is
// otherwise clean under --testing=native on Linux/glibc). On a host new
// enough, the re-derived extern declaration alone is already sufficient
// for the linker to resolve the real symbol, so the shims below are
// wrapped in the identical __GLIBC_PREREQ feature test src/stdlib/wide.c
// itself uses to choose between the real symbol and its own fallback
// (CCCC_HAVE_NATIVE_UCHAR_CONV / CCCC_HAVE_NATIVE_MBRTOC8) -- a host that
// already has the real symbol must never see a second, competing
// definition here ("duplicate symbol" at link time).
//
// Each fallback below is a near-verbatim port of its VM cfunc counterpart
// in src/stdlib/wide.c (cccc_mbrtoc16/cccc_c16rtomb/cccc_mbrtoc32/
// cccc_c32rtomb/cccc_mbrtoc8/cccc_c8rtomb) -- the two copies have no
// shared source (one is compiled into CCCC itself, the other is emitted
// text compiled by the host cc as part of the guest program) and must be
// kept in sync by hand; folding them into one generated .inc (the
// reflection_ffi_*.inc precedent) is a real follow-up, filed separately
// rather than attempted here.
//
// Like serialize_threads_shims above, deliberately does NOT #include
// <string.h>/<stdint.h> (same #1054-class shadowing hazard as sched.h/
// string.h there) -- __builtin_memcpy/__builtin_memset replace memcpy/
// memset, and internal accumulator fields use plain `unsigned` instead of
// uint32_t. mbrtowc/wcrtomb/mbstate_t/char16_t/char32_t/char8_t/wchar_t
// need no header of their own here: uchar.h's own `#include "wchar.h"` is
// auto-captured from a cccc-only includer (preprocess.c's #1103-era
// widened gate) whenever any of these six functions is used at all, so
// their declarations/typedefs are already visible in the output by the
// time this runs.
void serialize_uchar_shims(FILE *f, VirtualMachine *vm, Obj *prog) {
    if (vm->compiler.emit_cccc)
        return;

    bool use_mbrtoc16 = shim_fn_is_used(vm, prog, "mbrtoc16", "uchar.h");
    bool use_c16rtomb = shim_fn_is_used(vm, prog, "c16rtomb", "uchar.h");
    bool use_mbrtoc32 = shim_fn_is_used(vm, prog, "mbrtoc32", "uchar.h");
    bool use_c32rtomb = shim_fn_is_used(vm, prog, "c32rtomb", "uchar.h");
    bool use_mbrtoc8  = shim_fn_is_used(vm, prog, "mbrtoc8", "uchar.h");
    bool use_c8rtomb  = shim_fn_is_used(vm, prog, "c8rtomb", "uchar.h");
    bool any16_32 =
        use_mbrtoc16 || use_c16rtomb || use_mbrtoc32 || use_c32rtomb;
    bool any8 = use_mbrtoc8 || use_c8rtomb;

    if (!any16_32 && !any8)
        return;

    fprintf(f, "#if defined(__GLIBC__)\n"
               "#include <features.h>\n"
               "#endif\n"
               "#include <errno.h>\n");

    if (any16_32) {
        // Nested, not `&&`-combined: `#if defined(__GLIBC__) &&
        // __GLIBC_PREREQ(2, 16)` looks equivalent but isn't -- the
        // preprocessor macro-expands an ENTIRE #if line before evaluating
        // any of it, `&&` included, so __GLIBC_PREREQ(2, 16) is expanded
        // (and errors, "function-like macro is not defined") on a host
        // with no __GLIBC__ at all, never mind its value. An #elif's
        // condition, by contrast, is only expanded once every earlier
        // branch in the same chain has already been evaluated false --
        // exactly the short-circuit the combined form was trying (and
        // failing) to get. Confirmed the hard way: this exact `&&` form
        // shipped first and broke test_suite_strings.c's own native
        // compile on macOS/clang with precisely that diagnostic.
        fprintf(f, "#if !defined(__GLIBC__)\n"
                   "#define __CCCC_NEED_UCHAR16_32_SHIM 1\n"
                   "#elif !__GLIBC_PREREQ(2, 16)\n"
                   "#define __CCCC_NEED_UCHAR16_32_SHIM 1\n"
                   "#endif\n"
                   "#ifdef __CCCC_NEED_UCHAR16_32_SHIM\n");
        if (use_mbrtoc16)
            fprintf(f, "size_t mbrtoc16(char16_t *pc16, const char *s, size_t "
                       "n, mbstate_t *ps) {\n"
                       "    wchar_t wc;\n"
                       "    size_t rc = mbrtowc(&wc, s, n, ps);\n"
                       "    if (rc == (size_t)-1 || rc == (size_t)-2 || rc == "
                       "0)\n"
                       "        return rc;\n"
                       "    if (pc16) *pc16 = (char16_t)wc;\n"
                       "    return rc;\n"
                       "}\n");
        if (use_c16rtomb)
            fprintf(f, "size_t c16rtomb(char *s, char16_t c16, mbstate_t *ps) "
                       "{\n"
                       "    return wcrtomb(s, (wchar_t)c16, ps);\n"
                       "}\n");
        if (use_mbrtoc32)
            fprintf(f, "size_t mbrtoc32(char32_t *pc32, const char *s, size_t "
                       "n, mbstate_t *ps) {\n"
                       "    wchar_t wc;\n"
                       "    size_t rc = mbrtowc(&wc, s, n, ps);\n"
                       "    if (rc == (size_t)-1 || rc == (size_t)-2 || rc == "
                       "0)\n"
                       "        return rc;\n"
                       "    if (pc32) *pc32 = (char32_t)wc;\n"
                       "    return rc;\n"
                       "}\n");
        if (use_c32rtomb)
            fprintf(f, "size_t c32rtomb(char *s, char32_t c32, mbstate_t *ps) "
                       "{\n"
                       "    return wcrtomb(s, (wchar_t)c32, ps);\n"
                       "}\n");
        fprintf(f, "#endif\n");
    }

    if (any8) {
        // Same nested-#if reasoning as the c16/c32 block above.
        fprintf(f, "#if !defined(__GLIBC__)\n"
                   "#define __CCCC_NEED_UCHAR8_SHIM 1\n"
                   "#elif !__GLIBC_PREREQ(2, 36)\n"
                   "#define __CCCC_NEED_UCHAR8_SHIM 1\n"
                   "#endif\n"
                   "#ifdef __CCCC_NEED_UCHAR8_SHIM\n");
        fprintf(f,
                "typedef struct { unsigned char magic; unsigned char buf[4];\n"
                "                 unsigned char len; unsigned char pos; } "
                "__cccc_c8state;\n"
                "#define __CCCC_C8STATE_MAGIC 0xC8\n"
                "_Static_assert(sizeof(mbstate_t) >= sizeof(__cccc_c8state),\n"
                "               \"cccc: host mbstate_t too small to hold "
                "__cccc_c8state\");\n"
                "typedef struct { unsigned char buf[4]; unsigned char len;\n"
                "                 unsigned char need; } __cccc_c8out_state;\n"
                "_Static_assert(sizeof(mbstate_t) >= "
                "sizeof(__cccc_c8out_state),\n"
                "               \"cccc: host mbstate_t too small to hold "
                "__cccc_c8out_state\");\n"
                "static unsigned __cccc_utf8_encode(unsigned char out[4], "
                "unsigned cp) {\n"
                "    if (cp <= 0x7F) { out[0] = (unsigned char)cp; return 1; "
                "}\n"
                "    if (cp <= 0x7FF) {\n"
                "        out[0] = (unsigned char)(0xC0 | (cp >> 6));\n"
                "        out[1] = (unsigned char)(0x80 | (cp & 0x3F));\n"
                "        return 2;\n"
                "    }\n"
                "    if (cp >= 0xD800 && cp <= 0xDFFF) return 0;\n"
                "    if (cp <= 0xFFFF) {\n"
                "        out[0] = (unsigned char)(0xE0 | (cp >> 12));\n"
                "        out[1] = (unsigned char)(0x80 | ((cp >> 6) & "
                "0x3F));\n"
                "        out[2] = (unsigned char)(0x80 | (cp & 0x3F));\n"
                "        return 3;\n"
                "    }\n"
                "    if (cp <= 0x10FFFF) {\n"
                "        out[0] = (unsigned char)(0xF0 | (cp >> 18));\n"
                "        out[1] = (unsigned char)(0x80 | ((cp >> 12) & "
                "0x3F));\n"
                "        out[2] = (unsigned char)(0x80 | ((cp >> 6) & "
                "0x3F));\n"
                "        out[3] = (unsigned char)(0x80 | (cp & 0x3F));\n"
                "        return 4;\n"
                "    }\n"
                "    return 0;\n"
                "}\n"
                "static int __cccc_utf8_decode(const unsigned char *buf, "
                "unsigned len, unsigned *out) {\n"
                "    unsigned cp;\n"
                "    switch (len) {\n"
                "        case 1:\n"
                "            if (buf[0] & 0x80) return -1;\n"
                "            cp = buf[0];\n"
                "            break;\n"
                "        case 2:\n"
                "            if ((buf[1] & 0xC0) != 0x80) return -1;\n"
                "            cp = (unsigned)(buf[0] & 0x1F) << 6 | (buf[1] & "
                "0x3F);\n"
                "            if (cp < 0x80) return -1;\n"
                "            break;\n"
                "        case 3:\n"
                "            if ((buf[1] & 0xC0) != 0x80 || (buf[2] & 0xC0) "
                "!= 0x80) return -1;\n"
                "            cp = (unsigned)(buf[0] & 0x0F) << 12 | "
                "(unsigned)(buf[1] & 0x3F) << 6 | (buf[2] & 0x3F);\n"
                "            if (cp < 0x800) return -1;\n"
                "            if (cp >= 0xD800 && cp <= 0xDFFF) return -1;\n"
                "            break;\n"
                "        case 4:\n"
                "            if ((buf[1] & 0xC0) != 0x80 || (buf[2] & 0xC0) "
                "!= 0x80 ||\n"
                "                (buf[3] & 0xC0) != 0x80) return -1;\n"
                "            cp = (unsigned)(buf[0] & 0x07) << 18 | "
                "(unsigned)(buf[1] & 0x3F) << 12 |\n"
                "                 (unsigned)(buf[2] & 0x3F) << 6 | (buf[3] & "
                "0x3F);\n"
                "            if (cp < 0x10000 || cp > 0x10FFFF) return -1;\n"
                "            break;\n"
                "        default:\n"
                "            return -1;\n"
                "    }\n"
                "    *out = cp;\n"
                "    return 0;\n"
                "}\n");
        if (use_mbrtoc8)
            fprintf(f,
                    "size_t mbrtoc8(char8_t *pc8, const char *s, size_t n, "
                    "mbstate_t *ps) {\n"
                    "    static mbstate_t internal_state;\n"
                    "    if (!ps) ps = &internal_state;\n"
                    "    __cccc_c8state st;\n"
                    "    __builtin_memcpy(&st, ps, sizeof(st));\n"
                    "    if (st.magic == __CCCC_C8STATE_MAGIC && st.len > 0) "
                    "{\n"
                    "        if (pc8) *pc8 = st.buf[st.pos];\n"
                    "        st.pos++;\n"
                    "        st.len--;\n"
                    "        if (st.len == 0) __builtin_memset(ps, 0, "
                    "sizeof(*ps));\n"
                    "        else __builtin_memcpy(ps, &st, sizeof(st));\n"
                    "        return (size_t)-3;\n"
                    "    }\n"
                    "    wchar_t wc;\n"
                    "    size_t rc = mbrtowc(&wc, s, n, ps);\n"
                    "    if (rc == (size_t)-1 || rc == (size_t)-2) return "
                    "rc;\n"
                    "    if (rc == 0) { if (pc8) *pc8 = 0; return 0; }\n"
                    "    unsigned char enc[4];\n"
                    "    unsigned elen = __cccc_utf8_encode(enc, (unsigned)"
                    "wc);\n"
                    "    if (elen == 0) { errno = EILSEQ; return (size_t)-1; "
                    "}\n"
                    "    if (pc8) *pc8 = enc[0];\n"
                    "    if (elen > 1) {\n"
                    "        __cccc_c8state newst;\n"
                    "        __builtin_memset(&newst, 0, sizeof(newst));\n"
                    "        newst.magic = __CCCC_C8STATE_MAGIC;\n"
                    "        __builtin_memcpy(newst.buf, enc + 1, elen - "
                    "1);\n"
                    "        newst.len = (unsigned char)(elen - 1);\n"
                    "        newst.pos = 0;\n"
                    "        __builtin_memcpy(ps, &newst, sizeof(newst));\n"
                    "    }\n"
                    "    return rc;\n"
                    "}\n");
        if (use_c8rtomb)
            fprintf(f,
                    "size_t c8rtomb(char *s, char8_t c8, mbstate_t *ps) {\n"
                    "    static mbstate_t internal_state;\n"
                    "    if (!ps) ps = &internal_state;\n"
                    "    if (!s) { __builtin_memset(ps, 0, sizeof(*ps)); "
                    "return 0; }\n"
                    "    __cccc_c8out_state st;\n"
                    "    __builtin_memcpy(&st, ps, sizeof(st));\n"
                    "    if (c8 == 0) {\n"
                    "        if (st.len != 0) { errno = EILSEQ; return "
                    "(size_t)-1; }\n"
                    "        mbstate_t wcs;\n"
                    "        __builtin_memset(&wcs, 0, sizeof(wcs));\n"
                    "        return wcrtomb(s, L'\\0', &wcs);\n"
                    "    }\n"
                    "    if (st.len == 0) {\n"
                    "        unsigned need;\n"
                    "        if ((c8 & 0x80) == 0x00) need = 1;\n"
                    "        else if ((c8 & 0xE0) == 0xC0) need = 2;\n"
                    "        else if ((c8 & 0xF0) == 0xE0) need = 3;\n"
                    "        else if ((c8 & 0xF8) == 0xF0) need = 4;\n"
                    "        else { errno = EILSEQ; return (size_t)-1; }\n"
                    "        st.need = (unsigned char)need;\n"
                    "    } else if ((c8 & 0xC0) != 0x80) {\n"
                    "        __builtin_memset(ps, 0, sizeof(*ps));\n"
                    "        errno = EILSEQ;\n"
                    "        return (size_t)-1;\n"
                    "    }\n"
                    "    st.buf[st.len++] = (unsigned char)c8;\n"
                    "    if (st.len < st.need) {\n"
                    "        __builtin_memcpy(ps, &st, sizeof(st));\n"
                    "        return 0;\n"
                    "    }\n"
                    "    unsigned cp;\n"
                    "    int ok = __cccc_utf8_decode(st.buf, st.len, &cp) == "
                    "0;\n"
                    "    __builtin_memset(ps, 0, sizeof(*ps));\n"
                    "    if (!ok) { errno = EILSEQ; return (size_t)-1; }\n"
                    "    mbstate_t wcs;\n"
                    "    __builtin_memset(&wcs, 0, sizeof(wcs));\n"
                    "    return wcrtomb(s, (wchar_t)cp, &wcs);\n"
                    "}\n");
        fprintf(f, "#endif\n");
    }

    fprintf(f, "\n");
}

// #1140: real native-mode definitions for the `--posix-emulation` symbols
// that have no host primitive on some target (ppoll, the sched_*
// process-scheduling family) and for the ungated gethostbyname_r/
// gethostbyaddr_r/getnetbyname_r resolvers, which have no host primitive on
// macOS at all regardless of --posix-emulation. Same shape/placement as
// serialize_threads_shims/serialize_uchar_shims above: only emitted where
// the VM's own equivalent (src/stdlib/posix_poll.c, posix_sched.c,
// posix_net.c) isn't a passthrough to a real host symbol, gated to
// !generated_only by the caller and !emit_cccc here, and each body wrapped
// in `#if !defined(__linux__)` in the *emitted* output (not a host #ifdef
// in this function) so a real host cc on Linux -- where every one of these
// is a genuine libc symbol -- drops the shim entirely and keeps calling the
// real thing. bundled_shim_fn_is_used() (above) requires the declaration to
// come from CCCC's own bundled poll.h/sched.h/netdb.h copy, mirroring the
// "did this bodiless prototype get dropped by the native serializer" test
// the include-replay/prototype-emission gate already applies.
//
// The struct/type declarations these bodies need (struct pollfd, nfds_t,
// pid_t, struct sched_param, struct hostent, struct netent, HOST_NOT_FOUND)
// are already visible in the output: they only exist if the guest program
// itself included <poll.h>/<sched.h>/<netdb.h>, which is replayed verbatim
// ahead of this point in cc_serialize_program. Only <pthread.h>/<signal.h>/
// <errno.h> are self-included here, matching serialize_threads_shims'
// own #1054-class shadowing avoidance -- no <string.h>/<sched.h>, since
// CCCC's bundled copies have no #include_next hand-off and would shadow the
// real ones reached via <pthread.h>'s own hand-off (#1022).
void serialize_posix_compat_shims(FILE *f, VirtualMachine *vm, Obj *prog) {
    if (vm->compiler.emit_cccc)
        return;

    bool use_poll  = bundled_shim_fn_is_used(vm, prog, "poll", "poll.h");
    bool use_ppoll = bundled_shim_fn_is_used(vm, prog, "ppoll", "poll.h");
    bool any_poll  = use_poll || use_ppoll;

    // #1145: struct in6_pktinfo (include/netinet/in.h) -- CCCC's own
    // definition is suppressed from native output the same way every
    // from_include struct's body is (member access re-resolves against the
    // replayed #include's real host layout). On Linux, though, glibc's real
    // <netinet/in.h> only defines this one under _GNU_SOURCE/__USE_GNU
    // (confirmed against a real glibc 2.39 header: sizeof/member access
    // both work once -D_GNU_SOURCE is added, fail identically to this bug
    // otherwise), which this generated TU never defines -- same policy as
    // every other gap in this file, forward-supply the missing piece rather
    // than flipping on _GNU_SOURCE for the whole TU. Layout ported verbatim
    // from include/netinet/in.h's own struct (already verified there to be
    // identical on macOS and Linux, sizeof == 20 on both), gated so this
    // definition is a no-op wherever the host already provides one.
    if (bundled_tag_is_declared(vm, "in6_pktinfo", "netinet/in.h"))
        fprintf(f, "#if defined(__linux__) && !defined(_GNU_SOURCE) && "
                   "!defined(__USE_GNU)\n"
                   "struct in6_pktinfo {\n"
                   "    struct in6_addr ipi6_addr;\n"
                   "    int ipi6_ifindex;\n"
                   "};\n"
                   "#endif\n");

    // #1145: aio_fsync()'s NULL-aiocbp guard -- wrap_aio_fsync
    // (src/stdlib/posix_aio.c) rejects a NULL aiocbp with EINVAL/-1 before
    // ever reaching the real host aio_fsync(); POSIX itself leaves a NULL
    // aiocbp undefined, so this is deliberate CCCC-contract behavior (see
    // the test's own comment, tests/suites/test_suite_posix.c), not a
    // portability workaround -- worth reproducing here the same way any
    // other wrap_* contract is. Confirmed without this guard, glibc's
    // aio_fsync64 dereferences the NULL pointer directly (SIGSEGV) rather
    // than validating it. Deliberately does NOT port
    // cccc_posix_sigevent_prepare()'s SIGEV_THREAD cookie machinery --
    // that bridges a guest function pointer into the VM's own callback
    // dispatch, which doesn't exist under native (a guest sigev_notify_
    // function is already a real, directly host-callable function
    // pointer there).
    if (bundled_shim_fn_is_used(vm, prog, "aio_fsync", "aio.h")) {
        if (rename_bundled_extern_for_native_shim(vm, prog, "aio_fsync",
                                                  "aio.h"))
            fprintf(f, "static int __cccc_native_aio_fsync(int op, struct "
                       "aiocb *aiocbp) {\n"
                       "    if (!aiocbp) { errno = EINVAL; return -1; }\n"
                       "    return aio_fsync(op, aiocbp);\n"
                       "}\n");
    }

    bool use_sched_setparam =
        bundled_shim_fn_is_used(vm, prog, "sched_setparam", "sched.h");
    bool use_sched_getparam =
        bundled_shim_fn_is_used(vm, prog, "sched_getparam", "sched.h");
    bool use_sched_setscheduler =
        bundled_shim_fn_is_used(vm, prog, "sched_setscheduler", "sched.h");
    bool use_sched_getscheduler =
        bundled_shim_fn_is_used(vm, prog, "sched_getscheduler", "sched.h");
    bool use_sched_rr_get_interval =
        bundled_shim_fn_is_used(vm, prog, "sched_rr_get_interval", "sched.h");
    bool any_sched = use_sched_setparam || use_sched_getparam ||
                     use_sched_setscheduler || use_sched_getscheduler ||
                     use_sched_rr_get_interval;

    bool use_gethostbyname_r =
        bundled_shim_fn_is_used(vm, prog, "gethostbyname_r", "netdb.h");
    bool use_gethostbyaddr_r =
        bundled_shim_fn_is_used(vm, prog, "gethostbyaddr_r", "netdb.h");
    bool use_getnetbyname_r =
        bundled_shim_fn_is_used(vm, prog, "getnetbyname_r", "netdb.h");
    bool any_resolver_r =
        use_gethostbyname_r || use_gethostbyaddr_r || use_getnetbyname_r;

    // #1146: the plain gethostbyname()/gethostbyaddr()/getnetbyname() side
    // of the NSS mutex residual (see the mutex comment below) -- only
    // relevant, and only probed, when the guest program also uses the _r
    // family, so a program that never touches _r is emitted byte-identical
    // to before this change.
    bool use_gethostbyname =
        any_resolver_r &&
        bundled_shim_fn_is_used(vm, prog, "gethostbyname", "netdb.h");
    bool use_gethostbyaddr =
        any_resolver_r &&
        bundled_shim_fn_is_used(vm, prog, "gethostbyaddr", "netdb.h");
    bool use_getnetbyname =
        any_resolver_r &&
        bundled_shim_fn_is_used(vm, prog, "getnetbyname", "netdb.h");
    bool any_resolver_plain =
        use_gethostbyname || use_gethostbyaddr || use_getnetbyname;

    if (!any_poll && !any_sched && !any_resolver_r)
        return;

    fprintf(f, "#if !defined(__linux__)\n"
               "#include <pthread.h>\n"
               "#include <signal.h>\n"
               "#include <errno.h>\n"
               "#include <stdint.h>\n"
               "#endif\n");
    if (any_poll)
        // stdlib.h for malloc/free, errno.h for errno/ENOMEM -- needed
        // unconditionally (unlike the pthread/signal/errno/stdint bundle
        // just above, which is only ever referenced from !defined(__linux__)
        // code): __cccc_native_poll below is defined and used on every
        // host, not just non-Linux ones.
        fprintf(f, "#include <stdlib.h>\n"
                   "#include <errno.h>\n");

    // POLLWRNORM/POLLWRBAND (#821/#1146) -- POLLRDNORM/POLLRDBAND happen to
    // share the same bit values on macOS and glibc, but POLLWRNORM/
    // POLLWRBAND diverge (macOS aliases POLLWRNORM to POLLOUT and uses
    // 0x0100 for POLLWRBAND; glibc uses 0x0100/0x0200, which is what
    // CCCC's canonical numbering, include/poll.h, copies). Ported verbatim
    // from guest_to_host_pollev/host_to_guest_pollev
    // (src/stdlib/posix_poll.c), including the documented aliasing
    // artifact: because host POLLWRNORM == POLLOUT on macOS, a host
    // POLLOUT revent sets both canonical POLLOUT and canonical POLLWRNORM
    // in the guest -- intentional, not a bug. Neither helper needs a
    // top-level guard (unlike ppoll below): both compile to a no-op
    // #else arm on Linux, where canonical already equals the host's real
    // values.
    if (any_poll) {
        if (use_poll)
            rename_bundled_extern_for_native_shim(vm, prog, "poll", "poll.h");
        fprintf(
            f,
            "static short __cccc_native_guest_to_host_pollev(short "
            "guest_events) {\n"
            "#ifdef __APPLE__\n"
            "    short host = guest_events & (short)~(0x0040 | 0x0080 | "
            "0x0100 | 0x0200);\n"
            "    if (guest_events & 0x0040) host |= POLLRDNORM;\n"
            "    if (guest_events & 0x0080) host |= POLLRDBAND;\n"
            "    if (guest_events & 0x0100) host |= POLLWRNORM;\n"
            "    if (guest_events & 0x0200) host |= POLLWRBAND;\n"
            "    return host;\n"
            "#else\n"
            "    return guest_events;\n"
            "#endif\n"
            "}\n"
            "static short __cccc_native_host_to_guest_pollev(short "
            "host_revents) {\n"
            "#ifdef __APPLE__\n"
            "    short guest = host_revents & (short)~(POLLRDNORM | "
            "POLLRDBAND | POLLWRNORM | POLLWRBAND);\n"
            "    if (host_revents & POLLRDNORM) guest |= 0x0040;\n"
            "    if (host_revents & POLLRDBAND) guest |= 0x0080;\n"
            "    if (host_revents & POLLWRNORM) guest |= 0x0100;\n"
            "    if (host_revents & POLLWRBAND) guest |= 0x0200;\n"
            "    return guest;\n"
            "#else\n"
            "    return host_revents;\n"
            "#endif\n"
            "}\n"
            // Marshals through a host-side heap array rather than
            // translating in place, same rationale as poll_marshal_in/out
            // (posix_poll.c): never mutate guest memory in place while
            // translating. Simpler than that function's stack-buffer fast
            // path (no perf-critical native-mode caller identified) --
            // always heap-allocates, still correct.
            "static struct pollfd *__cccc_native_poll_marshal_in(struct "
            "pollfd *guest_fds, nfds_t nfds) {\n"
            "    struct pollfd *host = (struct pollfd *)malloc(sizeof(struct "
            "pollfd) * (nfds ? nfds : 1));\n"
            "    if (!host) return 0;\n"
            "    for (nfds_t i = 0; i < nfds; i++) {\n"
            "        host[i].fd = guest_fds[i].fd;\n"
            "        host[i].events = "
            "__cccc_native_guest_to_host_pollev(guest_fds[i].events);\n"
            "        host[i].revents = 0;\n"
            "    }\n"
            "    return host;\n"
            "}\n"
            "static void __cccc_native_poll_marshal_out(struct pollfd "
            "*host, struct pollfd *guest_fds, nfds_t nfds) {\n"
            "    for (nfds_t i = 0; i < nfds; i++)\n"
            "        guest_fds[i].revents = "
            "__cccc_native_host_to_guest_pollev(host[i].revents);\n"
            "}\n");
        if (use_poll)
            fprintf(f,
                    "static int __cccc_native_poll(struct pollfd *fds, nfds_t "
                    "nfds, int timeout) {\n"
                    "    struct pollfd *host = "
                    "__cccc_native_poll_marshal_in(fds, nfds);\n"
                    "    if (!host) { errno = ENOMEM; return -1; }\n"
                    "    int r = poll(host, nfds, timeout);\n"
                    "    int saved_errno = errno;\n"
                    "    __cccc_native_poll_marshal_out(host, fds, nfds);\n"
                    "    free(host);\n"
                    "    errno = saved_errno;\n"
                    "    return r;\n"
                    "}\n");
    }

    // ppoll() (#821/#1140/#1146) -- pthread_sigmask()+poll() emulation,
    // ported from ppoll_emulate_macos/wrap_ppoll_gil
    // (src/stdlib/posix_poll.c). Not atomic like the real syscall -- a
    // signal delivered between the mask swap and poll()'s wait is not
    // guaranteed to interrupt it -- exactly the same accepted, documented
    // limitation as the VM's own emulation (see man/COVERAGE.md's
    // <poll.h> entry). Unlike before #1146, this now DOES translate
    // pollfd.events/revents through the same
    // __cccc_native_poll_marshal_in/out helpers plain poll() uses just
    // above -- the two are now consistent with each other in the same
    // binary, closing the very residual this comment used to describe.
    if (use_ppoll)
        fprintf(f, "#if !defined(__linux__)\n"
                   "static int ppoll(struct pollfd *fds, nfds_t nfds,\n"
                   "                 const struct timespec *timeout,\n"
                   "                 const sigset_t *sigmask) {\n"
                   "    sigset_t old_set;\n"
                   "    int have_old = 0;\n"
                   "    if (sigmask) {\n"
                   "        if (pthread_sigmask(SIG_SETMASK, sigmask, "
                   "&old_set) == 0)\n"
                   "            have_old = 1;\n"
                   "    }\n"
                   "    int ms = -1;\n"
                   "    if (timeout)\n"
                   "        ms = (int)(timeout->tv_sec * 1000 + "
                   "timeout->tv_nsec / 1000000);\n"
                   "    struct pollfd *host = "
                   "__cccc_native_poll_marshal_in(fds, nfds);\n"
                   "    if (!host) { errno = ENOMEM; return -1; }\n"
                   "    int r = poll(host, nfds, ms);\n"
                   "    int saved_errno = errno;\n"
                   "    __cccc_native_poll_marshal_out(host, fds, nfds);\n"
                   "    free(host);\n"
                   "    if (have_old)\n"
                   "        pthread_sigmask(SIG_SETMASK, &old_set, NULL);\n"
                   "    errno = saved_errno;\n"
                   "    return r;\n"
                   "}\n"
                   "#endif\n");
    // #1145: on Linux, ppoll() genuinely is a host primitive (no emulation
    // needed, matching the #if !defined(__linux__) branch above) -- but
    // it's a glibc extension gated behind __USE_GNU, which the replayed
    // `#include <poll.h>` above only exposes under _GNU_SOURCE, which this
    // generated TU never defines. Forward-declared locally instead, ported
    // verbatim from wrap_ppoll_gil's identical comment/declaration
    // (src/stdlib/posix_poll.c) -- glibc still exports the real symbol
    // regardless of the declaration being visible.
    if (use_ppoll)
        fprintf(f, "#if defined(__linux__)\n"
                   "extern int ppoll(struct pollfd *fds, nfds_t nfds,\n"
                   "                 const struct timespec *timeout,\n"
                   "                 const sigset_t *sigmask);\n"
                   "#endif\n");

    // sched_setparam/getparam/setscheduler/getscheduler/rr_get_interval
    // (#824/#1140) -- macOS has no process-scheduling API at all, so the
    // VM's own non-Linux wrap_sched_* family (src/stdlib/posix_sched.c)
    // always returns ENOSYS; ported verbatim.
    if (any_sched) {
        if (use_sched_setparam)
            fprintf(f, "#if !defined(__linux__)\n"
                       "static int sched_setparam(pid_t pid, const struct "
                       "sched_param *param) {\n"
                       "    (void)pid; (void)param;\n"
                       "    errno = ENOSYS;\n"
                       "    return -1;\n"
                       "}\n"
                       "#endif\n");
        if (use_sched_getparam)
            fprintf(f, "#if !defined(__linux__)\n"
                       "static int sched_getparam(pid_t pid, struct "
                       "sched_param *param) {\n"
                       "    (void)pid; (void)param;\n"
                       "    errno = ENOSYS;\n"
                       "    return -1;\n"
                       "}\n"
                       "#endif\n");
        if (use_sched_setscheduler)
            fprintf(f, "#if !defined(__linux__)\n"
                       "static int sched_setscheduler(pid_t pid, int "
                       "policy, const struct sched_param *param) {\n"
                       "    (void)pid; (void)policy; (void)param;\n"
                       "    errno = ENOSYS;\n"
                       "    return -1;\n"
                       "}\n"
                       "#endif\n");
        if (use_sched_getscheduler)
            fprintf(f, "#if !defined(__linux__)\n"
                       "static int sched_getscheduler(pid_t pid) {\n"
                       "    (void)pid;\n"
                       "    errno = ENOSYS;\n"
                       "    return -1;\n"
                       "}\n"
                       "#endif\n");
        if (use_sched_rr_get_interval)
            fprintf(f, "#if !defined(__linux__)\n"
                       "static int sched_rr_get_interval(pid_t pid, struct "
                       "timespec *interval) {\n"
                       "    (void)pid; (void)interval;\n"
                       "    errno = ENOSYS;\n"
                       "    return -1;\n"
                       "}\n"
                       "#endif\n");
    }

    // gethostbyname_r/gethostbyaddr_r/getnetbyname_r (#785/#1140) -- macOS
    // has no _r resolver family at all (glibc-only extensions), so this
    // ports the VM's own portable shim (nss_*_r_shim family,
    // src/stdlib/posix_net.c) rather than a host passthrough: the mutex
    // serializes access to the underlying plain lookup's static buffer,
    // and the result is deep-copied into the caller's own buffer before
    // the mutex is released.
    //
    // Known residual, ported as-is rather than fixed here: on the VM,
    // this same nss_static_mutex is ALSO taken by the plain
    // gethostbyname()/gethostbyaddr()/getnetbyname() wrappers, making the
    // two families mutually exclusive -- that mutual exclusion is what
    // makes the deep copy race-free. Natively, the plain lookups are
    // direct calls straight to the host's own gethostbyname() etc (no
    // CCCC wrapper exists to add a mutex to, and the host already declares
    // them, so shadowing with a same-named static definition is not legal
    // C) -- so a concurrent plain lookup from another thread can still
    // overwrite the same static internal buffer mid-copy here, a torn
    // result rather than a crash. --posix-emulation guest code mixing the
    // plain and _r families across threads under -c=native inherits this
    // gap; see #1146 for closing it (e.g. a call-site rewrite to a
    // mutex-taking wrapper for the plain family too).
    if (any_resolver_r) {
        fprintf(f, "#if !defined(__linux__)\n"
                   "static pthread_mutex_t __cccc_nss_native_mutex = "
                   "PTHREAD_MUTEX_INITIALIZER;\n"
                   "static size_t __cccc_nss_r_layout_size(int count, "
                   "size_t str_bytes) {\n"
                   "    size_t ptrs = (size_t)(count + 1) * sizeof(char *);\n"
                   "    return ptrs + sizeof(char *) + str_bytes;\n"
                   "}\n"
                   "static char **__cccc_nss_r_copy_ptr_array(char **list, "
                   "int count, char **cursor, char *end) {\n"
                   "    char *p = (char *)*cursor;\n"
                   "    p = (char *)(((uintptr_t)p + sizeof(char *) - 1) &\n"
                   "                 ~(uintptr_t)(sizeof(char *) - 1));\n"
                   "    char **arr = (char **)(void *)p;\n"
                   "    if ((char *)(arr + count + 1) > end)\n"
                   "        return NULL;\n"
                   "    char *strp = (char *)(arr + count + 1);\n"
                   "    for (int i = 0; i < count; i++) {\n"
                   "        size_t len = __builtin_strlen(list[i]) + 1;\n"
                   "        if (strp + len > end)\n"
                   "            return NULL;\n"
                   "        __builtin_memcpy(strp, list[i], len);\n"
                   "        arr[i] = strp;\n"
                   "        strp += len;\n"
                   "    }\n"
                   "    arr[count] = 0;\n"
                   "    *cursor = strp;\n"
                   "    return arr;\n"
                   "}\n"
                   "static int __cccc_nss_count_list(char **list) {\n"
                   "    int n = 0;\n"
                   "    if (list) while (list[n]) n++;\n"
                   "    return n;\n"
                   "}\n"
                   "#endif\n");

        if (use_gethostbyname_r)
            fprintf(f,
                    "#if !defined(__linux__)\n"
                    "static int gethostbyname_r(const char *name, struct "
                    "hostent *ret, char *buf, size_t buflen,\n"
                    "                           struct hostent **result, int "
                    "*h_errnop) {\n"
                    "    pthread_mutex_lock(&__cccc_nss_native_mutex);\n"
                    "    struct hostent *src = gethostbyname(name);\n"
                    "    int rc = 0;\n"
                    "    if (!src) {\n"
                    "        *result = 0;\n"
                    "        if (h_errnop) *h_errnop = HOST_NOT_FOUND;\n"
                    "        goto done;\n"
                    "    }\n"
                    "    {\n"
                    "    int naliases = __cccc_nss_count_list(src->h_aliases);"
                    "\n"
                    "    int naddrs = __cccc_nss_count_list(src->h_addr_list);"
                    "\n"
                    "    size_t need = __builtin_strlen(src->h_name) + 1;\n"
                    "    for (int i = 0; i < naliases; i++)\n"
                    "        need += __builtin_strlen(src->h_aliases[i]) + "
                    "1;\n"
                    "    need = __cccc_nss_r_layout_size(naliases, need) +\n"
                    "           __cccc_nss_r_layout_size(naddrs, (size_t)"
                    "naddrs * (size_t)src->h_length);\n"
                    "    if (need > buflen) { rc = ERANGE; *result = 0; goto "
                    "done; }\n"
                    "    char *cursor = buf;\n"
                    "    char *end = buf + buflen;\n"
                    "    size_t namelen = __builtin_strlen(src->h_name) + 1;\n"
                    "    if (cursor + namelen > end) { rc = ERANGE; *result = "
                    "0; goto done; }\n"
                    "    __builtin_memcpy(cursor, src->h_name, namelen);\n"
                    "    ret->h_name = cursor;\n"
                    "    cursor += namelen;\n"
                    "    char **aliases = __cccc_nss_r_copy_ptr_array(src->"
                    "h_aliases, naliases, &cursor, end);\n"
                    "    if (!aliases) { rc = ERANGE; *result = 0; goto "
                    "done; }\n"
                    "    ret->h_aliases = aliases;\n"
                    "    ret->h_addrtype = src->h_addrtype;\n"
                    "    ret->h_length = src->h_length;\n"
                    "    char *p = cursor;\n"
                    "    p = (char *)(((uintptr_t)p + sizeof(char *) - 1) &\n"
                    "                 ~(uintptr_t)(sizeof(char *) - 1));\n"
                    "    char **addrs = (char **)(void *)p;\n"
                    "    if ((char *)(addrs + naddrs + 1) > end) { rc = "
                    "ERANGE; *result = 0; goto done; }\n"
                    "    char *ap = (char *)(addrs + naddrs + 1);\n"
                    "    for (int i = 0; i < naddrs; i++) {\n"
                    "        if (ap + src->h_length > end) { rc = ERANGE; "
                    "*result = 0; goto done; }\n"
                    "        __builtin_memcpy(ap, src->h_addr_list[i], "
                    "(size_t)src->h_length);\n"
                    "        addrs[i] = ap;\n"
                    "        ap += src->h_length;\n"
                    "    }\n"
                    "    addrs[naddrs] = 0;\n"
                    "    ret->h_addr_list = addrs;\n"
                    "    *result = ret;\n"
                    "    }\n"
                    "done:\n"
                    "    pthread_mutex_unlock(&__cccc_nss_native_mutex);\n"
                    "    return rc;\n"
                    "}\n"
                    "#endif\n");

        if (use_gethostbyaddr_r)
            fprintf(f,
                    "#if !defined(__linux__)\n"
                    "static int gethostbyaddr_r(const void *addr, socklen_t "
                    "len, int type, struct hostent *ret, char *buf,\n"
                    "                           size_t buflen, struct "
                    "hostent **result, int *h_errnop) {\n"
                    "    pthread_mutex_lock(&__cccc_nss_native_mutex);\n"
                    "    struct hostent *src = gethostbyaddr(addr, len, "
                    "type);\n"
                    "    int rc = 0;\n"
                    "    if (!src) {\n"
                    "        *result = 0;\n"
                    "        if (h_errnop) *h_errnop = HOST_NOT_FOUND;\n"
                    "        goto done;\n"
                    "    }\n"
                    "    {\n"
                    "    int naliases = __cccc_nss_count_list(src->h_aliases);"
                    "\n"
                    "    int naddrs = __cccc_nss_count_list(src->h_addr_list);"
                    "\n"
                    "    size_t need = __builtin_strlen(src->h_name) + 1;\n"
                    "    for (int i = 0; i < naliases; i++)\n"
                    "        need += __builtin_strlen(src->h_aliases[i]) + "
                    "1;\n"
                    "    need = __cccc_nss_r_layout_size(naliases, need) +\n"
                    "           __cccc_nss_r_layout_size(naddrs, (size_t)"
                    "naddrs * (size_t)src->h_length);\n"
                    "    if (need > buflen) { rc = ERANGE; *result = 0; goto "
                    "done; }\n"
                    "    char *cursor = buf;\n"
                    "    char *end = buf + buflen;\n"
                    "    size_t namelen = __builtin_strlen(src->h_name) + 1;\n"
                    "    if (cursor + namelen > end) { rc = ERANGE; *result = "
                    "0; goto done; }\n"
                    "    __builtin_memcpy(cursor, src->h_name, namelen);\n"
                    "    ret->h_name = cursor;\n"
                    "    cursor += namelen;\n"
                    "    char **aliases = __cccc_nss_r_copy_ptr_array(src->"
                    "h_aliases, naliases, &cursor, end);\n"
                    "    if (!aliases) { rc = ERANGE; *result = 0; goto "
                    "done; }\n"
                    "    ret->h_aliases = aliases;\n"
                    "    ret->h_addrtype = src->h_addrtype;\n"
                    "    ret->h_length = src->h_length;\n"
                    "    char *p = cursor;\n"
                    "    p = (char *)(((uintptr_t)p + sizeof(char *) - 1) &\n"
                    "                 ~(uintptr_t)(sizeof(char *) - 1));\n"
                    "    char **addrs = (char **)(void *)p;\n"
                    "    if ((char *)(addrs + naddrs + 1) > end) { rc = "
                    "ERANGE; *result = 0; goto done; }\n"
                    "    char *ap = (char *)(addrs + naddrs + 1);\n"
                    "    for (int i = 0; i < naddrs; i++) {\n"
                    "        if (ap + src->h_length > end) { rc = ERANGE; "
                    "*result = 0; goto done; }\n"
                    "        __builtin_memcpy(ap, src->h_addr_list[i], "
                    "(size_t)src->h_length);\n"
                    "        addrs[i] = ap;\n"
                    "        ap += src->h_length;\n"
                    "    }\n"
                    "    addrs[naddrs] = 0;\n"
                    "    ret->h_addr_list = addrs;\n"
                    "    *result = ret;\n"
                    "    }\n"
                    "done:\n"
                    "    pthread_mutex_unlock(&__cccc_nss_native_mutex);\n"
                    "    return rc;\n"
                    "}\n"
                    "#endif\n");

        if (use_getnetbyname_r)
            fprintf(f,
                    "#if !defined(__linux__)\n"
                    "static int getnetbyname_r(const char *name, struct "
                    "netent *ret, char *buf, size_t buflen,\n"
                    "                          struct netent **result, int "
                    "*h_errnop) {\n"
                    "    pthread_mutex_lock(&__cccc_nss_native_mutex);\n"
                    "    struct netent *src = getnetbyname(name);\n"
                    "    int rc = 0;\n"
                    "    if (!src) {\n"
                    "        *result = 0;\n"
                    "        if (h_errnop) *h_errnop = HOST_NOT_FOUND;\n"
                    "        goto done;\n"
                    "    }\n"
                    "    {\n"
                    "    int naliases = __cccc_nss_count_list(src->"
                    "n_aliases);\n"
                    "    size_t need = __builtin_strlen(src->n_name) + 1;\n"
                    "    for (int i = 0; i < naliases; i++)\n"
                    "        need += __builtin_strlen(src->n_aliases[i]) + "
                    "1;\n"
                    "    need = __cccc_nss_r_layout_size(naliases, need);\n"
                    "    if (need > buflen) { rc = ERANGE; *result = 0; goto "
                    "done; }\n"
                    "    char *cursor = buf;\n"
                    "    char *end = buf + buflen;\n"
                    "    size_t namelen = __builtin_strlen(src->n_name) + "
                    "1;\n"
                    "    if (cursor + namelen > end) { rc = ERANGE; *result "
                    "= 0; goto done; }\n"
                    "    __builtin_memcpy(cursor, src->n_name, namelen);\n"
                    "    ret->n_name = cursor;\n"
                    "    cursor += namelen;\n"
                    "    char **aliases = __cccc_nss_r_copy_ptr_array(src->"
                    "n_aliases, naliases, &cursor, end);\n"
                    "    if (!aliases) { rc = ERANGE; *result = 0; goto "
                    "done; }\n"
                    "    ret->n_aliases = aliases;\n"
                    "    ret->n_addrtype = src->n_addrtype;\n"
                    "    ret->n_net = src->n_net;\n"
                    "    *result = ret;\n"
                    "    }\n"
                    "done:\n"
                    "    pthread_mutex_unlock(&__cccc_nss_native_mutex);\n"
                    "    return rc;\n"
                    "}\n"
                    "#endif\n");
    }

    // #1146: closes the residual documented above -- on the VM,
    // nss_static_mutex (src/stdlib/posix_net.c) is taken by the plain
    // gethostbyname()/gethostbyaddr()/getnetbyname() wrappers too, which is
    // what makes the _r family's deep copy into the caller's own buffer
    // race-free; a torn result was possible natively because the plain
    // family had no wrapper to add a mutex to. Renaming the plain family
    // (only when it's actually used alongside the _r family, per
    // any_resolver_plain above) and giving it a same-mutex wrapper restores
    // that mutual exclusion. Gated on any_resolver_r, not emitted at all
    // otherwise, so a program that never uses the _r family is unaffected.
    //
    // Unlike the _r shims above (whole-function #if !defined(__linux__),
    // legal because on Linux the real glibc _r functions are used directly
    // under their own un-renamed names), these wrappers must be defined
    // unconditionally: the rename that redirects the guest's call sites is
    // baked in at cccc-serialize time, independent of which host later
    // compiles this file, so a definition gated out on Linux would leave a
    // Linux native build with an undefined __cccc_native_gethostbyname
    // symbol. Only the mutex lock/unlock (meaningless on Linux, since
    // __cccc_nss_native_mutex above is itself only declared under
    // !defined(__linux__)) is guarded internally; on Linux this reduces to
    // a plain passthrough, which is correct since no _r shim -- and so no
    // race to guard against -- exists there either.
    if (any_resolver_plain) {
        if (use_gethostbyname)
            rename_bundled_extern_for_native_shim(vm, prog, "gethostbyname",
                                                  "netdb.h");
        if (use_gethostbyaddr)
            rename_bundled_extern_for_native_shim(vm, prog, "gethostbyaddr",
                                                  "netdb.h");
        if (use_getnetbyname)
            rename_bundled_extern_for_native_shim(vm, prog, "getnetbyname",
                                                  "netdb.h");

        if (use_gethostbyname)
            fprintf(f, "static struct hostent *__cccc_native_gethostbyname("
                       "const char *name) {\n"
                       "#if !defined(__linux__)\n"
                       "    pthread_mutex_lock(&__cccc_nss_native_mutex);\n"
                       "#endif\n"
                       "    struct hostent *r = gethostbyname(name);\n"
                       "#if !defined(__linux__)\n"
                       "    pthread_mutex_unlock(&__cccc_nss_native_mutex);\n"
                       "#endif\n"
                       "    return r;\n"
                       "}\n");
        if (use_gethostbyaddr)
            fprintf(f, "static struct hostent *__cccc_native_gethostbyaddr("
                       "const void *addr, socklen_t len, int type) {\n"
                       "#if !defined(__linux__)\n"
                       "    pthread_mutex_lock(&__cccc_nss_native_mutex);\n"
                       "#endif\n"
                       "    struct hostent *r = gethostbyaddr(addr, len, "
                       "type);\n"
                       "#if !defined(__linux__)\n"
                       "    pthread_mutex_unlock(&__cccc_nss_native_mutex);\n"
                       "#endif\n"
                       "    return r;\n"
                       "}\n");
        if (use_getnetbyname)
            fprintf(f, "static struct netent *__cccc_native_getnetbyname("
                       "const char *name) {\n"
                       "#if !defined(__linux__)\n"
                       "    pthread_mutex_lock(&__cccc_nss_native_mutex);\n"
                       "#endif\n"
                       "    struct netent *r = getnetbyname(name);\n"
                       "#if !defined(__linux__)\n"
                       "    pthread_mutex_unlock(&__cccc_nss_native_mutex);\n"
                       "#endif\n"
                       "    return r;\n"
                       "}\n");
    }

    fprintf(f, "\n");
}

// #1146: -c=native's counterpart to the VM's own canonical-constant
// translation. The VM folds several POSIX constant families to CCCC's own
// canonical numbering in the bundled headers (include/poll.h, langinfo.h,
// locale.h, sched.h) and translates to the host's real values inside a
// wrapper before calling the real libc function (guest_to_host_pollev/
// host_to_guest_pollev in src/stdlib/posix_poll.c, guest_to_host_nl_item in
// posix_lang.c, guest_to_host_lc/guest_to_host_lc_mask in locale.c,
// guest_to_host_sched_policy/host_to_guest_sched_policy in posix_sched.c).
// -c=native had no such wrapper: every guest use of one of these constants
// is already constant-folded to its canonical numeric value by the time an
// AST exists (ND_NUM carries only the folded int, no macro-name
// provenance survives to serialize_expr's ND_NUM case), so the emitted C
// passed the guest's canonical value straight to the host function with no
// translation at all -- silently wrong on whichever host's numbering
// *isn't* what CCCC's canonical numbering happens to copy, with no
// diagnostic anywhere (unlike #1140's undeclared-identifier errors, this
// is a call that compiles and links fine and just returns/behaves wrong).
//
// Fixed the same way #1140 supplies functions the host doesn't declare at
// all: rename_bundled_extern_for_native_shim() (above) renames the guest
// program's own declared-only reference from CCCC's bundled header to
// `__cccc_native_<name>`, and a translating wrapper is emitted under that
// new name, calling the real host function (still reachable under its
// original name via the replayed `#include`) with the constant translated
// first. Unlike serialize_posix_compat_shims()'s shims (ppoll/sched_*
// stubs/the _r resolver family), every function here IS host-declared on
// both platforms, so the rename and the wrapper are both unconditional --
// translate-vs-passthrough is decided *inside* each wrapper by the same
// #ifdef the VM's own translator uses, ported verbatim, so the identical
// generated C is correct whichever host later compiles it (mirrors how
// serialize_posix_compat_shims's per-function #if guards keep that file's
// output host-portable too, just pushed inside the function body here
// since the wrapper itself must exist unconditionally).
void serialize_canonical_const_shims(FILE *f, VirtualMachine *vm, Obj *prog) {
    if (vm->compiler.emit_cccc)
        return;

    bool use_nl_langinfo =
        bundled_shim_fn_is_used(vm, prog, "nl_langinfo", "langinfo.h");
    bool use_nl_langinfo_l =
        bundled_shim_fn_is_used(vm, prog, "nl_langinfo_l", "langinfo.h");
    bool use_setlocale =
        bundled_shim_fn_is_used(vm, prog, "setlocale", "locale.h");
    bool use_newlocale =
        bundled_shim_fn_is_used(vm, prog, "newlocale", "locale.h");
    bool use_sched_get_priority_min =
        bundled_shim_fn_is_used(vm, prog, "sched_get_priority_min", "sched.h");
    bool use_sched_get_priority_max =
        bundled_shim_fn_is_used(vm, prog, "sched_get_priority_max", "sched.h");
    bool use_sysconf = bundled_shim_fn_is_used(vm, prog, "sysconf", "unistd.h");
    bool use_pathconf =
        bundled_shim_fn_is_used(vm, prog, "pathconf", "unistd.h");
    bool use_fpathconf =
        bundled_shim_fn_is_used(vm, prog, "fpathconf", "unistd.h");
    bool use_confstr = bundled_shim_fn_is_used(vm, prog, "confstr", "unistd.h");

    bool any_nl_langinfo = use_nl_langinfo || use_nl_langinfo_l;
    bool any_locale      = use_setlocale || use_newlocale;
    bool any_sched_prio =
        use_sched_get_priority_min || use_sched_get_priority_max;
    bool any_sysconf_family =
        use_sysconf || use_pathconf || use_fpathconf || use_confstr;

    if (!any_nl_langinfo && !any_locale && !any_sched_prio &&
        !any_sysconf_family)
        return;

    // nl_item (#807/#1146) -- macOS uses a flat 0-56 sequence, which
    // CCCC's canonical numbering (include/langinfo.h) copies verbatim, so
    // translation is a no-op there; glibc packs (category << 16) | index.
    // Ported verbatim from guest_to_host_nl_item (src/stdlib/posix_lang.c)
    // including its own reasoning for using bare integer literals rather
    // than the CODESET/DAY_1/etc. macro names on the glibc side: this
    // generated file's own #include <langinfo.h> replay reaches the
    // *host's* real header, so on a glibc host those names already expand
    // to glibc's real values (e.g. CODESET is 14, not 0) and would
    // silently compare the guest's canonical input against the wrong
    // number if used as a switch label.
    if (any_nl_langinfo) {
        if (rename_bundled_extern_for_native_shim(vm, prog, "nl_langinfo",
                                                  "langinfo.h"))
            use_nl_langinfo = true;
        if (rename_bundled_extern_for_native_shim(vm, prog, "nl_langinfo_l",
                                                  "langinfo.h"))
            use_nl_langinfo_l = true;

        fprintf(
            f, "static int __cccc_native_guest_to_host_nl_item(int guest_item, "
               "long *host_item) {\n"
               "#ifdef __APPLE__\n"
               "    *host_item = guest_item;\n"
               "    return 1;\n"
               "#else\n"
               "    long v = (long)guest_item;\n"
               "    switch (v) {\n"
               "        case 0: *host_item = 14; break;\n"
               "        case 1: *host_item = 131112; break;\n"
               "        case 2: *host_item = 131113; break;\n"
               "        case 3: *host_item = 131114; break;\n"
               "        case 4: *host_item = 131115; break;\n"
               "        case 5: *host_item = 131110; break;\n"
               "        case 6: *host_item = 131111; break;\n"
               "        case 50: *host_item = 65536; break;\n"
               "        case 51: *host_item = 65537; break;\n"
               "        case 52: *host_item = 327680; break;\n"
               "        case 53: *host_item = 327681; break;\n"
               "        case 56: *host_item = 262159; break;\n"
               "        default:\n"
               "            if (v >= 7 && v <= 13) *host_item = 131079 + (v - "
               "7);\n"
               "            else if (v >= 14 && v <= 20) *host_item = 131072 + "
               "(v - 14);\n"
               "            else if (v >= 21 && v <= 32) *host_item = 131098 + "
               "(v - 21);\n"
               "            else if (v >= 33 && v <= 44) *host_item = 131086 + "
               "(v - 33);\n"
               "            else return 0;\n"
               "            break;\n"
               "    }\n"
               "    return 1;\n"
               "#endif\n"
               "}\n");
        if (use_nl_langinfo)
            fprintf(f, "static char *__cccc_native_nl_langinfo(nl_item "
                       "guest_item) {\n"
                       "    long host_item;\n"
                       "    if "
                       "(!__cccc_native_guest_to_host_nl_item((int)guest_item, "
                       "&host_item))\n"
                       "        return \"\";\n"
                       "    return nl_langinfo((nl_item)host_item);\n"
                       "}\n");
        if (use_nl_langinfo_l)
            fprintf(f, "static char *__cccc_native_nl_langinfo_l(nl_item "
                       "guest_item, locale_t loc) {\n"
                       "    long host_item;\n"
                       "    if "
                       "(!__cccc_native_guest_to_host_nl_item((int)guest_item, "
                       "&host_item))\n"
                       "        return \"\";\n"
                       "    return nl_langinfo_l((nl_item)host_item, loc);\n"
                       "}\n");
    }

    // LC_* / LC_*_MASK (#819/#820/#1146) -- ported verbatim from
    // guest_to_host_lc/guest_to_host_lc_mask (src/stdlib/locale.c). Neither
    // needs an #ifdef: both switch/branch straight to the *host's* real
    // LC_*/LC_*_MASK macro names (this file's own #include <locale.h>
    // replay reaches the host's real header), which resolve to whichever
    // host later compiles this, so the mapping is a correct no-op on
    // whichever host CCCC's canonical numbering happens to already copy.
    if (any_locale) {
        if (rename_bundled_extern_for_native_shim(vm, prog, "setlocale",
                                                  "locale.h"))
            use_setlocale = true;
        if (rename_bundled_extern_for_native_shim(vm, prog, "newlocale",
                                                  "locale.h"))
            use_newlocale = true;

        if (use_setlocale)
            fprintf(f,
                    "static int __cccc_native_guest_to_host_lc(int "
                    "guest_category) {\n"
                    "    switch (guest_category) {\n"
                    "        case 0: return LC_ALL;\n"
                    "        case 1: return LC_COLLATE;\n"
                    "        case 2: return LC_CTYPE;\n"
                    "        case 3: return LC_MONETARY;\n"
                    "        case 4: return LC_NUMERIC;\n"
                    "        case 5: return LC_TIME;\n"
                    "        case 6: return LC_MESSAGES;\n"
                    "        default: return guest_category;\n"
                    "    }\n"
                    "}\n"
                    "static char *__cccc_native_setlocale(int guest_category, "
                    "const char *locale) {\n"
                    "    return "
                    "setlocale(__cccc_native_guest_to_host_lc(guest_category), "
                    "locale);\n"
                    "}\n");
        if (use_newlocale)
            fprintf(
                f, "static int __cccc_native_guest_to_host_lc_mask(int "
                   "guest_mask) {\n"
                   "    if (guest_mask == 0x3f) return LC_ALL_MASK;\n"
                   "    int host_mask = 0;\n"
                   "    if (guest_mask & (1 << 0)) host_mask |= "
                   "LC_COLLATE_MASK;\n"
                   "    if (guest_mask & (1 << 1)) host_mask |= "
                   "LC_CTYPE_MASK;\n"
                   "    if (guest_mask & (1 << 2)) host_mask |= "
                   "LC_MESSAGES_MASK;\n"
                   "    if (guest_mask & (1 << 3)) host_mask |= "
                   "LC_MONETARY_MASK;\n"
                   "    if (guest_mask & (1 << 4)) host_mask |= "
                   "LC_NUMERIC_MASK;\n"
                   "    if (guest_mask & (1 << 5)) host_mask |= LC_TIME_MASK;\n"
                   "    return host_mask;\n"
                   "}\n"
                   "static locale_t __cccc_native_newlocale(int guest_mask, "
                   "const char *locale, locale_t base) {\n"
                   "    return "
                   "newlocale(__cccc_native_guest_to_host_lc_mask(guest_mask), "
                   "locale, base);\n"
                   "}\n");
    }

    // SCHED_* (#824/#1146) -- ported verbatim from
    // guest_to_host_sched_policy (src/stdlib/posix_sched.c). macOS's real
    // <sched.h> declares only sched_yield/sched_get_priority_min/max (no
    // process-scheduling API at all), so only those two need a translating
    // wrapper here -- sched_setscheduler/getscheduler/setparam/getparam/
    // rr_get_interval are not host-declared on macOS at all and are
    // already handled as ENOSYS stubs by serialize_posix_compat_shims()
    // under their own (never renamed) names.
    if (any_sched_prio) {
        if (rename_bundled_extern_for_native_shim(
                vm, prog, "sched_get_priority_min", "sched.h"))
            use_sched_get_priority_min = true;
        if (rename_bundled_extern_for_native_shim(
                vm, prog, "sched_get_priority_max", "sched.h"))
            use_sched_get_priority_max = true;

        // #1145: SCHED_BATCH/SCHED_IDLE are glibc extensions gated behind
        // __USE_GNU, which the replayed `#include <sched.h>` above only
        // exposes under _GNU_SOURCE -- this generated TU never defines that
        // (same policy as every other native shim in this file: locally
        // supply the missing macro rather than flipping on _GNU_SOURCE for
        // the whole TU, which would change other symbols' behavior too).
        // Ported verbatim from posix_util.h's own identical guard, which
        // the VM-side wrap_sched_setscheduler() etc. rely on the same way.
        fprintf(f, "#ifdef __linux__\n"
                   "#ifndef SCHED_BATCH\n"
                   "#define SCHED_BATCH 3\n"
                   "#endif\n"
                   "#ifndef SCHED_IDLE\n"
                   "#define SCHED_IDLE 5\n"
                   "#endif\n"
                   "#endif\n");
        fprintf(f, "static int __cccc_native_guest_to_host_sched_policy(int "
                   "guest_policy) {\n"
                   "    switch (guest_policy) {\n"
                   "        case 0: return SCHED_OTHER;\n"
                   "        case 1: return SCHED_FIFO;\n"
                   "        case 2: return SCHED_RR;\n"
                   "#ifdef __linux__\n"
                   "        case 3: return SCHED_BATCH;\n"
                   "        case 5: return SCHED_IDLE;\n"
                   "#endif\n"
                   "        default: return guest_policy;\n"
                   "    }\n"
                   "}\n");
        if (use_sched_get_priority_min)
            fprintf(f, "static int __cccc_native_sched_get_priority_min(int "
                       "policy) {\n"
                       "    return sched_get_priority_min("
                       "__cccc_native_guest_to_host_sched_policy(policy));\n"
                       "}\n");
        if (use_sched_get_priority_max)
            fprintf(f, "static int __cccc_native_sched_get_priority_max(int "
                       "policy) {\n"
                       "    return sched_get_priority_max("
                       "__cccc_native_guest_to_host_sched_policy(policy));\n"
                       "}\n");
    }

    // #1145: sysconf()/pathconf()/fpathconf()/confstr() -- ported from
    // wrap_sysconf/wrap_pathconf/wrap_fpathconf/wrap_confstr
    // (src/stdlib/posix_sched.c). include/unistd.h's own _SC_*/_PC_*/_CS_*
    // macros are CCCC's own canonical numbering (kept deliberately
    // independent of any host's, since macOS and glibc disagree on nearly
    // all of them) -- guest code folds those to plain integers at parse
    // time with no macro-name provenance, so passing them straight to the
    // replayed #include <unistd.h>'s real host sysconf()/etc silently asks
    // for the wrong thing (e.g. guest _SC_PAGESIZE, 11, is 29 on macOS and
    // 30 on glibc). Same shape as guest_to_host_nl_item just above: switch
    // labels are CCCC's bare canonical integers, case *bodies* name the
    // host's real _SC_*/_PC_*/_CS_* macro (this file's own #include
    // <unistd.h> replay reaches the host's real header, so the name
    // resolves per-host) -- never the reverse, or the label would silently
    // mean whatever the host happens to number it, defeating the whole
    // point of canonical numbering.
    if (any_sysconf_family) {
        // errno.h for errno/EINVAL -- needed unconditionally by the
        // unrecognized-name default arm of every wrapper below, even for a
        // guest program that includes <unistd.h> but never separately
        // includes <errno.h> itself (same self-include rationale as
        // any_poll's own errno.h include above).
        fprintf(f, "#include <errno.h>\n");
        if (rename_bundled_extern_for_native_shim(vm, prog, "sysconf",
                                                  "unistd.h"))
            use_sysconf = true;
        if (rename_bundled_extern_for_native_shim(vm, prog, "pathconf",
                                                  "unistd.h"))
            use_pathconf = true;
        if (rename_bundled_extern_for_native_shim(vm, prog, "fpathconf",
                                                  "unistd.h"))
            use_fpathconf = true;
        if (rename_bundled_extern_for_native_shim(vm, prog, "confstr",
                                                  "unistd.h"))
            use_confstr = true;

        if (use_sysconf)
            // _SC_VERSION/_SC_2_VERSION/_SC_XOPEN_VERSION deliberately do
            // NOT forward to the host: wrap_sysconf answers these from
            // CCCC's own VM-model constants (200809L/200809L/700), not
            // whatever POSIX revision the host libc claims (macOS: 200112/
            // 200112/600) -- forwarding would silently change what these
            // three report. The unknown-name default is EINVAL/-1, not a
            // guest-value passthrough (unlike guest_to_host_lc below):
            // sysconf(<unrecognized>) must return -1, not misinterpret an
            // arbitrary guest integer as some unrelated host _SC_ number.
            fprintf(f,
                    "static long __cccc_native_sysconf(int name) {\n"
                    "    switch (name) {\n"
                    "        case 10: return 200809L;\n"
                    "        case 17: return 200809L;\n"
                    "        case 18: return 700L;\n"
                    "#ifdef _SC_ARG_MAX\n"
                    "        case 1: return (long)sysconf(_SC_ARG_MAX);\n"
                    "#endif\n"
                    "#ifdef _SC_CHILD_MAX\n"
                    "        case 2: return (long)sysconf(_SC_CHILD_MAX);\n"
                    "#endif\n"
                    "#ifdef _SC_CLK_TCK\n"
                    "        case 3: return (long)sysconf(_SC_CLK_TCK);\n"
                    "#endif\n"
                    "#ifdef _SC_NGROUPS_MAX\n"
                    "        case 4: return (long)sysconf(_SC_NGROUPS_MAX);\n"
                    "#endif\n"
                    "#ifdef _SC_OPEN_MAX\n"
                    "        case 5: return (long)sysconf(_SC_OPEN_MAX);\n"
                    "#endif\n"
                    "#ifdef _SC_STREAM_MAX\n"
                    "        case 6: return (long)sysconf(_SC_STREAM_MAX);\n"
                    "#endif\n"
                    "#ifdef _SC_TZNAME_MAX\n"
                    "        case 7: return (long)sysconf(_SC_TZNAME_MAX);\n"
                    "#endif\n"
                    "#ifdef _SC_JOB_CONTROL\n"
                    "        case 8: return (long)sysconf(_SC_JOB_CONTROL);\n"
                    "#endif\n"
                    "#ifdef _SC_SAVED_IDS\n"
                    "        case 9: return (long)sysconf(_SC_SAVED_IDS);\n"
                    "#endif\n"
                    "#ifdef _SC_PAGESIZE\n"
                    "        case 11: return (long)sysconf(_SC_PAGESIZE);\n"
                    "#endif\n"
                    "#ifdef _SC_NPROCESSORS_CONF\n"
                    "        case 12: return "
                    "(long)sysconf(_SC_NPROCESSORS_CONF);\n"
                    "#endif\n"
                    "#ifdef _SC_NPROCESSORS_ONLN\n"
                    "        case 13: return "
                    "(long)sysconf(_SC_NPROCESSORS_ONLN);\n"
                    "#endif\n"
                    "#ifdef _SC_PHYS_PAGES\n"
                    "        case 14: return (long)sysconf(_SC_PHYS_PAGES);\n"
                    "#endif\n"
                    "#ifdef _SC_LINE_MAX\n"
                    "        case 15: return (long)sysconf(_SC_LINE_MAX);\n"
                    "#endif\n"
                    "#ifdef _SC_RE_DUP_MAX\n"
                    "        case 16: return (long)sysconf(_SC_RE_DUP_MAX);\n"
                    "#endif\n"
                    "#ifdef _SC_HOST_NAME_MAX\n"
                    "        case 19: return "
                    "(long)sysconf(_SC_HOST_NAME_MAX);\n"
                    "#endif\n"
                    "#ifdef _SC_LOGIN_NAME_MAX\n"
                    "        case 20: return "
                    "(long)sysconf(_SC_LOGIN_NAME_MAX);\n"
                    "#endif\n"
                    "#ifdef _SC_TTY_NAME_MAX\n"
                    "        case 21: return "
                    "(long)sysconf(_SC_TTY_NAME_MAX);\n"
                    "#endif\n"
                    "#ifdef _SC_SYMLOOP_MAX\n"
                    "        case 22: return (long)sysconf(_SC_SYMLOOP_MAX);\n"
                    "#endif\n"
                    "#ifdef _SC_ATEXIT_MAX\n"
                    "        case 23: return (long)sysconf(_SC_ATEXIT_MAX);\n"
                    "#endif\n"
                    "#ifdef _SC_IOV_MAX\n"
                    "        case 24: return (long)sysconf(_SC_IOV_MAX);\n"
                    "#endif\n"
                    "#ifdef _SC_GETPW_R_SIZE_MAX\n"
                    "        case 25: return "
                    "(long)sysconf(_SC_GETPW_R_SIZE_MAX);\n"
                    "#endif\n"
                    "#ifdef _SC_GETGR_R_SIZE_MAX\n"
                    "        case 26: return "
                    "(long)sysconf(_SC_GETGR_R_SIZE_MAX);\n"
                    "#endif\n"
                    "#ifdef _SC_MONOTONIC_CLOCK\n"
                    "        case 27: return "
                    "(long)sysconf(_SC_MONOTONIC_CLOCK);\n"
                    "#endif\n"
                    "        default: errno = EINVAL; return -1;\n"
                    "    }\n"
                    "}\n");

        // _PC_* -- pathconf()/fpathconf() share the same canonical->host
        // switch body, parameterized only in how the host is asked
        // (path vs. fd), so both wrappers below share one macro-driven
        // list rather than duplicating the 9-way switch twice.
#define CCCC_NATIVE_PATHCONF_CASES(call_expr)                                  \
    "        case 1:\n"                                                        \
    "#ifdef _PC_LINK_MAX\n"                                                    \
    "            return (long)" call_expr ", _PC_LINK_MAX);\n"                 \
    "#else\n"                                                                  \
    "            break;\n"                                                     \
    "#endif\n"                                                                 \
    "        case 2:\n"                                                        \
    "#ifdef _PC_MAX_CANON\n"                                                   \
    "            return (long)" call_expr ", _PC_MAX_CANON);\n"                \
    "#else\n"                                                                  \
    "            break;\n"                                                     \
    "#endif\n"                                                                 \
    "        case 3:\n"                                                        \
    "#ifdef _PC_MAX_INPUT\n"                                                   \
    "            return (long)" call_expr ", _PC_MAX_INPUT);\n"                \
    "#else\n"                                                                  \
    "            break;\n"                                                     \
    "#endif\n"                                                                 \
    "        case 4:\n"                                                        \
    "#ifdef _PC_NAME_MAX\n"                                                    \
    "            return (long)" call_expr ", _PC_NAME_MAX);\n"                 \
    "#else\n"                                                                  \
    "            break;\n"                                                     \
    "#endif\n"                                                                 \
    "        case 5:\n"                                                        \
    "#ifdef _PC_PATH_MAX\n"                                                    \
    "            return (long)" call_expr ", _PC_PATH_MAX);\n"                 \
    "#else\n"                                                                  \
    "            break;\n"                                                     \
    "#endif\n"                                                                 \
    "        case 6:\n"                                                        \
    "#ifdef _PC_PIPE_BUF\n"                                                    \
    "            return (long)" call_expr ", _PC_PIPE_BUF);\n"                 \
    "#else\n"                                                                  \
    "            break;\n"                                                     \
    "#endif\n"                                                                 \
    "        case 7:\n"                                                        \
    "#ifdef _PC_CHOWN_RESTRICTED\n"                                            \
    "            return (long)" call_expr ", _PC_CHOWN_RESTRICTED);\n"         \
    "#else\n"                                                                  \
    "            break;\n"                                                     \
    "#endif\n"                                                                 \
    "        case 8:\n"                                                        \
    "#ifdef _PC_NO_TRUNC\n"                                                    \
    "            return (long)" call_expr ", _PC_NO_TRUNC);\n"                 \
    "#else\n"                                                                  \
    "            break;\n"                                                     \
    "#endif\n"                                                                 \
    "        case 9:\n"                                                        \
    "#ifdef _PC_VDISABLE\n"                                                    \
    "            return (long)" call_expr ", _PC_VDISABLE);\n"                 \
    "#else\n"                                                                  \
    "            break;\n"                                                     \
    "#endif\n"

        if (use_pathconf)
            fprintf(f, "static long __cccc_native_pathconf(const char *path, "
                       "int name) {\n"
                       "    switch (name) {\n" CCCC_NATIVE_PATHCONF_CASES(
                           "pathconf(path") "    }\n"
                                            "    errno = EINVAL;\n"
                                            "    return -1;\n"
                                            "}\n");
        if (use_fpathconf)
            fprintf(f, "static long __cccc_native_fpathconf(int fd, int name) "
                       "{\n"
                       "    switch (name) {\n" CCCC_NATIVE_PATHCONF_CASES(
                           "fpathconf(fd") "    }\n"
                                           "    errno = EINVAL;\n"
                                           "    return -1;\n"
                                           "}\n");
#undef CCCC_NATIVE_PATHCONF_CASES

        if (use_confstr)
            fprintf(f, "static size_t __cccc_native_confstr(int name, char "
                       "*buf, size_t len) {\n"
                       "    switch (name) {\n"
                       "#ifdef _CS_PATH\n"
                       "        case 1: return confstr(_CS_PATH, buf, len);\n"
                       "#endif\n"
                       "        default: errno = EINVAL; return 0;\n"
                       "    }\n"
                       "}\n");
    }

    fprintf(f, "\n");
}

// #1105: dlopen/dlsym/dlclose/dlerror shims that reproduce the VM's own
// dynamic-library registry (cccc_rt_dlopen/dlsym/dlclose/dlerror, src/vm.c)
// instead of forwarding straight to the host libdl. Ported deliberately,
// not incidentally: the VM's dlclose refuses to close a handle with any
// still-"live" dlsym'd symbol (`live_symbol_count > 0`, and that count is
// never decremented -- there is no "un-dlsym" in the VM's own model), which
// a bare host dlclose() does not enforce. Reproducing that refusal natively
// is a deliberate policy choice (ticket #1105, user sign-off) that trades
// away otherwise-valid POSIX code (`h=dlopen(); f=dlsym(h,...);
// dlclose(h);` succeeds on every real libdl but fails here, exactly as it
// already does on the VM) for exact VM-vs-native parity of test_dlfcn
// (tests/suites/test_suite_ffi.c).
//
// Registry design note: a table keyed by the *host* handle would be wrong
// -- dlopen(NULL) returns the very same pointer on every call, while the
// VM mints a fresh token (with its own live-symbol count starting at 0)
// per dlopen (cccc_add_dynamic_library, src/vm.c). Two dlopen(NULL) calls
// in the same process -- exactly what tests/suites/test_suite_ffi.c's
// test_dlfcn/test_dlfcn_close_no_symbols/test_dlfcn_missing do, back to
// back, under --testing=native's single generated harness process -- would
// otherwise share one entry and let an earlier subtest's dlsym poison a
// later, unrelated one's dlclose. So the guest's `void *handle` here is an
// **opaque per-open token** (a registry node's own address), exactly like
// the VM's: it must never be passed to anything but these four functions,
// the same constraint the VM's own token already imposes. Nodes are never
// freed or reused (same as the VM's own dynlibs array, which lives until
// VM teardown) -- unbounded, not gated on a fixed table size.
//
// Thread safety: the VM's registry is entirely GIL-protected; this table
// is not, so every mutation goes through __atomic_* builtins (never
// <stdatomic.h> -- see serialize_threads_shims' own comment on why CCCC's
// copy of that header is unusable in emitted output), the same choice
// __cccc_ensure_mtx/__cccc_ensure_cnd made for the identical reason
// (serialize_threads_shims, this file).
//
// The error slot is `_Thread_local`, which is NOT parity with the VM --
// `vm->dyn_error` is one field shared by every guest thread under the GIL
// -- but a deliberate improvement a native binary can afford now that
// there is no GIL serializing concurrent dl* calls. dlerror() itself does
// NOT clear the slot on read, matching cccc_rt_dlerror (src/vm.c) exactly;
// each of dlopen/dlsym/dlclose clears it on entry instead, also matching
// cccc_clear_dyn_error's own call sites.
//
// `mode` is passed through to the real dlopen() unchanged, matching the
// VM's own behaviour byte-for-byte -- including its documented, unrelated
// bug of assuming glibc's RTLD_LOCAL/RTLD_GLOBAL encoding even on macOS,
// where the real values differ (tracked separately, not fixed here).
void serialize_dlfcn_shims(FILE *f, VirtualMachine *vm, Obj *prog) {
    // #1088-style --emit-cccc exemption: a consumer cccc already has the
    // real DLOPEN/DLSYM/DLCLOSE/DLERROR opcodes, so emitting shim
    // definitions here would shadow them with a second, divergent
    // implementation.
    if (vm->compiler.emit_cccc)
        return;

    bool use_dlopen  = bundled_shim_fn_is_used(vm, prog, "dlopen", "dlfcn.h");
    bool use_dlsym   = bundled_shim_fn_is_used(vm, prog, "dlsym", "dlfcn.h");
    bool use_dlclose = bundled_shim_fn_is_used(vm, prog, "dlclose", "dlfcn.h");
    bool use_dlerror = bundled_shim_fn_is_used(vm, prog, "dlerror", "dlfcn.h");
    bool any_dlfcn   = use_dlopen || use_dlsym || use_dlclose || use_dlerror;
    if (!any_dlfcn)
        return;

    if (use_dlopen)
        rename_bundled_extern_for_native_shim(vm, prog, "dlopen", "dlfcn.h");
    if (use_dlsym)
        rename_bundled_extern_for_native_shim(vm, prog, "dlsym", "dlfcn.h");
    if (use_dlclose)
        rename_bundled_extern_for_native_shim(vm, prog, "dlclose", "dlfcn.h");
    if (use_dlerror)
        rename_bundled_extern_for_native_shim(vm, prog, "dlerror", "dlfcn.h");

    // Self-contained #includes, same rationale as serialize_threads_shims'
    // own comment -- harmless if <dlfcn.h> was already replayed (its own
    // include guard makes a repeat #include a no-op).
    fprintf(f, "#include <dlfcn.h>\n"
               "#include <stdlib.h>\n"
               "struct __cccc_dl_node {\n"
               "    void *handle;\n"
               "    int live;\n"
               "    int closed;\n"
               "    struct __cccc_dl_node *next;\n"
               "};\n"
               "static struct __cccc_dl_node *__cccc_dl_head = 0;\n"
               "static _Thread_local char *__cccc_dl_error = 0;\n"
               // Matches cccc_find_dynamic_library (src/vm.c): a token whose
               // node is marked closed is treated as unknown, same as one
               // that was never registered -- a double dlclose/dlsym on an
               // already-closed handle gets "invalid dynamic library
               // handle", not a use of the freed host handle.
               "static struct __cccc_dl_node *__cccc_dl_find(void *token) {\n"
               "    for (struct __cccc_dl_node *n = "
               "__atomic_load_n(&__cccc_dl_head, __ATOMIC_ACQUIRE); n;\n"
               "         n = n->next)\n"
               "        if ((void *)n == token && "
               "!__atomic_load_n(&n->closed, __ATOMIC_ACQUIRE))\n"
               "            return n;\n"
               "    return 0;\n"
               "}\n"
               "static void __cccc_dl_push(struct __cccc_dl_node *n) {\n"
               "    struct __cccc_dl_node *old_head;\n"
               "    do {\n"
               "        old_head = __atomic_load_n(&__cccc_dl_head, "
               "__ATOMIC_ACQUIRE);\n"
               "        n->next = old_head;\n"
               "    } while (!__atomic_compare_exchange_n(&__cccc_dl_head, "
               "&old_head, n, 0,\n"
               "                                          __ATOMIC_ACQ_REL, "
               "__ATOMIC_ACQUIRE));\n"
               "}\n");

    if (use_dlopen)
        fprintf(f, "static void *__cccc_native_dlopen(const char *path, int "
                   "mode) {\n"
                   "    __cccc_dl_error = 0;\n"
                   "    void *h = dlopen(path, mode ? mode : RTLD_LAZY);\n"
                   "    if (!h) {\n"
                   "        char *err = dlerror();\n"
                   "        __cccc_dl_error = err ? err : \"dlopen failed\";\n"
                   "        return 0;\n"
                   "    }\n"
                   "    struct __cccc_dl_node *n = malloc(sizeof(*n));\n"
                   "    if (!n) {\n"
                   "        __cccc_dl_error = \"dynamic library registry "
                   "allocation failed\";\n"
                   "        return 0;\n"
                   "    }\n"
                   "    n->handle = h;\n"
                   "    n->live = 0;\n"
                   "    n->closed = 0;\n"
                   "    __cccc_dl_push(n);\n"
                   "    return (void *)n;\n"
                   "}\n");
    if (use_dlsym)
        fprintf(
            f, "static void *__cccc_native_dlsym(void *token, const char "
               "*symbol) {\n"
               "    __cccc_dl_error = 0;\n"
               "    if (!symbol) {\n"
               "        __cccc_dl_error = \"dlsym requires a symbol name\";\n"
               "        return 0;\n"
               "    }\n"
               "    struct __cccc_dl_node *n = __cccc_dl_find(token);\n"
               "    if (!n) {\n"
               "        __cccc_dl_error = \"invalid dynamic library handle\";\n"
               "        return 0;\n"
               "    }\n"
               "    dlerror();\n"
               "    void *ptr = dlsym(n->handle, symbol);\n"
               "    char *err = dlerror();\n"
               "    if (err) {\n"
               "        __cccc_dl_error = err;\n"
               "        return 0;\n"
               "    }\n"
               "    __atomic_fetch_add(&n->live, 1, __ATOMIC_ACQ_REL);\n"
               "    return ptr;\n"
               "}\n");
    if (use_dlclose)
        fprintf(f, "static int __cccc_native_dlclose(void *token) {\n"
                   "    __cccc_dl_error = 0;\n"
                   "    struct __cccc_dl_node *n = __cccc_dl_find(token);\n"
                   "    if (!n) {\n"
                   "        __cccc_dl_error = \"invalid dynamic library "
                   "handle\";\n"
                   "        return -1;\n"
                   "    }\n"
                   "    if (__atomic_load_n(&n->live, __ATOMIC_ACQUIRE) > 0) "
                   "{\n"
                   "        __cccc_dl_error = \"cannot dlclose handle with "
                   "live callable symbols\";\n"
                   "        return -1;\n"
                   "    }\n"
                   "    if (dlclose(n->handle) != 0) {\n"
                   "        char *err = dlerror();\n"
                   "        __cccc_dl_error = err ? err : \"dlclose "
                   "failed\";\n"
                   "        return -1;\n"
                   "    }\n"
                   "    __atomic_store_n(&n->closed, 1, __ATOMIC_RELEASE);\n"
                   "    return 0;\n"
                   "}\n");
    if (use_dlerror)
        fprintf(f, "static char *__cccc_native_dlerror(void) {\n"
                   "    return __cccc_dl_error;\n"
                   "}\n");

    fprintf(f, "\n");
}
