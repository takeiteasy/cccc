#pragma once

@ template(T) int size_of(void) {
    return sizeof(T);
}

struct Shared {
    long a, b;
};
