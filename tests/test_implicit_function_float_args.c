// CCCC_FLAGS: --std=c89
int main(void) { return g(2.5, 40); }

int g(double d, int i) { return (int)d + i; }
