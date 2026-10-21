// A struct passed by value is copied under -c=native.
struct s1078 {
    long a;
    long b;
    char pad[24];
};
static void mutate(struct s1078 b) {
    b.a      = 99;
    b.pad[0] = 1;
}
int main(void) {
    struct s1078 s;
    s.a      = 1;
    s.b      = 2;
    s.pad[0] = 0;
    mutate(s);
    return (s.a == 1 && s.pad[0] == 0) ? 42 : 7;
}
