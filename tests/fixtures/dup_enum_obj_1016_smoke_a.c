static int AA1016Smoke = 3;
int        BB1016Smoke = 7;
int CC1016Smoke(void) {
    return 9;
}
int a_use_1016_smoke(void) {
    return AA1016Smoke + BB1016Smoke + CC1016Smoke();
}
