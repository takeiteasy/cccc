// #815: a second "default:" label in the same switch used to silently
// overwrite the first with no diagnostic. Must now be a compile error.
// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: multiple default labels in one switch
int main(void) {
    switch (1) {
    case 1: break;
    default: break;
    default: break;
    }
    return 42;
}
