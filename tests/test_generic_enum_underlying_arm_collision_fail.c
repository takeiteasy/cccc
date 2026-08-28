// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: '_Generic' specifies two compatible types
//
// #1224 (reachable via #1223): an enum is compatible with its underlying
// integer type, so an `enum G:` arm and an `unsigned int:` arm in the same
// _Generic now collide -- gcc and clang both reject this. Before #1223 the
// enum arm matched nothing, so the collision could not be constructed.
enum G {
    G1 = 1,
};

int main(void) {
    enum G g = G1;
    return _Generic(g, enum G: 1, unsigned int: 2, default: 0);
}
