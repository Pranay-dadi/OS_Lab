#include "syscall.h"

/* Helper: print label + number + newline */
void printLabelNum(char *label, int n) {
    PrintString(label);
    PrintNum(n);
    PrintChar('\n');
}

int main() {
    /* ✅ ALL declarations first */
    int *a, *b, *c, *d, *p, *q;

    PrintString("=== Test 1: Basic alloc + read/write ===\n");
    a = (int *)Malloc(4 * sizeof(int));
    a[0] = 10; a[1] = 20; a[2] = 30; a[3] = 40;
    printLabelNum("a[0]=", a[0]);
    printLabelNum("a[3]=", a[3]);

    PrintString("=== Test 2: Second alloc while first is live ===\n");
    b = (int *)Malloc(2 * sizeof(int));
    b[0] = 100; b[1] = 200;
    printLabelNum("b[0]=", b[0]);
    printLabelNum("b[1]=", b[1]);
    printLabelNum("a[0] still=", a[0]);

    PrintString("=== Test 3: Free tail block (heap shrinks) ===\n");
    Free(b);
    printLabelNum("a[0] after Free(b)=", a[0]);
    printLabelNum("a[3] after Free(b)=", a[3]);

    PrintString("=== Test 4: Reuse freed block ===\n");
    c = (int *)Malloc(2 * sizeof(int));
    c[0] = 55; c[1] = 66;
    printLabelNum("c[0]=", c[0]);
    printLabelNum("c[1]=", c[1]);

    PrintString("=== Test 5: Free all, then alloc fresh ===\n");
    Free(a);
    Free(c);
    d = (int *)Malloc(3 * sizeof(int));
    d[0] = 7; d[1] = 8; d[2] = 9;
    printLabelNum("d[0]=", d[0]);
    printLabelNum("d[2]=", d[2]);

    PrintString("=== Test 6: Free(NULL) must not crash ===\n");
    Free(0);
    PrintString("Free(NULL) OK\n");

    PrintString("=== Test 7: Overwrite check ===\n");
    p = (int *)Malloc(2 * sizeof(int));
    q = (int *)Malloc(2 * sizeof(int));
    p[0] = 111; p[1] = 222;
    q[0] = 333; q[1] = 444;
    printLabelNum("p[0]=", p[0]);
    printLabelNum("p[1]=", p[1]);
    printLabelNum("q[0]=", q[0]);

    Free(p); Free(q); Free(d);

    PrintString("=== ALL TESTS PASSED ===\n");
    return 0;
}