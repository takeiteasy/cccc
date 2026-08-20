// #680: _Exit() must NOT run destructors -- matching GCC/ISO C (C23
// 7.24.4.4: _Exit terminates without running atexit handlers or destructors,
// only exit() and normal return from main() do). If the destructor below
// ran, it would _exit(1); reaching the process's own _exit(42) instead
// proves it did not.
#include <stdlib.h>
#include <unistd.h>

__attribute__((destructor)) void d(void) {
    _exit(1);
}

int main(void) {
    _Exit(42);
}
