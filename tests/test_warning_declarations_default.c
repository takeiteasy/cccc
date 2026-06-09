// CCCC_REJECT_STDERR: warning:
global_value;

int zero(void) {
    return;
}

void set_value(int *p) {
    return (*p = 42);
}

int fallthrough(void) {
}

int main(void) {
    int value = 0;
    set_value(&value);
    return value + zero() + fallthrough() + implicit_later();
}

int implicit_later(void) {
    return 0;
}
