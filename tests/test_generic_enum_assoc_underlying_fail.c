// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: 'enum' underlying type may not be specified here
//
// #1223: inside a _Generic association the `:` is the association colon.
// A C23 `enum E : T` underlying type may not be respelled there -- gcc and
// clang both reject it with this exact wording. `_Generic(x, enum E: ...)`
// (no `: T`) is fine and is covered by the positive suites.
enum E : int {
    E1 = 1,
};

int main(void) {
    enum E e = E1;
    return _Generic(e, enum E: int: 0, default: 1);
}
