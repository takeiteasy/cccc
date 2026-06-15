// MSVC-style #pragma comment(lib, "name") is an alternate form of
// #pragma cccc link("name") (#357).

#pragma comment(lib, "m")

extern double sqrt(double x);

int main(void) {
    double r = sqrt(16.0);
    if ((int)r != 4)
        return 1;
    return 42;
}
