// EXPECT_COMPILE_ERROR
// The specifier form covers the parameters too, including after a storage
// class (`extern _Checked int f(int *p)`).

extern _Checked int f(int *p) {
    return p != 0;
}

int main(void) {
    return 42;
}
