// `%` is a constant expression: array dimensions, including the macOS SDK's
// howmany() spelling for fd_set.
#define HOWMANY(x, y) ((((x) % (y)) == 0) ? ((x) / (y)) : (((x) / (y)) + 1))

int  file_scope[10 % 4];
struct Bits {
    int b[HOWMANY(1024, sizeof(int) * 8)];
};
enum { Rem = 17 % 5 };
_Static_assert(sizeof(struct Bits) == 128, "howmany");
_Static_assert(sizeof(file_scope) == 2 * sizeof(int), "mod dim");

int main(void) {
    int local[100 % 7];
    return sizeof(local) == 2 * sizeof(int) && Rem == 2 ? 42 : 1;
}
