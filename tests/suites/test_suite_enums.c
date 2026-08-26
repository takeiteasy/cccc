// CCCC_FLAGS: --testing
// Consolidated suite: enum declarations, sizeof, switch, C23 enum extensions
// Source tests: test_c23_enum, test_enum_advanced, test_enum_demo,
// test_enum_simple, test_enum_sizeof, test_enum_switch

#include <stdint.h>

// [from test_c23_enum]
// C23 enum enhancements: underlying type, forward declaration, wide values

// --- Underlying type: unsigned char (1 byte) ---
enum ByteColor : unsigned char { BC_RED = 0, BC_GREEN = 1, BC_BLUE = 255 };

// --- Underlying type: short (2 bytes) ---
enum ShortDir : short { SD_NORTH = -1, SD_SOUTH = 1 };

// --- Underlying type: unsigned long (8 bytes) ---
enum WideFlags : unsigned long {
    WF_NONE = 0,
    WF_A    = 0x100000000UL,
    WF_B    = 0x200000000UL
};

// --- Underlying type: long long for large signed values ---
enum BigVal : long long {
    BV_MAX = 0x3FFFFFFFFFFFFFFFLL,
    BV_NEG = -0x3FFFFFFFFFFFFFFFLL
};

// --- Forward declaration with underlying type, then definition ---
enum Fwd : int;
enum Fwd { FWD_A = 10, FWD_B = 20 };

// --- Plain int-sized enum still works (no underlying type) ---
enum Plain { P_X = 7, P_Y = 42 };

// --- Typedef with underlying type ---
typedef enum TdEnum : unsigned char { TD_ZERO = 0, TD_MAX = 200 } TdEnum;

// [from test_enum_typedef]
// Test enum scoping and typedef enum (anonymous typedef)
enum TcEnumTypedefColor {
    TC_ENUM_TYPEDEF_RED,
    TC_ENUM_TYPEDEF_GREEN,
    TC_ENUM_TYPEDEF_BLUE
};

typedef enum {
    TC_ENUM_TYPEDEF_SMALL  = 1,
    TC_ENUM_TYPEDEF_MEDIUM = 2,
    TC_ENUM_TYPEDEF_LARGE  = 3
} TcEnumTypedefSize;

static int get_typedef_color_value(enum TcEnumTypedefColor c) {
    return c;
}

static int get_typedef_size_value(TcEnumTypedefSize s) {
    return s;
}

// [from test_enum_advanced]
// Test enum edge cases and advanced features
enum Large {
    TC_ENUM_ADVANCED_A = 100,
    TC_ENUM_ADVANCED_B, // 101
    TC_ENUM_ADVANCED_C = 10,
    TC_ENUM_ADVANCED_D  // 11
};

// [from test_enum_demo]
// Comprehensive enum demonstration for CCCC
// Shows all supported enum features

// Basic enum with implicit values
enum tc_enum_demo_Color {
    TC_ENUM_DEMO_RED,   // 0
    TC_ENUM_DEMO_GREEN, // 1
    TC_ENUM_DEMO_BLUE   // 2
};

// Enum with explicit values
enum Status { IDLE = 0, ACTIVE = 10, WAITING = 20, COMPLETE = 30 };

// Mixed auto/explicit values
enum Priority {
    LOW,          // 0
    MEDIUM = 5,   // 5
    HIGH,         // 6
    CRITICAL = 10 // 10
};

// Bitwise flags
enum Flags {
    FLAG_READ  = 1, // 0b0001
    FLAG_WRITE = 2, // 0b0010
    FLAG_EXEC  = 4  // 0b0100
};

// Function using enum parameter

static int calculate_score(enum Priority p, enum Status s) {
    return p + s;
}

// Function returning enum

static enum tc_enum_demo_Color get_color(int value) {
    if (value < 1)
        return TC_ENUM_DEMO_RED;
    if (value < 2)
        return TC_ENUM_DEMO_GREEN;
    return TC_ENUM_DEMO_BLUE;
}

// [from test_enum_simple]
// Test basic enum functionality
enum tc_enum_simple_Color {
    TC_ENUM_SIMPLE_RED,
    TC_ENUM_SIMPLE_GREEN,
    TC_ENUM_SIMPLE_BLUE
};

// [from test_enum_sizeof]
// Test sizeof with enums
enum tc_enum_sizeof_Color {
    TC_ENUM_SIZEOF_RED,
    TC_ENUM_SIZEOF_GREEN,
    TC_ENUM_SIZEOF_BLUE
};

// [from test_enum_switch]
// Test enum with mixed auto/explicit values
enum Mixed {
    TC_ENUM_SWITCH_A,      // 0
    TC_ENUM_SWITCH_B,      // 1
    TC_ENUM_SWITCH_C = 10, // 10
    TC_ENUM_SWITCH_D,      // 11
    E = 20,                // 20
    F                      // 21
};

#pragma cccc suite begin "enums"

// test_c23_enum
[[cccc::test(return = 42)]]
int test_c23_enum(void) {
    // size / align of underlying-typed enums
    if (sizeof(enum ByteColor) != 1)
        return 1;
    if (sizeof(enum ShortDir) != 2)
        return 2;
    if (sizeof(enum WideFlags) != 8)
        return 3;
    if (sizeof(enum BigVal) != 8)
        return 4;

    // Correct storage and round-trip of unsigned char enum
    enum ByteColor c = BC_BLUE;
    if (c != 255)
        return 5;
    if ((unsigned char)c != 255)
        return 6;

    // Short enum with negative value
    enum ShortDir tc_c23_enum_d = SD_NORTH;
    if (tc_c23_enum_d != -1)
        return 7;

    // 8-byte unsigned enum value above INT32_MAX
    enum WideFlags f = WF_A;
    if (f != 0x100000000UL)
        return 8;
    f = WF_B;
    if (f != 0x200000000UL)
        return 9;

    // Large signed long long enum value
    enum BigVal bv = BV_MAX;
    if (bv != 0x3FFFFFFFFFFFFFFFLL)
        return 10;
    bv = BV_NEG;
    if (bv != -0x3FFFFFFFFFFFFFFFLL)
        return 11;

    // Forward-declared then completed enum
    enum Fwd fwd = FWD_A;
    if (fwd != 10)
        return 12;
    fwd = FWD_B;
    if (fwd != 20)
        return 13;

    // Plain int-sized enum is unchanged
    enum Plain p = P_Y;
    if (p != 42)
        return 14;
    if (sizeof(enum Plain) != 4)
        return 15;

    // Typedef'tc_c23_enum_d enum with underlying type
    TdEnum td = TD_MAX;
    if (td != 200)
        return 16;
    if (sizeof(TdEnum) != 1)
        return 17;

    // unsigned enum: unsigned char underlying type means values wrap at 256
    enum ByteColor wrap = (enum ByteColor)256;
    // 256 as unsigned char wraps to 0
    if (wrap != 0)
        return 18;

    // Arithmetic with wide enum
    unsigned long wide_sum = WF_A + WF_B;
    if (wide_sum != 0x300000000UL)
        return 19;

    return 42;
}

// test_enum_advanced
[[cccc::test(return = 42)]]
int test_enum_advanced(void) {
    // Test sequential values
    enum Large x = TC_ENUM_ADVANCED_A;
    if (x != 100)
        return 1;

    x = TC_ENUM_ADVANCED_B;
    if (x != 101)
        return 2;

    x = TC_ENUM_ADVANCED_C;
    if (x != 10)
        return 3;

    x = TC_ENUM_ADVANCED_D;
    if (x != 11)
        return 4;

    // Test arithmetic with enums
    int sum = TC_ENUM_ADVANCED_A + TC_ENUM_ADVANCED_C; // 100 + 10 = 110
    if (sum != 110)
        return 5;

    // Test using enum in calculations
    int result = (TC_ENUM_ADVANCED_B - TC_ENUM_ADVANCED_A) +
                 (TC_ENUM_ADVANCED_D -
                  TC_ENUM_ADVANCED_C); // (101-100) + (11-10) = 1 + 1 = 2
    if (result != 2)
        return 6;

    // Calculate final result
    result = TC_ENUM_ADVANCED_C + TC_ENUM_ADVANCED_D + x +
             result; // 10 + 11 + 11 + 2 = 34
    if (result != 34)
        return 7;

    return 42; // All tests passed
}

// test_enum_demo
[[cccc::test(return = 42)]]
int test_enum_demo(void) {
    // Test 1: Basic enum usage
    enum tc_enum_demo_Color c = TC_ENUM_DEMO_RED;
    if (c != 0)
        return 1;

    c = TC_ENUM_DEMO_BLUE;
    if (c != 2)
        return 2;

    // Test 2: Explicit values
    enum Status s = ACTIVE;
    if (s != 10)
        return 3;

    // Test 3: Arithmetic with enums
    int sum = ACTIVE + WAITING; // 10 + 20 = 30
    if (sum != 30)
        return 4;

    // Test 4: Enum comparisons
    enum Priority p = HIGH;
    if (p != 6)
        return 5; // HIGH should be 6
    if (!(p > MEDIUM))
        return 5; // 6 > 5

    // Test 5: Bitwise operations with enums
    int perms = FLAG_READ | FLAG_WRITE; // 0b0011 = 3
    if (perms != 3)
        return 6;

    if ((perms & FLAG_READ) == 0)
        return 7; // Check has read
    if ((perms & FLAG_EXEC) != 0)
        return 8; // Check no exec

    // Test 6: Enum in array subscript
    int values[3] = {100, 200, 300};
    int val       = values[TC_ENUM_DEMO_GREEN]; // values[1] = 200
    if (val != 200)
        return 9;

    // Test 7: Enum as function parameter
    int score = calculate_score(HIGH, ACTIVE); // 6 + 10 = 16
    if (score != 16)
        return 10;

    // Test 8: Enum from function return
    enum tc_enum_demo_Color result_color = get_color(2);
    if (result_color != TC_ENUM_DEMO_BLUE)
        return 11;

    // Test 9: Comparison operations
    if (HIGH <= MEDIUM)
        return 12;
    if (!(CRITICAL > HIGH))
        return 13;

    // Test 10: Using enum in expressions
    int total = LOW + MEDIUM + HIGH + CRITICAL; // 0 + 5 + 6 + 10 = 21
    if (total != 21)
        return 14;

    // All tests passed!
    return 42;
}

// test_enum_simple
[[cccc::test(return = 42)]]
int test_enum_simple(void) {
    enum tc_enum_simple_Color c = TC_ENUM_SIMPLE_RED;
    if (c == 0) {
        c = TC_ENUM_SIMPLE_GREEN;
    }
    if (c == 1) {
        c = TC_ENUM_SIMPLE_BLUE;
    }
    if (c != 2)
        return 1; // Assert c == 2 (TC_ENUM_SIMPLE_BLUE)
    return 42;
}

// test_enum_sizeof
[[cccc::test(return = 42)]]
int test_enum_sizeof(void) {
    enum tc_enum_sizeof_Color c = TC_ENUM_SIZEOF_RED;

    // In CCCC, sizeof(int) = 4, so sizeof(enum) should also be 4
    // since enums are treated as integers
    int s = sizeof(enum tc_enum_sizeof_Color);

    // Verify it's correct
    if (s != 4)
        return 1;

    return 42; // Success!
}

// test_enum_typedef: plain anonymous typedef enum
[[cccc::test(return = 42)]]
int test_enum_typedef(void) {
    enum TcEnumTypedefColor c = TC_ENUM_TYPEDEF_GREEN;
    TcEnumTypedefSize       s = TC_ENUM_TYPEDEF_MEDIUM;

    int                     result =
        get_typedef_color_value(c) + get_typedef_size_value(s); // 1 + 2 = 3
    if (result != 3)
        return 1;

    // Reassignment and multiply
    c = TC_ENUM_TYPEDEF_BLUE;
    s = TC_ENUM_TYPEDEF_LARGE;

    result =
        get_typedef_color_value(c) * get_typedef_size_value(s); // 2 * 3 = 6
    if (result != 6)
        return 2;

    return result * 7; // 6 * 7 = 42
}

// test_enum_switch
[[cccc::test(return = 42)]]
int test_enum_switch(void) {
    int sum = TC_ENUM_SWITCH_A + TC_ENUM_SWITCH_B + TC_ENUM_SWITCH_C +
              TC_ENUM_SWITCH_D + E + F; // 0 + 1 + 10 + 11 + 20 + 21 = 63

    // Test using enum in switch
    enum Mixed x = TC_ENUM_SWITCH_C;
    switch (x) {
        case TC_ENUM_SWITCH_A:
            return 1;
        case TC_ENUM_SWITCH_B:
            return 2;
        case TC_ENUM_SWITCH_C:
            return 42; // This should execute
        case TC_ENUM_SWITCH_D:
            return 4;
        default:
            return 0;
    }
}

// #1175: an enum with no fixed `: type` underlying type now widens past the
// plain-`int` default when an enumerator wouldn't fit in 32 bits, matching
// gcc-16/clang exactly (both widen as an extension predating C23's own
// 6.7.2.2 "must be able to represent every enumerator" requirement). Values
// already survived before this fix (stored as int64_t) -- this pins the
// sizeof/_Alignof/signedness selection specifically. Verified against
// gcc-16/clang for every shape below; `enum tc_1175_small` is a deliberate,
// documented exception (see the assertion below).
enum tc_1175_wide_unsigned { TC_1175_WU = 0x100000000LL };
enum tc_1175_wide_mixed { TC_1175_WM_NEG = -1, TC_1175_WM = 0x100000000LL };
enum tc_1175_narrow_unsigned { TC_1175_NU = 0x80000000ULL };
// A value near INT64_MAX rather than UINT64_MAX -- enumerator values are
// stored as int64_t (see the comment above and man/COVERAGE.md:182), a
// separate, pre-existing representation limit that already loses
// information for a value past INT64_MAX (e.g. 0xFFFFFFFFFFFFFFFFULL folds
// to -1 before #1175's own underlying-type selection ever runs) -- out of
// #1175's scope, not exercised here.
enum tc_1175_wide_unsigned64 { TC_1175_WU64 = 0x7FFFFFFF00000000ULL };
enum tc_1175_small { TC_1175_SMALL = 1 };
enum tc_1175_neg { TC_1175_NEG = -1 };

[[cccc::test(return = 42)]]
int test_wide_enum_underlying_type(void) {
    // Wide unsigned value (> INT32_MAX) -- widens to 8 bytes, unsigned.
    _Static_assert(sizeof(enum tc_1175_wide_unsigned) == 8, "cccc");
    _Static_assert(_Alignof(enum tc_1175_wide_unsigned) == 8, "cccc");
    if ((enum tc_1175_wide_unsigned) - 1 < 0)
        return 1; // must be unsigned

    // Mixed negative + wide-positive -- widens to 8 bytes, signed (only a
    // signed type can hold both -1 and a value past INT32_MAX).
    _Static_assert(sizeof(enum tc_1175_wide_mixed) == 8, "cccc");
    _Static_assert(_Alignof(enum tc_1175_wide_mixed) == 8, "cccc");
    if (!((enum tc_1175_wide_mixed) - 1 < 0))
        return 2; // must be signed
    if ((TC_1175_WM >> 32) != 1)
        return 3; // value itself already survived pre-#1175

    // Fits in unsigned int (> INT32_MAX but <= UINT32_MAX) -- stays 4
    // bytes, becomes unsigned.
    _Static_assert(sizeof(enum tc_1175_narrow_unsigned) == 4, "cccc");
    _Static_assert(_Alignof(enum tc_1175_narrow_unsigned) == 4, "cccc");
    if ((enum tc_1175_narrow_unsigned) - 1 < 0)
        return 4; // must be unsigned

    // Fits in int64_t but not uint32_t -- widens to 8 bytes, unsigned.
    _Static_assert(sizeof(enum tc_1175_wide_unsigned64) == 8, "cccc");
    _Static_assert(_Alignof(enum tc_1175_wide_unsigned64) == 8, "cccc");
    if ((enum tc_1175_wide_unsigned64) - 1 < 0)
        return 5; // must be unsigned

    // A small all-non-negative enum: gcc/clang give `unsigned int` here,
    // cccc deliberately keeps plain `int` (signed) -- sizeof/_Alignof agree
    // at 4/4 either way, so this is outside #1170's layout-parity bar, and
    // it's a separate, documented divergence (follow-up filed against
    // #1170), not a #1175 regression. Only the layout is pinned here
    // (agrees on both VM and every host); deliberately no runtime
    // signedness check like the shapes above -- this file is also
    // recompiled and re-run natively (see man/TESTING.md's native
    // round-trip mode), and a real host compiler picks `unsigned int` here
    // where the VM picks signed `int`, so a `(enum tc_1175_small)-1 < 0`
    // check would itself disagree between VM and native, not a bug in
    // either, just this exact deliberately-unfixed gap.
    _Static_assert(sizeof(enum tc_1175_small) == 4, "cccc");

    // A small negative-only enum was already correct before #1175 --
    // regression guard.
    _Static_assert(sizeof(enum tc_1175_neg) == 4, "cccc");
    _Static_assert(_Alignof(enum tc_1175_neg) == 4, "cccc");
    if (!((enum tc_1175_neg) - 1 < 0))
        return 7;

    return 42;
}

#pragma cccc suite end
