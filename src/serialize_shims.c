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

// -c=native support-shim text: one `static const char
// CCCC_SHIM_<group>_<name>[]` per chunk, generated from src/shims/*.c by
// tools/gen_shims.py. Gating and rationale for each shim stay here, next to the
// code that selects it.
#include "shims.inc"

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
    {"__cccc_stdin", CCCC_SHIM_native_accessor___cccc_stdin},
    {"__cccc_stdout", CCCC_SHIM_native_accessor___cccc_stdout},
    {"__cccc_stderr", CCCC_SHIM_native_accessor___cccc_stderr},
    {"__cccc_errno_ptr", CCCC_SHIM_native_accessor___cccc_errno_ptr},
    {"__cccc_optarg_ptr", CCCC_SHIM_native_accessor___cccc_optarg_ptr},
    {"__cccc_optind_ptr", CCCC_SHIM_native_accessor___cccc_optind_ptr},
    {"__cccc_opterr_ptr", CCCC_SHIM_native_accessor___cccc_opterr_ptr},
    {"__cccc_optopt_ptr", CCCC_SHIM_native_accessor___cccc_optopt_ptr},
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
    {"__cccc_isnan_f", CCCC_SHIM_native_accessor___cccc_isnan_f},
    {"__cccc_isnan_d", CCCC_SHIM_native_accessor___cccc_isnan_d},
    {"__cccc_isinf_f", CCCC_SHIM_native_accessor___cccc_isinf_f},
    {"__cccc_isinf_d", CCCC_SHIM_native_accessor___cccc_isinf_d},
    {"__cccc_signbit_f", CCCC_SHIM_native_accessor___cccc_signbit_f},
    {"__cccc_signbit_d", CCCC_SHIM_native_accessor___cccc_signbit_d},
    {"__cccc_fpclassify_f", CCCC_SHIM_native_accessor___cccc_fpclassify_f},
    {"__cccc_fpclassify_d", CCCC_SHIM_native_accessor___cccc_fpclassify_d},
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
    {"__cccc_flt_rounds", CCCC_SHIM_native_accessor___cccc_flt_rounds},
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
    {"__cccc_issignaling_f", CCCC_SHIM_native_accessor___cccc_issignaling_f},
    {"__cccc_issignaling_d", CCCC_SHIM_native_accessor___cccc_issignaling_d},
    {"__cccc_iseqsig_f", CCCC_SHIM_native_accessor___cccc_iseqsig_f},
    {"__cccc_iseqsig_d", CCCC_SHIM_native_accessor___cccc_iseqsig_d},
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
     CCCC_SHIM_native_accessor___cccc_mb_cur_max__linux
#else
     CCCC_SHIM_native_accessor___cccc_mb_cur_max__other
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
    {"__cccc_environ_ptr", CCCC_SHIM_native_accessor___cccc_environ_ptr},
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

// #1155: not part of native_accessor_shims above because that table's own
// matching loop looks for a used Obj sharing the *shim's* name -- every
// other entry there is reached through a macro expansion that makes that
// true (e.g. `stdout` expands to a call to `__cccc_stdout`), but a guest
// call to `reallocarray` reaches this shim through serialize_expr.c's
// ND_FUNCALL remap (mirroring the setjmp/longjmp remap immediately above
// it) instead, so the Obj that shows up as used is still named
// "reallocarray", not "__cccc_reallocarray". reallocarray is declared by
// CCCC's own bundled include/stdlib.h (routed through the VM heap's
// overflow-checked REALCA opcode -- see codegen_expr.c/ops.c) but has no
// definition on a host libc that lacks it (this SDK's macOS does not), and
// since the guest's own `#include <stdlib.h>` is captured and replayed,
// #901's bodiless-prototype pass correctly declines to re-derive a
// declaration for it (that header genuinely is in scope) -- leaving the
// emitted call entirely undeclared without this shim. The overflow check
// mirrors the VM-side cccc_reallocarray polyfill (src/stdlib/stdlib.c) so
// both backends agree on the ENOMEM-without-touching-ptr contract real
// reallocarray(3) guarantees.
void serialize_reallocarray_shim(FILE *f, Obj *prog) {
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (!obj->is_function || !obj->is_used)
            continue;
        if (strcmp(obj->name, "reallocarray") != 0)
            continue;
        // #include <errno.h> here rather than relying on the guest's own
        // #include being replayed: reallocarray's failure contract
        // (ENOMEM, ptr untouched) is part of *this shim's* semantics, not
        // the guest source's, so it must hold regardless of what the guest
        // itself included.
        fprintf(f, "%s\n", CCCC_SHIM_reallocarray_body);
        return;
    }
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

    // #1184-adjacent: the guest-visible name "call_once" is macro-aliased
    // to "__cccc_call_once" by include/threads.h, expanded away before the
    // AST is built -- the Obj this program actually parsed is already
    // named __cccc_call_once, so that's what shim_fn_is_used must match.
    bool use_call_once =
        shim_fn_is_used(vm, prog, "__cccc_call_once", "threads.h");

    if (!any_thrd && !any_mtx && !any_cnd && !any_tss && !use_call_once)
        return;

    // Self-contained #includes rather than trusting the nested-include
    // capture, following the __cccc_iseqsig_* precedent above -- harmless if
    // repeated thanks to each header's own include guard.
    fprintf(f, "%s", CCCC_SHIM_threads_includes);
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
        fprintf(f, "%s", CCCC_SHIM_threads_thrd_trampoline);
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
        fprintf(f, "%s", CCCC_SHIM_threads_ensure_mtx);
    }
    if (any_cnd) {
        // Port of ensure_cond (src/stdlib/pthread.c:463-478); same
        // atomic-compare-exchange race closure as __cccc_ensure_mtx above.
        fprintf(f, "%s", CCCC_SHIM_threads_ensure_cnd);
    }

    // ---- Thread lifecycle (port of pthread.c:923-981) ----
    if (use_thrd_create)
        fprintf(f, "%s", CCCC_SHIM_threads_thrd_create);
    if (use_thrd_join)
        fprintf(f, "%s", CCCC_SHIM_threads_thrd_join);
    if (use_thrd_exit)
        // Matches thrd_create's trampoline encoding: returning `rc` from the
        // thread function is equivalent (POSIX) to pthread_exit() with that
        // same value, so thrd_join's narrowing agrees regardless of which
        // path a thread actually exits through.
        fprintf(f, "%s", CCCC_SHIM_threads_thrd_exit);
    if (use_thrd_detach)
        fprintf(f, "%s", CCCC_SHIM_threads_thrd_detach);
    if (use_thrd_yield)
        fprintf(f, "%s", CCCC_SHIM_threads_thrd_yield);
    if (use_thrd_sleep)
        fprintf(f, "%s", CCCC_SHIM_threads_thrd_sleep);
    if (use_thrd_current)
        fprintf(f, "%s", CCCC_SHIM_threads_thrd_current);
    if (use_thrd_equal)
        fprintf(f, "%s", CCCC_SHIM_threads_thrd_equal);

    // ---- Mutex (port of pthread.c:1016-1130) ----
    if (use_mtx_init)
        fprintf(f, "%s", CCCC_SHIM_threads_mtx_init);
    if (use_mtx_lock)
        fprintf(f, "%s", CCCC_SHIM_threads_mtx_lock);
    if (use_mtx_trylock)
        fprintf(f, "%s", CCCC_SHIM_threads_mtx_trylock);
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
        fprintf(f, "%s", CCCC_SHIM_threads_mtx_timedlock);
    if (use_mtx_unlock)
        fprintf(f, "%s", CCCC_SHIM_threads_mtx_unlock);
    if (use_mtx_destroy)
        fprintf(f, "%s", CCCC_SHIM_threads_mtx_destroy);

    // ---- Condition variable (port of pthread.c:1132-1178) ----
    if (use_cnd_init)
        fprintf(f, "%s", CCCC_SHIM_threads_cnd_init);
    if (use_cnd_wait)
        fprintf(f, "%s", CCCC_SHIM_threads_cnd_wait);
    if (use_cnd_signal)
        fprintf(f, "%s", CCCC_SHIM_threads_cnd_signal);
    if (use_cnd_broadcast)
        fprintf(f, "%s", CCCC_SHIM_threads_cnd_broadcast);
    if (use_cnd_timedwait)
        fprintf(f, "%s", CCCC_SHIM_threads_cnd_timedwait);
    if (use_cnd_destroy)
        fprintf(f, "%s", CCCC_SHIM_threads_cnd_destroy);

    // ---- Thread-specific storage (port of pthread.c:1180-1195) ----
    // tss_t is re-derived as a plain alias of the host's own pthread_key_t
    // (include/threads.h: `typedef pthread_key_t tss_t;`, and pthread_key_t
    // itself comes from the replayed real <pthread.h>) and tss_dtor_t
    // (`void (*)(void *)`) already matches pthread's own destructor
    // signature exactly -- so these forward straight through, no adapter
    // needed.
    if (use_tss_create)
        fprintf(f, "%s", CCCC_SHIM_threads_tss_create);
    if (use_tss_get)
        fprintf(f, "%s", CCCC_SHIM_threads_tss_get);
    if (use_tss_set)
        fprintf(f, "%s", CCCC_SHIM_threads_tss_set);
    if (use_tss_delete)
        fprintf(f, "%s", CCCC_SHIM_threads_tss_delete);

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
        fprintf(f, "%s", CCCC_SHIM_threads_call_once);

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

    fprintf(f, "%s", CCCC_SHIM_uchar_includes);

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
        fprintf(f, "%s", CCCC_SHIM_uchar_guard_16_32_open);
        if (use_mbrtoc16)
            fprintf(f, "%s", CCCC_SHIM_uchar_mbrtoc16);
        if (use_c16rtomb)
            fprintf(f, "%s", CCCC_SHIM_uchar_c16rtomb);
        if (use_mbrtoc32)
            fprintf(f, "%s", CCCC_SHIM_uchar_mbrtoc32);
        if (use_c32rtomb)
            fprintf(f, "%s", CCCC_SHIM_uchar_c32rtomb);
        fprintf(f, "#endif\n");
    }

    if (any8) {
        // Same nested-#if reasoning as the c16/c32 block above.
        fprintf(f, "%s", CCCC_SHIM_uchar_guard_8_open);
        fprintf(f, "%s", CCCC_SHIM_uchar_utf8_helpers);
        if (use_mbrtoc8)
            fprintf(f, "%s", CCCC_SHIM_uchar_mbrtoc8);
        if (use_c8rtomb)
            fprintf(f, "%s", CCCC_SHIM_uchar_c8rtomb);
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
        fprintf(f, "%s", CCCC_SHIM_posix_compat_in6_pktinfo);

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
            fprintf(f, "%s", CCCC_SHIM_posix_compat_aio_fsync);
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

    fprintf(f, "%s", CCCC_SHIM_posix_compat_includes);
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
        fprintf(f, "%s", CCCC_SHIM_posix_compat_poll_marshal);
        if (use_poll)
            fprintf(f, "%s", CCCC_SHIM_posix_compat_poll);
    }

    // ppoll() (#821/#1140/#1146) -- pthread_sigmask()+poll() emulation,
    // ported from ppoll_emulate_macos/wrap_ppoll_gil
    // (src/stdlib/posix_poll.c). Not atomic like the real syscall -- a
    // signal delivered between the mask swap and poll()'s wait is not
    // guaranteed to interrupt it -- exactly the same accepted, documented
    // limitation as the VM's own emulation (see man/NATIVE.md's
    // <poll.h> entry). Unlike before #1146, this now DOES translate
    // pollfd.events/revents through the same
    // __cccc_native_poll_marshal_in/out helpers plain poll() uses just
    // above -- the two are now consistent with each other in the same
    // binary, closing the very residual this comment used to describe.
    if (use_ppoll)
        fprintf(f, "%s", CCCC_SHIM_posix_compat_ppoll);
    // #1145: on Linux, ppoll() genuinely is a host primitive (no emulation
    // needed, matching the #if !defined(__linux__) branch above) -- but
    // it's a glibc extension gated behind __USE_GNU, which the replayed
    // `#include <poll.h>` above only exposes under _GNU_SOURCE, which this
    // generated TU never defines. Forward-declared locally instead, ported
    // verbatim from wrap_ppoll_gil's identical comment/declaration
    // (src/stdlib/posix_poll.c) -- glibc still exports the real symbol
    // regardless of the declaration being visible.
    if (use_ppoll)
        fprintf(f, "%s", CCCC_SHIM_posix_compat_ppoll_linux_decl);

    // sched_setparam/getparam/setscheduler/getscheduler/rr_get_interval
    // (#824/#1140) -- macOS has no process-scheduling API at all, so the
    // VM's own non-Linux wrap_sched_* family (src/stdlib/posix_sched.c)
    // always returns ENOSYS; ported verbatim.
    if (any_sched) {
        if (use_sched_setparam)
            fprintf(f, "%s", CCCC_SHIM_posix_compat_sched_setparam);
        if (use_sched_getparam)
            fprintf(f, "%s", CCCC_SHIM_posix_compat_sched_getparam);
        if (use_sched_setscheduler)
            fprintf(f, "%s", CCCC_SHIM_posix_compat_sched_setscheduler);
        if (use_sched_getscheduler)
            fprintf(f, "%s", CCCC_SHIM_posix_compat_sched_getscheduler);
        if (use_sched_rr_get_interval)
            fprintf(f, "%s", CCCC_SHIM_posix_compat_sched_rr_get_interval);
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
        fprintf(f, "%s", CCCC_SHIM_posix_compat_nss_helpers);

        if (use_gethostbyname_r)
            fprintf(f, "%s", CCCC_SHIM_posix_compat_gethostbyname_r);

        if (use_gethostbyaddr_r)
            fprintf(f, "%s", CCCC_SHIM_posix_compat_gethostbyaddr_r);

        if (use_getnetbyname_r)
            fprintf(f, "%s", CCCC_SHIM_posix_compat_getnetbyname_r);
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
            fprintf(f, "%s", CCCC_SHIM_posix_compat_gethostbyname);
        if (use_gethostbyaddr)
            fprintf(f, "%s", CCCC_SHIM_posix_compat_gethostbyaddr);
        if (use_getnetbyname)
            fprintf(f, "%s", CCCC_SHIM_posix_compat_getnetbyname);
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

        fprintf(f, "%s", CCCC_SHIM_canonical_const_nl_item_xlate);
        if (use_nl_langinfo)
            fprintf(f, "%s", CCCC_SHIM_canonical_const_nl_langinfo);
        if (use_nl_langinfo_l)
            fprintf(f, "%s", CCCC_SHIM_canonical_const_nl_langinfo_l);
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
            fprintf(f, "%s", CCCC_SHIM_canonical_const_setlocale);
        if (use_newlocale)
            fprintf(f, "%s", CCCC_SHIM_canonical_const_newlocale);
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
        fprintf(f, "%s", CCCC_SHIM_canonical_const_sched_batch_idle_defs);
        fprintf(f, "%s", CCCC_SHIM_canonical_const_sched_policy_xlate);
        if (use_sched_get_priority_min)
            fprintf(f, "%s", CCCC_SHIM_canonical_const_sched_get_priority_min);
        if (use_sched_get_priority_max)
            fprintf(f, "%s", CCCC_SHIM_canonical_const_sched_get_priority_max);
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
            fprintf(f, "%s", CCCC_SHIM_canonical_const_sysconf);

        // _PC_* -- pathconf() and fpathconf() share one 9-way canonical->host
        // _PC_* switch, differing only in how the host is asked (path vs fd).
        // src/shims/canonical_const.c holds that switch inlined in both
        // sections; tools/gen_shims.py's pathconf_drift_check() fails the build
        // if their _PC_* sets or case counts diverge.
        if (use_pathconf)
            fprintf(f, "%s", CCCC_SHIM_canonical_const_pathconf);
        if (use_fpathconf)
            fprintf(f, "%s", CCCC_SHIM_canonical_const_fpathconf);

        if (use_confstr)
            fprintf(f, "%s", CCCC_SHIM_canonical_const_confstr);
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
// VM's own behaviour byte-for-byte. This is correct on every platform:
// the guest's RTLD_* macros (include/dlfcn.h) are derived from the real
// host <dlfcn.h> this cccc binary was built against (init_dlfcn_macros(),
// src/preprocess.c), not hand-transcribed, so `mode` already carries
// values the host's own dlopen() understands (#1152).
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
    fprintf(f, "%s", CCCC_SHIM_dlfcn_registry);

    if (use_dlopen)
        fprintf(f, "%s", CCCC_SHIM_dlfcn_dlopen);
    if (use_dlsym)
        fprintf(f, "%s", CCCC_SHIM_dlfcn_dlsym);
    if (use_dlclose)
        fprintf(f, "%s", CCCC_SHIM_dlfcn_dlclose);
    if (use_dlerror)
        fprintf(f, "%s", CCCC_SHIM_dlfcn_dlerror);

    fprintf(f, "\n");
}

// #1195: real definitions for the C23 IEC 60559 fromfp/ufromfp/fromfpx/
// ufromfpx family (int-rounding functions, C23 7.12.9.6-9) under
// -c=native -- CCCC's bundled include/math.h (forced into scope for every
// -c=native/-m TU by the #1143 math.h/float.h substitution,
// serialize_program.c) declares these with the signature `intmax_t
// fromfp(double, int, unsigned int)`, matching the historical TS 18661-1 /
// glibc <= 2.42 entry point (`fromfp@GLIBC_2.25`). glibc 2.43 changed
// what the plain, undecorated `fromfp` symbol resolves to: the new
// default (`fromfp@@GLIBC_2.43`) is the C23-standardized signature, which
// instead RETURNS double. Both entry points still exist side by side
// (confirmed via `nm -D libm.so.6`), so this is neither a link failure
// nor a compile failure -- a call written against CCCC's own
// (old-signature) declaration silently reads its return value out of the
// wrong ABI register (an integer callee return in rax vs a
// floating-point return in xmm0), i.e. garbage (#1195, confirmed
// directly in the cccc-linux-amd64 Colima container against real Ubuntu
// 26.04/glibc 2.43). Darwin's libm exports none of this family at all,
// unconditionally -- the permanent #1037 gap this shim does not attempt
// to close (see NATIVE_SKIP_TESTS_MACOS, tools/testing/__init__.py); only
// #1195's own actual failure (fromfp/ufromfp/fromfpx/ufromfpx, the one
// sub-family with a Linux round-trip bug, not the wider "Darwin has none
// of this at all" gap) is shimmed here.
//
// Fixed with a self-contained shim, near-verbatim ports of the VM's own
// cccc_fromfp_impl/cccc_fromfp*/cccc_ufromfp*/cccc_fromfpx*/
// cccc_ufromfpx* (src/stdlib/math.c) -- this never calls the host's own
// fromfp* at all, so it is immune to the glibc version split entirely
// (and would work identically on a host with no fromfp* symbol
// whatsoever). Renamed to __cccc_native_<name> via
// rename_bundled_extern_for_native_shim so every guest call site routes
// here instead of the host's own (whichever ABI it happens to resolve
// to) -- same shape as serialize_dlfcn_shims'/serialize_posix_compat_
// shims' dlopen/aio_fsync shims just above. `l` (long double) variants
// narrow to double like the VM's own (#491: CCCC's guest long double is
// modeled as an 8-byte double, so this only reproduces the VM's own
// existing precision contract, not a new loss).
void serialize_c23_fromfp_shims(FILE *f, VirtualMachine *vm, Obj *prog) {
    if (vm->compiler.emit_cccc)
        return;

    bool use_fromfp   = bundled_shim_fn_is_used(vm, prog, "fromfp", "math.h");
    bool use_fromfpf  = bundled_shim_fn_is_used(vm, prog, "fromfpf", "math.h");
    bool use_fromfpl  = bundled_shim_fn_is_used(vm, prog, "fromfpl", "math.h");
    bool use_ufromfp  = bundled_shim_fn_is_used(vm, prog, "ufromfp", "math.h");
    bool use_ufromfpf = bundled_shim_fn_is_used(vm, prog, "ufromfpf", "math.h");
    bool use_ufromfpl = bundled_shim_fn_is_used(vm, prog, "ufromfpl", "math.h");
    bool use_fromfpx  = bundled_shim_fn_is_used(vm, prog, "fromfpx", "math.h");
    bool use_fromfpxf = bundled_shim_fn_is_used(vm, prog, "fromfpxf", "math.h");
    bool use_fromfpxl = bundled_shim_fn_is_used(vm, prog, "fromfpxl", "math.h");
    bool use_ufromfpx = bundled_shim_fn_is_used(vm, prog, "ufromfpx", "math.h");
    bool use_ufromfpxf =
        bundled_shim_fn_is_used(vm, prog, "ufromfpxf", "math.h");
    bool use_ufromfpxl =
        bundled_shim_fn_is_used(vm, prog, "ufromfpxl", "math.h");
    bool any = use_fromfp || use_fromfpf || use_fromfpl || use_ufromfp ||
               use_ufromfpf || use_ufromfpl || use_fromfpx || use_fromfpxf ||
               use_fromfpxl || use_ufromfpx || use_ufromfpxf || use_ufromfpxl;
    if (!any)
        return;

    if (use_fromfp)
        rename_bundled_extern_for_native_shim(vm, prog, "fromfp", "math.h");
    if (use_fromfpf)
        rename_bundled_extern_for_native_shim(vm, prog, "fromfpf", "math.h");
    if (use_fromfpl)
        rename_bundled_extern_for_native_shim(vm, prog, "fromfpl", "math.h");
    if (use_ufromfp)
        rename_bundled_extern_for_native_shim(vm, prog, "ufromfp", "math.h");
    if (use_ufromfpf)
        rename_bundled_extern_for_native_shim(vm, prog, "ufromfpf", "math.h");
    if (use_ufromfpl)
        rename_bundled_extern_for_native_shim(vm, prog, "ufromfpl", "math.h");
    if (use_fromfpx)
        rename_bundled_extern_for_native_shim(vm, prog, "fromfpx", "math.h");
    if (use_fromfpxf)
        rename_bundled_extern_for_native_shim(vm, prog, "fromfpxf", "math.h");
    if (use_fromfpxl)
        rename_bundled_extern_for_native_shim(vm, prog, "fromfpxl", "math.h");
    if (use_ufromfpx)
        rename_bundled_extern_for_native_shim(vm, prog, "ufromfpx", "math.h");
    if (use_ufromfpxf)
        rename_bundled_extern_for_native_shim(vm, prog, "ufromfpxf", "math.h");
    if (use_ufromfpxl)
        rename_bundled_extern_for_native_shim(vm, prog, "ufromfpxl", "math.h");

    // Deliberately does NOT `#include <math.h>` itself, unlike
    // serialize_threads_shims'/serialize_dlfcn_shims' own self-contained
    // #includes -- ceil/floor/trunc/round/rint/ldexp are already in scope
    // by construction (fromfp/ufromfp/etc can only be *used* here if the
    // guest itself declared them, which only exists in CCCC's own bundled
    // math.h, forced into scope ahead of this shim by the #1143
    // substitution in the include-replay loop above). A bare `#include
    // <math.h>` here would resolve differently: unlike a CAPTURED
    // `#include <math.h>` line (rewritten to an absolute quoted include of
    // CCCC's own copy by that same #1143 substitution), a literal
    // `#include <math.h>` in emitted shim text is angle-bracket,
    // unsubstituted, ordinary host header search -- and CCCC's bundled
    // include dir is demoted to `-idirafter` (#1143), so it resolves to
    // the REAL host math.h instead, which reintroduces the exact
    // guard-name mismatch #1143's own comment (further up this file)
    // documents for math.h/tgmath.h -- confirmed directly: this doubled
    // math.h include broke both the FP_INFINITE enum (CCCC's own #define
    // colliding with the real header's enumerator of the same name) and
    // fromfp's own declaration (a second, real-glibc-signature
    // declaration conflicting with CCCC's) in the cccc-linux-amd64
    // container. <fenv.h>/<stdint.h> have no such substitution and are
    // safe to include normally -- harmless if already replayed (include
    // guards make a repeat #include a no-op).
    //
    // Also deliberately does NOT write the plain `isnan`/`isinf` names --
    // #1021's own comment just above (native_accessor_shims) explains why:
    // once CCCC's math.h is in scope, those are `_Generic`-dispatching
    // macros to __cccc_isnan_f/_d (accessor shims gated on a GUEST
    // reference to that exact name, per native_accessor_shims' own
    // matching loop -- this shim body's internal use doesn't create one,
    // so the helper the macro expands to is never actually emitted,
    // confirmed directly: "undeclared identifier '__cccc_isnan_f'" in the
    // cccc-linux-amd64 container). __builtin_isnan/__builtin_isinf are the
    // same portable clang/gcc intrinsics #1021's shims themselves use, no
    // such indirection.
    fprintf(f, "%s", CCCC_SHIM_c23_fromfp_impl);

    if (use_fromfp)
        fprintf(f, "%s", CCCC_SHIM_c23_fromfp_fromfp);
    if (use_ufromfp)
        fprintf(f, "%s", CCCC_SHIM_c23_fromfp_ufromfp);
    if (use_fromfpx)
        fprintf(f, "%s", CCCC_SHIM_c23_fromfp_fromfpx);
    if (use_ufromfpx)
        fprintf(f, "%s", CCCC_SHIM_c23_fromfp_ufromfpx);

    if (use_fromfpf)
        fprintf(f, "%s", CCCC_SHIM_c23_fromfp_fromfpf);
    if (use_ufromfpf)
        fprintf(f, "%s", CCCC_SHIM_c23_fromfp_ufromfpf);
    if (use_fromfpxf)
        fprintf(f, "%s", CCCC_SHIM_c23_fromfp_fromfpxf);
    if (use_ufromfpxf)
        fprintf(f, "%s", CCCC_SHIM_c23_fromfp_ufromfpxf);

    if (use_fromfpl)
        fprintf(f, "%s", CCCC_SHIM_c23_fromfp_fromfpl);
    if (use_ufromfpl)
        fprintf(f, "%s", CCCC_SHIM_c23_fromfp_ufromfpl);
    if (use_fromfpxl)
        fprintf(f, "%s", CCCC_SHIM_c23_fromfp_fromfpxl);
    if (use_ufromfpxl)
        fprintf(f, "%s", CCCC_SHIM_c23_fromfp_ufromfpxl);

    fprintf(f, "\n");
}
