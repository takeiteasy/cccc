// EXPECT_RUNTIME_ERROR
// CCCC_FLAGS: --heap-canaries -V
// CCCC_EXPECT_STDERR: HEAP CANARY CORRUPTED
// Heap overflow: write one byte past the allocation end corrupts rear canary

void *malloc(long size);
void free(void *ptr);

int main() {
    char *buf = (char *)malloc(8);
    buf[0] = 'A';
    // Write past the end of the 8-byte allocation, corrupting the rear canary
    buf[8] = 'X';
    buf[9] = 'X';
    buf[10] = 'X';
    buf[11] = 'X';
    buf[12] = 'X';
    buf[13] = 'X';
    buf[14] = 'X';
    buf[15] = 'X';
    free(buf); // rear canary check fires here
    return 42;
}
