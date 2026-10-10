// Expected return: 42
// sizeof/typeof/pointer arithmetic/copy of an initialised flexible-array
// struct use the declared type; the initializer storage stays intact.

struct F {
    int       n;
    long long x;
    int       tail[];
};

struct F gf = {2, 5, {10, 20, 30}};

int main(void) {
    if (sizeof(gf) != sizeof(struct F))
        return 1;
    if (sizeof(typeof(gf)) != sizeof(struct F))
        return 2;
    if ((char *)(&gf + 1) - (char *)&gf != (long)sizeof(struct F))
        return 3;
    if (gf.tail[0] != 10 || gf.tail[1] != 20 || gf.tail[2] != 30)
        return 4;

    static struct F sf = {1, 7, {4, 5}};
    if (sizeof(sf) != sizeof(struct F) || sf.tail[1] != 5)
        return 5;

    struct F c = gf;
    if (c.n != 2 || c.x != 5)
        return 6;
    return 42;
}
