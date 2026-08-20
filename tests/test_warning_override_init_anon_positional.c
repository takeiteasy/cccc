// CCCC_FLAGS: -Woverride-init
// CCCC_EXPECT_STDERR: initializer overrides prior initialization of
// 'i'.*\[-Woverride-init\] CCCC_REJECT_STDERR: anonymous member #961: a
// positional initializer for the whole anonymous member (`{1}`) followed by a
// designator into one of its fields (`.i = 5`) used to warn with the generic
// "...anonymous member" text (the wrapper-level is_set check firing since the
// positional initializer set it). Now the wrapper-level check is skipped for an
// anonymous member and the recursive designation() call resolves and names the
// real leaf field
// ('i') instead -- so this warns exactly once, naming the field.

struct S {
    union {
        int   i;
        float f;
    };
    int tag;
};

int main(void) {
    struct S s = {{1}, .i = 42};
    return s.i;
}
