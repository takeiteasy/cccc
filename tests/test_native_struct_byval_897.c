// -c=native, struct-by-value parameter serializes with the real tag
struct Point {
    int x;
    int y;
};
int helper(struct Point q) {
    return q.x + q.y;
}
int main(void) {
    struct Point z = {40, 2};
    return helper(z);
}
