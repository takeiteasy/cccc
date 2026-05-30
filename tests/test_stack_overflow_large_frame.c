// EXPECT_RUNTIME_ERROR - Test stack overflow detection with large stack frame

void large_frame() {
    long long arr[500000];
    arr[0] = 42;
}

int main() {
    large_frame();
    return 42;
}
