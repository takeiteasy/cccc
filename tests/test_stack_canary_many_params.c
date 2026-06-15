// CCCC_FLAGS: --stack-canaries
// #445 — more than 8 params exercises the stack-passed-arg branch in ENT3
// (bp[2 + (i-8)]); the register-spilled params must still land canary-shifted.

int sum10(int a, int b, int c, int d, int e,
          int f, int g, int h, int i, int j) {
    return a + b + c + d + e + f + g + h + i + j;
}

int main(void) {
    // 1+2+...+10 = 55
    if (sum10(1, 2, 3, 4, 5, 6, 7, 8, 9, 10) != 55)
        return 1;
    return 42;
}
