#include "syscall.h"

int main() {
    int x = -10;
    int y;
    y = Abs(x);

    PrintNum(y);
    Halt();
    /* not reached */
}
