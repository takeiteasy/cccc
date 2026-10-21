#ifndef VENDORED_1304_SMOKE_LIB2_H
#define VENDORED_1304_SMOKE_LIB2_H
#define V1304_SMOKE2_DECORATE(name) v1304_smoke2_##name
int V1304_SMOKE2_DECORATE(add)(int x);
#ifdef VENDORED_1304_SMOKE2_IMPLEMENTATION
int V1304_SMOKE2_DECORATE(add)(int x) {
    return x + 1;
}
#endif
#endif
