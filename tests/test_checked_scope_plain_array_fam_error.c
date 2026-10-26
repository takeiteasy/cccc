// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: unchecked array struct member
// A plain flexible array member is not allowed in a checked region.

#pragma cccc checked begin
struct S {
    int n;
    int d[];
};
#pragma cccc checked end

int main(void) {
    return 42;
}
