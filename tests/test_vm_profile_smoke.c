int main(void) {
    int acc = 0;
    for (int i = 0; i < 6; i++)
        acc += i;
    return acc == 15 ? 42 : 1;
}
