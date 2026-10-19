// A macro defined after code that uses its name, and a keyword macro
// undefined around main, leave the already-expanded native output alone.

static int twice(int count) {
    return count * 2;
}

#define count 21

#define int   long long
static int widened(void) {
    return sizeof(int);
}
#undef int

int main(int argc, char **argv) {
    (void)argv;
    if (widened() != 8 || argc < 1)
        return 1;
    return twice(count);
}

#define int long long
