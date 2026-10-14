// CCCC_FLAGS: --std=gnu89
// Expected return: 42
// GNU89 selects the GNU inline model.
int main(void) {
#if defined(__GNUC_GNU_INLINE__) && !defined(__GNUC_STDC_INLINE__)
    return 42;
#else
    return 1;
#endif
}
