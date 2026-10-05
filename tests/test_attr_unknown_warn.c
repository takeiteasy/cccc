// Tests that unknown attributes emit CCCC_WARN_ATTRIBUTES warnings, not errors,
// in every attribute position, while recognized ones stay silent.
// CCCC_FLAGS: -Wattributes
// CCCC_EXPECT_STDERR: 5 warnings generated

__attribute__((bogus_a)) int g = 0;
__attribute__((cold, bogus_b)) void cold_fn(void) {}
void hot_fn(void) __attribute__((hot, bogus_c));
void hot_fn(void) {}
[[gnu::bogus_d]] void gnu_fn(void) {}
[[bogus_e]] int std_g;

int main(void) {
    cold_fn();
    hot_fn();
    gnu_fn();
    return 42 + g + std_g;
}
