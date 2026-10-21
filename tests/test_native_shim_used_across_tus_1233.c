// CCCC_FLAGS: tests/fixtures/shim_used_1233_a.c tests/fixtures/shim_used_1233_b.c
// A shim used in several TUs builds under -c=native.
int unrelated_1233(int x);
int report_1233(int x);
int main(void) {
    return report_1233(unrelated_1233(41));
}
