#include "kernel.h"
#include "thread.h"
#include "scheduler.h"
#include "interrupt.h"
#include "debug.h"

extern Kernel *kernel;

void
PriorityThread(void* arg)
{
    int id = (int)(long)arg;
    for (int i = 0; i < 5; i++) {
        printf("Thread %d running at tick %d\n",
               id, kernel->stats->totalTicks);
        kernel->interrupt->OneTick();
    }
}

void PrioritySchedulerTest()
{
    printf("\n--- Priority Scheduler Test ---\n");

    // Enable priority scheduling
    kernel->scheduler->EnablePriorityScheduling(true);

    Thread *low = new Thread((char *)"LowPriority");
    Thread *mid = new Thread((char *)"MidPriority");
    Thread *high = new Thread((char *)"HighPriority");

    low->setPriority(1);
    mid->setPriority(5);
    high->setPriority(10);

    low->Fork(PriorityThread, (void *)1);
    mid->Fork(PriorityThread, (void *)5);
    high->Fork(PriorityThread, (void *)10);

    // ────────────────────────────────────────────────
    // VERY IMPORTANT: Give time for the threads to run!
    // ────────────────────────────────────────────────
    printf("Main thread yielding to let priority threads run...\n");

    for (int i = 0; i < 100; i++) {           // 50–200 is usually enough
        kernel->currentThread->Yield();
    }

    printf("Priority test finished.\n");
}
