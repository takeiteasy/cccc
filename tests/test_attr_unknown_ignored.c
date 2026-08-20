// Tests that unknown GNU __attribute__ and C23 [[...]] attributes are
// silently ignored (no error, no warning without -Wattributes).
// CCCC_FLAGS: -Wno-attributes

__attribute__((used)) int global_var = 5;

__attribute__((cold)) void cold_func(void) {}
__attribute__((hot)) void hot_func(void) {}

__attribute__((visibility("default"))) int visible_func(void) {
    return 1;
}

[[gnu::cold]] void gnu_scoped_cold(void) {}

int main(void) {
    cold_func();
    hot_func();
    gnu_scoped_cold();
    if (global_var != 5)
        return 1;
    if (visible_func() != 1)
        return 1;
    return 42;
}
