// Invalid UTF-8 bytes inside comments are not an error (SDK headers carry
// them).

int main(void) {
    int x = 40; /* µ stray byte */
    // line comment with ÿþ bytes
    return x + 2;
}
