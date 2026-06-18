// CCCC_FLAGS: -Wredundant-decls -Wno-redundant-decls
// CCCC_REJECT_STDERR: warning:

extern int foo;
extern int foo;

int foo = 42;

int main(void) {
    return foo;
}
