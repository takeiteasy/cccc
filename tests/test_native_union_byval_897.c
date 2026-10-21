// -c=native, union-by-value parameter serializes with the real tag
union U {
    int   x;
    float f;
};
int helper(union U q) {
    return q.x;
}
int main(void) {
    union U z;
    z.x = 42;
    return helper(z);
}
