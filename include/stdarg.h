/*
 * stdarg.h - Variable Argument Lists
 * CCCC C Compiler - Standard C Library Header
 *
 * Implements C99/C11 variable argument list support for the CCCC VM.
 *
 * Stack Layout for Variadic Functions (after ENT3):
 *   Stack Args      (call args 8+, caller's frame)
 *   [arg9]          ← bp[+3]
 *   [arg8]          ← bp[+2]   (first stack-passed arg)
 *   [ret_addr]      ← bp[+1]
 *   [old_bp]        ← bp[+0]   (base pointer points here)
 *   ---- callee frame below ----
 *   [param0]        ← bp[-1]   (spilled from REG_A0)
 *   [param1]        ← bp[-2]   (spilled from REG_A1)
 *   ...
 *   [param7]        ← bp[-8]   (spilled from REG_A7, last register arg)
 *   [locals...]     ← bp[-9] and below
 *
 * Register-based calling: first 8 args in REG_A0-A7, spilled by ENT3.
 * Args 9+ are pushed to stack before CALL. ENT3 copies fixed stack-passed
 * parameters into callee local slots; va_arg reads the remaining variadic tail
 * from the caller's stack area.
 *
 * va_list is a struct that tracks position in both regions and knows
 * when to switch from the register-spill area to the stack args area.
 */

/* #1040 follow-on: this header is also what a real host cc resolves */
/* `#include <stdarg.h>` to while compiling under --build (push_compile_flags */
/* forwards cccc's own -I./include to every target's real compile command) */
/* or while re-lexing a -c=native/-c=generated serializer replay -- same */
/* header-shadow trap as include/stdio.h/getopt.h/stdint.h (#1040), just not */
/* noticed there because it doesn't collide on its own; it only surfaces */
/* transitively: glibc's own <stdio.h> does `#include <stdarg.h>` to pick up */
/* __gnuc_va_list, and since -I./include is searched first, it got this */
/* file's CCCC-only struct va_list instead, which doesn't define */
/* __gnuc_va_list -- "unknown type name '__gnuc_va_list'" throughout glibc's */
/* stdio.h the moment any real project source under --build includes */
/* <stdio.h> (confirmed: examples/build_demo/src/greet.c). Guarded the same */
/* way, handing off to the host's own <stdarg.h> via #include_next whenever */
/* a genuine host compiler (not CCCC's own preprocessor, which always */
/* defines __CCCC__ before any header is read) is the one reading this file. */
/* */
/* #1018 follow-up: the guard macro this comment originally settled on, */
/* `__STDARG_H` (changed from `_STDARG_H` specifically to avoid colliding */
/* with GCC's own single-underscore guard -- see the paragraph below, */
/* preserved for history), turned out to *itself* collide with clang's own */
/* real <stdarg.h>, which also guards under the exact double-underscore */
/* spelling `__STDARG_H` (confirmed directly in the cccc-linux-amd64 */
/* container, clang 18: `grep __STDARG_H */
/* $(clang -print-resource-dir)/include/stdarg.h`). This was invisible */
/* until #1018 actually made a variadic function *definition* compile */
/* natively at all -- before that, every -c=native build of a variadic */
/* function failed on the VM-ABI leak long before reaching this header's */
/* own #include_next hand-off, so the collision never got exercised. Same */
/* failure shape as the GCC collision below (guard already defined -> real */
/* header's body entirely skipped -> `va_list` left undeclared), just a */
/* different host compiler. Renamed the guard to `__CCCC_STDARG_H`, a */
/* spelling no real host's own <stdarg.h> is likely to also pick (verified */
/* against both glibc's `_STDARG_H` and clang's `__STDARG_H` directly, not */
/* just by absence of a hit) -- rather than chasing a third guard rename if */
/* some future host also happens to collide. */
/* */
/* The guard macro itself had to change too, from _STDARG_H to __STDARG_H */
/* (same double-underscore convention include/stdio.h/getopt.h/stdint.h */
/* already use): GCC's own real <stdarg.h> guards its body with the exact */
/* same single-underscore `_STDARG_H` name. #include_next still correctly */
/* resolves to that real file, but its own `#ifndef _STDARG_H` then sees */
/* the guard our file already defined and skips its entire body, leaving */
/* __gnuc_va_list undefined regardless of the #include_next hand-off -- */
/* confirmed against sr.ht's actual runner (gcc-15 as /usr/bin/cc; the */
/* local container repro used clang, whose <stdarg.h> guards under a */
/* different name and didn't expose this -- that "different name" claim */
/* itself turned out to be clang-version-dependent, see above). */
/* */
/* #1018 follow-up 2: the outer guard (whatever it's spelled) must NOT */
/* wrap the `#include_next` hand-off below -- confirmed directly (cccc- */
/* linux-amd64 container) with a program that includes <stdio.h> before */
/* <stdarg.h>: glibc's real <stdio.h> issues its own PARTIAL stdarg.h */
/* request first (`#define __need___va_list` then `#include <stdarg.h>`, */
/* glibc's standard idiom for wanting only __gnuc_va_list, not the full */
/* va_start/va_arg machinery) to pick up __gnuc_va_list for its own */
/* prototypes. Clang's real <stdarg.h> handles a partial request */
/* correctly -- it does NOT set its own `__STDARG_H` guard on a */
/* __need_*-restricted pass, specifically so a later *full* `#include */
/* <stdarg.h>` (this program's own explicit one, or any other TU-level */
/* one) still runs to completion. But when the outer guard here wrapped */
/* the whole file including this #include_next, CCCC's own guard was */
/* already permanently set by that FIRST partial pass -- so the later, */
/* full `#include <stdarg.h>` line hit `#ifndef <guard>` already false and */
/* skipped the #include_next entirely, leaving va_start/va_arg/va_end */
/* never macro-defined at all ("call to undeclared library function */
/* 'va_start'"). Fixed by moving the guard to wrap only the `#ifdef */
/* __CCCC__` branch's own body (CCCC's own struct/macros only need */
/* defining once per TU, ordinary header-guard reasoning) and leaving the */
/* `#else` branch's `#include_next <stdarg.h>` unconditional -- every */
/* #include <stdarg.h>, partial or full, now always reaches the real host */
/* header, which already has its own correct guard/partial-request logic */
/* designed for exactly this repeated-inclusion pattern. */
#ifdef __CCCC__
#ifndef __CCCC_STDARG_H
#define __CCCC_STDARG_H

/*
 * va_list type: struct tracking position in two memory regions
 * - reg_ptr: current position in spilled register area (bp[-1...-8])
 * - stack_ptr: current position in stack args area (bp[+2...])
 * - reg_count: number of register-spill slots remaining before switching
 *
 * #1059: the real fields above total 20 bytes (24 with reg_count's
 * trailing pad). -c=native folds sizeof(va_list)/offsetof(...) against
 * *this* layout at guest compile time, but replays the user's own
 * `#include <stdarg.h>` verbatim, which resolves to the real host's own
 * <stdarg.h> at native-compile time (see the #include_next guard below) --
 * same soundness class as #1054's jmp_buf. Measured the real host size
 * directly on every supported platform x arch combo (not recalled): macOS
 * arm64 8 bytes (a bare `char *`), macOS x86_64 24, glibc x86_64 32, glibc
 * aarch64 32 (both glibc targets: an array of one `struct __va_list_tag`).
 * `__reserved` pads this struct to 64 bytes so any guest-folded
 * sizeof/offsetof over-allocates real storage on every one of them,
 * mirroring #1054's jmp_buf widening rather than diagnosing the fold.
 */
struct __cccc_va_list {
    char *reg_ptr;        /* Current position in register spill area */
    char *stack_ptr;      /* Current position in stack overflow area */
    int   reg_count;      /* Remaining slots in register spill area */
    char  __reserved[40]; /* #1059: headroom for the real host va_list size */
};
typedef struct __cccc_va_list va_list;

/*
 * va_start(ap, last) / va_start(ap) - Initialize va_list
 * @ap:   va_list to initialize
 * @last: name of the last fixed parameter before '...'; optional in C23 and
 *        never evaluated. C23 ignores any further arguments.
 *
 * Computes:
 * - reg_ptr: address of the first variadic arg's register-spill slot
 * - stack_ptr: address of the first stack variadic arg
 * - reg_count: how many varargs fit in the remaining register spill slots
 *
 * Stack geometry: ENT3 spills the 8 argument registers to bp[-1-canary]
 * downward, so named parameter i lives at bp[-(i+1)-canary] and the first
 * variadic one at bp[-(N+1)-canary] for N named parameters. Params past the
 * eighth are copied into local slots by ENT3; the first stack variadic arg
 * follows them in the caller's frame.
 *
 * N comes from __cccc_va_named_count(), which codegen replaces with the length
 * of the enclosing function's parameter list (the list ENT3 spills), so the
 * geometry never depends on where `last` itself ended up.
 * __builtin_frame_address compiles to LEA 0, which gives bp. Under
 * --stack-canaries, ENT3 reserves bp[-1] for the canary and shifts everything
 * one slot lower (#445);
 * __CCCC_STACK_CANARIES__ is 1 when canaries are on, else 0.
 *
 * -c=native: __cccc_va_start() (src/parse_postfix.c) wraps this expansion as
 * its trailing argument and stamps `ap`/`last` onto it as serializer
 * annotation (Node.va_form, src/cccc.h). VM codegen sees only this expansion;
 * the serializer (serialize_stmt, src/serialize.c) prints the real host
 * `va_start(ap, last)` instead of walking it.
 */
#define __CCCC_VA_START_IMPL(ap)                                                 \
    ({                                                                           \
        long long *__bp    = (long long *)__builtin_frame_address(0);            \
        int        __named = __cccc_va_named_count();                            \
        (ap).reg_ptr   = (char *)(__bp - __named - 1 - __CCCC_STACK_CANARIES__); \
        (ap).reg_count = __named < 8 ? 8 - __named : 0;                          \
        (ap).stack_ptr = (char *)(__bp + 2 + (__named > 8 ? __named - 8 : 0));   \
        (void)0;                                                                 \
    })

#if __STDC_VERSION__ >= 202311L
/* C23: arguments after the first one following `ap` are ignored. */
#define __CCCC_VA_FIRST(last, ...) last
#define va_start(ap, ...)                                                      \
    __cccc_va_start(ap __VA_OPT__(, __CCCC_VA_FIRST(__VA_ARGS__)),             \
                    __CCCC_VA_START_IMPL(ap))
#else
#define va_start(ap, last) __cccc_va_start(ap, last, __CCCC_VA_START_IMPL(ap))
#endif

/*
 * va_arg(ap, type) - Retrieve next argument
 * @ap:   va_list to read from
 * @type: type of the argument to retrieve
 *
 * If reg_count > 0: read from reg_ptr, decrement both reg_ptr and reg_count
 * Else: read from stack_ptr, increment stack_ptr
 *
 * For floating-point types, we cast to double* to get correct IEEE 754 value.
 *
 * The fp vs non-fp split uses __builtin_choose_expr (not a runtime "?:") so
 * that each arm has a single, consistent element type.  A plain conditional
 * would fuse the double and 'type' arms via the usual arithmetic conversions
 * and mistype the result (e.g. typing va_arg(ap,int*) as double), which then
 * breaks a dereference or member access on the result.
 *
 * A GNU vector_size vector (ticket #721) is a third case, checked via
 * __builtin_classify_type(*(type *)0) == 99 (CCCC_VECTOR_TYPE_CLASS, see
 * src/parse.c). The operand is never evaluated -- like sizeof(expr), only
 * its type is inspected -- so the null dereference is compile-time only.
 * Unlike a scalar, a variadic vector argument does NOT live directly in its
 * 8-byte slot: the call site (gen_vector_arg_ptr, #714) always passes it BY
 * POINTER to a caller-frame scratch copy, regardless of the vector's width,
 * so it always occupies exactly one slot no matter how large the vector is
 * (16/32/64 bytes, #722) and never straddles the register/stack boundary.
 * The vector arm therefore reads the slot as a pointer and derefs it, one
 * extra level of indirection versus the scalar arms.
 *
 * _Decimal32/64/128 (#829) shares that same by-pointer arm: a decimal
 * variadic argument is likewise never in its own 8-byte slot, so its
 * discriminant (== 98, CCCC_DECIMAL_TYPE_CLASS) is folded into the same
 * vector-or-decimal check via `||` rather than adding a fourth
 * __builtin_choose_expr level. See gen_decimal_arg_ptr (src/codegen.c) for
 * the call-site half of this convention -- it always copies into a fresh
 * 16-byte scratch slot regardless of the argument's actual 4/8/16-byte
 * width, since the reading side's width comes from the %Hf/%Df/%DDf
 * modifier in a format string the compiler generally can't correlate with
 * the argument's declared type.
 *
 * #1018: __cccc_va_arg() (src/parse_postfix.c) parses `ap` and `type` a
 * second, independent time to stamp them as serializer annotation onto
 * the returned impl node -- the __builtin_choose_expr chain below is
 * parsed and returned completely unchanged (already reduced to a single
 * arm by the time __cccc_va_arg sees it, since __builtin_choose_expr
 * itself discards its unchosen arm at parse time), so VM codegen is
 * unaffected; only the serializer prints the real host `va_arg(ap, type)`
 * form using the annotation instead of walking this subtree.
 */
#define va_arg(ap, type)                                                       \
    __cccc_va_arg(                                                             \
        ap, type,                                                              \
        __builtin_choose_expr(                                                 \
            __builtin_types_compatible_p(type, double) ||                      \
                __builtin_types_compatible_p(type, float),                     \
            (((ap).reg_count > 0)                                              \
                 ? (--((ap).reg_count),                                        \
                    (*(double *)(((ap).reg_ptr) -= 8, ((ap).reg_ptr) + 8)))    \
                 : (*(double *)(((ap).stack_ptr) += 8,                         \
                                ((ap).stack_ptr) - 8))),                       \
            __builtin_choose_expr(                                             \
                __builtin_classify_type(*(type *)0) == 99 ||                   \
                    __builtin_classify_type(*(type *)0) == 98,                 \
                (((ap).reg_count > 0)                                          \
                     ? (--((ap).reg_count),                                    \
                        (*(type *)(*(void **)(((ap).reg_ptr) -= 8,             \
                                              ((ap).reg_ptr) + 8))))           \
                     : (*(type *)(*(void **)(((ap).stack_ptr) += 8,            \
                                             ((ap).stack_ptr) - 8)))),         \
                (((ap).reg_count > 0)                                          \
                     ? (--((ap).reg_count),                                    \
                        (*(type *)(((ap).reg_ptr) -= 8, ((ap).reg_ptr) + 8)))  \
                     : (*(type *)(((ap).stack_ptr) += 8,                       \
                                  ((ap).stack_ptr) - 8))))))

/*
 * va_end(ap) - Cleanup va_list
 * @ap: va_list to cleanup
 *
 * No-op in this implementation. Included for C standard compliance.
 *
 * #1018: still a no-op VM-side (__cccc_va_end() just stamps the
 * annotation and returns ((void)0) unchanged) -- the real host va_end()
 * is also a no-op on every supported host, but the serializer prints it
 * anyway for source fidelity, in case a future host ever needs it to do
 * real work.
 */
#define va_end(ap) __cccc_va_end(ap, ((void)0))

/*
 * va_copy(dest, src) - Copy va_list (C99)
 * @dest: destination va_list
 * @src:  source va_list
 *
 * Struct assignment copies all fields.
 *
 * #1018: __cccc_va_copy() stamps the annotation; the struct assignment
 * itself is unchanged.
 */
#define va_copy(dest, src) __cccc_va_copy(dest, src, ((dest) = (src)))

/*
 * __builtin_va_* aliases
 *
 * GCC exposes these alternative names for the va_arg family.  They are aliased
 * to the corresponding va_* macros here so that code using either form works
 * identically when <stdarg.h> is included.
 *
 * Note: the preprocessor defines __builtin_va_list and __gnuc_va_list as
 * 'struct __cccc_va_list' (preprocess.c), so system headers' own va_list
 * typedefs name this same struct.
 */
#if __STDC_VERSION__ >= 202311L
#define __builtin_va_start(ap, ...) va_start(ap __VA_OPT__(, ) __VA_ARGS__)
#else
#define __builtin_va_start(ap, last) va_start(ap, last)
#endif
#define __builtin_va_end(ap)         va_end(ap)
#define __builtin_va_copy(d, s)      va_copy(d, s)
#define __builtin_va_arg(ap, type)   va_arg(ap, type)

#endif /* __CCCC_STDARG_H */
#else
#include_next <stdarg.h>
#endif /* __CCCC__ */
