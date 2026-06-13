// CCCC_FLAGS: --std=c17
// In C17, int main() has no prototype, so recursive calls with arguments are legal.
static int depth = 0;

int main() {
    if (depth++ == 0)
        main(1, "arg");
    return depth == 2 ? 42 : 1;
}
