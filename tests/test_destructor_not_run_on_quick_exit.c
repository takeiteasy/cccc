// #680: quick_exit() must NOT run destructors -- same rationale as
// test_destructor_not_run_on_underscore_exit.c (C23 7.24.4.7: quick_exit
// runs at_quick_exit handlers only, never atexit handlers or destructors).
#include <stdlib.h>
#include <unistd.h>

__attribute__((destructor)) void d(void) { _exit(1); }

int main(void) {
    quick_exit(42);
}
