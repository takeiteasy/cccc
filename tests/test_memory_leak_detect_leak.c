// CCCC_FLAGS: --memory-leak-detection -V
// CCCC_EXPECT_STDERR: MEMORY LEAK DETECTED
// Leak detection test — malloc without free triggers leak report on shutdown

void *malloc(long size);

int main() {
    int *p = (int *)malloc(sizeof(int) * 8);
    p[0] = 42;
    // intentionally not calling free(p)
    return 42;
}
