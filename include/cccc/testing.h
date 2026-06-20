/*!
 * @file testing.h
 * @brief CCCC Test Framework — assertion API injected in --testing mode.
 *
 * This header provides macros and underlying FFI-callable builtins for writing
 * self-verifying test programs.  It is automatically injected when CCCC is
 * invoked with the @c --testing flag and should not be included directly.
 *
 * All assertion macros use the @c $ prefix to match the reflection.h convention.
 * A test passes if it exits with code 42.
 *
 * ## Usage
 *
 * @code
 * int main(void) {
 *     $assert_eq(1 + 1, 2);
 *     $assert_streq("hello", "hello");
 *     $assert_null(NULL);
 *     return 42;
 * }
 * @endcode
 *
 * Each assertion macro expands to a call to an FFI-callable @c __builtin_assert_*
 * function.  On failure, the builtin prints the source location and expression,
 * then terminates with a failure exit code.
 */

#pragma once

// ============================================================================
// Underlying FFI-callable assertion functions
// ============================================================================

/*!
 * @function __builtin_assert
 * @abstract Assert that a condition is true.
 * @param cond The boolean condition (must be non-zero to pass).
 * @param expr Stringified expression for diagnostic output.
 * @param file Source file name.
 * @param line Source line number.
 * @discussion Backend for @c $assert and @c $assert_true.
 */
void __builtin_assert(int cond, const char *expr, const char *file, int line);

/*!
 * @function __builtin_assert_false
 * @abstract Assert that a condition is false.
 * @param cond The boolean condition (must be zero to pass).
 * @param expr Stringified expression for diagnostic output.
 * @param file Source file name.
 * @param line Source line number.
 * @discussion Backend for @c $assert_false.
 */
void __builtin_assert_false(int cond, const char *expr, const char *file, int line);

/*!
 * @function __builtin_assert_fail
 * @abstract Unconditionally fail a test.
 * @param file Source file name.
 * @param line Source line number.
 * @discussion Backend for @c $assert_fail.
 */
void __builtin_assert_fail(const char *file, int line);

/*!
 * @function __builtin_assert_fail_msg
 * @abstract Unconditionally fail a test with a custom message.
 * @param msg Custom failure message.
 * @param file Source file name.
 * @param line Source line number.
 * @discussion Backend for @c $assert_fail_msg.
 */
void __builtin_assert_fail_msg(const char *msg, const char *file, int line);

/*!
 * @function __builtin_assert_eq
 * @abstract Assert that two integers are equal.
 * @param a Left-hand operand.
 * @param b Right-hand operand.
 * @param as Stringified left expression.
 * @param bs Stringified right expression.
 * @param file Source file name.
 * @param line Source line number.
 * @discussion Backend for @c $assert_eq.
 */
void __builtin_assert_eq(long long a, long long b, const char *as, const char *bs,
                     const char *file, int line);

/*!
 * @function __builtin_assert_neq
 * @abstract Assert that two integers are not equal.
 * @param a Left-hand operand.
 * @param b Right-hand operand.
 * @param as Stringified left expression.
 * @param bs Stringified right expression.
 * @param file Source file name.
 * @param line Source line number.
 * @discussion Backend for @c $assert_neq.
 */
void __builtin_assert_neq(long long a, long long b, const char *as, const char *bs,
                      const char *file, int line);

/*!
 * @function __builtin_assert_gt
 * @abstract Assert that one integer is greater than another.
 * @param a Left-hand operand.
 * @param b Right-hand operand.
 * @param as Stringified left expression.
 * @param bs Stringified right expression.
 * @param file Source file name.
 * @param line Source line number.
 * @discussion Backend for @c $assert_gt.
 */
void __builtin_assert_gt(long long a, long long b, const char *as, const char *bs,
                     const char *file, int line);

/*!
 * @function __builtin_assert_lt
 * @abstract Assert that one integer is less than another.
 * @param a Left-hand operand.
 * @param b Right-hand operand.
 * @param as Stringified left expression.
 * @param bs Stringified right expression.
 * @param file Source file name.
 * @param line Source line number.
 * @discussion Backend for @c $assert_lt.
 */
void __builtin_assert_lt(long long a, long long b, const char *as, const char *bs,
                     const char *file, int line);

/*!
 * @function __builtin_assert_ge
 * @abstract Assert that one integer is greater than or equal to another.
 * @param a Left-hand operand.
 * @param b Right-hand operand.
 * @param as Stringified left expression.
 * @param bs Stringified right expression.
 * @param file Source file name.
 * @param line Source line number.
 * @discussion Backend for @c $assert_ge.
 */
void __builtin_assert_ge(long long a, long long b, const char *as, const char *bs,
                     const char *file, int line);

/*!
 * @function __builtin_assert_le
 * @abstract Assert that one integer is less than or equal to another.
 * @param a Left-hand operand.
 * @param b Right-hand operand.
 * @param as Stringified left expression.
 * @param bs Stringified right expression.
 * @param file Source file name.
 * @param line Source line number.
 * @discussion Backend for @c $assert_le.
 */
void __builtin_assert_le(long long a, long long b, const char *as, const char *bs,
                     const char *file, int line);

/*!
 * @function __builtin_assert_within
 * @abstract Assert that an integer is within a delta of an expected value.
 * @param delta Maximum allowed absolute difference.
 * @param expected Expected value.
 * @param actual Actual value.
 * @param ds Stringified delta expression.
 * @param es Stringified expected expression.
 * @param as Stringified actual expression.
 * @param file Source file name.
 * @param line Source line number.
 * @discussion Backend for @c $assert_within.  Passes if
 *             |actual - expected| <= delta.
 */
void __builtin_assert_within(long long delta, long long expected, long long actual,
                         const char *ds, const char *es, const char *as,
                         const char *file, int line);

/*!
 * @function __builtin_assert_null
 * @abstract Assert that a pointer is NULL.
 * @param p The pointer to check.
 * @param ps Stringified pointer expression.
 * @param file Source file name.
 * @param line Source line number.
 * @discussion Backend for @c $assert_null.
 */
void __builtin_assert_null(const void *p, const char *ps,
                       const char *file, int line);

/*!
 * @function __builtin_assert_not_null
 * @abstract Assert that a pointer is not NULL.
 * @param p The pointer to check.
 * @param ps Stringified pointer expression.
 * @param file Source file name.
 * @param line Source line number.
 * @discussion Backend for @c $assert_not_null.
 */
void __builtin_assert_not_null(const void *p, const char *ps,
                           const char *file, int line);

/*!
 * @function __builtin_assert_streq
 * @abstract Assert that two C strings are equal (full strcmp).
 * @param a Expected string.
 * @param b Actual string.
 * @param as Stringified expected expression.
 * @param bs Stringified actual expression.
 * @param file Source file name.
 * @param line Source line number.
 * @discussion Backend for @c $assert_streq.  Uses strcmp for comparison.
 */
void __builtin_assert_streq(const char *a, const char *b,
                        const char *as, const char *bs,
                        const char *file, int line);

/*!
 * @function __builtin_assert_streq_len
 * @abstract Assert that two C strings are equal up to a given length.
 * @param a Expected string.
 * @param b Actual string.
 * @param len Maximum number of characters to compare.
 * @param as Stringified expected expression.
 * @param bs Stringified actual expression.
 * @param file Source file name.
 * @param line Source line number.
 * @discussion Backend for @c $assert_streq_len.  Uses strncmp for comparison.
 */
void __builtin_assert_streq_len(const char *a, const char *b, long long len,
                            const char *as, const char *bs,
                            const char *file, int line);

/*!
 * @function __builtin_assert_mem_eq
 * @abstract Assert that two memory regions are byte-identical.
 * @param expected Pointer to expected data.
 * @param actual Pointer to actual data.
 * @param len Number of bytes to compare.
 * @param es Stringified expected expression.
 * @param as Stringified actual expression.
 * @param file Source file name.
 * @param line Source line number.
 * @discussion Backend for @c $assert_mem_eq.  Uses memcmp for comparison.
 */
void __builtin_assert_mem_eq(const void *expected, const void *actual, long long len,
                         const char *es, const char *as,
                         const char *file, int line);

/*!
 * @function __builtin_assert_float_within
 * @abstract Assert that a float is within a delta of an expected value.
 * @param delta Maximum allowed absolute difference.
 * @param expected Expected value.
 * @param actual Actual value.
 * @param ds Stringified delta expression.
 * @param es Stringified expected expression.
 * @param as Stringified actual expression.
 * @param file Source file name.
 * @param line Source line number.
 * @discussion Backend for @c $assert_float_within and @c $assert_float_eq.
 *             Passes if |actual - expected| <= delta.
 */
void __builtin_assert_float_within(double delta, double expected, double actual,
                               const char *ds, const char *es, const char *as,
                               const char *file, int line);

/*!
 * @function __builtin_assert_double_within
 * @abstract Assert that a double is within a delta of an expected value.
 * @param delta Maximum allowed absolute difference.
 * @param expected Expected value.
 * @param actual Actual value.
 * @param ds Stringified delta expression.
 * @param es Stringified expected expression.
 * @param as Stringified actual expression.
 * @param file Source file name.
 * @param line Source line number.
 * @discussion Backend for @c $assert_double_within and @c $assert_double_eq.
 *             Passes if |actual - expected| <= delta.
 */
void __builtin_assert_double_within(double delta, double expected, double actual,
                                const char *ds, const char *es, const char *as,
                                const char *file, int line);

/*!
 * @function __builtin_assert_bits
 * @abstract Assert that specific bits match between expected and actual values.
 * @param mask Bit mask to apply.
 * @param expected Expected masked value.
 * @param actual Actual value to mask and compare.
 * @param ms Stringified mask expression.
 * @param es Stringified expected expression.
 * @param as Stringified actual expression.
 * @param file Source file name.
 * @param line Source line number.
 * @discussion Backend for @c $assert_bits.  Passes if
 *             (actual & mask) == (expected & mask).
 */
void __builtin_assert_bits(long long mask, long long expected, long long actual,
                       const char *ms, const char *es, const char *as,
                       const char *file, int line);

/*!
 * @function __builtin_assert_bit_high
 * @abstract Assert that a specific bit is set (high/1).
 * @param bit Zero-based bit index.
 * @param actual Value to inspect.
 * @param bs Stringified bit index expression.
 * @param as Stringified actual expression.
 * @param file Source file name.
 * @param line Source line number.
 * @discussion Backend for @c $assert_bit_high.  Passes if (actual >> bit) & 1.
 */
void __builtin_assert_bit_high(int bit, long long actual,
                           const char *bs, const char *as,
                           const char *file, int line);

/*!
 * @function __builtin_assert_bit_low
 * @abstract Assert that a specific bit is clear (low/0).
 * @param bit Zero-based bit index.
 * @param actual Value to inspect.
 * @param bs Stringified bit index expression.
 * @param as Stringified actual expression.
 * @param file Source file name.
 * @param line Source line number.
 * @discussion Backend for @c $assert_bit_low.  Passes if !((actual >> bit) & 1).
 */
void __builtin_assert_bit_low(int bit, long long actual,
                          const char *bs, const char *as,
                          const char *file, int line);

/*!
 * @function __builtin_assert_eq_array
 * @abstract Assert that two arrays are element-by-element equal.
 * @param expected Pointer to expected array.
 * @param actual Pointer to actual array.
 * @param elem_size Size of each element in bytes.
 * @param count Number of elements to compare.
 * @param es Stringified expected expression.
 * @param as Stringified actual expression.
 * @param file Source file name.
 * @param line Source line number.
 * @discussion Backend for @c $assert_eq_array.  Uses memcmp internally.
 */
void __builtin_assert_eq_array(const void *expected, const void *actual,
                           long long elem_size, long long count,
                           const char *es, const char *as,
                           const char *file, int line);

// ============================================================================
// Message-appending variants
// ============================================================================

/*!
 * @function __builtin_assert_msg
 * @abstract Assert a condition is true with a custom message.
 * @param cond The boolean condition (must be non-zero to pass).
 * @param expr Stringified expression for diagnostic output.
 * @param msg Custom failure message.
 * @param file Source file name.
 * @param line Source line number.
 * @discussion Backend for @c $assert_msg and @c $assert_true_msg.
 */
void __builtin_assert_msg(int cond, const char *expr, const char *msg,
                      const char *file, int line);

/*!
 * @function __builtin_assert_eq_msg
 * @abstract Assert two integers are equal with a custom message.
 * @param a Left-hand operand.
 * @param b Right-hand operand.
 * @param as Stringified left expression.
 * @param bs Stringified right expression.
 * @param msg Custom failure message.
 * @param file Source file name.
 * @param line Source line number.
 * @discussion Backend for @c $assert_eq_msg.
 */
void __builtin_assert_eq_msg(long long a, long long b,
                         const char *as, const char *bs, const char *msg,
                         const char *file, int line);

/*!
 * @function __builtin_assert_streq_msg
 * @abstract Assert two C strings are equal with a custom message.
 * @param a Expected string.
 * @param b Actual string.
 * @param as Stringified expected expression.
 * @param bs Stringified actual expression.
 * @param msg Custom failure message.
 * @param file Source file name.
 * @param line Source line number.
 * @discussion Backend for @c $assert_streq_msg.
 */
void __builtin_assert_streq_msg(const char *a, const char *b,
                            const char *as, const char *bs, const char *msg,
                            const char *file, int line);

/*!
 * @function __builtin_assert_null_msg
 * @abstract Assert a pointer is NULL with a custom message.
 * @param p The pointer to check.
 * @param ps Stringified pointer expression.
 * @param msg Custom failure message.
 * @param file Source file name.
 * @param line Source line number.
 * @discussion Backend for @c $assert_null_msg.
 */
void __builtin_assert_null_msg(const void *p, const char *ps, const char *msg,
                           const char *file, int line);

/*!
 * @function __builtin_assert_not_null_msg
 * @abstract Assert a pointer is not NULL with a custom message.
 * @param p The pointer to check.
 * @param ps Stringified pointer expression.
 * @param msg Custom failure message.
 * @param file Source file name.
 * @param line Source line number.
 * @discussion Backend for @c $assert_not_null_msg.
 */
void __builtin_assert_not_null_msg(const void *p, const char *ps, const char *msg,
                               const char *file, int line);

/*!
 * @function __builtin_assert_bits_msg
 * @abstract Assert specific bits match with a custom message.
 * @param mask Bit mask to apply.
 * @param expected Expected masked value.
 * @param actual Actual value to mask and compare.
 * @param ms Stringified mask expression.
 * @param es Stringified expected expression.
 * @param as Stringified actual expression.
 * @param msg Custom failure message.
 * @param file Source file name.
 * @param line Source line number.
 * @discussion Backend for @c $assert_bits_msg.  Passes if
 *             (actual & mask) == (expected & mask).
 */
void __builtin_assert_bits_msg(long long mask, long long expected, long long actual,
                           const char *ms, const char *es, const char *as,
                           const char *msg, const char *file, int line);

// ============================================================================
// Basic Validity Assertions
// ============================================================================

/*!
 * @define $assert
 * @abstract Assert that a condition is true.
 * @param cond The condition to test.
 * @discussion Expands to __builtin_assert with stringified expression,
 *             source file, and line.  Equivalent to @c $assert_true.
 */
#define $assert(cond) \
    __builtin_assert(!!(cond), #cond, __FILE__, __LINE__)

/*!
 * @define $assert_true
 * @abstract Assert that a condition is true (alias for $assert).
 * @param cond The condition to test.
 */
#define $assert_true(cond) \
    $assert(cond)

/*!
 * @define $assert_false
 * @abstract Assert that a condition is false.
 * @param cond The condition to test (must evaluate to zero).
 */
#define $assert_false(cond) \
    __builtin_assert_false(!!(cond), #cond, __FILE__, __LINE__)

/*!
 * @define $assert_fail
 * @abstract Unconditionally fail the current test.
 */
#define $assert_fail() \
    __builtin_assert_fail(__FILE__, __LINE__)

/*!
 * @define $assert_fail_msg
 * @abstract Unconditionally fail the current test with a custom message.
 * @param msg Custom failure message.
 */
#define $assert_fail_msg(msg) \
    __builtin_assert_fail_msg(msg, __FILE__, __LINE__)

// ============================================================================
// Numerical Assertions: Integers
// ============================================================================

/*!
 * @define $assert_eq
 * @abstract Assert two integers are equal.
 * @param a Left-hand operand.
 * @param b Right-hand operand.
 */
#define $assert_eq(a, b) \
    __builtin_assert_eq((long long)(a), (long long)(b), #a, #b, __FILE__, __LINE__)

/*!
 * @define $assert_neq
 * @abstract Assert two integers are not equal.
 * @param a Left-hand operand.
 * @param b Right-hand operand.
 */
#define $assert_neq(a, b) \
    __builtin_assert_neq((long long)(a), (long long)(b), #a, #b, __FILE__, __LINE__)

/*!
 * @define $assert_gt
 * @abstract Assert one integer is greater than another.
 * @param a Left-hand operand.
 * @param b Right-hand operand.
 */
#define $assert_gt(a, b) \
    __builtin_assert_gt((long long)(a), (long long)(b), #a, #b, __FILE__, __LINE__)

/*!
 * @define $assert_lt
 * @abstract Assert one integer is less than another.
 * @param a Left-hand operand.
 * @param b Right-hand operand.
 */
#define $assert_lt(a, b) \
    __builtin_assert_lt((long long)(a), (long long)(b), #a, #b, __FILE__, __LINE__)

/*!
 * @define $assert_ge
 * @abstract Assert one integer is greater than or equal to another.
 * @param a Left-hand operand.
 * @param b Right-hand operand.
 */
#define $assert_ge(a, b) \
    __builtin_assert_ge((long long)(a), (long long)(b), #a, #b, __FILE__, __LINE__)

/*!
 * @define $assert_le
 * @abstract Assert one integer is less than or equal to another.
 * @param a Left-hand operand.
 * @param b Right-hand operand.
 */
#define $assert_le(a, b) \
    __builtin_assert_le((long long)(a), (long long)(b), #a, #b, __FILE__, __LINE__)

/*!
 * @define $assert_within
 * @abstract Assert an integer is within a delta of an expected value.
 * @param delta Maximum allowed absolute difference.
 * @param expected Expected value.
 * @param actual Actual value.
 * @discussion Passes if |actual - expected| <= delta.
 */
#define $assert_within(delta, expected, actual) \
    __builtin_assert_within((long long)(delta), (long long)(expected), (long long)(actual), \
                        #delta, #expected, #actual, __FILE__, __LINE__)

// ============================================================================
// Numerical Assertions: Bitwise
// ============================================================================

/*!
 * @define $assert_bits
 * @abstract Assert that specific bits match between expected and actual values.
 * @param mask Bit mask to apply.
 * @param expected Expected masked value.
 * @param actual Actual value to mask and compare.
 * @discussion Passes if (actual & mask) == (expected & mask).
 */
#define $assert_bits(mask, expected, actual) \
    __builtin_assert_bits((long long)(mask), (long long)(expected), (long long)(actual), \
                      #mask, #expected, #actual, __FILE__, __LINE__)

/*!
 * @define $assert_bit_high
 * @abstract Assert that a specific bit is set (high/1).
 * @param bit Zero-based bit index.
 * @param actual Value to inspect.
 * @discussion Passes if (actual >> bit) & 1.
 */
#define $assert_bit_high(bit, actual) \
    __builtin_assert_bit_high((int)(bit), (long long)(actual), \
                          #bit, #actual, __FILE__, __LINE__)

/*!
 * @define $assert_bit_low
 * @abstract Assert that a specific bit is clear (low/0).
 * @param bit Zero-based bit index.
 * @param actual Value to inspect.
 * @discussion Passes if !((actual >> bit) & 1).
 */
#define $assert_bit_low(bit, actual) \
    __builtin_assert_bit_low((int)(bit), (long long)(actual), \
                         #bit, #actual, __FILE__, __LINE__)

// ============================================================================
// Numerical Assertions: Floats / Doubles
// ============================================================================

/*!
 * @define $assert_float_within
 * @abstract Assert a float is within a delta of an expected value.
 * @param delta Maximum allowed absolute difference.
 * @param expected Expected value.
 * @param actual Actual value.
 */
#define $assert_float_within(delta, expected, actual) \
    __builtin_assert_float_within((double)(delta), (double)(expected), (double)(actual), \
                              #delta, #expected, #actual, __FILE__, __LINE__)

/*!
 * @define $assert_double_within
 * @abstract Assert a double is within a delta of an expected value.
 * @param delta Maximum allowed absolute difference.
 * @param expected Expected value.
 * @param actual Actual value.
 */
#define $assert_double_within(delta, expected, actual) \
    __builtin_assert_double_within((double)(delta), (double)(expected), (double)(actual), \
                               #delta, #expected, #actual, __FILE__, __LINE__)

/*!
 * @define $assert_float_eq
 * @abstract Assert two floats are approximately equal (default epsilon 1e-6).
 * @param expected Expected value.
 * @param actual Actual value.
 * @discussion Uses a default delta of 1e-6.
 */
#define $assert_float_eq(expected, actual) \
    __builtin_assert_float_within(1e-6, (double)(expected), (double)(actual), \
                              #expected, #expected, #actual, __FILE__, __LINE__)

/*!
 * @define $assert_double_eq
 * @abstract Assert two doubles are approximately equal (default epsilon 1e-15).
 * @param expected Expected value.
 * @param actual Actual value.
 * @discussion Uses a default delta of 1e-15.
 */
#define $assert_double_eq(expected, actual) \
    __builtin_assert_double_within(1e-15, (double)(expected), (double)(actual), \
                               #expected, #expected, #actual, __FILE__, __LINE__)

// ============================================================================
// Pointer Assertions
// ============================================================================

/*!
 * @define $assert_null
 * @abstract Assert a pointer is NULL.
 * @param p The pointer to check.
 */
#define $assert_null(p) \
    __builtin_assert_null((const void *)(p), #p, __FILE__, __LINE__)

/*!
 * @define $assert_not_null
 * @abstract Assert a pointer is not NULL.
 * @param p The pointer to check.
 */
#define $assert_not_null(p) \
    __builtin_assert_not_null((const void *)(p), #p, __FILE__, __LINE__)

// ============================================================================
// String Assertions
// ============================================================================

/*!
 * @define $assert_streq
 * @abstract Assert two C strings are equal.
 * @param a Expected string.
 * @param b Actual string.
 */
#define $assert_streq(a, b) \
    __builtin_assert_streq((a), (b), #a, #b, __FILE__, __LINE__)

/*!
 * @define $assert_streq_len
 * @abstract Assert two C strings are equal up to a given length.
 * @param a Expected string.
 * @param b Actual string.
 * @param len Maximum number of characters to compare.
 */
#define $assert_streq_len(a, b, len) \
    __builtin_assert_streq_len((a), (b), (long long)(len), #a, #b, __FILE__, __LINE__)

// ============================================================================
// Memory Assertions
// ============================================================================

/*!
 * @define $assert_mem_eq
 * @abstract Assert two memory regions are byte-identical.
 * @param expected Pointer to expected data.
 * @param actual Pointer to actual data.
 * @param len Number of bytes to compare.
 */
#define $assert_mem_eq(expected, actual, len) \
    __builtin_assert_mem_eq((const void *)(expected), (const void *)(actual), \
                        (long long)(len), #expected, #actual, __FILE__, __LINE__)

// ============================================================================
// Array Assertions
// ============================================================================

/*!
 * @define $assert_eq_array
 * @abstract Assert two arrays are element-by-element equal.
 * @param expected Pointer to expected array.
 * @param actual Pointer to actual array.
 * @param count Number of elements (element size is inferred).
 * @discussion Uses sizeof((expected)[0]) to infer element size.
 */
#define $assert_eq_array(expected, actual, count) \
    __builtin_assert_eq_array((const void *)(expected), (const void *)(actual), \
                          (long long)sizeof((expected)[0]), (long long)(count), \
                          #expected, #actual, __FILE__, __LINE__)

/*!
 * @define $assert_each_eq
 * @abstract Assert each element of an array equals a scalar expected value.
 * @param expected Scalar expected value.
 * @param actual Array expression.
 * @param count Number of elements.
 * @discussion Iterates over the array and compares each element individually.
 *             On failure, reports the failing index as "[i]".
 */
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

/*!
 * @define $assert_msg
 * @abstract Assert a condition is true with a custom message.
 * @param cond The condition to test.
 * @param msg Custom failure message.
 */
#define $assert_msg(cond, msg) \
    __builtin_assert_msg(!!(cond), #cond, msg, __FILE__, __LINE__)

/*!
 * @define $assert_true_msg
 * @abstract Assert a condition is true with a custom message (alias for $assert_msg).
 * @param cond The condition to test.
 * @param msg Custom failure message.
 */
#define $assert_true_msg(cond, msg) \
    $assert_msg(cond, msg)

/*!
 * @define $assert_false_msg
 * @abstract Assert a condition is false with a custom message.
 * @param cond The condition to test.
 * @param msg Custom failure message.
 */
#define $assert_false_msg(cond, msg) \
    __builtin_assert_msg(!(cond), #cond, msg, __FILE__, __LINE__)

/*!
 * @define $assert_eq_msg
 * @abstract Assert two integers are equal with a custom message.
 * @param a Left-hand operand.
 * @param b Right-hand operand.
 * @param msg Custom failure message.
 */
#define $assert_eq_msg(a, b, msg) \
    __builtin_assert_eq_msg((long long)(a), (long long)(b), #a, #b, msg, \
                        __FILE__, __LINE__)

/*!
 * @define $assert_streq_msg
 * @abstract Assert two C strings are equal with a custom message.
 * @param a Expected string.
 * @param b Actual string.
 * @param msg Custom failure message.
 */
#define $assert_streq_msg(a, b, msg) \
    __builtin_assert_streq_msg((a), (b), #a, #b, msg, __FILE__, __LINE__)

/*!
 * @define $assert_null_msg
 * @abstract Assert a pointer is NULL with a custom message.
 * @param p The pointer to check.
 * @param msg Custom failure message.
 */
#define $assert_null_msg(p, msg) \
    __builtin_assert_null_msg((const void *)(p), #p, msg, __FILE__, __LINE__)

/*!
 * @define $assert_not_null_msg
 * @abstract Assert a pointer is not NULL with a custom message.
 * @param p The pointer to check.
 * @param msg Custom failure message.
 */
#define $assert_not_null_msg(p, msg) \
    __builtin_assert_not_null_msg((const void *)(p), #p, msg, __FILE__, __LINE__)

/*!
 * @define $assert_bits_msg
 * @abstract Assert specific bits match with a custom message.
 * @param mask Bit mask to apply.
 * @param expected Expected masked value.
 * @param actual Actual value to mask and compare.
 * @param msg Custom failure message.
 */
#define $assert_bits_msg(mask, expected, actual, msg) \
    __builtin_assert_bits_msg((long long)(mask), (long long)(expected), (long long)(actual), \
                          #mask, #expected, #actual, msg, __FILE__, __LINE__)
