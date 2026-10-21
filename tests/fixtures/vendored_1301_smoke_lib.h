#ifndef VENDORED_1301_SMOKE_LIB_H
#define VENDORED_1301_SMOKE_LIB_H
#define V1301_SMOKE_DECORATE(name) v1301_smoke_##name
int V1301_SMOKE_DECORATE(add)(int x);
#ifdef VENDORED_1301_SMOKE_IMPLEMENTATION
int V1301_SMOKE_DECORATE(add)(int x) {
    return x + 1;
}
#endif
#endif
