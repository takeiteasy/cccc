// EXPECT_COMPILE_ERROR
// Unknown keys in #pragma cccc config(...) are hard errors (#357).
#pragma cccc config(frobnicate = 1)

int main(void) { return 42; }
