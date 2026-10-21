// A plain #include <stdio.h> resolved from the embedded header table still
// builds and runs under -c=native.
#include <stdio.h>
int main(void) {
    printf("hi\n");
    return 42;
}
