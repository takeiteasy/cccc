// JCC_FLAGS: --std=c99
int arr[3] = { [1] = 42 };
struct { int x; int y; } p = { .y = 10, .x = 5 };
int main(void) { return arr[1] == 42 && p.x == 5 ? 42 : 1; }
