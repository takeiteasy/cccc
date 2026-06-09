// Tests for error_count comparison operators in [[cccc::test]] (ticket #343).
// CCCC_FLAGS: --testing

#pragma cccc suite begin "error_count_ops"

// Exact equality (explicit = operator)
[[cccc::test(error = "undeclared", error_count = 1)]]
void test_ec_eq_one(void) {
    int a = undeclared_var;
}

// != operator: two errors produced, != 1 passes
[[cccc::test(error = "undeclared", error_count != 1)]]
void test_ec_ne_one(void) {
    int a = undeclared_x + undeclared_y;
}

// > operator: two errors, > 1 passes
[[cccc::test(error = "undeclared", error_count > 1)]]
void test_ec_gt_one(void) {
    int a = undeclared_x + undeclared_y;
}

// >= operator: two errors, >= 2 passes
[[cccc::test(error = "undeclared", error_count >= 2)]]
void test_ec_ge_two(void) {
    int a = undeclared_x + undeclared_y;
}

// < operator: one error, < 3 passes
[[cccc::test(error = "undeclared", error_count < 3)]]
void test_ec_lt_three(void) {
    int a = undeclared_var;
}

// <= operator: two errors, <= 2 passes
[[cccc::test(error = "undeclared", error_count <= 2)]]
void test_ec_le_two(void) {
    int a = undeclared_x + undeclared_y;
}

#pragma cccc suite end

// Negated pattern: error != "nonexistent" passes when pattern is absent from errors
[[cccc::test(error != "nonexistent_pattern_xyz", suite = "error_pat_negate")]]
void test_neg_pattern_absent(void) {
    int a = undeclared_var;
}
