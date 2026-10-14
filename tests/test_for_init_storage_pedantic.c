// CCCC_FLAGS: --std=c17 -Wpedantic
// CCCC_EXPECT_STDERR: declaration of static variable in 'for' loop initial declaration
// CCCC_EXPECT_STDERR: declaration of non-variable in 'for' loop initial declaration
// CCCC_EXPECT_STDERR: declaration of 'extern' variable in 'for' loop initial declaration
//
// Before C23 only auto/register objects may be declared in a for-init; other
// storage classes still compile, with a pedantic warning.
int g;
int main(void) {
    int r = 0;
    for (static int s = 0; s < 1; s++)
        r++;
    for (typedef int T; 0;) {
    }
    for (extern int g; g < 1; g++)
        r++;
    return r == 2 ? 42 : 1;
}
