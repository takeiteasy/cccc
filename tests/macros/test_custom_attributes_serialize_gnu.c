// reflection.h's @serialize handler also fires through the GNU
// __attribute__ spelling, with no comptime code of the program's own.

__attribute__((serialize)) struct Vec2 {
    int x;
    int y;
};

int main(void) {
    struct Vec2 v = {7, 8};
    char        buf[sizeof(struct Vec2)];

    int         n = Vec2_serialize(&v, buf);
    if (n != (int)sizeof(struct Vec2))
        return 1;
    return 42;
}
