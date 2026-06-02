int dlopen(int x) {
    return x + 40;
}

int main(void) {
    return dlopen(2);
}
