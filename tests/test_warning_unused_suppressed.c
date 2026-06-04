// JCC_FLAGS: -Wunused
// JCC_REJECT_STDERR: warning:

static int c23_global [[maybe_unused]];

static int __attribute__((unused)) gnu_function(void) {
    return 1;
}

int check(int [[maybe_unused]] parameter) {
    int __attribute__((unused)) local = 1;
    [[maybe_unused]] unused_label:
    return 42;
}

int main(void) {
    return check(0);
}
