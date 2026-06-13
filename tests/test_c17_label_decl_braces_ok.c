// CCCC_FLAGS: --std=c17
int main(void) {
    int v = 1;
    switch (v) {
        case 1: { int x = 5; return x == 5 ? 42 : 1; }
    }
    return 1;
}
