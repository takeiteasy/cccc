// -c=native, struct used by-pointer in two functions and by-value in a third
struct Point {
    int x;
    int y;
};
int f(struct Point *p) {
    return p->x;
}
int g(struct Point *p) {
    return p->y;
}
int helper(struct Point q) {
    return q.x + q.y;
}
int main(void) {
    struct Point z = {40, 2};
    return (f(&z) + g(&z) + helper(z)) - 42;
}
