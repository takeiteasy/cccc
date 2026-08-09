// Test advanced forward declaration scenarios
// Expected return: 42

// Multiple forward declarations
struct A;
struct B;
struct C;

// Struct A references B, B references C, C references A (circular)
struct A {
    int a_val;
    struct B *b_ptr;
};

struct B {
    int b_val;
    struct C *c_ptr;
};

struct C {
    int c_val;
    struct A *a_ptr;
};

// Forward-declared typedef (common pattern)
typedef struct Node Node;
struct Node {
    int data;
    Node *left;   // Using typedef'd name
    Node *right;
};

// Forward declaration with function pointers
struct Handler;
typedef void (*HandlerFunc)(struct Handler *h, int value);

struct Handler {
    int state;
    HandlerFunc callback;
};

// Callback function
void process(struct Handler *h, int val) {
    h->state = h->state + val;
}

// Forward-declared union with typedef
typedef union Data Data;
union Data {
    int i;
    char c;
};

// Array of forward-declared pointers
struct Item;
struct Container {
    struct Item *items[3];
    int count;
};

struct Item {
    int value;
};

int main() {
    // Test 1: Circular references (A->B->C->A)
    struct A a;
    struct B b;
    struct C c;
    
    a.a_val = 10;
    b.b_val = 15;
    c.c_val = 17;
    
    a.b_ptr = &b;
    b.c_ptr = &c;
    c.a_ptr = &a;
    
    // Navigate the circular chain
    int sum1 = a.a_val + a.b_ptr->b_val + a.b_ptr->c_ptr->c_val;
    // 10 + 15 + 17 = 42
    if (sum1 != 42) return 1;
    
    // Test 2: Forward-declared typedef (binary tree node)
    Node root;
    Node left;
    Node right;
    
    root.data = 20;
    left.data = 10;
    right.data = 12;
    
    root.left = &left;
    root.right = &right;
    
    int sum2 = root.data + root.left->data + root.right->data;
    // 20 + 10 + 12 = 42
    if (sum2 != 42) return 2;
    
    // Test 3: Forward-declared struct with function pointer
    struct Handler h;
    h.state = 0;
    h.callback = process;
    
    h.callback(&h, 42);
    if (h.state != 42) return 3;
    
    // Test 4: Forward-declared union with typedef
    Data d;
    d.i = 42;
    if (d.i != 42) return 4;
    
    // Test 5: Array of forward-declared pointers
    struct Item i1, i2, i3;
    i1.value = 10;
    i2.value = 20;
    i3.value = 12;
    
    struct Container container;
    container.items[0] = &i1;
    container.items[1] = &i2;
    container.items[2] = &i3;
    container.count = 3;
    
    int sum3 = container.items[0]->value + 
               container.items[1]->value + 
               container.items[2]->value;
    // 10 + 20 + 12 = 42
    if (sum3 != 42) return 5;
    
    return 42;
}
