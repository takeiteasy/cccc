#include <signal.h>
static volatile int handled;
static void handler(int sig) { handled = sig == SIGSEGV; }
int main(void) {
  signal(SIGSEGV, handler);
  volatile int *p = (volatile int *)0;
  volatile int value = *p;
  (void)value;
  return handled ? 42 : 1;
}
