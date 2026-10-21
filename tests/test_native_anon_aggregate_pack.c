// -c=native: a tagless struct under #pragma pack used by value twice
#pragma pack(push, 1)
static struct {
    char c;
    int  i;
} a, b;
#pragma pack(pop)
int main(void) {
    a.c = 1;
    a.i = 40;
    b   = a;
    return b.c + b.i + (int)sizeof(a) - 4;
}
