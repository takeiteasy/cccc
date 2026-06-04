// JCC_FLAGS: -Wimplicit-int
// JCC_EXPECT_STDERR: 4 warnings generated.
global_value;

identity(value) {
    static local_value;
    local_value = value;
    return local_value;
}

int main(void) {
    global_value = identity(42);
    return global_value;
}
