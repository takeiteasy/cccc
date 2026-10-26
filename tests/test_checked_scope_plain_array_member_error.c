// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: unchecked array struct member
// A plain array member is not allowed in a checked region.

#pragma cccc checked begin
struct S {
    int a[2];
};
#pragma cccc checked end

int main(void) {
    return 42;
}
