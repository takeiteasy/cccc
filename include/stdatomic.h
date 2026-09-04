#ifndef __STDATOMIC_H
#define __STDATOMIC_H

#define ATOMIC_BOOL_LOCK_FREE     1
#define ATOMIC_CHAR_LOCK_FREE     1
#define ATOMIC_CHAR16_T_LOCK_FREE 1
#define ATOMIC_CHAR32_T_LOCK_FREE 1
#define ATOMIC_WCHAR_T_LOCK_FREE  1
#define ATOMIC_SHORT_LOCK_FREE    1
#define ATOMIC_INT_LOCK_FREE      1
#define ATOMIC_LONG_LOCK_FREE     1
#define ATOMIC_LLONG_LOCK_FREE    1
#define ATOMIC_POINTER_LOCK_FREE  1

typedef enum {
    memory_order_relaxed,
    memory_order_consume,
    memory_order_acquire,
    memory_order_release,
    memory_order_acq_rel,
    memory_order_seq_cst,
} memory_order;

/* C11 7.17.1: ATOMIC_FLAG_INIT is an object-like initializer macro, used as */
/* `atomic_flag f = ATOMIC_FLAG_INIT;` -- no parentheses/argument. (#1188: */
/* was previously defined function-like, `#define ATOMIC_FLAG_INIT(x) (x)`, */
/* which is non-conforming; atomic_flag is _Atomic _Bool, so 0 is "clear".) */
#define ATOMIC_FLAG_INIT       0
#define atomic_init(addr, val) (*(addr) = (val))
#define kill_dependency(x)     (x)
/* C11 7.17.2p1: ATOMIC_VAR_INIT(value) initializes an _Atomic-qualified */
/* object at declaration, e.g. `atomic_int x = ATOMIC_VAR_INIT(5);` -- */
/* equivalent to a plain `= value` for any type this page supports (#1190). */
/* Deprecated in C17 (Annex 4), removed entirely in C23; gated the same way */
/* glibc's and clang's own <stdatomic.h> gate it, so a C23 program using it */
/* gets the same undefined-macro error a real C23 compiler gives, while a */
/* C11/C17 program (--std=c11/--std=c17) still gets a working expansion. */
#if __STDC_VERSION__ <= 201710L
#define ATOMIC_VAR_INIT(value) (value)
#endif
/* #1188: real fences, lowered via the new ND_FENCE node (parse_postfix.c, */
/* type.c, codegen_expr.c, serialize_expr.c). Under -c=native this emits a */
/* genuine __atomic_thread_fence/__atomic_signal_fence with the requested */
/* order (not a fixed __ATOMIC_SEQ_CST like every other operation on this */
/* page -- a fence with no order does nothing at all, unlike seq_cst being */
/* merely stronger than requested elsewhere here). Under the VM these are */
/* no-ops: the GIL is held for the whole vm_eval and released only at */
/* explicit blocking cfunc points (save_and_release_gil, */
/* src/stdlib/pthread.c), never between bytecode instructions, so no */
/* cross-thread reordering of guest memory accesses is ever observable */
/* there, and guest signal handlers dispatch at safe points */
/* (cccc_call_guest_callback) rather than asynchronously -- see */
/* codegen_expr.c's ND_FENCE case for the full reasoning. */
#define atomic_thread_fence(order) __builtin_atomic_thread_fence(order)
#define atomic_signal_fence(order) __builtin_atomic_signal_fence(order)
#define atomic_is_lock_free(x)     1

#define atomic_load(addr)          __builtin_atomic_load(addr)
#define atomic_store(addr, val)    __builtin_atomic_store((addr), (val))

/* #1184: every operation on this page is __ATOMIC_SEQ_CST regardless of the */
/* `order` argument -- the argument is accepted (as C11 requires it must be a */
/* constant expression) but otherwise discarded. This is conforming: seq_cst */
/* is stronger than any order a caller can request, never weaker, so nothing */
/* observable is lost by ignoring it. The only cost is performance (relaxed/ */
/* acquire-release ops pay a full seq_cst fence they didn't ask for) -- see */
/* the fence follow-up below for the related, currently-unaddressed gap. */
#define atomic_load_explicit(addr, order) __builtin_atomic_load(addr)
#define atomic_store_explicit(addr, val, order)                                \
    __builtin_atomic_store((addr), (val))

/* #1184: a genuine CAS retry loop, not a load-then-store. The old two-step */
/* ALDR+ASTR form ("_old = load(obj); store(obj, _old + val)") was atomic */
/* only under the VM's GIL -- a plain pthread_mutex held for the whole */
/* vm_eval and released only at explicit blocking cfunc points */
/* (save_and_release_gil, src/stdlib/pthread.c), never between bytecode */
/* instructions, so the two-step form was uninterruptible there. Under */
/* -c=native there is no GIL: real thread parallelism made this a genuine */
/* data race with silently lost updates (confirmed via stress testing, */
/* #1184). ND_CAS already lowers to a real atomic op on both backends -- */
/* ACAS in the VM (op_ACAS_fn, src/ops.c, which still calls */
/* check_atomic_access() to tag atomic_shadow for #447 mixed-access */
/* detection, exactly as the old ALDR/ASTR did) and */
/* __atomic_compare_exchange_n natively (serialize_expr.c) -- because */
/* to_assign()'s `_Atomic x op= y` desugar (src/parse_expr.c) already builds */
/* exactly this shape. The only difference from that desugar is that this */
/* yields the OLD value, as C11 7.17.7.5 requires (to_assign() yields new). */
/* */
/* obj/val are hoisted into __cccc_fetch_p/__cccc_fetch_v *before* the loop, */
/* exactly like to_assign()'s own addr/val lvars -- the retry body/condition */
/* touch only those temps. Do not inline (obj)/(val) directly into the loop: */
/* a CAS retry would then re-evaluate them on every failed attempt (e.g. */
/* atomic_fetch_add(&x, f()) would call f() once per retry, a regression */
/* from single evaluation). No test can catch a regression here -- under the */
/* VM's GIL the CAS always succeeds on the first try, so re-evaluation is */
/* invisible there too -- so this hoist has to stay as a documented */
/* invariant, not something a future "simplification" undoes. */
/* */
/* __typeof__(*(obj)) yields _Atomic T, so &__cccc_fetch_old is _Atomic T *, */
/* matching what to_assign()'s own `old` lvar is; serialize_atomic_addr() */
/* (src/serialize_expr.c, #1101) strips the qualifier for the native */
/* builtin, so no cast is needed here. */
/* */
/* Note: ND_CAS codegen hard-errors on a float/non-{1,2,4,8}-byte pointee, */
/* where the old ALDR-based form silently fell back to a plain load. */
/* Neither was ever valid C for atomic_fetch_* (C11 7.17.7.5 requires an */
/* atomic *integer* type), so this turns silent nonsense into a diagnostic. */
/* */
/* #1295: since serialize_type()/serialize_type_decl() started spelling a */
/* bare (non-typedef'd) _Atomic-qualified declaration correctly, this */
/* statement expression's overall type is genuinely `_Atomic T` (from its */
/* last statement, __cccc_fetch_old itself) rather than the plain T it used */
/* to silently decay to when the qualifier went unprinted. C11 6.3.2.1p2's */
/* footnote makes an atomic lvalue-to-rvalue conversion (T, non-atomic) */
/* implicit -- real gcc and current upstream clang both accept assigning */
/* that value straight to a plain-T variable -- but the Apple-clang release */
/* this project also targets as a native host compiler mistypes the */
/* statement expression's result as still-atomic and rejects the outer */
/* `old = (...)` assignment ("assigning to 'int' from incompatible type */
/* '_Atomic(int)'"). Route the yield through an explicit plain-T temporary */
/* so the atomic-to-nonatomic conversion happens via a plain-typed */
/* initializer INSIDE the statement expression, which every tested */
/* compiler accepts, rather than relying on the statement expression's own */
/* result type. */
#define __cccc_atomic_fetch_op(obj, val, op)                                   \
    ({                                                                         \
        __typeof__(obj)    __cccc_fetch_p = (obj);                             \
        __typeof__(val)    __cccc_fetch_v = (val);                             \
        __typeof__(*(obj)) __cccc_fetch_old =                                  \
            __builtin_atomic_load(__cccc_fetch_p);                             \
        __typeof__(*(obj)) __cccc_fetch_new;                                   \
        do {                                                                   \
            __cccc_fetch_new = __cccc_fetch_old op __cccc_fetch_v;             \
        } while (!__builtin_compare_and_swap(                                  \
            __cccc_fetch_p, &__cccc_fetch_old, __cccc_fetch_new));             \
        __typeof__(val) __cccc_fetch_result = __cccc_fetch_old;                \
        __cccc_fetch_result;                                                   \
    })
#define atomic_fetch_add(obj, val) __cccc_atomic_fetch_op((obj), (val), +)
#define atomic_fetch_sub(obj, val) __cccc_atomic_fetch_op((obj), (val), -)
#define atomic_fetch_or(obj, val)  __cccc_atomic_fetch_op((obj), (val), |)
#define atomic_fetch_xor(obj, val) __cccc_atomic_fetch_op((obj), (val), ^)
#define atomic_fetch_and(obj, val) __cccc_atomic_fetch_op((obj), (val), &)

#define atomic_fetch_add_explicit(obj, val, order) atomic_fetch_add(obj, val)
#define atomic_fetch_sub_explicit(obj, val, order) atomic_fetch_sub(obj, val)
#define atomic_fetch_or_explicit(obj, val, order)  atomic_fetch_or(obj, val)
#define atomic_fetch_xor_explicit(obj, val, order) atomic_fetch_xor(obj, val)
#define atomic_fetch_and_explicit(obj, val, order) atomic_fetch_and(obj, val)

/* weak and strong are the same expansion (__builtin_compare_and_swap is a */
/* single-attempt CAS with no spurious-failure path), so "weak" is really */
/* strong here -- conforming (C11 permits an implementation of the weak form */
/* that never spuriously fails), left this way deliberately: do not "fix" */
/* weak to loop internally, that would just be strong with extra steps. */
#define atomic_compare_exchange_weak(p, old, new)                              \
    __builtin_compare_and_swap((p), (old), (new))

#define atomic_compare_exchange_strong(p, old, new)                            \
    __builtin_compare_and_swap((p), (old), (new))

#define atomic_exchange(obj, val) __builtin_atomic_exchange((obj), (val))
#define atomic_exchange_explicit(obj, val, order)                              \
    __builtin_atomic_exchange((obj), (val))

#define atomic_flag_test_and_set(obj)                 atomic_exchange((obj), 1)
#define atomic_flag_test_and_set_explicit(obj, order) atomic_exchange((obj), 1)
/* #1184: was a plain, non-atomic `*(obj) = 0` -- not even ALDR/ASTR-tagged, */
/* so unlike every other operation on this page it was never atomic on */
/* EITHER backend, and invisible to #447 mixed-access detection. Route */
/* through the same tagged atomic store atomic_flag_test_and_set already */
/* uses via atomic_exchange, above. */
#define atomic_flag_clear(obj)                 __builtin_atomic_store((obj), 0)
#define atomic_flag_clear_explicit(obj, order) __builtin_atomic_store((obj), 0)

typedef _Atomic _Bool              atomic_flag;
typedef _Atomic _Bool              atomic_bool;
typedef _Atomic char               atomic_char;
typedef _Atomic signed char        atomic_schar;
typedef _Atomic unsigned char      atomic_uchar;
typedef _Atomic short              atomic_short;
typedef _Atomic unsigned short     atomic_ushort;
typedef _Atomic int                atomic_int;
typedef _Atomic unsigned int       atomic_uint;
typedef _Atomic long               atomic_long;
typedef _Atomic unsigned long      atomic_ulong;
typedef _Atomic long long          atomic_llong;
typedef _Atomic unsigned long long atomic_ullong;
typedef _Atomic unsigned short     atomic_char16_t;
typedef _Atomic unsigned           atomic_char32_t;
typedef _Atomic unsigned           atomic_wchar_t;
typedef _Atomic signed char        atomic_int_least8_t;
typedef _Atomic unsigned char      atomic_uint_least8_t;
typedef _Atomic short              atomic_int_least16_t;
typedef _Atomic unsigned short     atomic_uint_least16_t;
typedef _Atomic int                atomic_int_least32_t;
typedef _Atomic unsigned int       atomic_uint_least32_t;
typedef _Atomic long               atomic_int_least64_t;
typedef _Atomic unsigned long      atomic_uint_least64_t;
typedef _Atomic signed char        atomic_int_fast8_t;
typedef _Atomic unsigned char      atomic_uint_fast8_t;
typedef _Atomic short              atomic_int_fast16_t;
typedef _Atomic unsigned short     atomic_uint_fast16_t;
typedef _Atomic int                atomic_int_fast32_t;
typedef _Atomic unsigned int       atomic_uint_fast32_t;
typedef _Atomic long               atomic_int_fast64_t;
typedef _Atomic unsigned long      atomic_uint_fast64_t;
typedef _Atomic long               atomic_intptr_t;
typedef _Atomic unsigned long      atomic_uintptr_t;
typedef _Atomic unsigned long      atomic_size_t;
typedef _Atomic long               atomic_ptrdiff_t;
typedef _Atomic long               atomic_intmax_t;
typedef _Atomic unsigned long      atomic_uintmax_t;

#endif
