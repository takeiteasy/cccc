// JCC_FLAGS: -Wreturn-type
// JCC_EXPECT_STDERR: 3 warnings generated.
int zero(void) {
    return;
}

void set_value(int *p) {
    return (*p = 42);
}

int fallthrough(void) {
}

int main(void) {
    int value = 0;
    set_value(&value);
    return value + zero() + fallthrough();
}
