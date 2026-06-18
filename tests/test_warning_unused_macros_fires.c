// CCCC_FLAGS: -Wunused-macros
// CCCC_EXPECT_STDERR: macro 'UNUSED_FOO' defined but not used.*\[-Wunused-macros\]

#define USED_BAR 42
#define UNUSED_FOO 99

int main(void) {
    return USED_BAR;
}
