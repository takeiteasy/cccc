// -c=native, bit-manipulation builtins keep their width (popcount/parity encode
// width 0, so the ll variant comes from the argument type)
int main(void) {
    int       a = __builtin_popcount(7);
    int       b = __builtin_clz(1u);
    int       c = __builtin_ctz(8u);
    int       d = __builtin_ffs(4);
    unsigned  e = __builtin_bswap32(0x01000000);
    long long w = 0xFFFFFFFFFFLL;
    int       g = __builtin_popcountll(w);
    return a + b + c + d + (int)e + g - 39;
}
