// CCCC_FLAGS: --std=c23
int main(void) {
    int value = 1;
    int result = 0;
    switch (value) {
        case 1:
            int x = 5;
            result = x;
            break;
        case 2:
            result = x * 2;
            break;
        default:
            int y = 99;
            result = y;
            break;
    }
    goto done;
done:
    int z = 42;
    result += z;
    return result == 47 ? 42 : 1;
}
