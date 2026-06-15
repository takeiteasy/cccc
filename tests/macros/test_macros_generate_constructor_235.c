// Ticket #235: @generate_constructor custom attribute.

@generate_constructor
struct Point {
    int x;
    int y;
};

int main(void) {
    struct Point p = Point_create(3, 4);
    if (p.x != 3) return 1;
    if (p.y != 4) return 2;
    return 42;
}
