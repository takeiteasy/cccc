#include "greet.h"
#include <stdio.h>

#ifndef GREET_DEFAULT
#define GREET_DEFAULT "world"
#endif

int main(void) {
    int xs[] = {1, 2, 3, 4};
    printf("%s\n", greet(GREET_DEFAULT));
    printf("sum = %d\n", sum(xs, 4));
    return 0;
}
