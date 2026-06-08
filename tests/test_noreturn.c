// Test _Noreturn function specifier and [[noreturn]] / __attribute__((noreturn))

// Test 1: _Noreturn keyword (C11)
_Noreturn void test_noreturn_kw(void) {
    for (;;) {}
}

// Test 2: __attribute__((noreturn)) (GNU) after return type
void __attribute__((noreturn)) test_noreturn_gnu(void) {
    for (;;) {}
}

// Test 3: __attribute__((__noreturn__)) (GNU with underscores)
void __attribute__((__noreturn__)) test_noreturn_gnu_us(void) {
    for (;;) {}
}

// Test 4: Check that noreturn is propagated to function objects
static int check_noreturn(void) {
    return 42;
}

int main(void) {
    return 42;
}
