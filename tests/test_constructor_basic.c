// Tests that __attribute__((constructor)) functions run before main().
// A bare constructor (no priority) sets a global; main() checks it was
// already set by the time it starts.

int ready = 0;

__attribute__((constructor)) void init(void) {
    ready = 1;
}

int main(void) {
    if (ready != 1)
        return 1;
    return 42;
}
