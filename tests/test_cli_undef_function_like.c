// CCCC_FLAGS: -DF(x)=40 -DG(x)=1 -UF(x) -UG

int F(int x) {
    return x + 41;
}

int G(int x) {
    return x;
}

int main(void) {
    return F(1) + G(0);
}
