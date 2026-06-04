#include <stdio.h>
#include <stdlib.h>

#ifndef BENCH_N
#define BENCH_N 100000
#endif

typedef struct Node {
    int key;
    struct Node *left;
    struct Node *right;
} Node;

static Node *root;
static long checksum;
static long visit_count;

static Node *new_node(int k) {
    Node *n = malloc(sizeof(Node));
    n->key = k;
    n->left = n->right = 0;
    return n;
}

static Node *insert(Node *n, int k) {
    if (!n) return new_node(k);
    if (k < n->key) n->left = insert(n->left, k);
    else n->right = insert(n->right, k);
    return n;
}

static void inorder(Node *n) {
    if (!n) return;
    inorder(n->left);
    checksum += n->key;
    visit_count++;
    inorder(n->right);
}

static unsigned long mrand(unsigned long *s) {
    *s = *s * 6364136223846793005ULL + 1442695040888963407ULL;
    return *s >> 11;
}

int main(void) {
    unsigned long s = 0x9E3779B97F4A7C15ULL;
    long n = BENCH_N;
    root = 0;
    for (long i = 0; i < n; i++) {
        int k = (int)(mrand(&s) & 0x7FFFFFFFL);
        root = insert(root, k);
    }
    inorder(root);
    printf("result: visits=%ld checksum=%ld\n", visit_count, checksum);
    return 42;
}
