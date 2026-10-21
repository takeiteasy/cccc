// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: extern int g;
// -m output for an extern global keeps extern, never a bare tentative
// definition.
extern int g;
int        g = 5;
int main(void) {
    return g == 5 ? 42 : 1;
}
