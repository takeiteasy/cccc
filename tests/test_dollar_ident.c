// `$` is an ordinary identifier character outside quasi-quote templates.
int $counter = 40;

int $add(int $a, int $b) {
    return $a + $b;
}

int main(void) {
    int $x = 2;
    return $add($counter, $x);
}
