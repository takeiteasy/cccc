// CCCC_FLAGS: -Wredundant-decls
// CCCC_EXPECT_STDERR: redundant redeclaration of 'foo'.*\[-Wredundant-decls\]

extern int foo;
extern int foo;

int foo = 42;

int main(void) {
    return foo;
}
