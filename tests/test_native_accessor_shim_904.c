// CCCC_VM_SKIP: errno is not zero at program start in the VM
// -c=native, stdout/errno macro-expanded accessor shims compile and run
#include <stdio.h>
#include <errno.h>
int main(void) {
    (void)stdout;
    return errno == 0 ? 42 : 1;
}
