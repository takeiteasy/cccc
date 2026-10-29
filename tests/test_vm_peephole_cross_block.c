// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: SX2
// CCCC_EXPECT_STDOUT: SX1
// CCCC_REJECT_STDOUT: SX4
// Both arms of the conditional leave a value that already fits an int, so the
// cast after the join needs no sign extension.
long pick(long v, long k) {
    return (int)(k ? (long)(short)v : (long)(signed char)v);
}

int main(void) {
    return pick(300, 1) == 300 && pick(300, 0) == 44 ? 42 : 1;
}
