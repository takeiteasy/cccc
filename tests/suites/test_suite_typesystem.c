// CCCC_FLAGS: --testing
// Consolidated suite: type system: typedef, typeof, generics, int128, unsigned,
// bitwise Source tests: test_bitwise, test_generic, test_int128,
// test_int_sizes, test_keywords_accepted, test_keywords_simple,
// test_multireg_basic, test_sizeof_expressions, test_stdint_constant_macros,
// test_type_conversions, test_typedef_advanced, test_typeof,
// test_typeof_generic, test_unary, test_unsigned_ops,
//   test_typedef_simple, test_typedef_struct, test_typedef_enum_union,
//   test_typedef_funcptr, test_typedef_comprehensive

#include <complex.h>
#include <stdint.h>

// [from test_generic]
// Test _Generic type-generic expressions
// Tests compile-time type selection based on controlling expression type

// Helper functions to identify which type was selected

static int int_func(int x) {
    return 1;
}

static int long_func(long x) {
    return 2;
}

static int float_func(float x) {
    return 3;
}

static int double_func(double x) {
    return 4;
}

static int ptr_func(int *x) {
    return 5;
}

static int char_func(char x) {
    return 6;
}

static int default_func(void *x) {
    return 99;
}

// [from test_int128]
// GNU __int128 / __int128_t / __uint128_t support, implemented on top of the
// _BitInt(128) machinery. Exercises the spellings, signed/unsigned arithmetic,
// unary negation/complement, 128-bit-wide overflow, and feature detection.

#ifndef __SIZEOF_INT128__
#error "__SIZEOF_INT128__ should be defined when __int128 is supported"
#endif

_Static_assert(sizeof(__int128) == 16, "__int128 must be 16 bytes");
_Static_assert(sizeof(__uint128_t) == 16, "__uint128_t must be 16 bytes");
_Static_assert(sizeof(unsigned __int128) == 16,
               "unsigned __int128 must be 16 bytes");

// [from test_int_sizes]
// Test that different integer types have correct sizes
// char=1, short=2, int=4, long=8

// [from test_keywords_accepted]
/*
 * Test that volatile, restrict, register, and inline keywords
 * are accepted by the compiler (even though they may be ignored)
 */

// Test inline functions

static inline int inline_add(int a, int b) {
    return a + b;
}

// Test static inline (common pattern)

static inline int static_inline_multiply(int a, int b) {
    return a * b;
}

// Test volatile (commonly used for hardware registers, signal handlers)

static int test_volatile() {
    volatile int x = 10;
    x              = 20;
    return x;
}

// Test register (optimization hint from old C code)

static int test_register() {
    register int i;
    register int sum = 0;
    for (i = 0; i < 10; i++) {
        sum += i;
    }
    return sum;
}

// Test restrict (pointer aliasing hint)

static void test_restrict(int *restrict a, int *restrict b, int n) {
    for (int i = 0; i < n; i++) {
        a[i] = b[i] + 1;
    }
}

// Test __restrict and __restrict__ variants

static void test_restrict_variants(int *__restrict a, int *__restrict__ b) {
    *a = *b + 1;
}

// Test volatile in function parameters

static int test_volatile_param(volatile int *ptr) {
    return *ptr;
}

// Test const volatile (common for memory-mapped I/O)

static int test_const_volatile() {
    const volatile int x = 42;
    return x;
}

// Test inline with extern (external inline function)

static extern inline int extern_inline_subtract(int a, int b) {
    return a - b;
}

// Test multiple qualifiers on pointers
void test_pointer_qualifiers(int *const    p1, // const pointer
                             const int    *p2, // pointer to const
                             volatile int *p3, // pointer to volatile
                             int *restrict p4, // restrict pointer
                             const volatile int *restrict p5 // all qualifiers
) {
    // Just test that declaration is accepted (use proper (void) casts)
    (void)p1;
    (void)p2;
    (void)p3;
    (void)p4;
    (void)p5;
}

// Test in array dimensions

static void test_array_restrict(int arr[restrict static 10]) {
    arr[0] = 1;
}

// Test auto (C23 type inference)

static int test_auto() {
    auto x = 5;
    return x;
}

// [from test_keywords_simple]
/*
 * Simple demonstration that volatile, restrict, register, and inline
 * keywords are now accepted by the CCCC compiler
 */

// Inline function example

static inline int add(int x, int y) {
    return x + y;
}

// Static inline (common C99/C11 pattern)

static inline int square(int x) {
    return x * x;
}

// [from test_multireg_basic]
// Test for multi-register opcodes
// This test validates the new ADD3, SUB3, etc. opcodes work correctly

// [from test_sizeof_expressions]
// Test sizeof with various expressions and contexts

// [from test_stdint_constant_macros]
// <stdint.h> must provide the C99 7.18.4 integer-constant macros
// (INTn_C / UINTn_C / INTMAX_C / UINTMAX_C).  They were missing, so e.g.
// UINT64_C(x) parsed as a call to an undeclared function and was rejected as
// "not a compile-time constant" inside a static initializer (the libsqlite
// "static const u64 aBase[] = { UINT64_C(0x80...) }" idiom).

// Must be usable in a static, constant initializer.
static const uint64_t TAB[] = {
    UINT64_C(0x8000000000000000),
    UINT64_C(0xa000000000000000),
};

// [from test_type_conversions]
// Test comprehensive type conversions and promotions (C99 compliance)
// Tests integer promotions, usual arithmetic conversions, explicit casts

// [from test_typedef_advanced]
// Test nested and chained typedefs

typedef int     Integer;
typedef Integer MyInt;    // Typedef of typedef
typedef MyInt  *MyIntPtr; // Typedef of typedef with pointer

typedef struct Point {
    int x;
    int y;
} Point, *PointPtr; // Multiple typedefs in one declaration

// Typedef of array of pointers
typedef int *PtrArray[3];

// Typedef of pointer to array
typedef int (*ArrayPtr)[5];

// Typedef of function returning pointer
typedef int *(*FuncReturningPtr)(int);

static int get_value(int x) {
    return x;
}

// [from test_typeof]
// Test typeof operator
// Tests compile-time type inquiry for variables, expressions, and complex types

// [from test_typeof_generic]
// Test typeof and _Generic working together
// Demonstrates how these features complement each other for generic programming

// Generic max macro using typeof and _Generic
#define max(a, b)                                                              \
    ({                                                                         \
        typeof(a) _a = (a);                                                    \
        typeof(b) _b = (b);                                                    \
        _Generic(_a,                                                           \
            int: (_a > _b ? _a : _b),                                          \
            long: (_a > _b ? _a : _b),                                         \
            double: (_a > _b ? _a : _b),                                       \
            default: (_a > _b ? _a : _b));                                     \
    })

// Generic swap macro using typeof
#define swap(a, b)                                                             \
    do {                                                                       \
        typeof(a) _tmp = (a);                                                  \
        (a)            = (b);                                                  \
        (b)            = _tmp;                                                 \
    } while (0)

// Type-safe print function selector using _Generic

static int print_int(int x) {
    return 1;
}

static int print_long(long x) {
    return 2;
}

static int print_double(double x) {
    return 3;
}

static int print_char(char x) {
    return 4;
}

#define print(x)                                                               \
    _Generic((x),                                                              \
        int: print_int,                                                        \
        long: print_long,                                                      \
        double: print_double,                                                  \
        char: print_char)(x)

// [from test_unsigned_ops]
// Test USHR3, UDIV3, UMOD3 unsigned arithmetic opcodes.
// Uses unsigned long long with the high bit set so results genuinely differ
// from their signed counterparts — the test fails on the old SHR3/DIV3/MOD3
// opcodes and passes only when the correct unsigned ops are emitted.

#pragma cccc suite begin "typesystem"

// test_bitwise
[[cccc::test(return = 42)]]
int test_bitwise(void) {
    int a = 12; // 1100 in binary
    int b = 10; // 1010 in binary

    // Bitwise operations
    int or_result  = a | b;   // 1110 = 14
    int xor_result = a ^ b;   // 0110 = 6
    int and_result = a & b;   // 1000 = 8
    int shl_result = 1 << 3;  // 8
    int shr_result = 16 >> 2; // 4

    // Sum: 14 + 6 + 8 + 8 + 4 = 40
    int result = or_result + xor_result + and_result + shl_result + shr_result;

    if (result != 40)
        return 1; // Assert result == 40
    return 42;
}

// test_generic
[[cccc::test(return = 42)]]
int test_generic(void) {
    // Test 1: Basic _Generic with int
    int i      = 10;
    int result = _Generic(i,
        int: int_func,
        long: long_func,
        double: double_func,
        default: default_func)(i);
    if (result != 1)
        return 1;

    // Test 2: _Generic with long
    long l = 20;
    result = _Generic(l,
        int: int_func,
        long: long_func,
        double: double_func,
        default: default_func)(l);
    if (result != 2)
        return 2;

    // Test 3: _Generic with double
    double d = 3.14;
    result   = _Generic(d,
        int: int_func,
        long: long_func,
        double: double_func,
        default: default_func)(d);
    if (result != 4)
        return 3;

    // Test 4: _Generic with pointer
    int *p = &i;
    result = _Generic(p,
        int: int_func,
        int *: ptr_func,
        double: double_func,
        default: default_func)(p);
    if (result != 5)
        return 4;

    // Test 5: _Generic with char
    char c = 'A';
    result = _Generic(c,
        int: int_func,
        char: char_func,
        double: double_func,
        default: default_func)(c);
    if (result != 6)
        return 5;

    // Test 6: _Generic selecting values directly (not functions)
    int value = _Generic(i, int: 100, long: 200, double: 300, default: 999);
    if (value != 100)
        return 6;

    // Test 7: _Generic with default case
    short s = 5;
    result  = _Generic(s,
        int: int_func,
        long: long_func,
        double: double_func,
        default: default_func)(&s);
    if (result != 99)
        return 7;

    // Test 8: _Generic with expression as controlling expression
    result = _Generic(i + l, int: 10, long: 20, double: 30, default: 40);
    if (result != 20)
        return 8; // int + long = long

    // Test 9: _Generic with const type
    const int ci = 50;
    value        = _Generic(ci, int: 111, const int: 222, default: 333);
    if (value != 111)
        return 9;

    // Test 10: Multiple _Generic in same expression
    int sum =
        _Generic(i, int: 5, default: 0) + _Generic(d, double: 10, default: 0);
    if (sum != 15)
        return 10;

    return 42; // Success
}

// #1223: a _Generic controlling expression of enumerated type matches an
// association naming its implementation-defined underlying integer type
// (and vice versa), exactly as gcc/clang do. An all-non-negative enum that
// fits `int` has underlying type `unsigned int` (#1205); a negative
// enumerator keeps it `int`; a value past UINT32_MAX widens it to 8 bytes;
// a C23 fixed `enum E : T` base wins outright. enum-vs-enum stays a
// distinct-type mismatch. The same fix also gave _Bool and tagged
// struct/union associations a working arm.
enum ug_1223 { UG1 = 1 };           // -> unsigned int
enum sg_1223 { SG1 = -1 };          // -> int
enum wg_1223 { WG1 = 0x100000000 }; // -> 8-byte unsigned
enum other_1223 { OG1 = 1 };
struct sgen_1223 {
    int a;
};

[[cccc::test(return = 42)]]
int test_generic_enum(void) {
    // all-non-negative small enum: underlying type is `unsigned int`
    if (_Generic((enum ug_1223)0, unsigned int: 1, int: 2, default: 0) != 1)
        return 1;
    // enum with a negative enumerator: stays `int`
    if (_Generic((enum sg_1223)0, unsigned int: 1, int: 2, default: 0) != 2)
        return 2;
    // value beyond UINT32_MAX: widened to an 8-byte type (one arm only --
    // is_compatible does not distinguish long from long long)
    if (_Generic((enum wg_1223)0, long: 3, unsigned long: 3, default: 0) != 3)
        return 3;
    // matching direction is symmetric: integer controlling expr, enum arm
    if (_Generic(0u, enum ug_1223: 4, default: 0) != 4)
        return 4;
    // an association naming the enum's own tag still matches by identity
    enum ug_1223 e = UG1;
    if (_Generic(e, enum ug_1223: 5, default: 0) != 5)
        return 5;
    // two separately declared enums are NOT compatible
    if (__builtin_types_compatible_p(enum ug_1223, enum other_1223))
        return 6;
    // enum <-> its underlying type IS compatible per the builtin
    if (!__builtin_types_compatible_p(enum ug_1223, unsigned int))
        return 7;
    // _Bool association arm now works (was falling through to default)
    _Bool b = 1;
    if (_Generic(b, _Bool: 8, int: 9, default: 0) != 8)
        return 8;
    // tagged struct association arm now works (control type keeps origin)
    struct sgen_1223 s = {0};
    if (_Generic(s, struct sgen_1223: 9, default: 0) != 9)
        return 9;
    return 42;
}

// #1224: C23 6.7.11p2 forbids two associations with compatible types, and
// CCCC now diagnoses it (see the tests/test_generic_*_fail.c negatives).
// This pins the pairs that must NOT be flagged as a collision: `long` /
// `long long` (CCCC models both as one type, and the stdlib headers pair
// them in a single _Generic), and const-qualified vs unqualified pointee
// (`char *` / `const char *`, the <string.h> dispatch idiom). The mere
// fact that this function compiles is the const-pointer half of the test;
// the return values exercise the long / long long half.
[[cccc::test(return = 42)]]
int test_generic_no_false_collision(void) {
    long x = 0;
    if (_Generic(x, long: 1, long long: 2, default: 0) != 1)
        return 1;
    unsigned long u = 0;
    if (_Generic(u, unsigned long: 1, unsigned long long: 2, default: 0) != 1)
        return 2;
    char *p = 0;
    if (_Generic(p, char *: 3, const char *: 4, default: 0) != 3)
        return 3;
    const char *cp = 0;
    (void)_Generic(cp, char *: 3, const char *: 4, default: 0);
    return 42;
}

// #1225: _Generic *selection* (not just the collision diagnostic) honors
// pointee qualifiers, so the matched arm no longer depends on arm order for
// a `char *` / `const char *` pair -- this is what makes <string.h>'s
// const-correct dispatch macros pick the right return type. A top-level
// qualifier on an association is stripped from the controlling expression by
// lvalue conversion, so such an arm can never be selected (matches
// gcc/clang). __builtin_types_compatible_p follows the same rule below the
// top level while ignoring top-level qualifiers.
[[cccc::test(return = 42)]]
int test_generic_pointee_qualifiers(void) {
    char       *p  = 0;
    const char *cp = 0;

    // both arm orders: the controlling type decides, not the listing order
    if (_Generic(p, char *: 1, const char *: 2, default: 0) != 1)
        return 1;
    if (_Generic(p, const char *: 2, char *: 1, default: 0) != 1)
        return 2;
    if (_Generic(cp, char *: 1, const char *: 2, default: 0) != 2)
        return 3;
    if (_Generic(cp, const char *: 2, char *: 1, default: 0) != 2)
        return 4;

    // one level down
    char       **pp  = 0;
    const char **cpp = 0;
    if (_Generic(pp, char **: 1, const char **: 2, default: 0) != 1)
        return 5;
    if (_Generic(cpp, char **: 1, const char **: 2, default: 0) != 2)
        return 6;

    // a top-level-qualified association is unreachable after lvalue
    // conversion: an unqualified `int` control never picks the `const int`
    // arm regardless of order
    if (_Generic((int)0, const int: 1, int: 2, default: 0) != 2)
        return 7;

    // __builtin_types_compatible_p: pointee qualifiers count, top-level do
    // not
    if (__builtin_types_compatible_p(char *, const char *))
        return 8;
    if (!__builtin_types_compatible_p(int, const int))
        return 9;
    if (__builtin_types_compatible_p(char **, const char **))
        return 10;
    if (!__builtin_types_compatible_p(char *restrict, char *))
        return 11;
    // qualifier on an intermediate pointer level still counts
    if (__builtin_types_compatible_p(char *const *, char **))
        return 12;
    if (__builtin_types_compatible_p(char *restrict *, char **))
        return 13;

    // the controlling expression's own top-level `restrict` is stripped by
    // lvalue conversion, so a plain `char *` arm still matches
    char *restrict rp = 0;
    if (_Generic(rp, char *: 1, default: 0) != 1)
        return 14;

    return 42;
}

// #1122: file-scope initializers wider than 8 bytes (__int128,
// _BitInt(65..128), long double, _Complex) used to crash write_gvar_data
// (`internal error at src/parse_init.c:1601`) at parse time -- and, for the
// scalar tail specifically, would have silently truncated to 64 bits even
// if the crash were merely papered over, since eval2 is int64_t end-to-end.
// These fold via eval_wide, which reuses the same src/stdlib/wide_bitint.c
// word-array helpers the VM uses for runtime _BitInt(>64) arithmetic, so
// each global below is checked against the identical expression computed
// in a local at runtime (test_wide_global_init) rather than just checked
// for "doesn't crash".
static __int128          g_i128 = ((__int128)1 << 100) + 12345;
static unsigned __int128 g_u128 =
    (unsigned __int128)123456789012345678901234567890uwb;
static _BitInt(65) g_bi65   = ((_BitInt(65))1 << 64) | 7;
static _BitInt(128) g_bi128 = -((_BitInt(128))1 << 100);
static long double     g_ld = 12345.6789L;
static _Complex double g_cd = 3.5;
static _Complex float  g_cf = 2.5f;
// #1208: a complex global with a *non-zero* imaginary part -- `I`/`CMPLX()`
// and complex arithmetic now fold at compile time (eval_complex), and
// serialize_init_bytes emits `__builtin_complex(re, im)` under -c=native/-m
// for these. All three element widths, since the imaginary part's byte
// stride is base->size (4 / 8 / platform-split for long double).
static _Complex double g_cd_i = 3.0 + 4.0 * I;
static _Complex float  g_cf_i = 1.5f + 2.5f * I;
// `long double _Complex`, not `_Complex long double` -- the latter spelling
// hits a pre-existing declspec quirk ("invalid type") unrelated to #1208.
static long double _Complex g_cl_i = 5.0L + 6.0L * I;
static _Complex double g_cd_mul    = (2.0 + 1.0 * I) * (3.0 + 4.0 * I);
static _Complex double g_cd_conj   = conj(7.0 + 8.0 * I);
// #1122: a *narrow* global whose initializer merely contains wide
// arithmetic used to silently fold wrong -- eval2 evaluated the division in
// 64 bits and got 2 instead of the correct value below.
static unsigned long long g_narrow =
    (unsigned long long)((((unsigned __int128)1 << 64) | 7) / 3);

struct WideBitfield1122 {
    // #1122: a `T f : 64` bitfield's mask was computed as `1ULL <<
    // mem->bit_width` for bit_width==64 -- UB, observed to evaluate to 0 on
    // this host, silently discarding the entire field on both the global-
    // initializer RMW path (src/parse_init.c) and ordinary runtime bitfield
    // load/store (src/codegen_expr.c).
    unsigned long long f : 64;
};
static struct WideBitfield1122 g_bf = {0xFFFFFFFFFFFFFFFFull};

// test_wide_global_init
[[cccc::test(return = 42)]]
int test_wide_global_init(void) {
    __int128 l_i128 = ((__int128)1 << 100) + 12345;
    if (g_i128 != l_i128)
        return 1;

    unsigned __int128 l_u128 =
        (unsigned __int128)123456789012345678901234567890uwb;
    if (g_u128 != l_u128)
        return 2;

    _BitInt(65) l_bi65 = ((_BitInt(65))1 << 64) | 7;
    if (g_bi65 != l_bi65)
        return 3;

    _BitInt(128) l_bi128 = -((_BitInt(128))1 << 100);
    if (g_bi128 != l_bi128)
        return 4;

    long double l_ld = 12345.6789L;
    if ((double)g_ld != (double)l_ld)
        return 5;

    if (creal(g_cd) != 3.5)
        return 6;
    if (cimag(g_cd) != 0.0)
        return 7;
    if (crealf(g_cf) != 2.5f)
        return 8;

    unsigned long long l_narrow =
        (unsigned long long)((((unsigned __int128)1 << 64) | 7) / 3);
    if (g_narrow != l_narrow)
        return 9;
    if (g_narrow != 6148914691236517207ULL)
        return 10;

    if (g_bf.f != 0xFFFFFFFFFFFFFFFFull)
        return 11;

    // Wide bitfield RMW in a global initializer: read_buf/write_buf only
    // handled sizes up to 8, so a `_BitInt(128) f : 100;` member crashed
    // (`internal error at src/parse_init.c:1587`) rather than working the
    // way it already did for a local of the same type.
    //
    // #1126 (RESOLVED): serialize_init_bytes's own global-initializer
    // re-extraction (a completely different code path from the
    // write_gvar_data fold this comment is about) used to silently drop
    // bit 90 of gb's value under -c=native/-m, found while adding native
    // coverage for #1125. Fixed with a byte-granular extract -- this
    // assertion is now clean on both backends; dedicated native-corpus
    // coverage lives in tests/test_wide_bitfield_global_init_1126.c.
    struct WideBox1122 {
        _BitInt(128) f : 100;
    };
    static struct WideBox1122 gb = {((_BitInt(128))1 << 90) + 7};
    struct WideBox1122        lb;
    lb.f = ((_BitInt(128))1 << 90) + 7;
    if (gb.f != lb.f)
        return 12;

    // #1208: non-zero-imaginary complex globals, each checked against the
    // identical expression folded in a local (the folder must match
    // gen_complex_expr bit-for-bit, not just "not crash").
    _Complex double l_cd_i = 3.0 + 4.0 * I;
    if (creal(g_cd_i) != creal(l_cd_i) || cimag(g_cd_i) != cimag(l_cd_i))
        return 13;
    _Complex float l_cf_i = 1.5f + 2.5f * I;
    if (crealf(g_cf_i) != crealf(l_cf_i) || cimagf(g_cf_i) != cimagf(l_cf_i))
        return 14;
    long double _Complex l_cl_i = 5.0L + 6.0L * I;
    if ((double)creall(g_cl_i) != (double)creall(l_cl_i) ||
        (double)cimagl(g_cl_i) != (double)cimagl(l_cl_i))
        return 15;
    _Complex double l_cd_mul = (2.0 + 1.0 * I) * (3.0 + 4.0 * I);
    if (creal(g_cd_mul) != creal(l_cd_mul) ||
        cimag(g_cd_mul) != cimag(l_cd_mul))
        return 16;
    if (creal(g_cd_mul) != 2.0 || cimag(g_cd_mul) != 11.0)
        return 17;
    _Complex double l_cd_conj = conj(7.0 + 8.0 * I);
    if (creal(g_cd_conj) != creal(l_cd_conj) ||
        cimag(g_cd_conj) != cimag(l_cd_conj))
        return 18;
    if (creal(g_cd_conj) != 7.0 || cimag(g_cd_conj) != -8.0)
        return 19;

    return 42;
}

// test_int128
[[cccc::test(return = 42)]]
int test_int128(void) {
    // 128-bit overflow: 1e12 * 1e9 = 1e21, well beyond 64 bits.
    __int128           a  = 1000000000000LL;
    __int128           c  = a * 1000000000LL; // 1e21
    unsigned long long hi = (unsigned long long)(c / 1000000000000000000LL);
    if (hi != 1000)
        return 1;

    // Unary negation of a wide value (the path that previously crashed).
    __int128 n = -(a * 1000000LL); // -1e18
    if ((long long)(n / 1000000000000LL) != -1000000)
        return 2;

    // Bitwise complement.
    __int128 m = ~(__int128)5;
    if ((long long)m != -6)
        return 3;

    // unsigned __int128 / __uint128_t agreement and high-word arithmetic.
    __uint128_t u = (__uint128_t)1 << 100;
    if ((unsigned long long)(u >> 100) != 1ULL)
        return 4;
    unsigned __int128 v = u;
    if (v != u)
        return 5;

    // __int128_t alias is signed.
    __int128_t s = -1;
    if (s >= 0)
        return 6;

    // Truthiness must reflect the whole value, not just the low 64 bits.
    __int128 zero = 0;
    __int128 high = (__int128)1 << 100; // nonzero, low word is 0
    if (zero)
        return 7;  // if() on address would wrongly take this
    if (!high)
        return 8;  // ! must see the high bits
    if (!(_Bool)high)
        return 9;  // (_Bool) cast must see the high bits
    if (zero || !high)
        return 10; // || short-circuit truthiness
    if (!(high && !zero))
        return 11; // && truthiness
    int iters = 0;
    for (__int128 i = high; i; i = 0)
        iters++;
    if (iters != 1)
        return 12; // for-condition truthiness

    // #1121: a wb/uwb literal beyond 64 bits (full precision carried
    // out-of-band as digit text, not the truncated node->val) previously
    // serialized truncated to 64 bits under -c=native.
    unsigned __int128 w = 123456789012345678901234567890uwb;
    if ((unsigned long long)(w >> 64) != 6692605942ULL)
        return 13;
    if ((unsigned long long)w != 14083847773837265618ULL)
        return 14;

    // Modulo on a value with a nonzero high word.
    __int128 md = c % 1000000000000000000LL; // 1e21 % 1e18 == 0
    if (md != 0)
        return 15;
    __int128 md2 = (c + 7) % 1000000000000000000LL;
    if (md2 != 7)
        return 16;

    return 42;
}

// test_int_sizes
[[cccc::test(return = 42)]]
int test_int_sizes(void) {
    // Test sizeof for each type
    if (sizeof(char) != 1)
        return 1;
    if (sizeof(short) != 2)
        return 2;
    if (sizeof(int) != 4)
        return 3;
    if (sizeof(long) != 8)
        return 4;

    // Test that values work correctly
    char  c = 100;
    short s = 1000;
    int   i = 100000;
    long  l = 1000000000;

    if (c != 100)
        return 5;
    if (s != 1000)
        return 6;
    if (i != 100000)
        return 7;
    if (l != 1000000000)
        return 8;

    // Test arrays
    int arr[3];
    arr[0] = 10;
    arr[1] = 20;
    arr[2] = 30;

    if (sizeof(arr) != 12)
        return 9; // 3 * 4 bytes
    if (arr[0] + arr[1] + arr[2] != 60)
        return 10;

    // Test struct with mixed types
    struct Mixed {
        char  c;
        short s;
        int   i;
        long  l;
    };

    struct Mixed m;
    m.c = 1;
    m.s = 2;
    m.i = 3;
    m.l = 4;

    if (m.c + m.s + m.i + m.l != 10)
        return 11;

    // Test short arithmetic
    short a = 100;
    short b = 200;
    if (a + b != 300)
        return 12;

    return 42; // Success!
}

// test_keywords_accepted
[[cccc::test(return = 42)]]
int test_keywords_accepted(void) {
    int result = 0;

    // Test inline functions
    result += inline_add(5, 3);             // 8
    result += static_inline_multiply(2, 4); // 8, total: 16

    // Test volatile
    result += test_volatile(); // 20, total: 36

    // Test register
    result += test_register(); // 45 (0+1+...+9), total: 81

    // Test restrict with arrays
    int a[5];
    int b[5] = {1, 2, 3, 4, 5};
    test_restrict(a, b, 5);
    result += a[0]; // 2, total: 83

    // Test restrict variants
    int x = 10, y = 20;
    test_restrict_variants(&x, &y);
    result += x; // 21, total: 104

    // Test volatile parameter
    volatile int v  = 15;
    result         += test_volatile_param(&v); // 15, total: 119

    // Test const volatile
    result += test_const_volatile(); // 42, total: 161

    // Test extern inline
    result += extern_inline_subtract(10, 5); // 5, total: 166

    // Test auto
    result += test_auto(); // 5, total: 171

    if (result != 171)
        return 1;          // Assert result == 171
    return 42;
}

// test_keywords_simple
[[cccc::test(return = 42)]]
int test_keywords_simple(void) {
    // Test register keyword (optimization hint)
    register int i   = 0;
    register int sum = 0;

    // Test volatile keyword (prevents optimization)
    volatile int value = 42;

    // Test restrict keyword (pointer aliasing hint)
    int arr[10];
    int *restrict p = arr;

    // Use the inline functions
    sum = add(10, 20);         // 30
    sum = add(sum, square(5)); // 30 + 25 = 55

    // Use volatile variable
    sum = sum + value; // 55 + 42 = 97

    // Use register variable
    for (i = 0; i < 10; i++) {
        p[i] = i;
    }
    sum = sum + p[5]; // 97 + 5 = 102

    if (sum != 102)
        return 1;
    return 42;
}

// test_multireg_basic
[[cccc::test(return = 42)]]
int test_multireg_basic(void) {
    // Simple arithmetic - currently uses old opcodes
    int a = 10;
    int b = 20;
    int c = a + b; // Should be 30

    if (c != 30)
        return 1;

    int d = c - 5; // Should be 25
    if (d != 25)
        return 2;

    int e = d * 2; // Should be 50
    if (e != 50)
        return 3;

    int f = e / 5; // Should be 10
    if (f != 10)
        return 4;

    int g = f % 3; // Should be 1
    if (g != 1)
        return 5;

    // Comparisons
    if (!(a < b))
        return 6; // 10 < 20 is true
    if (!(b > a))
        return 7; // 20 > 10 is true
    if (!(c == 30))
        return 8;
    if (!(c != 31))
        return 9;
    if (!(a <= 10))
        return 10;
    if (!(b >= 20))
        return 11;

    // Bitwise
    int h = 0xFF & 0x0F; // Should be 0x0F (15)
    if (h != 15)
        return 12;

    int i = 0xF0 | 0x0F; // Should be 0xFF (255)
    if (i != 255)
        return 13;

    int j = 0xFF ^ 0xF0; // Should be 0x0F (15)
    if (j != 15)
        return 14;

    int k = 1 << 4; // Should be 16
    if (k != 16)
        return 15;

    int l = 32 >> 2; // Should be 8
    if (l != 8)
        return 16;

    return 42; // All tests passed
}

// test_sizeof_expressions
[[cccc::test(return = 42)]]
int test_sizeof_expressions(void) {
    int x = 10;
    int arr[10];

    // sizeof with variables
    if (sizeof(x) != 4)
        return 1;

    // sizeof with expressions (should not evaluate expression)
    int counter = 0;
    int size    = sizeof(counter++); // counter should NOT be incremented
    if (counter != 0)
        return 2;                    // Verify counter wasn't changed
    if (size != 4)
        return 3;

    // sizeof with array variables
    if (sizeof(arr) != 40)
        return 4; // 10 * 4

    // sizeof with array elements
    if (sizeof(arr[0]) != 4)
        return 5;

    // sizeof with pointer arithmetic expressions
    if (sizeof(arr[5]) != 4)
        return 6;

    // sizeof with complex expressions
    if (sizeof(x + 10) != 4)
        return 7;
    if (sizeof(x * 2) != 4)
        return 8;

    // sizeof with dereferenced pointers
    int *ptr = &x;
    if (sizeof(*ptr) != 4)
        return 9;

    // sizeof with struct
    struct Point {
        int x;
        int y;
    };
    if (sizeof(struct Point) != 8)
        return 10;

    struct Point p;
    if (sizeof(p) != 8)
        return 11;
    if (sizeof(p.x) != 4)
        return 12;

    return 42; // All tests passed
}

// test_stdint_constant_macros
[[cccc::test(return = 42)]]
int test_stdint_constant_macros(void) {
    if (TAB[0] != 0x8000000000000000ULL)
        return 1;
    if (TAB[1] != 0xa000000000000000ULL)
        return 2;

    // The 64-bit / max-width forms carry (unsigned) long long width, so the
    // high bit survives without truncation or sign issues.
    if (UINT64_C(0xFFFFFFFFFFFFFFFF) != 18446744073709551615ULL)
        return 3;
    if (UINTMAX_C(1) << 63 != 0x8000000000000000ULL)
        return 4;
    if (INT32_C(-5) + UINT32_C(5) != 0)
        return 5;
    if (INT64_C(1) << 40 != 1099511627776LL)
        return 6;

    return 42;
}

// test_type_conversions
[[cccc::test(return = 42)]]
int test_type_conversions(void) {
    int result = 0;

    // Test 1: Integer promotion - char to int
    {
        char c1  = 100;
        char c2  = 50;
        int  sum = c1 + c2; // Both promoted to int before addition
        if (sum != 150)
            return 1;
        result += 1;
    }

    // Test 2: Integer promotion - negative char
    {
        char c = -10;
        int  i = c; // Sign extension should preserve sign
        if (i != -10)
            return 2;
        result += 1;
    }

    // Test 3: Unsigned char to int
    {
        unsigned char uc = 200;
        int           i  = uc; // Should be 200, not -56
        if (i != 200)
            return 3;
        result += 1;
    }

    // Test 4: Short to int promotion
    {
        short s1  = 1000;
        short s2  = 2000;
        int   sum = s1 + s2;
        if (sum != 3000)
            return 4;
        result += 1;
    }

    // Test 5: Usual arithmetic conversions - int and long
    {
        int  i   = 42;
        long l   = 1000;
        long sum = i + l; // i promoted to long
        if (sum != 1042)
            return 5;
        result += 1;
    }

    // Test 6: Mixed signed/unsigned - check basic unsigned behavior
    {
        unsigned int ui  = 100;
        unsigned int ui2 = 200;
        unsigned int sum = ui + ui2;
        if (sum != 300)
            return 6;
        result += 1;
    }

    // Test 7: Explicit cast - truncation
    {
        int  i = 1000;
        char c = (char)i; // Should truncate to 232 (1000 & 0xFF = 0xE8 = 232
                          // unsigned, -24 signed)
        // When treated as signed char, 232 = -24
        if (c != -24)
            return 7;
        result += 1;
    }

    // Test 8: Explicit cast - sign extension
    {
        char c = -1;
        int  i = (int)c; // Should sign-extend to -1
        if (i != -1)
            return 8;
        result += 1;
    }

    // Test 9: Explicit cast - zero extension
    {
        unsigned char uc = 255;
        int           i  = (int)uc; // Should zero-extend to 255
        if (i != 255)
            return 9;
        result += 1;
    }

    // Test 10: Float to int conversion
    {
        float f = 42.7;
        int   i = (int)f; // Should truncate to 42
        if (i != 42)
            return 10;
        result += 1;
    }

    // Test 11: Int to float conversion
    {
        int   i    = 100;
        float f    = (float)i;
        int   back = (int)f;
        if (back != 100)
            return 11;
        result += 1;
    }

    // Test 12: Char arithmetic with promotion
    {
        char a   = 10;
        char b   = 20;
        char c   = 30;
        int  sum = a + b + c; // Each char promoted to int
        if (sum != 60)
            return 12;
        result += 1;
    }

    // Test 13: Short multiplication
    {
        short s1      = 100;
        short s2      = 200;
        int   product = s1 * s2; // Promoted to int
        if (product != 20000)
            return 13;
        result += 1;
    }

    // Test 14: Mixed sizes in expression
    {
        char  c   = 10;
        short s   = 100;
        int   i   = 1000;
        long  l   = 10000;
        long  sum = c + s + i + l; // All promoted to long
        if (sum != 11110)
            return 14;
        result += 1;
    }

    // Test 15: Comparison with different types
    {
        char c = 100;
        int  i = 100;
        if (!(c == i))
            return 15; // c promoted to int for comparison
        result += 1;
    }

    // Test 16: Assignment with implicit conversion
    {
        int   i = 1000;
        short s = i; // Implicit truncation
        // 1000 = 0x3E8, fits in short
        if (s != 1000)
            return 16;
        result += 1;
    }

    // Test 17: Bitwise operations with promotion
    {
        unsigned char c1        = 0x0F;
        unsigned char c2        = 0xF0;
        int           or_result = c1 | c2; // Both promoted to int
        if (or_result != 0xFF)
            return 17;
        result += 1;
    }

    // Test 18: Shift operations
    {
        char c       = 1;
        int  shifted = (int)c
                       << 10; // Explicit cast (workaround for promotion bug)
        if (shifted != 1024)
            return 18;
        result += 1;
    }

    // Test 19: Ternary operator with different types
    {
        int   i          = 1;
        char  c          = 10;
        short s          = 20;
        int   result_val = i ? c : s; // Both c and s promoted to int
        if (result_val != 10)
            return 19;
        result += 1;
    }

    // Test 20: Complex expression with mixed types
    {
        char  c = 5;
        short s = 10;
        int   i = 100;
        long  l = (c * s) +
                  i; // c and s promoted to int, then result promoted to long
        if (l != 150)
            return 20;
        result += 1;
    }

    // All tests passed - result should be 20
    if (result != 20)
        return 1;
    return 42;
}

// test_typedef_advanced
[[cccc::test(return = 42)]]
int test_typedef_advanced(void) {
    // Test chained typedef
    MyInt    a = 10;
    MyIntPtr b = &a;

    // Test multiple typedef names
    Point    p  = {5, 7};
    PointPtr pp = &p;

    // Verify values
    int result = a + p.x + p.y + pp->x + pp->y; // 10 + 5 + 7 + 5 + 7 = 34
    if (result != 34)
        return 1;
    return 42;
}

// test_typeof
[[cccc::test(return = 42)]]
int test_typeof(void) {
    // Test 1: Basic typeof with variables
    int       x = 5;
    typeof(x) y = 10; // y should be int
    if (y != 10)
        return 1;

    // Test 2: typeof with expressions
    typeof(x + y) z = 15; // z should be int (result of int + int)
    if (z != 15)
        return 2;

    // Test 3: typeof with pointer
    int        *ptr  = &x;
    typeof(ptr) ptr2 = &y; // ptr2 should be int*
    *ptr2            = 20;
    if (y != 20)
        return 3;

    // Test 4: typeof with array decay
    int             arr[3] = {1, 2, 3};
    typeof(&arr[0]) p      = arr; // p should be int*
    if (p[1] != 2)
        return 4;

    // Test 5: typeof preserves const
    const int  cx = 42;
    typeof(cx) cy = 100; // cy should be const int
    if (cy != 100)
        return 5;

    // Test 6: typeof with char
    char      c  = 'A';
    typeof(c) c2 = 'B';
    if (c2 != 'B')
        return 6;

    // Test 7: typeof in complex expressions
    typeof(1 + 2.5) d = 3.5; // Should be double (int promotes to double)
    if (d < 3.4 || d > 3.6)
        return 7;

    // Test 8: typeof with comparison result
    typeof(x < y) b = 1; // Should be int (comparison result)
    if (b != 1)
        return 8;

    // Test 9: Nested typeof
    typeof(typeof(x)) w = 30; // w should be int
    if (w != 30)
        return 9;

    // Test 10: typeof with function pointer would require more complex setup
    // Skipping for now as it would need function declarations

    return 42; // Success
}

// test_typeof_generic
[[cccc::test(return = 42)]]
int test_typeof_generic(void) {
    // Test 1: max with ints
    int a = 10, b = 20;
    int result = max(a, b);
    if (result != 20)
        return 1;

    // Test 2: max with doubles
    double dx = 3.14, dy = 2.71;
    double dresult = max(dx, dy);
    if (dresult < 3.13 || dresult > 3.15)
        return 2;

    // Test 3: swap with ints
    int x = 5, y = 15;
    swap(x, y);
    if (x != 15 || y != 5)
        return 3;

    // Test 4: swap with doubles
    double d1 = 1.5, d2 = 2.5;
    swap(d1, d2);
    if (d1 < 2.4 || d1 > 2.6 || d2 < 1.4 || d2 > 1.6)
        return 4;

    // Test 5: Using typeof to declare variables matching function returns
    typeof(max(10, 20)) z = 100; // z should be int
    if (z != 100)
        return 5;

    // Test 6: Generic print selector
    int print_result = print(42);
    if (print_result != 1)
        return 6;

    print_result = print(42L);
    if (print_result != 2)
        return 7;

    print_result = print(3.14);
    if (print_result != 3)
        return 8;

    // Character literals in C are type int, not char
    // So we need to test with a char variable instead
    char test_char = 'A';
    print_result   = print(test_char);
    if (print_result != 4)
        return 9;

    // Test 7: Complex typeof with _Generic
    int                     i            = 5;
    double                  d            = 5.5;
    typeof(_Generic(i > d ? i : d,
               int: 0,
               double: 0.0,
               default: 0)) mixed_result = 7.5;
    // Result should be double since comparison promotes to double
    if (mixed_result < 7.4 || mixed_result > 7.6)
        return 10;

    // Test 8: typeof preserves type through _Generic
    typeof(_Generic(i, int: 100, default: 200)) selected = 150;
    if (selected != 150)
        return 11;

    // Test 9: Array with typeof
    int            arr[5] = {1, 2, 3, 4, 5};
    typeof(arr[0]) sum    = 0;
    for (typeof(arr[0]) idx = 0; idx < 5; idx++) {
        sum = sum + arr[idx];
    }
    if (sum != 15)
        return 12;

    // Test 10: Pointer with typeof and _Generic
    int         value = 999;
    int        *ptr   = &value;
    typeof(ptr) ptr2;
    ptr2 = ptr;
    if (*ptr2 != 999)
        return 13;

    return 42; // Success
}

// test_unary
[[cccc::test(return = 42)]]
int test_unary(void) {
    int a = 5;
    int b = 0;

    // Unary operators
    int neg    = -a; // -5
    int not1   = !b; // 1 (0 is false, so !0 = 1)
    int not2   = !a; // 0 (5 is true, so !5 = 0)
    int bitnot = ~0; // -1 (all bits set)

    // Result: -5 + 1 + 0 + (-1) = -5
    // But we can't return negative, so let's adjust
    int result = (-neg) + not1 + not2 + (-bitnot);
    // = 5 + 1 + 0 + 1 = 7

    if (result != 7)
        return 1; // Assert result == 7
    return 42;
}

// test_unsigned_ops
[[cccc::test(return = 42)]]
int test_unsigned_ops(void) {
    // ---- Direct expressions ----

    // Logical right shift: 0xFFFFFFFFFFFFFFFF >> 4 == 0x0FFFFFFFFFFFFFFF
    // Signed arithmetic shift of -1 >> 4 would stay 0xFFFFFFFFFFFFFFFF.
    unsigned long long shr_result = 0xFFFFFFFFFFFFFFFFULL >> 4;
    if (shr_result != 0x0FFFFFFFFFFFFFFFULL)
        return 1;

    // Unsigned division: 0xFFFFFFFFFFFFFFFF / 2 == 9223372036854775807
    // Signed: (-1) / 2 == 0.
    unsigned long long div_result = 0xFFFFFFFFFFFFFFFFULL / 2ULL;
    if (div_result != 9223372036854775807ULL)
        return 2;

    // Unsigned modulo: 0xFFFFFFFFFFFFFFFF % 7 == 1
    // Signed: (-1) % 7 == -1 (wraps to 0xFFFFFFFFFFFFFFFF in register).
    unsigned long long mod_result = 0xFFFFFFFFFFFFFFFFULL % 7ULL;
    if (mod_result != 1ULL)
        return 3;

    // ---- Compound assignment forms ----

    unsigned long long a   = 0xFFFFFFFFFFFFFFFFULL;
    a                    >>= 4;
    if (a != 0x0FFFFFFFFFFFFFFFULL)
        return 4;

    unsigned long long b  = 0xFFFFFFFFFFFFFFFFULL;
    b                    /= 2ULL;
    if (b != 9223372036854775807ULL)
        return 5;

    unsigned long long c  = 0xFFFFFFFFFFFFFFFFULL;
    c                    %= 7ULL;
    if (c != 1ULL)
        return 6;

    // ---- Additional boundary values ----

    // Shift by 0 (identity)
    unsigned long long d = 0x8000000000000000ULL;
    if ((d >> 0) != 0x8000000000000000ULL)
        return 7;

    // Shift high bit out
    if ((d >> 63) != 1ULL)
        return 8;

    // Unsigned div where result > LLONG_MAX
    // 0xFFFFFFFFFFFFFFFF / 1 == 0xFFFFFFFFFFFFFFFF
    if ((0xFFFFFFFFFFFFFFFFULL / 1ULL) != 0xFFFFFFFFFFFFFFFFULL)
        return 9;

    // Unsigned mod 1 == 0 always
    if ((0xFFFFFFFFFFFFFFFFULL % 1ULL) != 0ULL)
        return 10;

    return 42;
}

// [from test_typedef_simple]
[[cccc::test(return = 42)]]
int test_typedef_simple(void) {
    typedef int  TdInteger;
    typedef char TdByte;
    TdInteger    x = 10, y = 32;
    TdByte       c = 'A';
    (void)c;
    return x + y;
}

// [from test_typedef_struct]
[[cccc::test(return = 42)]]
int test_typedef_struct(void) {
    typedef struct TdPoint TdPoint;
    struct TdPoint {
        int x;
        int y;
    };
    typedef struct {
        int width;
        int height;
    } TdSize;
    struct TdColor {
        int r;
        int g;
        int b;
    };
    typedef struct TdColor TdColor;
    TdPoint                p;
    p.x = 10;
    p.y = 20;
    TdSize s;
    s.width  = 5;
    s.height = 7;
    TdColor c;
    c.r = 255;
    c.g = 0;
    c.b = 0;
    (void)c;
    return p.x + s.width + s.height + p.y; // 10+5+7+20=42
}

// [from test_typedef_enum_union]
[[cccc::test(return = 42)]]
int test_typedef_enum_union(void) {
    typedef enum { TD_RED, TD_GREEN, TD_BLUE } TdColor2;
    typedef enum TdStatus { TD_INACTIVE, TD_ACTIVE = 10, TD_PENDING } TdStatus;
    typedef union {
        int  i;
        char c;
    } TdData;
    typedef union TdValue TdValue;
    union TdValue {
        int  x;
        long y;
    };
    TdColor2 col = TD_RED;
    (void)col;
    TdStatus st = TD_ACTIVE;
    (void)st;
    TdData d;
    d.i = 42;
    TdValue v;
    v.x = 100;
    (void)v;
    return d.i;
}

// [from test_typedef_funcptr]
// Function pointer typedef — helpers defined at file scope to avoid type-order
// issues.
static int td_funcptr_double_it(int x) {
    return x + x;
}
static int td_funcptr_apply(int (*f)(int), int v) {
    return f(v);
}

[[cccc::test(return = 42)]]
int test_typedef_funcptr(void) {
    typedef int (*TdCallback)(int);
    TdCallback f = td_funcptr_double_it;
    return td_funcptr_apply(f, 21); // 21*2=42
}

// [from test_typedef_comprehensive]
static int td_comp_add(int a, int b) {
    return a + b;
}

[[cccc::test(return = 42)]]
int test_typedef_comprehensive(void) {
    typedef int  TdComp_Integer;
    typedef char TdComp_Byte;
    typedef long TdComp_Long;
    typedef int *TdComp_IntPtr;
    struct TdComp_Point {
        int x;
        int y;
    };
    typedef struct TdComp_Point TdComp_Point;
    typedef struct {
        int width;
        int height;
    } TdComp_Rectangle;
    typedef int (*TdComp_BinaryOp)(int, int);
    typedef int    TdComp_IntArray[5];

    TdComp_Integer x = 10;
    TdComp_Byte    b = 32;
    TdComp_Long    l = 0;
    (void)l;
    TdComp_IntPtr ptr = &x;
    (void)ptr;
    TdComp_Point p;
    p.x = 5;
    p.y = 7;
    (void)p;
    TdComp_Rectangle r;
    r.width  = 3;
    r.height = 4;
    (void)r;
    TdComp_BinaryOp op     = td_comp_add;
    int             result = op(20, 10);
    (void)result; // 30
    TdComp_IntArray arr;
    arr[0] = 1;
    arr[1] = 2;
    (void)arr;
    return x + b; // 10+32=42
}

// #1125: a bitfield whose declared type is a wide _BitInt (N > 64) used to
// crash (VM) or silently corrupt/overrun (parse-time global-init RMW). The
// runtime read/write codegen coverage (bit_offset > 0, two narrow fields
// sharing one wide container) lives in the standalone
// tests/test_wide_bitfield_offsets_1125.c instead of here. This test keeps
// only the piece that was inherently VM-only when written: a
// nonzero-bit_offset *global* initializer compared against the identical
// local, extending test_wide_global_init's case 12 (bit_offset == 0 only)
// -- see this comment's own note there. Native-side too since #1126 landed
// (this file is back on the native corpus).
struct WideBitfieldGlobalOffset1125 {
    _BitInt(128) a : 5;   // bit_offset 0
    _BitInt(128) b : 100; // bit_offset 5 -- straddles into word 1
};
static struct WideBitfieldGlobalOffset1125 g_wbo = {
    -3, ((_BitInt(128))1 << 90) + 7};

// test_wide_bitfield_global_offset
[[cccc::test(return = 42)]]
int test_wide_bitfield_global_offset(void) {
    struct WideBitfieldGlobalOffset1125 l;
    l.a = -3;
    l.b = ((_BitInt(128))1 << 90) + 7;

    if (g_wbo.a != l.a)
        return 1;
    if (g_wbo.b != l.b)
        return 2;

    return 42;
}

// #1124's own width-masking coverage lives in the standalone
// tests/test_bitint_width_semantics_1124.c instead of here.

#pragma cccc suite end
