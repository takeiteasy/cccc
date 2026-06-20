#include "greet.h"
#include <stdio.h>

const char *greet(const char *name) {
    static char buf[128];
    snprintf(buf, sizeof(buf), "Hello, %s!", name);
    return buf;
}
