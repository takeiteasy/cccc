// MB_CUR_MAX agrees between the VM and -c=native.
#include <limits.h>
#include <locale.h>
#include <stdlib.h>

int main(void) {
    setlocale(LC_ALL, "C");
    if (MB_CUR_MAX != 1)
        return 1;
    if (MB_CUR_MAX < 1 || MB_CUR_MAX > MB_LEN_MAX)
        return 2;
    return 42;
}
