// CCCC_FLAGS: --std=c11
// Test that __thread is accepted without warning (TLS is now supported)
__thread int x = 0;
int main(void) { x = 42; return x; }
