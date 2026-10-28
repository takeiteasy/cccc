// __builtin_bzero needs no header and zeroes like bzero.
int main(void) {
    char a[8] = "abcdefg";
    __builtin_bzero(a, sizeof a);
    for (int i = 0; i < 8; i++)
        if (a[i])
            return 1;
    return __has_builtin(__builtin_bzero) ? 42 : 2;
}
