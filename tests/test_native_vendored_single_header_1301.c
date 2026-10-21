// CCCC_FLAGS: tests/fixtures/vendored_1301_smoke_a.c
// A single-header library with macro-pasted declarator names, implemented by
// one TU and only declared by another, builds under -c=native.
int v1301_smoke_add(int x);
int vendored_1301_smoke_call_a(void);
int main(void) {
    return vendored_1301_smoke_call_a() + v1301_smoke_add(20);
}
