// CCCC_FLAGS: --testing
// Suite: overloadable functions declared in a block.

#define OVL __attribute__((overloadable))

int OVL pick(int x) {
    return 1;
}
int OVL pick(double x) {
    return 2;
}

int plain(int x) {
    return 5;
}
int OVL plain(double x) {
    return 6;
}

[[cccc::test]]
void test_file_scope_set_outside_blocks(void) {
    AssertEq(pick(1.0), 2);
    {
        int OVL pick(int);
    }
    AssertEq(pick(1.0), 2);
}

[[cccc::test]]
void test_block_set_hides_the_outer_set(void) {
    int OVL pick(int);
    AssertEq(pick(1.0), 1);
}

[[cccc::test]]
void test_inner_block_hides_the_outer_block(void) {
    int OVL pick(int);
    {
        int OVL pick(double);
        AssertEq(pick(1), 2);
    }
    AssertEq(pick(1.0), 1);
}

[[cccc::test]]
void test_block_redeclaration_of_a_member(void) {
    int OVL pick(double);
    AssertEq(pick(1), 2);
}

[[cccc::test]]
void test_block_set_hides_a_plain_function(void) {
    AssertEq(plain(1), 5);
    int OVL plain(double);
    AssertEq(plain(1), 6);
}

[[cccc::test]]
void test_block_set_hides_a_variable(void) {
    int pick = 5;
    {
        int OVL pick(int);
        AssertEq(pick(2.5), 1);
    }
    AssertEq(pick, 5);
}

[[cccc::test]]
void test_variable_hides_an_outer_set(void) {
    int pick = 7;
    AssertEq(pick, 7);
}

[[cccc::test]]
void test_block_set_picks_by_target_type(void) {
    int OVL wide(int);
    int OVL wide(long);
    int (*p)(int)  = wide;
    int (*q)(long) = &wide;
    AssertEq(p(0), 3);
    AssertEq(q(0), 4);
}

int OVL wide(int x) {
    return 3;
}
int OVL wide(long x) {
    return 4;
}

[[cccc::test]]
void test_file_set_picks_by_target_type_after_a_block(void) {
    {
        int OVL pick(int);
    }
    int (*q)(double) = pick;
    AssertEq(q(0), 2);
}

// Declared in a block, defined at file scope afterwards.
[[cccc::test]]
void test_block_declared_before_its_definition(void) {
    int OVL later(int);
    int OVL later(double);
    AssertEq(later(1), 10);
    AssertEq(later(1.0), 20);
}

int OVL later(int x) {
    return 10;
}
int OVL later(double x) {
    return 20;
}

[[cccc::test]]
void test_definition_joins_the_file_scope_set(void) {
    AssertEq(later(1), 10);
    AssertEq(later(1.0), 20);
    AssertEq(later(1.5f), 20);
}

// A block set hides a function template of its name.
@template(T) int via(T x) {
    return 10;
}
int OVL via(int x) {
    return 1;
}
int OVL via(long x) {
    return 2;
}

[[cccc::test]]
void test_template_joins_the_file_scope_set(void) {
    AssertEq(via(2.0), 10);
}

[[cccc::test]]
void test_block_set_hides_a_template(void) {
    int OVL via(long);
    AssertEq(via(2.0), 2);
    AssertEq(via(1), 2);
}
