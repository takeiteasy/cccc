// CCCC_FLAGS: --testing
// CCCC_NATIVE_SKIP: negative (error=) tests
// Suite: overloadable methods and operators that cannot be resolved.

#define OVL __attribute__((overloadable))

typedef struct { int x; } V;

[[cccc::method(V, f)]]
OVL int v_f_int(V *self, int k) { return 1; }

[[cccc::method(V, f)]]
OVL int v_f_dbl(V *self, double k) { return 2; }

[[cccc::method(V, plain)]]
int v_plain(V *self) { return 1; }

[[cccc::operator(*)]]
OVL V v_mul_long(V a, long k) { return a; }

[[cccc::operator(*)]]
OVL V v_mul_dbl(V a, double k) { return a; }

[[cccc::test(error = "call to 'f' is ambiguous")]]
void test_ambiguous_method_call(void) {
    V v = {1};
    v.f(1L);
}

[[cccc::test(error = "no matching function for call to 'f'")]]
void test_no_matching_method(void) {
    V v = {1};
    v.f("text");
}

[[cccc::test(error = "call to 'operator*' is ambiguous")]]
void test_ambiguous_operator(void) {
    V v = {1};
    v * 2;
}

[[cccc::test(error = "invalid operands to *")]]
void test_no_matching_operator(void) {
    V v = {1};
    v * "text";
}
