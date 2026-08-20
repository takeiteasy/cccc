// EXPECT_COMPILE_ERROR
// Assigning to a const-qualified array parameter pointer must fail.
void f(int a[const 5]) {
    int x[5];
    a = x; // error: cannot assign to const-qualified pointer
}
int main(void) {
    return 0;
}
