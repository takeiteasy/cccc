// CCCC_FLAGS: -Wnull-dereference -Wrestrict -Warray-bounds -Wstringop-overflow
// -Wstringop-truncation CCCC_REJECT_STDERR: unknown warning option

int main(void) {
    return 42;
}
