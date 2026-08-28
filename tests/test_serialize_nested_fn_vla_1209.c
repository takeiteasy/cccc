// Ticket #1209 (#1074 residual): a genuinely nested function (Obj.is_nested,
// not an Apple block) reading a VLA local -- or a pointer-to-VLA local whose
// own declarator reads a runtime variable -- owned by one of its ancestors
// used to be rejected outright: `record_nested_upvar()`
// (src/serialize_program.c) had no way to take `&var`'s address at the top
// of the owning function, where every other upvar's env-struct field is
// filled, because a VLA's own declaration can't be hoisted there (#964) --
// its declarator re-reads the original length expression, which isn't in
// scope yet.
//
// Fixed by erasing the VLA's outermost extent from the env-struct field's
// type (`int (*)[]`, the same spelling C already uses for a flexible array
// member) and filling the field at the VLA's own in-place declaration site
// instead of the top of the function -- once `&var` is finally valid.
// `serialize_nested_upvar_ref()`, which rewrites every read inside the
// nested function to `(*env->__uvK)`, needed no change at all: the erased
// pointer dereferences and decays exactly like the original.
//
// A fully multi-dimensional VLA (every extent runtime-sized, `int
// v[n][m]`) still can't be captured this way -- the field would need to be
// `int (*)[][m]`, a pointer to an array of incomplete element type, which
// is illegal C. See test_serialize_nested_fn_vla_multidim_1221.c for that
// residual (#1221).

// (a): 1-D VLA local, read only.
static int outer_1d_read(void) {
    int n = 4;
    int v[n];
    for (int i = 0; i < n; i++)
        v[i] = i;
    int sum(void) {
        int s = 0;
        for (int i = 0; i < n; i++)
            s += v[i];
        return s;
    }
    return sum();
}

// (b): 1-D VLA local, write-through -- the nested function mutates the
// ancestor's storage, not a snapshot.
static int outer_1d_write(void) {
    int n = 4;
    int v[n];
    for (int i = 0; i < n; i++)
        v[i] = 0;
    void bump(int idx, int val) {
        v[idx] = val;
    }
    bump(2, 40);
    return v[2];
}

// (c): VLA declared inside a loop body -- proves the deferred field store
// re-executes on every iteration (a fresh address each time), not just
// once.
static int outer_1d_in_loop(void) {
    int total = 0;
    for (int k = 0; k < 3; k++) {
        int n = 2;
        int v[n];
        v[0] = k;
        v[1] = k * 10;
        int read_second(void) {
            return v[1];
        }
        total += read_second();
    }
    return total; // 0 + 10 + 20
}

// (d): VLA with a fixed inner extent (`int v[n][3]`) -- the outer extent is
// runtime-sized but the row type is ordinary TY_ARRAY, so the erased field
// is `int (*)[][3]`, not a multi-dimensional VLA. Must still work.
static int outer_2d_fixed_inner(void) {
    int n = 3;
    int v[n][3];
    for (int i = 0; i < n; i++)
        for (int j = 0; j < 3; j++)
            v[i][j] = i * 3 + j;
    int sum2d(void) {
        int s = 0;
        for (int i = 0; i < n; i++)
            for (int j = 0; j < 3; j++)
                s += v[i][j];
        return s;
    }
    return sum2d(); // 0+1+2+3+4+5+6+7+8 = 36
}

// (e): pointer-to-VLA local with an initializer (Obj.deferred_vla_ptr_init)
// -- one more level of indirection than a plain VLA local (`T (**)[]`).
static int outer_ptr_to_vla(void) {
    int n = 4;
    int v[n];
    for (int i = 0; i < n; i++)
        v[i] = i;
    int (*p)[n] = &v;
    int third(void) {
        return (*p)[3];
    }
    return third();
}

// (f): grandparent-level VLA upvar -- exercises the `->__up` chase through
// an intervening nested function that doesn't itself read `v`.
static int outer_grandparent(void) {
    int n = 4;
    int v[n];
    for (int i = 0; i < n; i++)
        v[i] = i;
    int mid(void) {
        int inner(void) {
            return v[3];
        }
        return inner();
    }
    return mid();
}

int main(void) {
    if (outer_1d_read() != 6) // 0+1+2+3
        return 1;
    if (outer_1d_write() != 40)
        return 2;
    if (outer_1d_in_loop() != 30)
        return 3;
    if (outer_2d_fixed_inner() != 36)
        return 4;
    if (outer_ptr_to_vla() != 3)
        return 5;
    if (outer_grandparent() != 3)
        return 6;
    return 42;
}
