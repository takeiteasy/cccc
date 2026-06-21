#include <signal.h>
int main(void) { raise(SIGBUS); return 42; }
