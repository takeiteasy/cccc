// Ticket #302: comptime functions handle comma increments and zero initializers.

[[cccc::comptime]]
void comma_increment_repro(void) {
    char src_buf[2] = "a";
    char dst_buf[2];
    char *dst = dst_buf;

    for (char *src = src_buf; *src; src++, dst++)
        *dst = *src;
    *dst = 0;

    if (dst_buf[0] != 'a' || dst_buf[1] != 0)
        MacroErrorAt(0, "comma increment repro failed");
}
comma_increment_repro();

[[cccc::comptime]]
void zero_initializer_repro(void) {
    unsigned char seen[256] = {0};
    if (seen[0] || seen[255])
        MacroErrorAt(0, "zero initializer repro failed");
}
zero_initializer_repro();

int main(void) {
    return 42;
}
