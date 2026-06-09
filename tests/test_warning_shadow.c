// CCCC_FLAGS: -Wshadow
// CCCC_EXPECT_STDERR: 3 warnings generated.

int value;

int check(int parameter) {
    int value = 1;
    int local = 2;
    {
        int parameter = 3;
        int local = 4;
        return value + parameter + local + 34;
    }
}

int main(void) {
    return check(0);
}
