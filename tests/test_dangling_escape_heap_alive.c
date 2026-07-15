// CCCC_FLAGS: -3
// Ticket #670: the new dereference-time dangling check (in CHKP3) must not
// flag a &local that has "escaped" into heap storage as long as its frame is
// still alive -- only derefs into an actually-dead frame (ptr < vm->sp) are
// dangling. Here &n is stored into a heap box and read back, but n's frame
// (main's) is still live the whole time, so every subsequent deref of it
// must run to completion, not abort.
#include <stdlib.h>

int main(void) {
    int n = 7;
    int **box = malloc(sizeof(int *));
    *box = &n;

    int *p = *box;
    *p = 42;

    int result = n;
    free(box);
    return result == 42 ? 42 : 1;
}
