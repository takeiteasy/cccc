// CCCC_FLAGS: --testing
// CCCC_NATIVE_SKIP: negative (error=) tests
// Suite: operator templates are matched by operand type, and a malformed or
// ambiguous one is reported where it is used.
#pragma cccc suite begin "operator_templates_errors"

@template(T) struct Vec2 { T x, y; };

typedef struct { int x, y; } Plain;

[[cccc::test(error = "invalid operands to +")]] void test_unrelated_struct_is_not_matched(void) {
    Plain p = {1, 2};
    Plain q = p + p;
}

@template(T) [[cccc::operator(^)]] @inst(T) Vec2 xor_a(@inst(T) Vec2 a, @inst(T) Vec2 b) { return a; }
@template(T) [[cccc::operator(^)]] @inst(T) Vec2 xor_b(@inst(T) Vec2 a, @inst(T) Vec2 b) { return b; }

[[cccc::test(error = "ambiguous operator templates for '^'")]] void test_two_templates_for_one_operand_pair_are_ambiguous(void) {
    @inst(int) Vec2 a = {1, 2};
    @inst(int) Vec2 c = a ^ a;
}

@template(T, U) [[cccc::operator(|)]] @inst(T) Vec2 or_t(@inst(T) Vec2 a, @inst(T) Vec2 b) { return a; }

[[cccc::test(error = "operator template parameter 'U' is not deduced by its operands")]] void test_undeduced_parameter(void) {
    @inst(int) Vec2 a = {1, 2};
    @inst(int) Vec2 c = a | a;
}

@template(T) [[cccc::operator(&)]] @inst(T) Vec2 and_t(@inst(T) Vec2 a, @inst(T) Vec2 b) { return (@inst(T) Vec2){a.nope, b.x}; }

[[cccc::test(error = "no such member")]] void test_error_in_instance_body_is_reported(void) {
    @inst(int) Vec2 a = {1, 2};
    @inst(int) Vec2 c = a & a;
}

#pragma cccc suite end
