// CCCC_FLAGS: --testing
// CCCC_NATIVE_SKIP: negative (error=) tests
// Consolidated suite: using a void value where a value is required is rejected.

#pragma cccc suite begin "void_value"

static void f(void) {}
static int g(int x) {
    return x;
}

[[cccc::test(error = "void value not ignored as it ought to be")]]
void test_void_value_initializer(void) {
    int v = f();
    (void)v;
}

[[cccc::test(error = "void value not ignored as it ought to be")]]
void test_void_value_assignment(void) {
    int v;
    v = f();
    (void)v;
}

[[cccc::test(error = "void value not ignored as it ought to be")]]
void test_void_value_call_argument(void) {
    (void)g(f());
}

[[cccc::test(error = "void value not ignored as it ought to be")]]
void test_void_value_cast_to_int(void) {
    (void)(int)f();
}

[[cccc::test(error = "void value not ignored as it ought to be")]]
void test_void_value_conditional_arm(void) {
    int v = 1 ? f() : 2;
    (void)v;
}

[[cccc::test(error = "void value not ignored as it ought to be")]]
void test_void_value_if_condition(void) {
    if (f()) {
    }
}

[[cccc::test(error = "void value not ignored as it ought to be")]]
void test_void_value_while_condition(void) {
    while (f()) {
        break;
    }
}

[[cccc::test(error = "void value not ignored as it ought to be")]]
void test_void_value_for_condition(void) {
    for (; f();) {
        break;
    }
}

[[cccc::test(error = "void value not ignored as it ought to be")]]
void test_void_value_do_condition(void) {
    do {
    } while (f());
}

[[cccc::test(error = "void value not ignored as it ought to be")]]
void test_void_value_switch_controlling(void) {
    switch (f()) {
        default:
            break;
    }
}

[[cccc::test(error = "void value not ignored as it ought to be")]]
void test_void_value_conditional_condition(void) {
    (void)(f() ? 1 : 2);
}

[[cccc::test(error = "void value not ignored as it ought to be")]]
void test_void_value_not_operand(void) {
    (void)!f();
}

[[cccc::test(error = "void value not ignored as it ought to be")]]
void test_void_value_logical_and_operand(void) {
    (void)(f() && 1);
}

[[cccc::test(error = "void value not ignored as it ought to be")]]
void test_void_value_logical_or_operand(void) {
    (void)(1 || f());
}

[[cccc::test(error = "void value not ignored as it ought to be")]]
void test_void_value_stmt_expr_value(void) {
    int v = ({
        if (1) {
        }
    });
    (void)v;
}

[[cccc::test(error = "void value not ignored as it ought to be")]]
int test_void_value_return(void) {
    return f();
}

static int hits;
static void hit(void) {
    hits++;
}
static void forward(void) {
    return hit();
}

// Void values are fine where no value is wanted.
[[cccc::test(return = 42)]]
int test_void_value_allowed(void) {
    int a = 1;
    (void)hit();
    hit();
    (void)(a ? hit() : hit());
    a ? hit() : hit();
    (hit(), hit());
    a && (hit(), 1);
    forward();
    return hits == 8 ? 42 : hits;
}

#pragma cccc suite end
