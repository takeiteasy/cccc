// CCCC_FLAGS: --std=c11
// Test that _Thread_local is accepted without warning (TLS is now supported)
_Thread_local int x = 0;
int main(void) { x = 42; return x; }
