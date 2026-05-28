// Regression fixture for -M type definition serialization

typedef struct {
    int width;
    int height;
} SerSize;

typedef enum {
    SER_RED = 1,
    SER_GREEN = 2,
} SerColor;

union SerValue {
    int i;
    char c;
};

struct SerPoint {
    int x;
    int y;
};

struct SerBox {
    struct SerPoint origin;
    SerSize size;
    union SerValue value;
    SerColor color;
};

int main(void) {
    struct SerBox box;
    box.origin.x = 10;
    box.origin.y = 20;
    box.size.width = 5;
    box.size.height = 4;
    box.value.i = 1;
    box.color = SER_GREEN;

    return box.origin.x + box.origin.y + box.size.width + box.size.height +
           box.value.i + box.color;
}
