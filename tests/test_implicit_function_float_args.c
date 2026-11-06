// CCCC_FLAGS: --std=c89
// CCCC_NATIVE_SKIP: -c=native rejects implicit function declarations at every standard
int main(void) { return g(2.5, 40); }

int g(double d, int i) { return (int)d + i; }
