// CCCC_FLAGS: -Wall
// CCCC_EXPECT_STDERR: 3 warnings generated
// CCCC_EXPECT_STDERR: do not call
// An attribute takes effect in every spelling: the GNU names in [[gnu::...]]
// and the standard names in __attribute__((...)).

[[gnu::warn_unused_result]] int w(void);
int w(void) {
    return 1;
}

__attribute__((nodiscard)) int nd(void);
int nd(void) {
    return 1;
}

[[gnu::warning("do not call")]] void old(void);
void old(void) {}

int main(void) {
    w();
    nd();
    old();
    return 42;
}
