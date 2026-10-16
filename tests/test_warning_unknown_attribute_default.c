// CCCC_EXPECT_STDERR: test_warning_unknown_attribute_default.c:7:.*@json_skp
// CCCC_EXPECT_STDERR: unknown attribute 'json_skp' ignored \[-Wunknown-attributes\]
// A misspelled @name warns by default and the diagnostic shows the source line.
struct P {
    int x;
    int y;
    int z @json_skp;
};

int main(void) {
    return 42;
}
