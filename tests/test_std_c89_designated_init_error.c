/* JCC_FLAGS: -std=c89 -Wpedantic */
/* JCC_EXPECT_STDERR: warning: designated initializers are a C99 extension \[-Wpedantic\] */
int arr[3] = { [1] = 42 };

int main(void) {
    return arr[1];
}
