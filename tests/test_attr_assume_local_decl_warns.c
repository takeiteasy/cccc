// CCCC_EXPECT_STDERR: 'assume' attribute ignored
int main(void) {
    int x = 1;
    [[gnu::assume(x)]] int y = 41;
    return y + x;
}
