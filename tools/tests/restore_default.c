#include <signal.h>
static void handler(int sig) { (void)sig; }
int main(void) {
  signal(SIGSEGV, handler);
  signal(SIGSEGV, SIG_DFL);
  volatile int *p = (volatile int *)0;
  return *p;
}
