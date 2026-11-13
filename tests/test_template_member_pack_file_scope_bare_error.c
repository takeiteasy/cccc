// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: member pack 'v' must be expanded with '\.\.\.'

@template(Ts...) struct Tuple {
    Ts... v;
};
Tuple @(char, int) g;
int n = g.v;

int main(void) {
    return 42;
}
