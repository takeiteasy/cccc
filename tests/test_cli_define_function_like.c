// CCCC_FLAGS: -DADD(a,b)=a+b -DTWO(x)=2 -DFIRST(...)=__VA_ARGS__ -DNAMED(args...)=args

int main(void) {
    return ADD(38, TWO(1)) + (FIRST(1)) + (NAMED(1));
}
