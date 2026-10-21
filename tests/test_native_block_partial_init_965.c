// -c=native, a __block aggregate's partial brace initializer, read from inside
// a block, round-trips as real C -- exercises ND_MEMZERO's is_block_var arm
int main(void) {
    __block int x[4] = {1};
    int (^sum)(void) = ^{
      return x[0] + x[1] + x[2] + x[3];
    };
    return sum() == 1 ? 42 : 1;
}
