// Every spelling of constructor/destructor runs the function.
static int ran;
[[gnu::constructor]] static void c1(void) {
    ran |= 1;
}
[[__gnu__::__constructor__]] static void c2(void) {
    ran |= 2;
}
__attribute__((__constructor__)) static void c3(void) {
    ran |= 4;
}
@constructor static void c4(void) {
    ran |= 8;
}

int main(void) {
    return ran == 15 ? 42 : 1;
}
