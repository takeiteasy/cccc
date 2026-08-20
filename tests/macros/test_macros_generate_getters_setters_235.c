// Ticket #235: @generate_getters/@generate_setters custom attributes.

@generate_getters @generate_setters struct Point {
    int x;
    int y;
};

int main(void) {
    struct Point p = {1, 2};

    if (get_x(&p) != 1)
        return 1;
    if (get_y(&p) != 2)
        return 2;

    set_x(&p, 10);
    set_y(&p, 20);
    if (get_x(&p) != 10)
        return 3;
    if (get_y(&p) != 20)
        return 4;
    if (p.x != 10)
        return 5;
    if (p.y != 20)
        return 6;

    return 42;
}
