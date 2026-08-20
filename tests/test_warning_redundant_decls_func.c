// CCCC_FLAGS: -Wredundant-decls
// CCCC_EXPECT_STDERR: redundant redeclaration of 'bar'.*\[-Wredundant-decls\]

void bar(void);
void bar(void);

void bar(void) {}

int main(void) {
    bar();
    return 42;
}
