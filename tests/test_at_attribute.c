// Tests for @identifier / @identifier(args) attribute syntax (#234).
// CCCC_FLAGS: --testing

// --- @comptime ---

@comptime
int double_val(int x) { return x * 2; }

// @comptime — callable in expression position.
@comptime
Node *make_doubled_42(void) {
    return MakeIntLiteral(double_val(21));
}

@comptime
Node *add_one(Node *x) {
    return MakeBinary(NK_ADD, x, MakeIntLiteral(1));
}

[[cccc::test]]
void test_at_comptime_inline(void) {
    int v = make_doubled_42();
    AssertEq(v, 42);
}

[[cccc::test]]
void test_at_macro_inline(void) {
    int v = add_one(10);
    AssertEq(v, 11);
}

// --- @test annotation ---

@test
void test_at_test_bare(void) {
    AssertEq(1 + 1, 2);
}

@test(suite="at_suite")
void test_at_test_with_suite(void) {
    AssertEq(2 * 3, 6);
}

@test(name="custom name")
void test_at_test_with_name(void) {
    Assert(1);
}

// --- @test_setup / @test_teardown ---

static int setup_count = 0;
static int teardown_count = 0;

@test_setup
void at_setup(void) { setup_count++; }

@test_teardown
void at_teardown(void) { teardown_count++; }

@test
void test_at_setup_teardown_ran(void) {
    Assert(setup_count > 0);
}

// --- @nodiscard (std C23 -> [[nodiscard]]) ---

int @nodiscard compute_value(void) { return 99; }

[[cccc::test]]
void test_at_nodiscard_fn(void) {
    int v = compute_value();
    AssertEq(v, 99);
}

// --- @packed (GNU -> __attribute__((packed))) ---

struct @packed packed_data {
    char a;
    int  b;
    char c;
};

[[cccc::test]]
void test_at_packed_size(void) {
    // packed struct: 1 + 4 + 1 = 6, no padding
    AssertEq((int)sizeof(struct packed_data), 6);
}

// --- @unused (GNU -> __attribute__((unused))) ---

[[cccc::test]]
void test_at_unused_var(void) {
    int @unused x = 7;
    (void)x;
    Assert(1);
}

// --- @unknown fallback: unknown attrs use GNU form, parser silently ignores ---
// (would generate -Wattributes; skip here to avoid test noise)
