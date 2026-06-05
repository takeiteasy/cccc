#include <glob.h>

int main(void) {
    glob_t g;
    return sizeof(g) > 0 ? 42 : 1;
}
