// A GNU attribute may follow a pointer's qualifiers, as the macOS SDK's
// fortified <string.h> writes `void *const __attribute__((...)) dst`.

static int first(char *const __attribute__((unused)) p, char *volatile q) {
    return *p;
}

int main(void) {
    char c = 42;
    return first(&c, &c);
}
