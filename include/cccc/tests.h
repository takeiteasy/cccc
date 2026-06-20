/*
 CCCC test framework header — injected automatically in --testing mode.
 Do not include this directly; use the --testing flag instead.
 All assertion macros use the $ prefix to match the reflection.h convention.
*/

#pragma once

// ============================================================================
// Underlying FFI-callable assertion functions
// ============================================================================

void __builtin_assert(int cond, const char *expr, const char *file, int line);
void __builtin_assert_false(int cond, const char *expr, const char *file, int line);
void __builtin_assert_fail(const char *file, int line);
void __builtin_assert_fail_msg(const char *msg, const char *file, int line);
void __builtin_assert_eq(long long a, long long b, const char *as, const char *bs,
                     const char *file, int line);
void __builtin_assert_neq(long long a, long long b, const char *as, const char *bs,
                      const char *file, int line);
void __builtin_assert_gt(long long a, long long b, const char *as, const char *bs,
                     const char *file, int line);
void __builtin_assert_lt(long long a, long long b, const char *as, const char *bs,
                     const char *file, int line);
void __builtin_assert_ge(long long a, long long b, const char *as, const char *bs,
                     const char *file, int line);
void __builtin_assert_le(long long a, long long b, const char *as, const char *bs,
                     const char *file, int line);
void __builtin_assert_within(long long delta, long long expected, long long actual,
                         const char *ds, const char *es, const char *as,
                         const char *file, int line);
void __builtin_assert_null(const void *p, const char *ps,
                       const char *file, int line);
void __builtin_assert_not_null(const void *p, const char *ps,
                           const char *file, int line);
void __builtin_assert_streq(const char *a, const char *b,
                        const char *as, const char *bs,
                        const char *file, int line);
void __builtin_assert_streq_len(const char *a, const char *b, long long len,
                            const char *as, const char *bs,
                            const char *file, int line);
void __builtin_assert_mem_eq(const void *expected, const void *actual, long long len,
                         const char *es, const char *as,
                         const char *file, int line);
void __builtin_assert_float_within(double delta, double expected, double actual,
                               const char *ds, const char *es, const char *as,
                               const char *file, int line);
void __builtin_assert_double_within(double delta, double expected, double actual,
                                const char *ds, const char *es, const char *as,
                                const char *file, int line);
void __builtin_assert_bits(long long mask, long long expected, long long actual,
                       const char *ms, const char *es, const char *as,
                       const char *file, int line);
void __builtin_assert_bit_high(int bit, long long actual,
                           const char *bs, const char *as,
                           const char *file, int line);
void __builtin_assert_bit_low(int bit, long long actual,
                          const char *bs, const char *as,
                          const char *file, int line);
void __builtin_assert_eq_array(const void *expected, const void *actual,
                           long long elem_size, long long count,
                           const char *es, const char *as,
                           const char *file, int line);

// Message-appending variants
void __builtin_assert_msg(int cond, const char *expr, const char *msg,
                      const char *file, int line);
void __builtin_assert_eq_msg(long long a, long long b,
                         const char *as, const char *bs, const char *msg,
                         const char *file, int line);
void __builtin_assert_streq_msg(const char *a, const char *b,
                            const char *as, const char *bs, const char *msg,
                            const char *file, int line);
void __builtin_assert_null_msg(const void *p, const char *ps, const char *msg,
                           const char *file, int line);
void __builtin_assert_not_null_msg(const void *p, const char *ps, const char *msg,
                               const char *file, int line);
void __builtin_assert_bits_msg(long long mask, long long expected, long long actual,
                           const char *ms, const char *es, const char *as,
                           const char *msg, const char *file, int line);

// ============================================================================
// Basic Validity Assertions
// ============================================================================

#define $assert(cond) \
    __builtin_assert(!!(cond), #cond, __FILE__, __LINE__)
#define $assert_true(cond) \
    $assert(cond)
#define $assert_false(cond) \
    __builtin_assert_false(!!(cond), #cond, __FILE__, __LINE__)
#define $assert_fail() \
    __builtin_assert_fail(__FILE__, __LINE__)
#define $assert_fail_msg(msg) \
    __builtin_assert_fail_msg(msg, __FILE__, __LINE__)

// ============================================================================
// Numerical Assertions: Integers
// ============================================================================

#define $assert_eq(a, b) \
    __builtin_assert_eq((long long)(a), (long long)(b), #a, #b, __FILE__, __LINE__)
#define $assert_neq(a, b) \
    __builtin_assert_neq((long long)(a), (long long)(b), #a, #b, __FILE__, __LINE__)
#define $assert_gt(a, b) \
    __builtin_assert_gt((long long)(a), (long long)(b), #a, #b, __FILE__, __LINE__)
#define $assert_lt(a, b) \
    __builtin_assert_lt((long long)(a), (long long)(b), #a, #b, __FILE__, __LINE__)
#define $assert_ge(a, b) \
    __builtin_assert_ge((long long)(a), (long long)(b), #a, #b, __FILE__, __LINE__)
#define $assert_le(a, b) \
    __builtin_assert_le((long long)(a), (long long)(b), #a, #b, __FILE__, __LINE__)
#define $assert_within(delta, expected, actual) \
    __builtin_assert_within((long long)(delta), (long long)(expected), (long long)(actual), \
                        #delta, #expected, #actual, __FILE__, __LINE__)

// ============================================================================
// Numerical Assertions: Bitwise
// ============================================================================

#define $assert_bits(mask, expected, actual) \
    __builtin_assert_bits((long long)(mask), (long long)(expected), (long long)(actual), \
                      #mask, #expected, #actual, __FILE__, __LINE__)
#define $assert_bit_high(bit, actual) \
    __builtin_assert_bit_high((int)(bit), (long long)(actual), \
                          #bit, #actual, __FILE__, __LINE__)
#define $assert_bit_low(bit, actual) \
    __builtin_assert_bit_low((int)(bit), (long long)(actual), \
                         #bit, #actual, __FILE__, __LINE__)

// ============================================================================
// Numerical Assertions: Floats / Doubles
// ============================================================================

#define $assert_float_within(delta, expected, actual) \
    __builtin_assert_float_within((double)(delta), (double)(expected), (double)(actual), \
                              #delta, #expected, #actual, __FILE__, __LINE__)
#define $assert_double_within(delta, expected, actual) \
    __builtin_assert_double_within((double)(delta), (double)(expected), (double)(actual), \
                               #delta, #expected, #actual, __FILE__, __LINE__)
#define $assert_float_eq(expected, actual) \
    __builtin_assert_float_within(1e-6, (double)(expected), (double)(actual), \
                              #expected, #expected, #actual, __FILE__, __LINE__)
#define $assert_double_eq(expected, actual) \
    __builtin_assert_double_within(1e-15, (double)(expected), (double)(actual), \
                               #expected, #expected, #actual, __FILE__, __LINE__)

// ============================================================================
// Pointer Assertions
// ============================================================================

#define $assert_null(p) \
    __builtin_assert_null((const void *)(p), #p, __FILE__, __LINE__)
#define $assert_not_null(p) \
    __builtin_assert_not_null((const void *)(p), #p, __FILE__, __LINE__)

// ============================================================================
// String Assertions
// ============================================================================

#define $assert_streq(a, b) \
    __builtin_assert_streq((a), (b), #a, #b, __FILE__, __LINE__)
#define $assert_streq_len(a, b, len) \
    __builtin_assert_streq_len((a), (b), (long long)(len), #a, #b, __FILE__, __LINE__)

// ============================================================================
// Memory Assertions
// ============================================================================

#define $assert_mem_eq(expected, actual, len) \
    __builtin_assert_mem_eq((const void *)(expected), (const void *)(actual), \
                        (long long)(len), #expected, #actual, __FILE__, __LINE__)

// ============================================================================
// Array Assertions
// ============================================================================

#define $assert_eq_array(expected, actual, count) \
    __builtin_assert_eq_array((const void *)(expected), (const void *)(actual), \
                          (long long)sizeof((expected)[0]), (long long)(count), \
                          #expected, #actual, __FILE__, __LINE__)
#define $assert_each_eq(expected, actual, count) \
    do { \
        long long _cccc_i_; \
        for (_cccc_i_ = 0; _cccc_i_ < (long long)(count); _cccc_i_++) \
            __builtin_assert_eq((long long)((expected)), \
                           (long long)(((const typeof((actual)[0])*)(actual))[_cccc_i_]), \
                           #expected, #actual "[i]", \
                           __FILE__, __LINE__); \
    } while (0)

// ============================================================================
// Message-appending variants (_msg suffix)
// ============================================================================

#define $assert_msg(cond, msg) \
    __builtin_assert_msg(!!(cond), #cond, msg, __FILE__, __LINE__)
#define $assert_true_msg(cond, msg) \
    $assert_msg(cond, msg)
#define $assert_false_msg(cond, msg) \
    __builtin_assert_msg(!(cond), #cond, msg, __FILE__, __LINE__)
#define $assert_eq_msg(a, b, msg) \
    __builtin_assert_eq_msg((long long)(a), (long long)(b), #a, #b, msg, \
                        __FILE__, __LINE__)
#define $assert_streq_msg(a, b, msg) \
    __builtin_assert_streq_msg((a), (b), #a, #b, msg, __FILE__, __LINE__)
#define $assert_null_msg(p, msg) \
    __builtin_assert_null_msg((const void *)(p), #p, msg, __FILE__, __LINE__)
#define $assert_not_null_msg(p, msg) \
    __builtin_assert_not_null_msg((const void *)(p), #p, msg, __FILE__, __LINE__)
#define $assert_bits_msg(mask, expected, actual, msg) \
    __builtin_assert_bits_msg((long long)(mask), (long long)(expected), (long long)(actual), \
                          #mask, #expected, #actual, msg, __FILE__, __LINE__)
