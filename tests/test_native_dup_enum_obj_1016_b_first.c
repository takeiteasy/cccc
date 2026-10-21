// CCCC_FLAGS: tests/fixtures/dup_enum_obj_1016_smoke_b.c tests/fixtures/dup_enum_obj_1016_smoke_a.c
// Two TUs declaring a same-named enum with enumerator-typed objects build under
// -c=native.
extern int a_use_1016_smoke(void);
extern int b_use_1016_smoke(void);
int main(void) {
    return a_use_1016_smoke() - 19 + (b_use_1016_smoke() - 303) + 42;
}
