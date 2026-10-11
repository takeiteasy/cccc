// CCCC_FLAGS: --testing
// Consolidated suite: malformed GNU extended asm is rejected at parse time.

#pragma cccc suite begin "asm_extended_errors"

[[cccc::test(error = "must start with '=' or '+'")]]
void test_asm_output_constraint_without_equals(void) {
    int x;
    __asm__("" : "r"(x));
}

[[cccc::test(error = "not an lvalue")]]
void test_asm_output_rvalue(void) {
    int x = 1;
    __asm__("" : "=r"(x + 1));
}

[[cccc::test(error = "not an lvalue")]]
void test_asm_output_constant(void) {
    __asm__("" : "=r"(5));
}

[[cccc::test(error = "not an lvalue")]]
void test_asm_output_array(void) {
    int a[2];
    __asm__("" : "=r"(a));
}

[[cccc::test(error = "const-qualified")]]
void test_asm_output_const(void) {
    const int c = 1;
    __asm__("" : "=r"(c));
}

[[cccc::test(error = "duplicate asm operand name")]]
void test_asm_duplicate_symbolic_name(void) {
    int x, y;
    __asm__("" : [n] "=r"(x), [n] "=r"(y));
}

[[cccc::test(error = "'goto' qualifier")]]
void test_asm_labels_without_goto(void) {
    __asm__("" : : : : out);
out:;
}

[[cccc::test(error = "undeclared label")]]
void test_asm_goto_undeclared_label(void) {
    __asm__ goto("" : : : : nowhere);
}

[[cccc::test(error = "more than 30 operands")]]
void test_asm_too_many_operands(void) {
    int v = 0;
    __asm__(""
            :
            : "r"(v), "r"(v), "r"(v), "r"(v), "r"(v), "r"(v), "r"(v), "r"(v),
              "r"(v), "r"(v), "r"(v), "r"(v), "r"(v), "r"(v), "r"(v), "r"(v),
              "r"(v), "r"(v), "r"(v), "r"(v), "r"(v), "r"(v), "r"(v), "r"(v),
              "r"(v), "r"(v), "r"(v), "r"(v), "r"(v), "r"(v), "r"(v));
}

[[cccc::test(error = "more than 30 operands")]]
void test_asm_read_write_counts_twice(void) {
    int v[16] = {0};
    __asm__(""
            : "+r"(v[0]), "+r"(v[1]), "+r"(v[2]), "+r"(v[3]), "+r"(v[4]),
              "+r"(v[5]), "+r"(v[6]), "+r"(v[7]), "+r"(v[8]), "+r"(v[9]),
              "+r"(v[10]), "+r"(v[11]), "+r"(v[12]), "+r"(v[13]), "+r"(v[14]),
              "+r"(v[15]));
}

[[cccc::test(error = "expected constraint string literal")]]
void test_asm_constraint_not_a_string(void) {
    int x;
    __asm__("" : x(x));
}

[[cccc::test(error = "expected clobber string literal")]]
void test_asm_clobber_not_a_string(void) {
    __asm__("" : : : memory);
}

[[cccc::test(error = "invalid operand number %2")]]
void test_asm_template_operand_out_of_range(void) {
    int x, y;
    __asm__("mov %2, %0" : "=r"(x) : "r"(y));
}

[[cccc::test(error = "undefined named operand 'nope'")]]
void test_asm_template_unknown_symbolic_name(void) {
    int x;
    __asm__("mov %[nope], %0" : "=r"(x));
}

[[cccc::test(error = "expected ')'")]]
void test_asm_too_many_sections(void) {
    __asm__ goto("" : : : : out :);
out:;
}

#pragma cccc suite end
