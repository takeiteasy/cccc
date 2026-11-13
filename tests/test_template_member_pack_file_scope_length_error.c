// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: the packs in one pattern have different lengths \(1 and 2\)

@template(Ts...) struct Tuple {
    Ts... v;
};
Tuple @(char) one;
Tuple @(char, int) two;
int sums[] = {one.v + two.v...};

int main(void) {
    return 42;
}
