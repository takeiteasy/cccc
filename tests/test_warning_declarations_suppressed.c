// CCCC_FLAGS: -Wall -Wno-implicit-int -Wno-implicit-function-declaration -Wno-return-type
// CCCC_REJECT_STDERR: warning:
global_value;

int zero(void) {
    return;
}

int fallthrough(void) {
}

int main(void) {
    return global_value + zero() + fallthrough() + implicit_later();
}

int implicit_later(void) {
    return 42;
}
