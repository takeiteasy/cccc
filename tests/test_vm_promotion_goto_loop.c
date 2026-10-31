// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: STR_LOCAL_D 18 +4294
// A loop closed by a backward goto promotes its locals like a for loop does.
long count(long n) {
    long sum = 0;
    long i   = 0;
again:
    sum += i * 3;
    i++;
    if (i < n)
        goto again;
    return sum;
}

int main(void) {
    return count(15) == 315 ? 42 : 1;
}
