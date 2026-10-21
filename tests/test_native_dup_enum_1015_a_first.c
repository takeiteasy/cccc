// CCCC_FLAGS: tests/fixtures/dup_enum_1015_smoke_a.c tests/fixtures/dup_enum_1015_smoke_b.c
// Two TUs declaring a same-named enum tag and enumerators build under
// -c=native.
extern int a_use_1015_smoke(void);
extern int b_use_1015_smoke(void);
int main(void) {
    return a_use_1015_smoke() + b_use_1015_smoke() + 28;
}
