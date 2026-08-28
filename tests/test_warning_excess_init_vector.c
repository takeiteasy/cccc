// CCCC_FLAGS: -Wexcess-init
// CCCC_EXPECT_STDERR: excess elements in vector initializer.*\[-Wexcess-init\]

typedef int v4si __attribute__((vector_size(16)));

int main(void) {
    v4si a = {1, 2, 3, 4, 5};
    return a[0] + 41;
}
