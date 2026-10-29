// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: JMPI
// CCCC_REJECT_STDOUT: ZX1
// A function with a computed goto gets the same clean-up as any other.
static unsigned char one = 1;

long pick(const unsigned char *p) {
    static void *targets[] = {&&yes, &&no};
    long         r         = 0;
    goto        *targets[p[0] ? 0 : 1];
yes:
    r = 1;
no:
    return r + 41;
}

int main(void) {
    return pick(&one);
}
