// GenerateSum/Map/Reduce/Filter publish once per element type; repeat calls,
// from one function or several, do nothing.

[[cccc::comptime]]
void first(void) {
    Type *t = GetType("int");
    GenerateSum(t);
    GenerateMap(t);
    GenerateReduce(t);
    GenerateFilter(t);
    GenerateSum(t);
}

[[cccc::comptime]]
void second(void) {
    Type *t = GetType("int");
    GenerateSum(t);
    GenerateMap(t);
    GenerateReduce(t);
    GenerateFilter(t);
}

first();
second();

int main(void) {
    int a[3] = {1, 2, 3};
    return sum_int(a, 3) == 6 ? 42 : 1;
}
