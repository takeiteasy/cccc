// -c=native, asm(...) is emitted verbatim and handed to the host compiler
int main(void) {
    asm("nop");
    return 42;
}
