// Verify const/volatile/restrict qualifiers in array params apply to the decayed pointer.
// CCCC_EXPECT_STDOUT: pass

#include <stdio.h>

void f_const(int a[const 5]) {
    (void)a[0]; // a is int *const — readable, pointer itself is const
}

void f_volatile(int a[volatile 5]) {
    (void)a[0]; // a is int *volatile
}

void f_restrict(int a[restrict 5]) {
    (void)a[0]; // a is int *restrict
}

void f_const_static(int a[const static 5]) {
    (void)a[0]; // both const pointer and static minimum-size
}

void f_const_restrict(int a[const restrict 5]) {
    (void)a[0]; // int *const restrict
}

void f_empty_const(int a[const]) {
    (void)a[0]; // unsized const array param → int *const
}

int main(void) {
    int arr[5] = {1, 2, 3, 4, 5};
    f_const(arr);
    f_volatile(arr);
    f_restrict(arr);
    f_const_static(arr);
    f_const_restrict(arr);
    f_empty_const(arr);
    printf("pass\n");
    return 42;
}
