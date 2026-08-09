// Comprehensive typedef test

// Basic scalar typedefs
typedef int Integer;
typedef char Byte;
typedef long Long;

// Pointer typedefs
typedef int *IntPtr;
typedef char *String;

// Struct typedef
struct Point {
    int x;
    int y;
};
typedef struct Point Point;

// Typedef with struct declaration
typedef struct {
    int width;
    int height;
} Rectangle;

// Function pointer typedef
typedef int (*BinaryOp)(int, int);

// Array typedef
typedef int IntArray[5];

int add(int a, int b) {
    return a + b;
}

int main() {
    // Test basic typedef
    Integer x = 10;
    Byte b = 32;
    Long l = 0;
    
    // Test pointer typedef
    IntPtr ptr = &x;
    
    // Test struct typedef
    Point p;
    p.x = 5;
    p.y = 7;
    
    // Test anonymous struct typedef
    Rectangle r;
    r.width = 3;
    r.height = 4;
    
    // Test function pointer typedef
    BinaryOp op = add;
    int result = op(20, 10);  // Should be 30
    
    // Test array typedef
    IntArray arr;
    arr[0] = 1;
    arr[1] = 2;
    
    return x + b;  // 10 + 32 = 42
}
