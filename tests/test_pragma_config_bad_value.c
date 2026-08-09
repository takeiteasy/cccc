// EXPECT_COMPILE_ERROR
// Out-of-range values for #pragma cccc config(...) are hard errors (#357).
// safety/optimisation accept 0..3 only.
#pragma cccc config(safety = 9)

int main(void) { return 42; }
