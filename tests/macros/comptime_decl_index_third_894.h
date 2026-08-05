// #894 fixture: a plain declaration-only header. It defines no
// [[cccc::comptime]] function or variable of its own, so under the old
// file-identity filter (#890) its declarations were invisible to comptime
// bodies unless the #include was routed @shared. The demand-driven
// declaration index has no such restriction for a non-system header.
typedef struct Point {
    int x;
    int y;
} Point;

int point_area(Point p);
