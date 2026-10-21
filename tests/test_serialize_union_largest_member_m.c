// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: \.l =
// -m output for a union global names the largest member.
union U {
    char c;
    long l;
};
union U u = {.c = 3};
int main(void) {
    return (u.l & 0xff) == 3 ? 42 : 1;
}
