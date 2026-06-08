// JCC_FLAGS: --std=c23
// Test [[fallthrough]] attribute
// Expected return: 42

int test_fallthrough(int x) {
    int result = 0;
    switch (x) {
        case 1:
            result = 10;
            [[fallthrough]];
        case 2:
            result = result + 20;
            break;
        default:
            result = 99;
    }
    return result;
}

int main(void) {
    if (test_fallthrough(1) != 30) return 1;
    if (test_fallthrough(2) != 20) return 2;
    if (test_fallthrough(5) != 99) return 3;
    return 42;
}