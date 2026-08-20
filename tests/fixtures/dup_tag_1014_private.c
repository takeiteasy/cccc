// #1014: a private, differently-shaped completion of the same tag name --
// deliberately does NOT include dup_tag_1014.h, so nothing here is
// header-exposed. This group must be renamed regardless of input-file
// order, since only the header-exposed group (dup_tag_1014_impl.c) can
// keep the plain `struct DyGC1014` spelling.
struct DyGC1014 {
    double d;
    char   pad;
};

int priv_use_1014(void) {
    struct DyGC1014 x;
    x.d   = 1.0;
    x.pad = 'a';
    return (int)x.d;
}
