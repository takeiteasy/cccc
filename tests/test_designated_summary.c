/*
 * Test summary for designated initializers support in CCCC
 * 
 * FULLY WORKING:
 * - Array designated initializers: [index] = value
 * - Multiple array designators: [0] = 1, [2] = 3, [4] = 5
 * - Struct designated initializers: .member = value
 * - Out-of-order struct designators: .y = 30, .x = 40
 * - Single nested designator: .top_left.x = 10
 * - Multiple nested designators: .tl.x = 10, .tl.y = 20  ✅ FIXED!
 */

struct Point {
    int x;
    int y;
};

struct Rect {
    struct Point tl;
    struct Point br;
};

int main() {
    int result = 0;
    
    // ✅ Array designated initializers work
    int arr1[5] = {[2] = 10, [4] = 20};
    result += arr1[2];  // 10
    result += arr1[4];  // 20, total: 30
    
    // ✅ Struct designated initializers work
    struct Point p1 = {.x = 5, .y = 7};
    result += p1.x + p1.y;  // 12, total: 42
    
    // ✅ Out-of-order works
    struct Point p2 = {.y = 100, .x = 0};
    if (p2.x == 0 && p2.y == 100) {
        result += 10;  // total: 52
    }
    
    // ✅ Single nested designator works
    struct Rect r1 = {.tl.x = 90};
    result += r1.tl.x;  // total: 142
    
    // ✅ Multiple nested designators NOW WORK!
    struct Rect r2 = {.tl.x = 10, .tl.y = 20, .br.x = 30, .br.y = 40};
    if (r2.tl.x == 10 && r2.tl.y == 20 && r2.br.x == 30 && r2.br.y == 40) {
        result += 8;  // total: 150
    }

    if (result != 150) return 1;  // Assert result == 150
    return 42;
}
