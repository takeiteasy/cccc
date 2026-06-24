// CCCC_FLAGS: --testing
// Consolidated suite: C23: _BitInt, auto, nullptr, constexpr, char8_t, embed, literals
// Source tests: test_c23_attributes, test_c23_auto, test_c23_bitint, test_c23_bitint_wide, test_c23_bitint_wide_safety, test_c23_bool_keywords, test_c23_bool_stdbool_compat, test_c23_char8, test_c23_decimal, test_c23_empty_params_void_equiv, test_c23_exp10_pi, test_c23_keywords, test_c23_literals, test_c23_mbrtoc8, test_c23_nullptr, test_c23_preprocessor, test_c23_tag_redeclarations, test_c23_wb_suffix, test_constexpr_basic, test_std_c23_stdbit, test_std_c23_stdckdint, test_std_c23_strtol_binary, test_std_c23_unreachable

#include <stdbool.h>
#include <stddef.h>
#include <math.h>
#include <locale.h>
#include <string.h>
#include <uchar.h>
#include <stdbit.h>
#include <stdckdint.h>
#include <limits.h>
#include <stdlib.h>

// [from test_c23_attributes]
// Test C23 [[...]] attribute syntax
// Layout-neutral attributes are accepted; selected diagnostic attributes have
// warning-system semantics.

// Test attributes on functions (attributes after return type, like __attribute__)
int [[nodiscard]] get_value(void) {
    return 42;
}

int [[deprecated]] old_function(void) {
    return 1;
}

static int [[maybe_unused]] helper(void) {
    return 2;
}

void [[noreturn]] exit_program(void) {
    // Would normally exit; empty body is valid for syntax testing
    for (;;) {}
}

// Test attributes on variables
int [[maybe_unused]] unused_var = 10;

// Test attributes with parameters
int [[deprecated("Use new_function instead")]] old_func2(void) {
    return 3;
}

// Test multiple attributes
int [[nodiscard, maybe_unused]] multi_attr_func(void) {
    return 4;
}

// Test attributes on struct
// Note: Attributes on struct members require more complex handling
struct TestStruct {
    int x;
    int y;
    int z;
};

// Test fallthrough attribute
// Note: [[fallthrough]] requires statement-level attribute support

static int test_fallthrough(int x) {
    int result = 0;
    switch (x) {
        case 1:
            result = 1;
            [[fallthrough]];
        case 2:
            result = 2;
            break;
        case 3:
            result = 3;
            break;
        default:
            result = -1;
    }
    return result;
}

// Test unsequenced and reproducible (function properties)
int [[unsequenced]] pure_func(int a) {
    return a * 2;
}

int [[reproducible]] repro_func(int a) {
    return a + 1;
}

// Main test function

// [from test_c23_auto]
// Test C23 auto type inference

typedef struct { int x; int y; } Point;

// File-scope auto (constant initializer)
auto global_i = 42;
auto global_d = 3.14;

static void void_fn(void) {}

// [from test_c23_bitint]
// Test C23 _BitInt(N) for N in [1, 64]

// [from test_c23_bitint_wide]
// Test C23 _BitInt(N) for N in (64, 65535] — wide multi-word storage (#401, #454)

static _BitInt(128) add128(_BitInt(128) a, _BitInt(128) b) {
    return a + b;
}

static unsigned _BitInt(256) pow2_256(int n) {
    unsigned _BitInt(256) r = 1;
    for (int i = 0; i < n; i++) r = r * 2;
    return r;
}

struct Box { _BitInt(128) v; int tag; };

static unsigned _BitInt(4096) pow2_4096(int n) {
    unsigned _BitInt(4096) r = 1;
    for (int i = 0; i < n; i++) r = r * 2;
    return r;
}

static void fill_box(struct Box *b, _BitInt(128) x) {
    b->v = x;
    b->tag = 1;
}

// [from test_c23_bitint_wide_safety]
// Test wide _BitInt(N>64) arithmetic under safety instrumentation (#457).
// Regression: --safety=standard/-2/-3 previously tripped the
// "UNINITIALIZED VARIABLE READ" trap on all wide _BitInt locals because the
// ND_VAR scalar instrumentation guard did not exclude wide _BitInt (address-
// based storage), while the write path correctly never emitted MARKI/MARKW.

// [from test_c23_bool_keywords]
// Test C23 bool/true/false as keywords, without <stdbool.h>

bool global_flag = true;

static bool is_even(int n) {
    return n % 2 == 0;
}

// [from test_c23_bool_stdbool_compat]
// Test that <stdbool.h> still works under C23, now that bool/true/false
// are keywords (stdbool.h should not redefine them in C23 mode).

// [from test_c23_decimal]
// Test C23 _Decimal32/64/128 types (placeholder: binary float aliases)
// Note: these use binary floating-point, not real IEEE-754-2008 decimal encoding.

// [from test_c23_empty_params_void_equiv]
// In C23, int foo() is equivalent to int foo(void).
// These two declarations must be compatible — no redeclaration error.
static int add();
static int add(void);

static int add(void) {
    return 42;
}

// [from test_c23_exp10_pi]
// Test C23 exp10/exp10f/exp10l and the sinpi/cospi/tanpi/asinpi/acospi/
// atanpi/atan2pi families (ticket #398).
//
// NOTE: the "f" (single-precision float) variants below - exp10f, sinpif,
// cospif, tanpif, asinpif, acospif, atanpif, atan2pif - are registered and
// implemented, but the native FFI bridge cannot currently marshal `float`
// args/returns correctly (pre-existing bug, see ticket #406; affects every
// "f"-suffixed libm function, not just these). They are exercised here for
// side effects only, without checking their results, until #406 is fixed.

static double fabs_d(double x) { return x < 0 ? -x : x; }

// [from test_c23_keywords]
// Test C23 keywords: static_assert and typeof_unqual

// Test static_assert (C23 keyword alias for _Static_assert)
static_assert(1, "static_assert should work");
static_assert(sizeof(int) >= 4, "int size check");
static_assert(sizeof(char) == 1);

// Test typeof_unqual (removes qualifiers)

// [from test_c23_literals]
// Test C23 digit separators (single quotes in numeric literals)

// [from test_c23_mbrtoc8]
// Test C23 mbrtoc8/c8rtomb (ticket #399): incremental UTF-8 <-> char8_t
// conversion, including the (size_t)-3 "queued byte" convention, input
// split across calls, and invalid sequences.

// [from test_c23_nullptr]
// Test C23 nullptr keyword and nullptr_t type

int *global_p = nullptr;

static int *take_ptr(int *p) {
    return p;
}

// [from test_c23_preprocessor]
// Test C23 features: empty initializers
// Note: #elifdef, #elifndef, #warning are implemented but need more testing

// [from test_c23_tag_redeclarations]
struct Point {
    int x;
    int y;
};

struct Point {
    int x;
    int y;
};

union Value {
    int i;
    long l;
};

union Value {
    long l;
    int i;
};

enum Color : unsigned int {
    RED = 1,
    GREEN = 2,
};

enum Color : unsigned int {
    GREEN = 2,
    RED = 1,
};

// [from test_c23_wb_suffix]
// Test C23 wb/uwb integer literal suffixes (ticket #395, wide literals #452)
// These produce _BitInt(N) / unsigned _BitInt(N) values where N is the
// minimum bit width needed to hold the constant.

// [from test_constexpr_basic]
// Test C23 constexpr object guarantees.
constexpr int MAX_SIZE = 5 + 4;
int purrs[MAX_SIZE] = { 0 };

constexpr struct Limits {
    int min;
    int max;
} limits = { 3, MAX_SIZE };

enum { LIMIT_ENUM = MAX_SIZE };
int global_from_constexpr = MAX_SIZE;

// [from test_std_c23_stdbit]
// <stdbit.h> - C23 bit manipulation, full coverage (tickets #390, #393)

// [from test_std_c23_stdckdint]
// <stdckdint.h> - C23 checked integer arithmetic (ticket #390)

// [from test_std_c23_strtol_binary]
// strtol/strtoll/strtoul/strtoull "0b"/"0B" prefix support (ticket #390)

// [from test_std_c23_unreachable]
// unreachable() macro in <stddef.h> (ticket #390)

static int classify(int x) {
    if (x > 0) return 1;
    if (x < 0) return -1;
    if (x == 0) return 0;
    unreachable();
}

#pragma cccc suite begin "c23"

// test_c23_attributes
[[cccc::test(return = 42)]]
int test_c23_attributes(void) {
    // Attributes do not change runtime behavior.
    int v = get_value();
    if (v != 42) return 1;

    int o = old_function();
    if (o != 1) return 2;

    int h = helper();
    if (h != 2) return 3;

    int o2 = old_func2();
    if (o2 != 3) return 4;

    int m = multi_attr_func();
    if (m != 4) return 5;

    // Test struct with attributes
    struct TestStruct s = {1, 2, 3};
    if (s.x != 1 || s.y != 2 || s.z != 3) return 6;

    // Test fallthrough with [[fallthrough]] annotation
    int f = test_fallthrough(1);
    if (f != 2) return 7;
    f = test_fallthrough(2);
    if (f != 2) return 8;
    f = test_fallthrough(3);
    if (f != 3) return 9;
    f = test_fallthrough(9);
    if (f != -1) return 10;

    // Test pure and reproducible functions
    int p = pure_func(5);
    if (p != 10) return 11;

    int r = repro_func(5);
    if (r != 6) return 12;

    return 42;  // Success
}

// test_c23_auto
[[cccc::test(return = 42)]]
int test_c23_auto(void) {
    // Scalar type deduction
    auto i = 5;
    if (sizeof(i) != sizeof(int)) return 1;
    if (i != 5) return 2;

    auto d = 3.14;
    if (sizeof(d) != sizeof(double)) return 3;

    auto f = 3.14f;
    if (sizeof(f) != sizeof(float)) return 4;

    // const stripping: const int -> int
    const int ci = 10;
    auto a = ci;
    a = 20; // must be assignable (not const)
    if (a != 20) return 5;

    // String literal: const char[] decays and strips const -> char *
    auto s = "hello";
    s = "world"; // must be assignable
    if (s[0] != 'w') return 6;

    // Array decay: int[3] -> int *
    int arr[3] = {1, 2, 3};
    auto p = arr;
    if (*p != 1) return 7;
    if (*(p + 2) != 3) return 8;

    // Function decay: void(void) -> void (*)(void)
    auto fn = void_fn;
    fn(); // must be callable
    if (fn != void_fn) return 9;

    // Pointer declarator: auto *q = &i -> int *
    int x = 99;
    auto *q = &x;
    if (*q != 99) return 10;
    *q = 100;
    if (x != 100) return 11;

    // Double pointer
    auto **pp = &q;
    if (**pp != 100) return 12;

    // Struct via compound literal
    auto pt = (Point){3, 7};
    if (pt.x != 3 || pt.y != 7) return 13;

    // Multiple declarators on one line, each with own type
    auto ai = 1, bd = 2.0;
    if (sizeof(ai) != sizeof(int)) return 14;
    if (sizeof(bd) != sizeof(double)) return 15;

    // File-scope globals
    if (global_i != 42) return 16;
    if (sizeof(global_i) != sizeof(int)) return 17;

    return 42;
}

// test_c23_bitint
[[cccc::test(return = 42)]]
int test_c23_bitint(void) {
    // sizeof checks (unsigned _BitInt(1) is valid; signed needs >= 2 bits)
    unsigned _BitInt(1) b1 = 0;
    if (sizeof(b1) != 1) return 1;

    _BitInt(8) b8 = 0;
    if (sizeof(b8) != 1) return 2;

    _BitInt(9) b9 = 0;
    if (sizeof(b9) != 2) return 3;

    _BitInt(17) b17 = 0;
    if (sizeof(b17) != 4) return 4;

    _BitInt(64) b64 = 0;
    if (sizeof(b64) != 8) return 5;

    // unsigned _BitInt wraparound: 5-bit unsigned max is 31, +1 wraps to 0
    unsigned _BitInt(5) u5 = 31;
    u5 = u5 + 1;
    if (u5 != 0) return 6;

    // Unsigned arithmetic: 16+17 = 33 but 5-bit max is 31, so 33-32 = 1
    unsigned _BitInt(5) ua = 16, ub = 17;
    unsigned _BitInt(5) uc = ua + ub;
    if (uc != 1) return 7;

    // Signed _BitInt(8): max is 127, +1 wraps to -128
    _BitInt(8) s8 = 127;
    s8 = s8 + 1;
    if (s8 != -128) return 8;

    // Sign extension: signed _BitInt(4) with bit pattern 0b1000 = -8
    _BitInt(4) s4 = -8;
    if (s4 != -8) return 9;

    // _BitInt(64) full range
    _BitInt(64) big = 1000000000LL;
    big = big * 1000000000LL;
    if (big != 1000000000000000000LL) return 10;

    // unsigned _BitInt(5) from constant
    unsigned _BitInt(5) u5b = 7;
    u5b = u5b * 5;  // 35 & 31 = 3
    if (u5b != 3) return 11;

    return 42;
}

// test_c23_bitint_wide
[[cccc::test(return = 42)]]
int test_c23_bitint_wide(void) {
    // sizeof for various wide widths
    _BitInt(65) w65 = 0;
    if (sizeof(w65) != 16) return 1;
    _BitInt(128) w128 = 0;
    if (sizeof(w128) != 16) return 2;
    _BitInt(192) w192 = 0;
    if (sizeof(w192) != 24) return 3;
    _BitInt(256) w256 = 0;
    if (sizeof(w256) != 32) return 4;

    // Basic arithmetic at N=80
    _BitInt(80) x = 5, y = 3;
    if (x + y != 8) return 5;
    if (x - y != 2) return 6;
    if (x * y != 15) return 7;
    if (x / y != 1) return 8;
    if (x % y != 2) return 9;

    // Negative values / signed div/mod at N=80
    _BitInt(80) neg = -7;
    if (neg != -7) return 10;
    if (neg / 2 != -3) return 11;
    if (neg % 2 != -1) return 12;

    // Function call/return at N=128 (exercises hidden-pointer ABI + RETBUF)
    _BitInt(128) a = 100, b = 200;
    _BitInt(128) c = add128(a, b);
    if (c != 300) return 13;

    // Chained calls returning wide _BitInt (RETBUF rotation)
    _BitInt(128) chained = add128(add128(a, b), add128(a, b));
    if (chained != 600) return 14;

    // Shifts at N=200, including shifting across word boundaries
    _BitInt(200) shifted = (_BitInt(200))1 << 150;
    _BitInt(200) back = shifted >> 150;
    if (back != 1) return 15;
    unsigned _BitInt(200) ushifted = (unsigned _BitInt(200))1 << 199;
    if ((ushifted >> 199) != 1) return 16;

    // Comparisons (signed and unsigned)
    _BitInt(96) sneg1 = -1, sneg2 = -1;
    if (!(sneg1 == sneg2)) return 17;
    if (!(sneg1 < (_BitInt(96))0)) return 18;
    unsigned _BitInt(96) uneg = (unsigned _BitInt(96))(-1);
    if (uneg < (unsigned _BitInt(96))0) return 19;

    // Multiplication overflow wraps at exact bit width (N=256)
    unsigned _BitInt(256) p100 = pow2_256(100);
    unsigned _BitInt(256) p64 = pow2_256(64);
    if (!(p100 > p64)) return 20;

    // Casts: wide -> narrow, narrow -> wide, wide -> wide (widen/narrow)
    _BitInt(128) small = (_BitInt(128))42;
    _BitInt(72) narrowed = (_BitInt(72))small;
    if (narrowed != 42) return 21;

    long long ll = 12345;
    _BitInt(150) fromll = (_BitInt(150))ll;
    if ((long long)fromll != 12345) return 22;

    _BitInt(70) widenarrow_src = -5;
    _BitInt(200) widened = (_BitInt(200))widenarrow_src;
    if (widened != -5) return 23; // must sign-extend, not zero-extend

    // double <-> wide _BitInt conversions
    double d = 3.5;
    _BitInt(100) fromd = (_BitInt(100))d;
    if (fromd != 3) return 24;
    double backd = (double)fromd;
    if (backd != 3.0) return 25;

    // Array of wide _BitInt
    _BitInt(128) arr[3];
    arr[0] = (_BitInt(128))10;
    arr[1] = (_BitInt(128))20;
    arr[2] = arr[0] + arr[1];
    if (arr[2] != 30) return 26;

    // Wide _BitInt as struct member, passed via pointer
    struct Box box;
    fill_box(&box, (_BitInt(128))999);
    if (box.v != 999 || box.tag != 1) return 27;

    // Division with multi-word divisor (N=150)
    unsigned _BitInt(150) big1 = (unsigned _BitInt(150))1 << 100;
    unsigned _BitInt(150) big2 = (unsigned _BitInt(150))1 << 50;
    if (big1 / big2 != ((unsigned _BitInt(150))1 << 50)) return 28;

    // Bitwise ops at N=128
    _BitInt(128) ba = (_BitInt(128))0xFF;
    _BitInt(128) bb = (_BitInt(128))0x0F;
    if ((ba & bb) != 0x0F) return 29;
    if ((ba | bb) != 0xFF) return 30;
    if ((ba ^ bb) != 0xF0) return 31;

    // sizeof for N > 256 (#454)
    _BitInt(300) w300 = 0;
    if (sizeof(w300) != 40) return 32;
    _BitInt(512) w512 = 0;
    if (sizeof(w512) != 64) return 33;
    _BitInt(1000) w1000 = 0;
    if (sizeof(w1000) != 128) return 34;
    _BitInt(4096) w4096 = 0;
    if (sizeof(w4096) != 512) return 35;
    _BitInt(65535) w65535 = 0;
    if (sizeof(w65535) != 8192) return 36;

    // Arithmetic at N=300 (5 words) — exercises udivmod's variable bounds
    _BitInt(300) x300 = 5, y300 = 3;
    if (x300 + y300 != 8) return 37;
    if (x300 - y300 != 2) return 38;
    if (x300 * y300 != 15) return 39;
    if (x300 / y300 != 1) return 40;
    if (x300 % y300 != 2) return 41;

    // Arithmetic at N=4096 (64 words)
    _BitInt(4096) x4096 = 5, y4096 = 3;
    if (x4096 + y4096 != 8) return 42;
    if (x4096 - y4096 != 2) return 43;
    if (x4096 * y4096 != 15) return 44;
    if (x4096 / y4096 != 1) return 45;
    if (x4096 % y4096 != 2) return 46;

    // Signed/unsigned comparison at N=512 (8 words)
    _BitInt(512) sneg512 = -1, szero512 = 0;
    if (!(sneg512 < szero512)) return 47;
    unsigned _BitInt(512) uneg512 = (unsigned _BitInt(512))(-1);
    if (uneg512 < (unsigned _BitInt(512))0) return 48;

    // Shift across many word boundaries at N=1000 (16 words)
    _BitInt(1000) shifted1000 = (_BitInt(1000))1 << 900;
    _BitInt(1000) back1000 = shifted1000 >> 900;
    if (back1000 != 1) return 49;
    _BitInt(1000) negshift = -1;
    _BitInt(1000) sshifted = negshift >> 900; // arithmetic shift keeps sign
    if (sshifted != -1) return 50;

    // int/double conversions at N=65535 (1024 words, worst-case stack usage)
    long long ll65535 = 123456;
    _BitInt(65535) fromll65535 = (_BitInt(65535))ll65535;
    if ((long long)fromll65535 != 123456) return 51;
    double d65535 = 7.0;
    _BitInt(65535) fromd65535 = (_BitInt(65535))d65535;
    if (fromd65535 != 7) return 52;
    if ((double)fromd65535 != 7.0) return 53;

    // Multiplication overflow wraps at exact bit width (N=4096, 64 words)
    unsigned _BitInt(4096) p4096_a = pow2_4096(4090);
    unsigned _BitInt(4096) p4096_b = pow2_4096(4000);
    if (!(p4096_a > p4096_b)) return 54;
    unsigned _BitInt(4096) wrap4096 = pow2_4096(4096); // 2^4096 mod 2^4096 == 0
    if (wrap4096 != 0) return 55;

    // --- WIDE_* opcode coverage (#456): +,-,*,/,%,<<,>>,>>> via opcodes ---

    // Unsigned div/mod (selects WIDE_DIV/WIDE_MOD's is_signed=0 path)
    unsigned _BitInt(80) ux = 17, uy = 5;
    if (ux / uy != 3) return 56;
    if (ux % uy != 2) return 57;

    // Signed div/mod with negative operands (is_signed=1 path)
    _BitInt(80) sx = -17, sy = 5;
    if (sx / sy != -3) return 58;
    if (sx % sy != -2) return 59;

    // Logical (unsigned) vs arithmetic (signed) shift on the same bit
    // pattern, to distinguish WIDE_SHR from WIDE_USHR.
    _BitInt(96) negval = -1;
    if ((negval >> 1) != -1) return 60; // arithmetic: sign-extends
    unsigned _BitInt(96) unegval = (unsigned _BitInt(96))(-1);
    if ((unegval >> 1) >= unegval) return 61; // logical: shifts in zero

    // Nested wide expression: (a + b) * c — multiple chained WIDE_* opcodes
    // with intermediate temporaries, exercising restrict-cache invalidation
    // and temp-register reuse across consecutive wide ops.
    _BitInt(150) na = 3, nb = 4, nc = 5;
    _BitInt(150) nested = (na + nb) * nc;
    if (nested != 35) return 62;

    // Wide binop result passed directly as a function argument (forces the
    // result through the same hidden-pointer ABI as add128's parameters,
    // right after a WIDE_ADD wrote it).
    _BitInt(128) argres = add128(a + (_BitInt(128))1, b);
    if (argres != 301) return 63;

    // Multiple simultaneously-live wide temporaries in one expression.
    _BitInt(128) t1 = (_BitInt(128))10, t2 = (_BitInt(128))20,
                 t3 = (_BitInt(128))30, t4 = (_BitInt(128))40;
    _BitInt(128) multi = (t1 + t2) + (t3 + t4);
    if (multi != 100) return 64;

    return 42;
}

// test_c23_bitint_wide_safety
[[cccc::test(return = 42)]]
int test_c23_bitint_wide_safety(void) {
    // Basic arithmetic at _BitInt(128) — the ticket's exact repro
    _BitInt(128) x = 5, y = 3;
    if (x + y != 8) return 1;
    if (x - y != 2) return 2;
    if (x * y != 15) return 3;

    // _BitInt(256) to exercise wider storage
    _BitInt(256) a = 1000000000wb, b = 999999999wb;
    if (a + b != 1999999999wb) return 4;
    if (a - b != 1wb) return 5;
    if (a > b ? 0 : 1) return 6;

    // Local assignment then read (the pattern that tripped #457)
    _BitInt(128) r;
    r = x + y;
    if (r != 8) return 7;

    return 42;
}

// test_c23_bool_keywords
[[cccc::test(return = 42)]]
int test_c23_bool_keywords(void) {
    bool a = true;
    bool b = false;

    if (a != true) return 1;
    if (b != false) return 2;
    if (a == b) return 3;

    // sizeof
    if (sizeof(bool) != 1) return 4;
    if (sizeof(true) != 1) return 5;

    // arithmetic / promotion
    int sum = a + b + true + false;
    if (sum != 2) return 6;

    // comparisons
    if (!(5 > 3) != false) return 7;
    if ((5 > 3) != true) return 8;

    // logical operators
    if ((a && b) != false) return 9;
    if ((a || b) != true) return 10;
    if (!a != false) return 11;
    if (!b != true) return 12;

    // function returning bool
    if (is_even(4) != true) return 13;
    if (is_even(3) != false) return 14;

    // global
    if (global_flag != true) return 15;

    // assignment / normalization: any nonzero value becomes 1
    bool c = 42;
    if (c != true) return 16;
    if ((int)c != 1) return 17;

    return 42;
}

// test_c23_bool_stdbool_compat
[[cccc::test(return = 42)]]
int test_c23_bool_stdbool_compat(void) {
    bool a = true;
    bool b = false;

    if (a != true) return 1;
    if (b != false) return 2;
    if (!__bool_true_false_are_defined) return 3;

    // nullptr_t available alongside stdbool.h
    nullptr_t np = nullptr;
    if (np != nullptr) return 4;

    return 42;
}

// test_c23_decimal
[[cccc::test(return = 42)]]
int test_c23_decimal(void) {
    // sizeof checks per C23 spec
    if (sizeof(_Decimal32) != 4) return 1;
    if (sizeof(_Decimal64) != 8) return 2;
    if (sizeof(_Decimal128) != 16) return 3;

    // Basic variable declarations and arithmetic
    _Decimal32 d32 = 1.5f;
    _Decimal64 d64 = 2.5;
    _Decimal128 d128 = 3.5L;

    if (d32 + d32 != 3.0f) return 4;
    if (d64 + d64 != 5.0) return 5;
    if (d128 + d128 != 7.0L) return 6;

    // Assignment and comparison
    _Decimal64 x = 10.0;
    x = x * 2.0;
    if (x != 20.0) return 7;

    return 42;
}

// test_c23_empty_params_void_equiv
[[cccc::test(return = 42)]]
int test_c23_empty_params_void_equiv(void) {
    return add();
}

// test_c23_exp10_pi
[[cccc::test(return = 42)]]
int test_c23_exp10_pi(void) {
    // exp10 family (double, long double - unaffected by #406)
    if (exp10(0.0) != 1.0) return 1;
    if (fabs_d(exp10(2.0) - 100.0) > 1e-9) return 2;
    if (fabs_d((double)exp10l(2.0L) - 100.0L) > 1e-9) return 3;

    // exp10f: registered but not result-checked, see #406
    (void)exp10f(2.0f);

    // sinpi/cospi/tanpi - exact at integer/half-integer points
    if (sinpi(0.0) != 0.0) return 5;
    if (sinpi(0.5) != 1.0) return 6;
    if (sinpi(1.0) != 0.0) return 7;
    if (cospi(0.0) != 1.0) return 8;
    if (cospi(1.0) != -1.0) return 9;
    if (cospi(0.5) != 0.0) return 10;
    if (tanpi(0.0) != 0.0) return 11;
    if (fabs_d(tanpi(0.25) - 1.0) > 1e-9) return 12;

    // long double variant - unaffected by #406
    if (tanpil(0.0L) != 0.0L) return 15;

    // sinpif/cospif: registered but not result-checked, see #406
    (void)sinpif(0.5f);
    (void)cospif(0.0f);

    // asinpi/acospi/atanpi/atan2pi (double, long double)
    if (fabs_d(asinpi(1.0) - 0.5) > 1e-9) return 16;
    if (fabs_d(acospi(-1.0) - 1.0) > 1e-9) return 17;
    if (fabs_d(atanpi(1.0) - 0.25) > 1e-9) return 18;
    if (fabs_d(atan2pi(1.0, 1.0) - 0.25) > 1e-9) return 19;
    if (fabs_d(atan2pi(1.0, 0.0) - 0.5) > 1e-9) return 20;
    if (fabs_d((double)atanpil(1.0L) - 0.25L) > 1e-9) return 22;

    // asinpif: registered but not result-checked, see #406
    (void)asinpif(1.0f);

    return 42;
}

// test_c23_keywords
[[cccc::test(return = 42)]]
int test_c23_keywords(void) {
    // Test static_assert in function scope
    static_assert(1 + 1 == 2, "basic math");
    static_assert(10 > 5, "comparison");
    static_assert(sizeof(long) == 8);

    // Test typeof_unqual removes const
    const int x = 42;
    typeof_unqual(x) y = 100;  // y should be non-const int
    y = 200;  // This should work since y is not const

    // Test typeof_unqual with volatile
    volatile int v = 10;
    typeof_unqual(v) w = 20;  // w should be non-volatile int
    w = 30;

    // Test typeof_unqual with const pointer
    const int *cp = &x;
    typeof_unqual(*cp) z = 50;  // z should be non-const int
    z = 60;

    // Test that regular typeof still works
    typeof(x) a = 77;  // a should be const int (const is preserved)

    // Test typeof_unqual with type names
    typeof_unqual(const int) b = 88;
    b = 99;

    // Verify values
    if (y != 200) return 1;
    if (w != 30) return 2;
    if (z != 60) return 3;
    if (b != 99) return 4;

    return 42;  // Success
}

// test_c23_literals
[[cccc::test(return = 42)]]
int test_c23_literals(void) {
    // Test decimal literals with separators
    int a = 1'000'000;
    if (a != 1000000) return 1;

    int b = 1'234'567;
    if (b != 1234567) return 2;

    int c = 100'000;
    if (c != 100000) return 3;

    // Test with smaller numbers
    int d = 1'000;
    if (d != 1000) return 4;

    int e = 10'000;
    if (e != 10000) return 5;

    // Test hexadecimal with separators
    int h1 = 0x1'23'45;
    if (h1 != 0x12345) return 6;

    int h2 = 0xFF'FF;
    if (h2 != 0xFFFF) return 7;

    int h3 = 0xDE'AD'BE'EF;
    if (h3 != 0xDEADBEEF) return 8;

    // Test binary with separators
    int bin1 = 0b1010'1010;
    if (bin1 != 0b10101010) return 9;

    int bin2 = 0b1111'0000'1111'0000;
    if (bin2 != 0b1111000011110000) return 10;

    int bin3 = 0b1'0'1'0;
    if (bin3 != 0b1010) return 11;

    // Test octal with separators
    int oct1 = 01'23'45;
    if (oct1 != 012345) return 12;

    int oct2 = 07'77;
    if (oct2 != 0777) return 13;

    // Test with suffixes
    long l = 1'000'000L;
    if (l != 1000000L) return 14;

    unsigned int u = 1'000'000U;
    if (u != 1000000U) return 15;

    unsigned long ul = 1'000'000UL;
    if (ul != 1000000UL) return 16;

    // Test various separator positions
    int sep1 = 1'2'3'4;
    if (sep1 != 1234) return 17;

    int sep2 = 12'34;
    if (sep2 != 1234) return 18;

    int sep3 = 123'4;
    if (sep3 != 1234) return 19;

    // Test large numbers
    long big = 1'000'000'000;
    if (big != 1000000000) return 20;

    // Test empty initializers with digit separators
    int arr[] = {1'000, 2'000, 3'000};
    if (arr[0] != 1000) return 21;
    if (arr[1] != 2000) return 22;
    if (arr[2] != 3000) return 23;

    return 42;  // Success
}

// test_c23_mbrtoc8
[[cccc::test(return = 42)]]
int test_c23_mbrtoc8(void) {
    if (!setlocale(LC_ALL, "en_US.UTF-8") && !setlocale(LC_ALL, "C.UTF-8"))
        return 1;

    mbstate_t st;
    char8_t c8;
    size_t rc;

    // ASCII 'A' - single byte, consumed immediately.
    memset(&st, 0, sizeof(st));
    rc = mbrtoc8(&c8, "A", 1, &st);
    if (rc != 1) return 2;
    if (c8 != 'A') return 3;

    // U+00E9 (e acute) -> UTF-8 0xC3 0xA9, all input available at once.
    // First call consumes both input bytes and emits the first char8_t;
    // the second byte is queued and drained via (size_t)-3.
    memset(&st, 0, sizeof(st));
    rc = mbrtoc8(&c8, "\xC3\xA9", 2, &st);
    if (rc != 2) return 4;
    if (c8 != 0xC3) return 5;
    rc = mbrtoc8(&c8, "", 0, &st);
    if (rc != (size_t)-3) return 6;
    if (c8 != 0xA9) return 7;

    // Same character, but input split across calls: first call only sees
    // the lead byte (incomplete sequence -> (size_t)-2), second call
    // completes it. Verifies the queued-byte state doesn't collide with
    // mbrtowc's own incomplete-sequence state in *ps.
    memset(&st, 0, sizeof(st));
    rc = mbrtoc8(&c8, "\xC3", 1, &st);
    if (rc != (size_t)-2) return 8;
    rc = mbrtoc8(&c8, "\xA9", 1, &st);
    if (rc == (size_t)-1 || rc == (size_t)-2) return 9;
    if (c8 != 0xC3) return 10;
    rc = mbrtoc8(&c8, "", 0, &st);
    if (rc != (size_t)-3) return 11;
    if (c8 != 0xA9) return 12;

    // U+20AC (EURO SIGN) -> UTF-8 0xE2 0x82 0xAC (3 bytes).
    memset(&st, 0, sizeof(st));
    rc = mbrtoc8(&c8, "\xE2\x82\xAC", 3, &st);
    if (rc == (size_t)-1 || rc == (size_t)-2) return 13;
    if (c8 != 0xE2) return 14;
    rc = mbrtoc8(&c8, "", 0, &st);
    if (rc != (size_t)-3 || c8 != 0x82) return 15;
    rc = mbrtoc8(&c8, "", 0, &st);
    if (rc != (size_t)-3 || c8 != 0xAC) return 16;

    // U+1F600 (GRINNING FACE) -> UTF-8 0xF0 0x9F 0x98 0x80 (4 bytes).
    memset(&st, 0, sizeof(st));
    rc = mbrtoc8(&c8, "\xF0\x9F\x98\x80", 4, &st);
    if (rc == (size_t)-1 || rc == (size_t)-2) return 17;
    if (c8 != 0xF0) return 18;
    rc = mbrtoc8(&c8, "", 0, &st);
    if (rc != (size_t)-3 || c8 != 0x9F) return 19;
    rc = mbrtoc8(&c8, "", 0, &st);
    if (rc != (size_t)-3 || c8 != 0x98) return 20;
    rc = mbrtoc8(&c8, "", 0, &st);
    if (rc != (size_t)-3 || c8 != 0x80) return 21;

    // mbrtoc8 with an invalid lead byte should report EILSEQ.
    memset(&st, 0, sizeof(st));
    rc = mbrtoc8(&c8, "\x80", 1, &st);
    if (rc != (size_t)-1) return 22;

    // mbrtoc8 with a null input character.
    memset(&st, 0, sizeof(st));
    rc = mbrtoc8(&c8, "", 1, &st);
    if (rc != 0 || c8 != 0) return 23;

    // c8rtomb: feed the EURO SIGN's three char8_t units one at a time;
    // should return 0 while incomplete, then the encoded byte count.
    char buf[8];
    memset(&st, 0, sizeof(st));
    if (c8rtomb(buf, 0xE2, &st) != 0) return 24;
    if (c8rtomb(buf, 0x82, &st) != 0) return 25;
    rc = c8rtomb(buf, 0xAC, &st);
    if (rc == 0 || rc == (size_t)-1) return 26;
    if (memcmp(buf, "\xE2\x82\xAC", rc) != 0) return 27;

    // c8rtomb round trip for the 4-byte GRINNING FACE sequence.
    memset(&st, 0, sizeof(st));
    if (c8rtomb(buf, 0xF0, &st) != 0) return 28;
    if (c8rtomb(buf, 0x9F, &st) != 0) return 29;
    if (c8rtomb(buf, 0x98, &st) != 0) return 30;
    rc = c8rtomb(buf, 0x80, &st);
    if (rc == 0 || rc == (size_t)-1) return 31;
    if (memcmp(buf, "\xF0\x9F\x98\x80", rc) != 0) return 32;

    // c8rtomb with an invalid lead byte should report EILSEQ.
    memset(&st, 0, sizeof(st));
    if (c8rtomb(buf, 0xFF, &st) != (size_t)-1) return 33;

    // c8rtomb null terminator.
    memset(&st, 0, sizeof(st));
    rc = c8rtomb(buf, 0, &st);
    if (rc != 1 || buf[0] != '\0') return 34;

    return 42;
}

// test_c23_nullptr
[[cccc::test(return = 42)]]
int test_c23_nullptr(void) {
    // Basic null pointer assignment via different pointer types
    int *ip = nullptr;
    char *cp = nullptr;
    void *vp = nullptr;
    if (ip != nullptr) return 1;
    if (cp != nullptr) return 2;
    if (vp != nullptr) return 3;

    // Comparisons in both orderings
    if (!(ip == nullptr)) return 4;
    if (!(nullptr == ip)) return 5;
    if (nullptr != ip) return 6;
    if (ip != nullptr) return 7;

    // Global initialized with nullptr
    if (global_p != nullptr) return 8;

    // Passing nullptr as a function argument
    if (take_ptr(nullptr) != nullptr) return 9;

    // Ternary with nullptr
    int x = 1;
    int *tp = x ? nullptr : ip;
    if (tp != nullptr) return 10;

    // sizeof(nullptr) and nullptr_t
    if (sizeof(nullptr) != sizeof(void *)) return 11;

    nullptr_t np = nullptr;
    if (sizeof(np) != sizeof(void *)) return 12;
    if (np != nullptr) return 13;

    // Reassign a pointer back to nullptr after pointing elsewhere
    int v = 5;
    ip = &v;
    if (ip == nullptr) return 14;
    ip = nullptr;
    if (ip != nullptr) return 15;

    return 42;
}

// test_c23_preprocessor
[[cccc::test(return = 42)]]
int test_c23_preprocessor(void) {
    // Test basic functionality
    int test = 1;

    // Test empty initializer (C23 feature)
    int empty_array[5] = {};
    // All elements should be zero
    for (int i = 0; i < 5; i++) {
        if (empty_array[i] != 0) return 7 + i;
    }

    // Test empty struct initializer
    struct {
        int a;
        int b;
        int c;
    } empty_struct = {};
    if (empty_struct.a != 0 || empty_struct.b != 0 || empty_struct.c != 0)
        return 12;

    return 42;  // Success
}

// test_c23_tag_redeclarations
[[cccc::test(return = 42)]]
int test_c23_tag_redeclarations(void) {
    struct Point p = { 20, 22 };
    union Value v;
    enum Color c = GREEN;
    v.l = 42;
    if (p.x + p.y != 42) return 1;
    if (v.i != 42) return 2;
    if (c != 2) return 3;
    if (RED != 1 || GREEN != 2) return 4;
    return 42;
}

// test_c23_wb_suffix
[[cccc::test(return = 42)]]
int test_c23_wb_suffix(void) {
    // 0wb -> signed _BitInt(2) (minimum for signed _BitInt)
    if (sizeof(0wb) != 1) return 1;

    // 1wb -> _BitInt(2): 1 value bit + 1 sign = 2 bits, stored in 1 byte
    if (sizeof(1wb) != 1) return 2;

    // 127wb -> _BitInt(8): 7 value bits + 1 sign = 8 bits
    if (sizeof(127wb) != 1) return 3;

    // 128wb -> _BitInt(9): 8 value bits + 1 sign = 9 bits, stored in 2 bytes
    if (sizeof(128wb) != 2) return 4;

    // 0uwb -> unsigned _BitInt(1) (minimum)
    if (sizeof(0uwb) != 1) return 5;

    // 1uwb -> unsigned _BitInt(1): 1 bit holds 0..1
    if (sizeof(1uwb) != 1) return 6;

    // 255uwb -> unsigned _BitInt(8)
    if (sizeof(255uwb) != 1) return 7;

    // 256uwb -> unsigned _BitInt(9), stored in 2 bytes
    if (sizeof(256uwb) != 2) return 8;

    // Arithmetic: wb values behave as _BitInt
    _BitInt(8) a = 127wb;
    a = a + 1wb;
    if (a != -128) return 9;    // signed overflow wraps

    unsigned _BitInt(5) b = 31uwb;
    b = b + 1uwb;
    if (b != 0) return 10;      // unsigned overflow wraps

    // Assignment from wb literal to _BitInt variable
    _BitInt(4) s4 = 7wb;        // 7 fits in _BitInt(4) (max 7)
    if (s4 != 7) return 11;

    // Mixed arithmetic with wb and regular integer
    unsigned _BitInt(8) c = 100uwb;
    c = c + 55;
    if (c != 155) return 12;

    // --- #452: literals needing > 64 bits must infer a wide width and
    // materialize the full-precision value, not just the truncated low 64
    // bits (the bug: tokenize.c parsed the digit text with strtoul into a
    // 64-bit value *before* computing the width).

    // Decimal literal needing 97/98 bits (bit_length(123456789012345678901234567890) == 97).
    if (sizeof(123456789012345678901234567890wb) != 16) return 13;  // width 98 (97 value bits + sign)
    if (sizeof(123456789012345678901234567890uwb) != 16) return 14; // width 97

    _BitInt(98) big_signed = 123456789012345678901234567890wb;
    unsigned _BitInt(97) big_unsigned = 123456789012345678901234567890uwb;
    // Verify the full-precision value round-tripped correctly by checking
    // the low and high 64-bit halves independently.
    if ((unsigned long long)big_signed != 14083847773837265618ULL) return 15;
    if ((unsigned long long)(big_signed >> 64) != 6692605942ULL) return 16;
    if ((unsigned long long)big_unsigned != 14083847773837265618ULL) return 17;
    if ((unsigned long long)(big_unsigned >> 64) != 6692605942ULL) return 18;

    // Hex literal needing 65 bits (0x1FFFFFFFFFFFFFFFF == 2^65 - 1) —
    // exercises the base-16 fast width-inference path.
    if (sizeof(0x1FFFFFFFFFFFFFFFFuwb) != 16) return 19; // width 65
    if (sizeof(0x1FFFFFFFFFFFFFFFFwb) != 16) return 20;  // width 66 (sign bit)
    unsigned _BitInt(65) hex_unsigned = 0x1FFFFFFFFFFFFFFFFuwb;
    _BitInt(66) hex_signed = 0x1FFFFFFFFFFFFFFFFwb;
    if ((unsigned long long)hex_unsigned != 0xFFFFFFFFFFFFFFFFULL) return 21;
    if ((unsigned long long)(hex_unsigned >> 64) != 1ULL) return 22;
    if ((unsigned long long)hex_signed != 0xFFFFFFFFFFFFFFFFULL) return 23;
    if ((unsigned long long)(hex_signed >> 64) != 1ULL) return 24;

    // Binary literal needing exactly 65 bits (2^64) — exercises the
    // base-2 fast width-inference path.
    unsigned _BitInt(65) bin_unsigned =
        0b10000000000000000000000000000000000000000000000000000000000000000uwb;
    if (sizeof(bin_unsigned) != 16) return 25;
    if ((unsigned long long)bin_unsigned != 0ULL) return 26;
    if ((unsigned long long)(bin_unsigned >> 64) != 1ULL) return 27;

    // Sign-bit boundary: 2^63 fits unsigned in 64 bits (narrow, scalar
    // storage) but needs 65 bits signed (wide, address-based storage) —
    // the same magnitude crosses from narrow to wide purely because of u.
    if (sizeof(9223372036854775808uwb) != 8) return 28;
    if (sizeof(9223372036854775808wb) != 16) return 29;

    return 42;
}

// test_constexpr_basic
[[cccc::test(return = 42)]]
int test_constexpr_basic(void) {
    constexpr int N = 10;
    constexpr int local_arr[] = { 1, 2, 3 };

    static_assert(N == 10);
    static_assert(MAX_SIZE == 9);
    static_assert(limits.max == 9);
    static_assert(LIMIT_ENUM == 9);
    static_assert(__builtin_constant_p(N));

    int local_purrs[N] = { 0 };
    if (sizeof(purrs) != 9 * sizeof(int)) return 1;
    if (sizeof(local_purrs) != 10 * sizeof(int)) return 2;
    if (sizeof(local_arr) != 3 * sizeof(int)) return 3;
    if (global_from_constexpr != 9) return 4;
    return 42;
}

// test_std_c23_stdbit
[[cccc::test(return = 42)]]
int test_std_c23_stdbit(void) {
    /* ---- stdc_leading_zeros ---- */
    if (stdc_leading_zeros_uc((unsigned char)0) != 8) return 1;
    if (stdc_leading_zeros_uc((unsigned char)1) != 7) return 2;
    if (stdc_leading_zeros_uc((unsigned char)0x80) != 0) return 3;
    if (stdc_leading_zeros_us((unsigned short)0) != 16) return 4;
    if (stdc_leading_zeros_us((unsigned short)1) != 15) return 5;
    if (stdc_leading_zeros_ui(0u) != 32) return 6;
    if (stdc_leading_zeros_ui(1u) != 31) return 7;
    if (stdc_leading_zeros_ull(0ull) != 64) return 8;
    if (stdc_leading_zeros_ull(1ull) != 63) return 9;
    if (stdc_leading_zeros_ul(1ul) != 63) return 10;

    /* ---- stdc_trailing_zeros ---- */
    if (stdc_trailing_zeros_uc((unsigned char)0) != 8) return 11;
    if (stdc_trailing_zeros_uc((unsigned char)8) != 3) return 12;
    if (stdc_trailing_zeros_us((unsigned short)0) != 16) return 13;
    if (stdc_trailing_zeros_us((unsigned short)8) != 3) return 14;
    if (stdc_trailing_zeros_ui(0u) != 32) return 15;
    if (stdc_trailing_zeros_ui(8u) != 3) return 16;
    if (stdc_trailing_zeros_ull(0ull) != 64) return 17;
    if (stdc_trailing_zeros_ull(8ull) != 3) return 18;

    /* ---- stdc_leading_ones ---- */
    if (stdc_leading_ones_uc((unsigned char)0xFF) != 8) return 19;
    if (stdc_leading_ones_uc((unsigned char)0xF0) != 4) return 20;
    if (stdc_leading_ones_uc((unsigned char)0) != 0) return 21;
    if (stdc_leading_ones_us((unsigned short)0xFFFF) != 16) return 22;
    if (stdc_leading_ones_us((unsigned short)0xFF00) != 8) return 23;
    if (stdc_leading_ones_ui(0xFFFFFFFFu) != 32) return 24;
    if (stdc_leading_ones_ui(0xF0000000u) != 4) return 25;
    if (stdc_leading_ones_ull(0xFFFFFFFFFFFFFFFFull) != 64) return 26;

    /* ---- stdc_trailing_ones ---- */
    if (stdc_trailing_ones_uc((unsigned char)0xFF) != 8) return 27;
    if (stdc_trailing_ones_uc((unsigned char)0x0F) != 4) return 28;
    if (stdc_trailing_ones_uc((unsigned char)0) != 0) return 29;
    if (stdc_trailing_ones_ui(0xFFFFFFFFu) != 32) return 30;
    if (stdc_trailing_ones_ui(7u) != 3) return 31;
    if (stdc_trailing_ones_ull(0xFFFFFFFFFFFFFFFFull) != 64) return 32;

    /* ---- stdc_count_ones ---- */
    if (stdc_count_ones_uc((unsigned char)0) != 0) return 33;
    if (stdc_count_ones_uc((unsigned char)0xFF) != 8) return 34;
    if (stdc_count_ones_us((unsigned short)0xFFFF) != 16) return 35;
    if (stdc_count_ones_ui(0u) != 0) return 36;
    if (stdc_count_ones_ui(0xFFu) != 8) return 37;
    if (stdc_count_ones_ull(0xFFFFFFFFFFFFFFFFull) != 64) return 38;

    /* ---- stdc_count_zeros ---- */
    if (stdc_count_zeros_uc((unsigned char)0xFF) != 0) return 39;
    if (stdc_count_zeros_uc((unsigned char)0) != 8) return 40;
    if (stdc_count_zeros_uc((unsigned char)0xF0) != 4) return 41;
    if (stdc_count_zeros_us((unsigned short)0) != 16) return 42;
    if (stdc_count_zeros_ui(0u) != 32) return 43;
    if (stdc_count_zeros_ui(0xFFFFFFFFu) != 0) return 44;
    if (stdc_count_zeros_ull(0xFFFFFFFFFFFFFFFFull) != 0) return 45;
    if (stdc_count_zeros_ull(0ull) != 64) return 46;

    /* ---- stdc_bit_width ---- */
    if (stdc_bit_width_uc((unsigned char)0) != 0) return 47;
    if (stdc_bit_width_uc((unsigned char)1) != 1) return 48;
    if (stdc_bit_width_uc((unsigned char)255) != 8) return 49;
    if (stdc_bit_width_us((unsigned short)0) != 0) return 50;
    if (stdc_bit_width_us((unsigned short)256) != 9) return 51;
    if (stdc_bit_width_ui(0u) != 0) return 52;
    if (stdc_bit_width_ui(1u) != 1) return 53;
    if (stdc_bit_width_ui(5u) != 3) return 54;
    if (stdc_bit_width_ull(0ull) != 0) return 55;
    if (stdc_bit_width_ull(0x100000000ull) != 33) return 56;
    if (stdc_bit_width_ul(0x100000000ul) != 33) return 57;

    /* ---- stdc_has_single_bit ---- */
    if (stdc_has_single_bit_uc((unsigned char)0)) return 58;
    if (!stdc_has_single_bit_uc((unsigned char)1)) return 59;
    if (!stdc_has_single_bit_uc((unsigned char)128)) return 60;
    if (stdc_has_single_bit_uc((unsigned char)3)) return 61;
    if (stdc_has_single_bit_ui(0u)) return 62;
    if (!stdc_has_single_bit_ui(1u)) return 63;
    if (!stdc_has_single_bit_ui(8u)) return 64;
    if (stdc_has_single_bit_ui(6u)) return 65;
    if (!stdc_has_single_bit_ull(0x100000000ull)) return 66;

    /* ---- stdc_bit_floor ---- */
    if (stdc_bit_floor_uc((unsigned char)0) != 0) return 67;
    if (stdc_bit_floor_uc((unsigned char)5) != 4) return 68;
    if (stdc_bit_floor_uc((unsigned char)128) != 128) return 69;
    if (stdc_bit_floor_ui(0u) != 0) return 70;
    if (stdc_bit_floor_ui(1u) != 1) return 71;
    if (stdc_bit_floor_ui(5u) != 4) return 72;
    if (stdc_bit_floor_ui(8u) != 8) return 73;
    if (stdc_bit_floor_ull(0x180000000ull) != 0x100000000ull) return 74;

    /* ---- stdc_bit_ceil ---- */
    if (stdc_bit_ceil_uc((unsigned char)0) != 1) return 75;
    if (stdc_bit_ceil_uc((unsigned char)5) != 8) return 76;
    if (stdc_bit_ceil_uc((unsigned char)128) != 128) return 77;
    if (stdc_bit_ceil_uc((unsigned char)129) != 0) return 78; /* unrepresentable */
    if (stdc_bit_ceil_ui(0u) != 1) return 79;
    if (stdc_bit_ceil_ui(1u) != 1) return 80;
    if (stdc_bit_ceil_ui(5u) != 8) return 81;
    if (stdc_bit_ceil_ui(8u) != 8) return 82;
    if (stdc_bit_ceil_ui(0x80000000u) != 0x80000000u) return 83;
    if (stdc_bit_ceil_ui(0x80000001u) != 0) return 84; /* unrepresentable */
    if (stdc_bit_ceil_ull(0x180000000ull) != 0x200000000ull) return 85;

    /* ---- stdc_first_leading_one ---- */
    if (stdc_first_leading_one_uc((unsigned char)0) != 0) return 86;
    if (stdc_first_leading_one_uc((unsigned char)0x80) != 1) return 87;
    if (stdc_first_leading_one_uc((unsigned char)0x40) != 2) return 88;
    if (stdc_first_leading_one_uc((unsigned char)1) != 8) return 89;
    if (stdc_first_leading_one_ui(0u) != 0) return 90;
    if (stdc_first_leading_one_ui(1u) != 32) return 91;
    if (stdc_first_leading_one_ui(0x80000000u) != 1) return 92;
    if (stdc_first_leading_one_ull(0ull) != 0) return 93;
    if (stdc_first_leading_one_ull(1ull) != 64) return 94;

    /* ---- stdc_first_leading_zero ---- */
    if (stdc_first_leading_zero_uc((unsigned char)0xFF) != 0) return 95;
    if (stdc_first_leading_zero_uc((unsigned char)0x7F) != 1) return 96;
    if (stdc_first_leading_zero_uc((unsigned char)0) != 1) return 97;
    if (stdc_first_leading_zero_ui(0xFFFFFFFFu) != 0) return 98;
    if (stdc_first_leading_zero_ui(0u) != 1) return 99;
    if (stdc_first_leading_zero_ull(0xFFFFFFFFFFFFFFFFull) != 0) return 100;

    /* ---- stdc_first_trailing_one ---- */
    if (stdc_first_trailing_one_uc((unsigned char)0) != 0) return 101;
    if (stdc_first_trailing_one_uc((unsigned char)1) != 1) return 102;
    if (stdc_first_trailing_one_uc((unsigned char)8) != 4) return 103;
    if (stdc_first_trailing_one_ui(0u) != 0) return 104;
    if (stdc_first_trailing_one_ui(8u) != 4) return 105;
    if (stdc_first_trailing_one_ull(0ull) != 0) return 106;
    if (stdc_first_trailing_one_ull(8ull) != 4) return 107;

    /* ---- stdc_first_trailing_zero ---- */
    if (stdc_first_trailing_zero_uc((unsigned char)0xFF) != 0) return 108;
    if (stdc_first_trailing_zero_uc((unsigned char)0xFE) != 1) return 109;
    if (stdc_first_trailing_zero_uc((unsigned char)0) != 1) return 110;
    if (stdc_first_trailing_zero_ui(0xFFFFFFFFu) != 0) return 111;
    if (stdc_first_trailing_zero_ui(0u) != 1) return 112;
    if (stdc_first_trailing_zero_ull(0xFFFFFFFFFFFFFFFFull) != 0) return 113;

    /* ---- _Generic dispatch ---- */
    unsigned char  uc = 0xF0;
    unsigned short us = 0xF000;
    unsigned int   ui = 0xF0000000u;
    unsigned long  ul = 0xF000000000000000ul;
    unsigned long long ull = 0xF000000000000000ull;

    if (stdc_leading_zeros(uc) != 0) return 114;
    if (stdc_leading_zeros(us) != 0) return 115;
    if (stdc_leading_zeros(ui) != 0) return 116;
    if (stdc_leading_zeros(ul) != 0) return 117;
    if (stdc_leading_zeros(ull) != 0) return 118;

    if (stdc_count_ones(uc) != 4) return 119;
    if (stdc_count_zeros(uc) != 4) return 120;
    if (stdc_has_single_bit(ui) != 0) return 121;

    if (stdc_bit_width((unsigned char)5) != 3) return 122;
    if (stdc_bit_floor((unsigned int)5u) != 4u) return 123;
    if (stdc_bit_ceil((unsigned int)5u) != 8u) return 124;

    if (stdc_first_leading_one(uc) != 1) return 125;
    if (stdc_first_leading_zero((unsigned char)0x7F) != 1) return 126;
    if (stdc_first_trailing_one((unsigned char)8) != 4) return 127;
    if (stdc_first_trailing_zero((unsigned char)0xFE) != 1) return 128;

    /* ---- Endian macros ---- */
#if __STDC_ENDIAN_NATIVE__ != __STDC_ENDIAN_LITTLE__ && \
    __STDC_ENDIAN_NATIVE__ != __STDC_ENDIAN_BIG__
    return 129;  /* NATIVE must be one of LITTLE or BIG */
#endif
#if __STDC_ENDIAN_LITTLE__ != 1234
    return 130;
#endif
#if __STDC_ENDIAN_BIG__ != 4321
    return 131;
#endif

    return 42;
}

// test_std_c23_stdckdint
[[cccc::test(return = 42)]]
int test_std_c23_stdckdint(void) {
    int r;

    // No overflow
    if (ckd_add(&r, 1, 2)) return 1;
    if (r != 3) return 2;

    if (ckd_sub(&r, 5, 3)) return 3;
    if (r != 2) return 4;

    if (ckd_mul(&r, 3, 4)) return 5;
    if (r != 12) return 6;

    // Overflow detected
    if (!ckd_add(&r, INT_MAX, 1)) return 7;
    if (!ckd_sub(&r, INT_MIN, 1)) return 8;
    if (!ckd_mul(&r, INT_MAX, 2)) return 9;

    // long long variant
    long long rll;
    if (ckd_add(&rll, 1ll, 2ll)) return 10;
    if (rll != 3ll) return 11;
    if (!ckd_mul(&rll, (long long)LLONG_MAX, 2ll)) return 12;

    return 42;
}

// test_std_c23_strtol_binary
[[cccc::test(return = 42)]]
int test_std_c23_strtol_binary(void) {
    char *end;

    // base 0: 0b/0B prefix selects binary
    if (strtol("0b1010", &end, 0) != 10) return 1;
    if (*end != '\0') return 2;

    if (strtol("0B1010", &end, 0) != 10) return 3;

    // explicit base 2 also accepts the prefix
    if (strtol("0b1010", &end, 2) != 10) return 4;

    // negative
    if (strtol("-0b1010", &end, 2) != -10) return 5;

    // leading whitespace
    if (strtol("  0b101", &end, 0) != 5) return 6;

    // endptr points past consumed binary digits, to trailing junk
    long v = strtol("0b101xyz", &end, 0);
    if (v != 5) return 7;
    if (strcmp(end, "xyz") != 0) return 8;

    // non-prefixed inputs still work as before
    if (strtol("123", &end, 10) != 123) return 9;
    if (strtol("0x1F", &end, 0) != 31) return 10;
    if (strtol("017", &end, 0) != 15) return 11; // octal

    // strtoul / strtoull with 0b prefix
    if (strtoul("0b11", &end, 0) != 3) return 12;
    if (strtoull("0B11111111", &end, 0) != 255ull) return 13;

    // strtoll with 0b prefix
    if (strtoll("0b10000000000", &end, 0) != (1ll << 10)) return 14;

    // "0b" with no valid binary digit after it falls back (parses leading "0")
    v = strtol("0bz", &end, 0);
    if (v != 0) return 15;
    if (strcmp(end, "bz") != 0) return 16;

    return 42;
}

// test_std_c23_unreachable
[[cccc::test(return = 42)]]
int test_std_c23_unreachable(void) {
    if (classify(5) != 1) return 1;
    if (classify(-5) != -1) return 2;
    if (classify(0) != 0) return 3;
    return 42;
}

#pragma cccc suite end
