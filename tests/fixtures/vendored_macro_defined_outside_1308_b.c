// TU B for tests/test_serialize_vendored_macro_defined_outside_1308.c
// (#1308). Forward-declares and calls the test file's own function --
// present only so this is a genuine multi-TU program (matching this
// ticket's own self-hosting-spike shape, a multi-file build), not because
// TU B itself is load-bearing for the bug.
int vendored_macro_outside_1308_use(void);

int main(void) {
    return vendored_macro_outside_1308_use();
}
