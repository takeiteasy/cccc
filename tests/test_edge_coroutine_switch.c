#define crBegin     static int state = 0; switch (state) { case 0:
#define crReturn(x) do { state = __LINE__; return (x); case __LINE__:; } while (0)
#define crFinish    }

static int counter(void) {
    static int i;
    crBegin;
    for (i = 0; i < 5; i++)
        crReturn(i);
    crFinish;
    return -1;
}

int main(void) {
    if (counter() != 0) return 1;
    if (counter() != 1) return 2;
    if (counter() != 2) return 3;
    if (counter() != 3) return 4;
    if (counter() != 4) return 5;
    return 42;
}
