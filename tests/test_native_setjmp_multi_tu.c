// CCCC_FLAGS: tests/fixtures/setjmp_multi_a.c tests/fixtures/setjmp_multi_b.c
// setjmp declared in several TUs resolves to the same builtin under -c=native.
extern int setjmp_multi_fn_a(void);
extern int setjmp_multi_fn_b(void);
int main(void) {
    setjmp_multi_fn_a();
    setjmp_multi_fn_b();
    return 42;
}
