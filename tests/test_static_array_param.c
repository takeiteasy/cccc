// Verify C99 array-parameter qualifiers parse without error.
void f1(int a[static 10]) {}
void f2(int a[const static 10]) {}
void f3(int a[restrict 5]) {}
void f4(int a[volatile 3]) {}
void f5(int a[static restrict 4]) {}

int main(void) { return 42; }
