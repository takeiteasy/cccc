// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: custom attributes on members are only supported in file-scope structs and unions

@comptime(attribute("tag")) void tag_member(AttrTarget *target) {
}

int main(void) {
    struct Local {
        int x @tag;
    } l = {1};
    return l.x + 41;
}
