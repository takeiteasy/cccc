// A user-written prototype of free, without <stdlib.h>, then its address in a
// static initialiser.
void free(void *);
static void (*p)(void *) = free;

int main(void) {
    return p ? 42 : 1;
}
