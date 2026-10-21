// -c=native, union global initializer reconstructs via its largest member
union U {
    char c;
    long l;
};
union U u = {.c = 3};
int main(void) {
    return (u.l & 0xff) == 3 ? 42 : 1;
}
