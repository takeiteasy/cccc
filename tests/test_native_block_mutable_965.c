// -c=native, a __block variable shared and mutated by a block round-trips as
// real C
int main(void) {
    __block int counter = 0;
    void (^inc)(void)   = ^{
      counter++;
    };
    inc();
    inc();
    return counter == 2 ? 42 : 1;
}
