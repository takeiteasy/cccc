// CCCC_FLAGS: -Wextra -Wno-shadow -Wno-sign-compare -Wno-conversion -Wno-pointer-arith -Wno-fallthrough -Wno-strict-prototypes
// CCCC_EXPECT_STDERR: old-style \(K&R\) function definition.*\[-Wold-style-definition\]

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
