// Every spelling of constructor/destructor runs the function.
static int ran;
[[gnu::constructor]] static void c1(void) {
    ran |= 1;
}
[[constructor]] static void c2(void) {
    ran |= 2;
}
[[cccc::constructor]] static void c3(void) {
    ran |= 4;
}
__attribute__((__constructor__)) static void c4(void) {
    ran |= 8;
}
@constructor static void c5(void) {
    ran |= 16;
}

int main(void) {
    return ran == 31 ? 42 : 1;
}
