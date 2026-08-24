// CCCC_FLAGS: --std=c89
// CCCC_REJECT_STDERR: warning:
// #1144: implicit function declaration is a hard error at C99+; this
// test's forward call to implicit_later() (below) needs --std=c89 to stay
// a (default-off, so silent here) warning rather than fail the compile
// outright -- the point of this test is "no warnings under default
// flags", not implicit declaration itself.
global_value;

int zero(void) {
    return;
}

void set_value(int *p) {
    return (*p = 42);
}

int fallthrough(void) {}

int main(void) {
    int value = 0;
    set_value(&value);
    return value + zero() + fallthrough() + implicit_later();
}

int implicit_later(void) {
    return 0;
}
