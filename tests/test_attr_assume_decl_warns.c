// CCCC_EXPECT_STDERR: 'assume' attribute ignored
[[gnu::assume(1)]] int g(void);
int main(void) {
    return 42;
}
