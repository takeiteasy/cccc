// CCCC_FLAGS: -Wold-style-definition
// CCCC_EXPECT_STDERR: old-style \(K&R\) function
// definition.*\[-Wold-style-definition\]

int add(a, b)
int a;
int b;
{
    return a + b;
}

int main(void) {
    (void)add(1, 2);
    return 42;
}
