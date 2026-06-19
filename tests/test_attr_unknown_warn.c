// Tests that unknown attributes emit CCCC_WARN_ATTRIBUTES warnings, not errors.
// CCCC_FLAGS: -Wattributes
// CCCC_EXPECT_STDERR: 5 warnings generated

__attribute__((used)) int g = 0;
__attribute__((cold)) void cold_fn(void) {}
__attribute__((hot))  void hot_fn(void)  {}
__attribute__((visibility("default"))) int vis_fn(void) { return 1; }
[[gnu::cold]] void gnu_cold_fn(void) {}

int main(void) {
    cold_fn();
    hot_fn();
    gnu_cold_fn();
    if (vis_fn() != 1) return 1;
    return 42;
}
