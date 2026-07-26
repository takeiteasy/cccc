// #815: two case labels stacked back-to-back with the same value must be
// diagnosed the same way as sequential duplicates elsewhere in the switch.
// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: duplicate case value '1'
int main(void) {
    switch (1) {
    case 1:
    case 1:
        break;
    }
    return 42;
}
