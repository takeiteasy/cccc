typedef struct {
    double x;
    double y;
    double z;
} TypedefShape1293;
static TypedefShape1293 typedef_shape_1293_mk(void) {
    TypedefShape1293 s;
    s.x = 1;
    s.y = 2;
    s.z = 3;
    return s;
}
int typedef_shape_1293_local_use(void) {
    TypedefShape1293 s = typedef_shape_1293_mk();
    return (int)(s.x + s.y + s.z);
}
