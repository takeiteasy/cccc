#include <sys/mman.h>
#include <unistd.h>
#include <string.h>

int main(void) {
    size_t pagesize = 4096;
    void *p = mmap(0, pagesize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == (void *)-1) return 1;

    strcpy((char *)p, "hello");
    if (strcmp((char *)p, "hello") != 0) return 2;

    if (mprotect(p, pagesize, PROT_READ) != 0) return 3;

    if (munmap(p, pagesize) != 0) return 4;
    return 42;
}
