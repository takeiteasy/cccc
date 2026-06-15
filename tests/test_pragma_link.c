// #pragma cccc link("name") queues a library for FFI resolution, the same
// as -l/--library (#357).

#pragma cccc link("m")

extern double sqrt(double x);

int main(void) {
    double r = sqrt(16.0);
    if ((int)r != 4)
        return 1;
    return 42;
}
