int main(void) {
  volatile int *p = (volatile int *)0;
  return *p;
}
