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

/* A constant `order` reaches -c=native/-m as the host __atomic_* order (a */
/* computed one is seq_cst); the VM runs every operation sequentially */
/* consistent, which is never weaker than a requested order. */
#define atomic_load_explicit(addr, order) __atomic_load_n((addr), (order))
#define atomic_store_explicit(addr, val, order)                                \
    __atomic_store_n((addr), (val), (order))
#define atomic_exchange_explicit(obj, val, order)                              \
    __atomic_exchange_n((obj), (val), (order))

/* Pointer operands scale by the pointee, as C11 requires (GCC's own */
/* __atomic_fetch_add moves bytes). */
#define atomic_fetch_add_explicit(obj, val, order)                             \
    __c11_atomic_fetch_add((obj), (val), (order))
#define atomic_fetch_sub_explicit(obj, val, order)                             \
    __c11_atomic_fetch_sub((obj), (val), (order))
#define atomic_fetch_or_explicit(obj, val, order)                              \
    __c11_atomic_fetch_or((obj), (val), (order))
#define atomic_fetch_xor_explicit(obj, val, order)                             \
    __c11_atomic_fetch_xor((obj), (val), (order))
#define atomic_fetch_and_explicit(obj, val, order)                             \
    __c11_atomic_fetch_and((obj), (val), (order))

/* The VM's compare-exchange never fails spuriously, so weak behaves as */
/* strong there; -c=native emits the weak flag. */
#define atomic_compare_exchange_strong_explicit(obj, expected, desired, succ,  \
                                                fail)                          \
    __atomic_compare_exchange_n((obj), (expected), (desired), 0, (succ), (fail))
#define atomic_compare_exchange_weak_explicit(obj, expected, desired, succ,    \
                                              fail)                            \
    __atomic_compare_exchange_n((obj), (expected), (desired), 1, (succ), (fail))

#define atomic_load(addr) atomic_load_explicit((addr), memory_order_seq_cst)
#define atomic_store(addr, val)                                                \
    atomic_store_explicit((addr), (val), memory_order_seq_cst)
#define atomic_exchange(obj, val)                                              \
    atomic_exchange_explicit((obj), (val), memory_order_seq_cst)
#define atomic_fetch_add(obj, val)                                             \
    atomic_fetch_add_explicit((obj), (val), memory_order_seq_cst)
#define atomic_fetch_sub(obj, val)                                             \
    atomic_fetch_sub_explicit((obj), (val), memory_order_seq_cst)
#define atomic_fetch_or(obj, val)                                              \
    atomic_fetch_or_explicit((obj), (val), memory_order_seq_cst)
#define atomic_fetch_xor(obj, val)                                             \
    atomic_fetch_xor_explicit((obj), (val), memory_order_seq_cst)
#define atomic_fetch_and(obj, val)                                             \
    atomic_fetch_and_explicit((obj), (val), memory_order_seq_cst)
#define atomic_compare_exchange_strong(obj, expected, desired)                 \
    atomic_compare_exchange_strong_explicit((obj), (expected), (desired),      \
                                            memory_order_seq_cst,              \
                                            memory_order_seq_cst)
#define atomic_compare_exchange_weak(obj, expected, desired)                   \
    atomic_compare_exchange_weak_explicit((obj), (expected), (desired),        \
                                          memory_order_seq_cst,                \
                                          memory_order_seq_cst)

#define atomic_flag_test_and_set_explicit(obj, order)                          \
    atomic_exchange_explicit((obj), 1, (order))
#define atomic_flag_test_and_set(obj)                                          \
    atomic_flag_test_and_set_explicit((obj), memory_order_seq_cst)
#define atomic_flag_clear_explicit(obj, order)                                 \
    atomic_store_explicit((obj), 0, (order))
#define atomic_flag_clear(obj)                                                 \
    atomic_flag_clear_explicit((obj), memory_order_seq_cst)

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
