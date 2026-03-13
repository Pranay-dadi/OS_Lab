/*
 * demandpage_test.c
 *
 * Tests NachOS demand paging:
 *   - String literals live in the .rodata segment → readonlyData pages
 *     are faulted in when PrintString first accesses them.
 *   - A 200-element stack array forces multiple stack page faults.
 *   - Data is verified to confirm pages were loaded correctly.
 *
 * Expected output:
 *   === Demand Paging Test ===
 *   Filling array...
 *   Sum forward  = 20100  PASSED
 *   Sum backward = 20100  PASSED
 *   === All tests passed! ===
 */

#include "syscall.h"

int main(void)
{
    int arr[200];   /* forces several stack page faults */
    int i, sum, revSum;

    /* These string literals are in .rodata → demand-paged on first access */
    PrintString("=== Demand Paging Test ===\n");
    PrintString("Filling array...\n");

    /* touch every element: each new stack page triggers a page fault */
    for (i = 0; i < 200; i++)
        arr[i] = i + 1;          /* values 1 .. 200 */

    /* forward sum = 200*201/2 = 20100 */
    sum = 0;
    for (i = 0; i < 200; i++)
        sum += arr[i];

    PrintString("Sum forward  = ");
    PrintNum(sum);
    if (sum == 20100) {
        PrintString("  PASSED\n");
    } else {
        PrintString("  FAILED\n");
        Exit(1);
    }

    /* backward sum — different page-access order */
    revSum = 0;
    for (i = 199; i >= 0; i--)
        revSum += arr[i];

    PrintString("Sum backward = ");
    PrintNum(revSum);
    if (revSum == 20100) {
        PrintString("  PASSED\n");
    } else {
        PrintString("  FAILED\n");
        Exit(1);
    }

    PrintString("=== All tests passed! ===\n");
    Exit(0);
    return 0;
}