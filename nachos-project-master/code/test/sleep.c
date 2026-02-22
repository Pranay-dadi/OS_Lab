#include "syscall.h"

int main() {
    PrintString("Starting sleep...\n");
    Sleep(100000);
    PrintString("Woke up after given number of ticks!\n");
    Halt();
}