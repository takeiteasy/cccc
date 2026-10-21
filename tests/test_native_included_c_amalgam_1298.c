// CCCC_FLAGS: tests/fixtures/amalgam_1298_smoke_a.c
// An external-linkage function defined in a .c file one TU #includes and
// another only declares builds under -c=native.
int amalgam_1298_smoke_add(int x);
int amalgam_1298_smoke_call_a(void);
int main(void) {
    return amalgam_1298_smoke_call_a() + amalgam_1298_smoke_add(20);
}
