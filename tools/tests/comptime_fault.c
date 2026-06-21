[[cccc::comptime(inline)]]
Node *crash(void) {
  volatile int *p = (volatile int *)0;
  int value = *p;
  return MakeIntLiteral(value);
}
int main(void) { return crash(); }
