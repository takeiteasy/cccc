// CCCC_FLAGS: --testing
// Consolidated suite: #embed directive and parameters
// Source tests: test_embed_all_params, test_embed_if_empty,
// test_embed_limit_constexpr,
//   test_embed_limit_zero_if_empty, test_embed_nested_parens,
//   test_embed_param_order, test_embed_prefix, test_embed_prefix_suffix,
//   test_embed_suffix, test_embed_basic, test_embed_empty, test_embed_minimal,
//   test_embed_limit, test_embed_limit_zero, test_embed_inline

// [from test_embed_all_params]
// Test #embed with all parameters: prefix, suffix, limit, if_empty

// [from test_embed_if_empty]
// Test #embed with if_empty parameter on empty file

// [from test_embed_limit_constexpr]
// Test #embed limit parameter with full constant expressions
#define LIMIT_BASE      1
#define LIMIT_EXPR      (LIMIT_BASE + 1)
#define LIMIT_MAX(a, b) ((a) > (b) ? (a) : (b))

// [from test_embed_limit_zero_if_empty]
// Test #embed with limit(0) and if_empty - limit(0) makes file "empty"

// [from test_embed_nested_parens]
// Test #embed with nested parentheses in parameters
// This tests that the parameter parser correctly handles depth tracking

// Helper macro to simulate nested parentheses
#define NESTED(x, y) ((x) + (y))

// [from test_embed_param_order]
// Test #embed parameter order independence
// Parameters should work in any order

// [from test_embed_prefix]
// Test #embed with prefix parameter

// [from test_embed_prefix_suffix]
// Test #embed with both prefix and suffix parameters

// [from test_embed_suffix]
// Test #embed with suffix parameter

#pragma cccc suite begin "embed"

// test_embed_all_params
[[cccc::test(return = 42)]]
int test_embed_all_params(void) {
    // test_data.bin contains 3 bytes, but we limit to 2
    unsigned char data[] = {
#embed "../embed_data/test_data.bin" prefix(1, ) suffix(, 9) limit(2)          \
    if_empty(0)
    };

    // Should have: 1, byte0, byte1, 9
    // if_empty is ignored because file is not empty after limit
    int size = sizeof(data);
    if (size != 4) {
        return 1; // Wrong size
    }

    // Verify prefix
    if (data[0] != 1) {
        return 2; // Prefix incorrect
    }

    // Verify suffix
    if (data[3] != 9) {
        return 3; // Suffix incorrect
    }

    return 42;    // Success
}

// test_embed_if_empty
[[cccc::test(return = 42)]]
int test_embed_if_empty(void) {
    // empty_file.bin is empty (0 bytes)
    unsigned char data[] = {
#embed "../embed_data/empty_file.bin" if_empty(42)
    };

    // Should have just the if_empty value: 42
    int size = sizeof(data);
    if (size != 1) {
        return 1; // Wrong size
    }

    // Verify if_empty value
    if (data[0] != 42) {
        return 2; // if_empty value incorrect
    }

    return 42;    // Success
}

// test_embed_limit_constexpr
[[cccc::test(return = 42)]]
int test_embed_limit_constexpr(void) {
    unsigned char arithmetic[] = {
#embed "../embed_data/test_data.bin" limit(1 + 1)
    };

    unsigned char macro[] = {
#embed "../embed_data/test_data.bin" limit(LIMIT_EXPR)
    };

    unsigned char conditional[] = {
#embed "../embed_data/test_data.bin" limit(0 ? 1 : 2)
    };

    unsigned char function_macro[] = {
#embed "../embed_data/test_data.bin" limit(LIMIT_MAX(1, 2))
    };

    unsigned char cast_sizeof[] = {
#embed "../embed_data/test_data.bin" limit((int)sizeof(char[2]))
    };

    if (sizeof(arithmetic) != 2 || sizeof(macro) != 2 ||
        sizeof(conditional) != 2 || sizeof(function_macro) != 2 ||
        sizeof(cast_sizeof) != 2) {
        return 1;
    }

    if (arithmetic[0] != 10 || macro[1] != 20 || conditional[0] != 10 ||
        function_macro[1] != 20 || cast_sizeof[1] != 20) {
        return 2;
    }

    return 42;
}

// test_embed_limit_zero_if_empty
[[cccc::test(return = 42)]]
int test_embed_limit_zero_if_empty(void) {
    // test_data.bin contains 3 bytes, but limit(0) makes it empty
    unsigned char data[] = {
#embed "../embed_data/test_data.bin" limit(0) if_empty(99)
    };

    // Should have just the if_empty value: 99
    // Because limit(0) results in 0 bytes, triggering if_empty
    int size = sizeof(data);
    if (size != 1) {
        return 1; // Wrong size
    }

    // Verify if_empty value
    if (data[0] != 99) {
        return 2; // if_empty value incorrect
    }

    return 42;    // Success
}

// test_embed_nested_parens
[[cccc::test(return = 42)]]
int test_embed_nested_parens(void) {
    // test_data.bin contains 3 bytes
    unsigned char data[] = {
#embed "../embed_data/test_data.bin" prefix(NESTED(1, 2), 5, )
    };

    // NESTED(1, 2) should expand to ((1) + (2)) = 3
    // So we should have: 3, 5, byte0, byte1, byte2
    int size = sizeof(data);
    if (size != 5) {
        return 1; // Wrong size
    }

    // Verify prefix values
    if (data[0] != 3 || data[1] != 5) {
        return 2; // Prefix values incorrect
    }

    return 42;    // Success
}

// test_embed_param_order
[[cccc::test(return = 42)]]
int test_embed_param_order(void) {
    // test_data.bin contains 3 bytes, but we limit to 2
    // Parameters specified in non-standard order
    unsigned char data[] = {
#embed "../embed_data/test_data.bin" suffix(, 88) prefix(77, ) limit(2)
    };

    // Should have: 77, byte0, byte1, 88
    int size = sizeof(data);
    if (size != 4) {
        return 1; // Wrong size
    }

    // Verify prefix
    if (data[0] != 77) {
        return 2; // Prefix incorrect
    }

    // Verify suffix
    if (data[3] != 88) {
        return 3; // Suffix incorrect
    }

    return 42;    // Success
}

// test_embed_prefix
[[cccc::test(return = 42)]]
int test_embed_prefix(void) {
    // test_data.bin contains 3 bytes
    unsigned char data[] = {
#embed "../embed_data/test_data.bin" prefix(100, 101, )
    };

    // Should have: 100, 101, byte0, byte1, byte2
    int size = sizeof(data);
    if (size != 5) {
        return 1; // Wrong size
    }

    // Verify prefix values
    if (data[0] != 100 || data[1] != 101) {
        return 2; // Prefix values incorrect
    }

    return 42;    // Success
}

// test_embed_prefix_suffix
[[cccc::test(return = 42)]]
int test_embed_prefix_suffix(void) {
    // test_data.bin contains 3 bytes
    unsigned char data[] = {
#embed "../embed_data/test_data.bin" prefix(1, 2, ) suffix(, 9, 10)
    };

    // Should have: 1, 2, byte0, byte1, byte2, 9, 10
    int size = sizeof(data);
    if (size != 7) {
        return 1; // Wrong size
    }

    // Verify prefix values
    if (data[0] != 1 || data[1] != 2) {
        return 2; // Prefix values incorrect
    }

    // Verify suffix values
    if (data[5] != 9 || data[6] != 10) {
        return 3; // Suffix values incorrect
    }

    return 42;    // Success
}

// test_embed_suffix
[[cccc::test(return = 42)]]
int test_embed_suffix(void) {
    // test_data.bin contains 3 bytes
    unsigned char data[] = {
#embed "../embed_data/test_data.bin" suffix(, 200, 201)
    };

    // Should have: byte0, byte1, byte2, 200, 201
    int size = sizeof(data);
    if (size != 5) {
        return 1; // Wrong size
    }

    // Verify suffix values
    if (data[3] != 200 || data[4] != 201) {
        return 2; // Suffix values incorrect
    }

    return 42;    // Success
}

// test_embed_basic: basic #embed reading 3 bytes that sum to 42
[[cccc::test(return = 42)]]
int test_embed_basic(void) {
    unsigned char data[] = {
#embed "../embed_data/test_data.bin"
    };
    if (sizeof(data) != 3)
        return 1;
    return data[0] + data[1] + data[2];
}

// test_embed_empty: #embed on empty file with single fallback byte
[[cccc::test(return = 42)]]
int test_embed_empty(void) {
    unsigned char data[] = {
#embed "../embed_data/empty_file.bin"
        42};
    if (sizeof(data) != 1)
        return 1;
    return data[0];
}

// test_embed_minimal: #embed with no surrounding whitespace
[[cccc::test(return = 42)]]
int test_embed_minimal(void) {
    unsigned char data[] = {
#embed "../embed_data/test_data.bin"
    };
    return data[0] + data[1] + data[2];
}

// test_embed_limit: #embed limit(2) reads first 2 bytes, add 12 to reach 42
[[cccc::test(return = 42)]]
int test_embed_limit_basic(void) {
    unsigned char data[] = {
#embed "../embed_data/test_data.bin" limit(2)
    };
    if (sizeof(data) != 2)
        return 1;
    return data[0] + data[1] + 12;
}

// test_embed_limit_zero: #embed limit(0) produces empty with fallback byte 42
[[cccc::test(return = 42)]]
int test_embed_limit_zero_basic(void) {
    unsigned char data[] = {
#embed "../embed_data/test_data.bin" limit(0)
        42};
    if (sizeof(data) != 1)
        return 1;
    return data[0];
}

// test_embed_inline: #embed on same line as initializer brace
[[cccc::test(return = 42)]]
int test_embed_inline_basic(void) {
    unsigned char data[] = { #embed "../embed_data/test_data.bin" };
    if (sizeof(data) != 3)
        return 1;
    return data[0] + data[1] + data[2];
}

#pragma cccc suite end
