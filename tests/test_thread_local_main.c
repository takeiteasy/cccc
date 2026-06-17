// Expected return: 42
// Verify _Thread_local variables work in the main thread.
_Thread_local int g_tls = 0;

int main(void) {
    g_tls = 42;
    return g_tls;
}
