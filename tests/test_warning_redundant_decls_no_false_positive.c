// CCCC_FLAGS: -Wredundant-decls
// CCCC_REJECT_STDERR: warning:

// A declaration followed by a definition must NOT warn.
void baz(void);
void baz(void) {}

extern int qux;
int        qux = 99;

int main(void) {
    baz();
    return qux - 57;
}
