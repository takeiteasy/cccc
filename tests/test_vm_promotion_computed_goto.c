// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: JMPI
// CCCC_EXPECT_STDOUT: STR_LOCAL_D 18 +4294
// A computed-goto dispatcher keeps its locals in registers across the jumps.
int main(void) {
    static void               *labels[] = {&&inc, &&dbl, &&done};
    static const unsigned char code[]   = {0, 1, 0, 1, 2};
    const unsigned char       *pc       = code;
    long                       acc      = 1;
    goto                      *labels[*pc++];
inc:
    acc += 3;
    goto *labels[*pc++];
dbl:
    acc *= 2;
    goto *labels[*pc++];
done:
    return acc == 22 ? 42 : 1;
}
