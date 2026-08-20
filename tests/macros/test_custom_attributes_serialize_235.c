// Ticket #235: @serialize/@deserialize attribute handlers shipped in
// reflection.h. Mirrors test_custom_attributes_335.c.

@serialize @deserialize struct Vec2 {
    int x;
    int y;
};

int main(void) {
    struct Vec2 v = {7, 8};
    char        buf[sizeof(struct Vec2)];

    int         n = Vec2_serialize(&v, buf);
    if (n != (int)sizeof(struct Vec2))
        return 1;

    struct Vec2 w = Vec2_deserialize(buf);
    if (w.x != 7 || w.y != 8)
        return 2;

    return 42;
}
