#pragma once

@ template(T) T twice(T v) {
    return v * 2;
}

@ template(T, int N) struct Arr {
    T v[N];
};

@ template(T) struct Tag {
    int kind;
};

@ template(T) @specialize(T *) struct Tag {
    int kind;
    int ptr;
};

@specialize(char) struct Tag {
    int  kind;
    char c;
};
