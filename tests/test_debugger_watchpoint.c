// Debugger watchpoint fixture

int watched_global;

int main(void) {
    int x;
    watched_global = 1;
    x = 10;
    x = x + watched_global;
    return 31 + x;
}
