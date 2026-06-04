// JCC_FLAGS: -Wreturn-type
// JCC_EXPECT_STDERR: 1 warning generated.
// JCC_REJECT_STDERR: 2 warnings generated.
int complete(int value) {
    if (value)
        return 20;
    else
        return 22;
}

int incomplete(int value) {
    if (value)
        return 1;
}

int main(void) {
    return complete(0) + incomplete(0) + 20;
}
