#ifndef VENDORED_1304_SMOKE_LIB_H
#define VENDORED_1304_SMOKE_LIB_H
#define V1304_SMOKE_DECORATE(name) v1304_smoke_##name
int V1304_SMOKE_DECORATE(add)(int x);
#ifdef VENDORED_1304_SMOKE_IMPLEMENTATION
int V1304_SMOKE_DECORATE(add)(int x) {
    return x + 1;
}
#endif
#endif
