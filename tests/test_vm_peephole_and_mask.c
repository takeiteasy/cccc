// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: : ANDI3 
// CCCC_REJECT_STDOUT: SX4
// An AND with a non-negative mask already leaves a value that fits an int.
long low31(long v) {
    return (int)(v & 0x7FFFFFFF);
}

int main(void) {
    return low31(0x100000005L) == 5 ? 42 : 1;
}
