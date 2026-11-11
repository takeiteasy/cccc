// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: 'Vec' is a template; instantiate it as 'Vec@\(args\)' or '@inst\(args\) Vec'
@template(T) struct Vec {
    T v;
};

Vec g;

int main(void) {
    return 0;
}
