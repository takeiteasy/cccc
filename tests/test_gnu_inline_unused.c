// Expected return: 42
// An `extern inline` + gnu_inline definition is inline-only: a body nothing
// calls is never emitted, so its undefined callee must not abort the link.
extern int undefined_everywhere(int);

extern inline __attribute__((gnu_inline)) int never_called(int x) {
    return undefined_everywhere(x);
}

extern __inline __attribute__((__gnu_inline__))
__attribute__((__always_inline__)) int
never_called_either(int x) {
    return undefined_everywhere(x) + never_called(x);
}

int main(void) {
#ifndef __GNUC_STDC_INLINE__
    return 1;
#endif
    return 42;
}
