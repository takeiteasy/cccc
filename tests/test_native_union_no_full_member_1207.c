// -c=native, a union whose largest member doesn't span alignment padding still
// round-trips
union U {
    char c[5];
    int  x;
};
union U u = {.c = {1, 2, 3, 4, 5}};
int main(void) {
    return u.c[0] + u.c[1] + u.c[2] + u.c[3] + u.c[4] == 15 ? 42 : 1;
}
