// A plain #include of a file using @comptime routing builds and runs.
#include "fixtures/comptime_routed_lib.c"
int main(void) {
    return helper();
}
