#include "dandy_pattern_999.h"
static int dp_open(void);
static int dp_close(void);
static const DPVT kVT = {.open = dp_open, .close = dp_close};
static int dp_open(void) {
    return (int)dp_box(40);
}
static int dp_close(void) {
    return (int)dp_box(2);
}
int dp_call_a(void) {
    return kVT.open() + kVT.close();
}
