// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: STR_LOCAL_W
// CCCC_REJECT_STDOUT: LEA3
// CCCC_REJECT_STDOUT: STR_LOCAL_D
// ++, -- and op= on a plain variable store to it directly: the address of the
// variable is never formed and no pointer temporary takes a frame slot.
int bump(int n) {
    int c = n;
    c++;
    c += 2;
    c -= 1;
    --c;
    return c;
}

int main(void) {
    return bump(40) == 41 ? 42 : 1;
}
