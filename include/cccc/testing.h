/*!
 * @file testing.h
 * @brief CCCC Test Framework — assertion API injected in --testing mode.
 *
 * This header provides macros and underlying FFI-callable builtins for writing
 * self-verifying test programs.  It is automatically injected when CCCC is
 * invoked with the @c --testing flag and should not be included directly.
 *
 * A test passes if it exits with code 42.
 *
 * ## Usage
 *
 * @code
 * int main(void) {
 *     AssertEq(1 + 1, 2);
 *     AssertStrEq("hello", "hello");
 *     AssertNull(NULL);
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
 * @discussion Backend for @c Assert and @c AssertTrue.
 */
void __builtin_assert(int cond, const char *expr, const char *file, int line);

/*!
 * @function __builtin_assert_false
 * @abstract Assert that a condition is false.
 * @param cond The boolean condition (must be zero to pass).
 * @param expr Stringified expression for diagnostic output.
 * @param file Source file name.
 * @param line Source line number.
 * @discussion Backend for @c AssertFalse.
 */
void __builtin_assert_false(int cond, const char *expr, const char *file, int line);

/*!
 * @function __builtin_assert_fail
 * @abstract Unconditionally fail a test.
 * @param file Source file name.
 * @param line Source line number.
 * @discussion Backend for @c AssertFail.
 */
void __builtin_assert_fail(const char *file, int line);

/*!
 * @function __builtin_assert_fail_msg
 * @abstract Unconditionally fail a test with a custom message.
 * @param msg Custom failure message.
 * @param file Source file name.
 * @param line Source line number.
 * @discussion Backend for @c AssertFailMsg.
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
 * @discussion Backend for @c AssertEq.
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
 * @discussion Backend for @c AssertNeq.
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
 * @discussion Backend for @c AssertGt.
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
 * @discussion Backend for @c AssertLt.
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
 * @discussion Backend for @c AssertGe.
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
 * @discussion Backend for @c AssertLe.
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
 * @discussion Backend for @c AssertWithin.  Passes if
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
 * @discussion Backend for @c AssertNull.
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
 * @discussion Backend for @c AssertNotNull.
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
 * @discussion Backend for @c AssertStrEq.  Uses strcmp for comparison.
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
 * @discussion Backend for @c AssertStrEqLen.  Uses strncmp for comparison.
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
 * @discussion Backend for @c AssertMemEq.  Uses memcmp for comparison.
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
 * @discussion Backend for @c AssertFloatWithin and @c AssertFloatEq.
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
 * @discussion Backend for @c AssertDoubleWithin and @c AssertDoubleEq.
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
 * @discussion Backend for @c AssertBits.  Passes if
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
 * @discussion Backend for @c AssertBitHigh.  Passes if (actual >> bit) & 1.
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
 * @discussion Backend for @c AssertBitLow.  Passes if !((actual >> bit) & 1).
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
 * @discussion Backend for @c AssertArrayEq.  Uses memcmp internally.
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
 * @discussion Backend for @c AssertMsg and @c AssertTrueMsg.
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
 * @discussion Backend for @c AssertEqMsg.
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
 * @discussion Backend for @c AssertStrEqMsg.
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
 * @discussion Backend for @c AssertNullMsg.
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
 * @discussion Backend for @c AssertNotNullMsg.
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
 * @discussion Backend for @c AssertBitsMsg.  Passes if
 *             (actual & mask) == (expected & mask).
 */
void __builtin_assert_bits_msg(long long mask, long long expected, long long actual,
                           const char *ms, const char *es, const char *as,
                           const char *msg, const char *file, int line);

// ============================================================================
// Basic Validity Assertions
// ============================================================================

/*!
 * @define Assert
 * @abstract Assert that a condition is true.
 * @param cond The condition to test.
 * @discussion Expands to __builtin_assert with stringified expression,
 *             source file, and line.  Equivalent to @c AssertTrue.
 */
#define Assert(cond) \
    __builtin_assert(!!(cond), #cond, __FILE__, __LINE__)

/*!
 * @define AssertTrue
 * @abstract Assert that a condition is true (alias for Assert).
 * @param cond The condition to test.
 */
#define AssertTrue(cond) \
    Assert(cond)

/*!
 * @define AssertFalse
 * @abstract Assert that a condition is false.
 * @param cond The condition to test (must evaluate to zero).
 */
#define AssertFalse(cond) \
    __builtin_assert_false(!!(cond), #cond, __FILE__, __LINE__)

/*!
 * @define AssertFail
 * @abstract Unconditionally fail the current test.
 */
#define AssertFail() \
    __builtin_assert_fail(__FILE__, __LINE__)

/*!
 * @define AssertFailMsg
 * @abstract Unconditionally fail the current test with a custom message.
 * @param msg Custom failure message.
 */
#define AssertFailMsg(msg) \
    __builtin_assert_fail_msg(msg, __FILE__, __LINE__)

// ============================================================================
// Numerical Assertions: Integers
// ============================================================================

/*!
 * @define AssertEq
 * @abstract Assert two integers are equal.
 * @param a Left-hand operand.
 * @param b Right-hand operand.
 */
#define AssertEq(a, b) \
    __builtin_assert_eq((long long)(a), (long long)(b), #a, #b, __FILE__, __LINE__)

/*!
 * @define AssertNeq
 * @abstract Assert two integers are not equal.
 * @param a Left-hand operand.
 * @param b Right-hand operand.
 */
#define AssertNeq(a, b) \
    __builtin_assert_neq((long long)(a), (long long)(b), #a, #b, __FILE__, __LINE__)

/*!
 * @define AssertGt
 * @abstract Assert one integer is greater than another.
 * @param a Left-hand operand.
 * @param b Right-hand operand.
 */
#define AssertGt(a, b) \
    __builtin_assert_gt((long long)(a), (long long)(b), #a, #b, __FILE__, __LINE__)

/*!
 * @define AssertLt
 * @abstract Assert one integer is less than another.
 * @param a Left-hand operand.
 * @param b Right-hand operand.
 */
#define AssertLt(a, b) \
    __builtin_assert_lt((long long)(a), (long long)(b), #a, #b, __FILE__, __LINE__)

/*!
 * @define AssertGe
 * @abstract Assert one integer is greater than or equal to another.
 * @param a Left-hand operand.
 * @param b Right-hand operand.
 */
#define AssertGe(a, b) \
    __builtin_assert_ge((long long)(a), (long long)(b), #a, #b, __FILE__, __LINE__)

/*!
 * @define AssertLe
 * @abstract Assert one integer is less than or equal to another.
 * @param a Left-hand operand.
 * @param b Right-hand operand.
 */
#define AssertLe(a, b) \
    __builtin_assert_le((long long)(a), (long long)(b), #a, #b, __FILE__, __LINE__)

/*!
 * @define AssertWithin
 * @abstract Assert an integer is within a delta of an expected value.
 * @param delta Maximum allowed absolute difference.
 * @param expected Expected value.
 * @param actual Actual value.
 * @discussion Passes if |actual - expected| <= delta.
 */
#define AssertWithin(delta, expected, actual) \
    __builtin_assert_within((long long)(delta), (long long)(expected), (long long)(actual), \
                        #delta, #expected, #actual, __FILE__, __LINE__)

// ============================================================================
// Numerical Assertions: Bitwise
// ============================================================================

/*!
 * @define AssertBits
 * @abstract Assert that specific bits match between expected and actual values.
 * @param mask Bit mask to apply.
 * @param expected Expected masked value.
 * @param actual Actual value to mask and compare.
 * @discussion Passes if (actual & mask) == (expected & mask).
 */
#define AssertBits(mask, expected, actual) \
    __builtin_assert_bits((long long)(mask), (long long)(expected), (long long)(actual), \
                      #mask, #expected, #actual, __FILE__, __LINE__)

/*!
 * @define AssertBitHigh
 * @abstract Assert that a specific bit is set (high/1).
 * @param bit Zero-based bit index.
 * @param actual Value to inspect.
 * @discussion Passes if (actual >> bit) & 1.
 */
#define AssertBitHigh(bit, actual) \
    __builtin_assert_bit_high((int)(bit), (long long)(actual), \
                          #bit, #actual, __FILE__, __LINE__)

/*!
 * @define AssertBitLow
 * @abstract Assert that a specific bit is clear (low/0).
 * @param bit Zero-based bit index.
 * @param actual Value to inspect.
 * @discussion Passes if !((actual >> bit) & 1).
 */
#define AssertBitLow(bit, actual) \
    __builtin_assert_bit_low((int)(bit), (long long)(actual), \
                         #bit, #actual, __FILE__, __LINE__)

// ============================================================================
// Numerical Assertions: Floats / Doubles
// ============================================================================

/*!
 * @define AssertFloatWithin
 * @abstract Assert a float is within a delta of an expected value.
 * @param delta Maximum allowed absolute difference.
 * @param expected Expected value.
 * @param actual Actual value.
 */
#define AssertFloatWithin(delta, expected, actual) \
    __builtin_assert_float_within((double)(delta), (double)(expected), (double)(actual), \
                              #delta, #expected, #actual, __FILE__, __LINE__)

/*!
 * @define AssertDoubleWithin
 * @abstract Assert a double is within a delta of an expected value.
 * @param delta Maximum allowed absolute difference.
 * @param expected Expected value.
 * @param actual Actual value.
 */
#define AssertDoubleWithin(delta, expected, actual) \
    __builtin_assert_double_within((double)(delta), (double)(expected), (double)(actual), \
                               #delta, #expected, #actual, __FILE__, __LINE__)

/*!
 * @define AssertFloatEq
 * @abstract Assert two floats are approximately equal (default epsilon 1e-6).
 * @param expected Expected value.
 * @param actual Actual value.
 * @discussion Uses a default delta of 1e-6.
 */
#define AssertFloatEq(expected, actual) \
    __builtin_assert_float_within(1e-6, (double)(expected), (double)(actual), \
                              #expected, #expected, #actual, __FILE__, __LINE__)

/*!
 * @define AssertDoubleEq
 * @abstract Assert two doubles are approximately equal (default epsilon 1e-15).
 * @param expected Expected value.
 * @param actual Actual value.
 * @discussion Uses a default delta of 1e-15.
 */
#define AssertDoubleEq(expected, actual) \
    __builtin_assert_double_within(1e-15, (double)(expected), (double)(actual), \
                               #expected, #expected, #actual, __FILE__, __LINE__)

// ============================================================================
// Pointer Assertions
// ============================================================================

/*!
 * @define AssertNull
 * @abstract Assert a pointer is NULL.
 * @param p The pointer to check.
 */
#define AssertNull(p) \
    __builtin_assert_null((const void *)(p), #p, __FILE__, __LINE__)

/*!
 * @define AssertNotNull
 * @abstract Assert a pointer is not NULL.
 * @param p The pointer to check.
 */
#define AssertNotNull(p) \
    __builtin_assert_not_null((const void *)(p), #p, __FILE__, __LINE__)

// ============================================================================
// String Assertions
// ============================================================================

/*!
 * @define AssertStrEq
 * @abstract Assert two C strings are equal.
 * @param a Expected string.
 * @param b Actual string.
 */
#define AssertStrEq(a, b) \
    __builtin_assert_streq((a), (b), #a, #b, __FILE__, __LINE__)

/*!
 * @define AssertStrEqLen
 * @abstract Assert two C strings are equal up to a given length.
 * @param a Expected string.
 * @param b Actual string.
 * @param len Maximum number of characters to compare.
 */
#define AssertStrEqLen(a, b, len) \
    __builtin_assert_streq_len((a), (b), (long long)(len), #a, #b, __FILE__, __LINE__)

// ============================================================================
// Memory Assertions
// ============================================================================

/*!
 * @define AssertMemEq
 * @abstract Assert two memory regions are byte-identical.
 * @param expected Pointer to expected data.
 * @param actual Pointer to actual data.
 * @param len Number of bytes to compare.
 */
#define AssertMemEq(expected, actual, len) \
    __builtin_assert_mem_eq((const void *)(expected), (const void *)(actual), \
                        (long long)(len), #expected, #actual, __FILE__, __LINE__)

// ============================================================================
// Array Assertions
// ============================================================================

/*!
 * @define AssertArrayEq
 * @abstract Assert two arrays are element-by-element equal.
 * @param expected Pointer to expected array.
 * @param actual Pointer to actual array.
 * @param count Number of elements (element size is inferred).
 * @discussion Uses sizeof((expected)[0]) to infer element size.
 */
#define AssertArrayEq(expected, actual, count) \
    __builtin_assert_eq_array((const void *)(expected), (const void *)(actual), \
                          (long long)sizeof((expected)[0]), (long long)(count), \
                          #expected, #actual, __FILE__, __LINE__)

/*!
 * @define AssertEachEq
 * @abstract Assert each element of an array equals a scalar expected value.
 * @param expected Scalar expected value.
 * @param actual Array expression.
 * @param count Number of elements.
 * @discussion Iterates over the array and compares each element individually.
 *             On failure, reports the failing index as "[i]".
 */
#define AssertEachEq(expected, actual, count) \
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
 * @define AssertMsg
 * @abstract Assert a condition is true with a custom message.
 * @param cond The condition to test.
 * @param msg Custom failure message.
 */
#define AssertMsg(cond, msg) \
    __builtin_assert_msg(!!(cond), #cond, msg, __FILE__, __LINE__)

/*!
 * @define AssertTrueMsg
 * @abstract Assert a condition is true with a custom message (alias for AssertMsg).
 * @param cond The condition to test.
 * @param msg Custom failure message.
 */
#define AssertTrueMsg(cond, msg) \
    AssertMsg(cond, msg)

/*!
 * @define AssertFalseMsg
 * @abstract Assert a condition is false with a custom message.
 * @param cond The condition to test.
 * @param msg Custom failure message.
 */
#define AssertFalseMsg(cond, msg) \
    __builtin_assert_msg(!(cond), #cond, msg, __FILE__, __LINE__)

/*!
 * @define AssertEqMsg
 * @abstract Assert two integers are equal with a custom message.
 * @param a Left-hand operand.
 * @param b Right-hand operand.
 * @param msg Custom failure message.
 */
#define AssertEqMsg(a, b, msg) \
    __builtin_assert_eq_msg((long long)(a), (long long)(b), #a, #b, msg, \
                        __FILE__, __LINE__)

/*!
 * @define AssertStrEqMsg
 * @abstract Assert two C strings are equal with a custom message.
 * @param a Expected string.
 * @param b Actual string.
 * @param msg Custom failure message.
 */
#define AssertStrEqMsg(a, b, msg) \
    __builtin_assert_streq_msg((a), (b), #a, #b, msg, __FILE__, __LINE__)

/*!
 * @define AssertNullMsg
 * @abstract Assert a pointer is NULL with a custom message.
 * @param p The pointer to check.
 * @param msg Custom failure message.
 */
#define AssertNullMsg(p, msg) \
    __builtin_assert_null_msg((const void *)(p), #p, msg, __FILE__, __LINE__)

/*!
 * @define AssertNotNullMsg
 * @abstract Assert a pointer is not NULL with a custom message.
 * @param p The pointer to check.
 * @param msg Custom failure message.
 */
#define AssertNotNullMsg(p, msg) \
    __builtin_assert_not_null_msg((const void *)(p), #p, msg, __FILE__, __LINE__)

/*!
 * @define AssertBitsMsg
 * @abstract Assert specific bits match with a custom message.
 * @param mask Bit mask to apply.
 * @param expected Expected masked value.
 * @param actual Actual value to mask and compare.
 * @param msg Custom failure message.
 */
#define AssertBitsMsg(mask, expected, actual, msg) \
    __builtin_assert_bits_msg((long long)(mask), (long long)(expected), (long long)(actual), \
                          #mask, #expected, #actual, msg, __FILE__, __LINE__)
