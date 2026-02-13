// scheduler.cc
#include "copyright.h"
#include "debug.h"
#include "scheduler.h"
#include "main.h"
#include "kernel.h"

//----------------------------------------------------------------------
// Scheduler::Scheduler
//----------------------------------------------------------------------
Scheduler::Scheduler() {
    readyList = new List<Thread*>;
    toBeDestroyed = NULL;
    usePriorityScheduling = false;
}

//----------------------------------------------------------------------
// Scheduler::~Scheduler
//----------------------------------------------------------------------
Scheduler::~Scheduler() {
    delete readyList;
}

//----------------------------------------------------------------------
// Scheduler::EnablePriorityScheduling
//----------------------------------------------------------------------
void
Scheduler::EnablePriorityScheduling(bool enable) {
    usePriorityScheduling = enable;
    DEBUG(dbgThread, "Priority scheduling now: " << (enable ? "ON" : "OFF"));
}

//----------------------------------------------------------------------
// Scheduler::ReadyToRun
//----------------------------------------------------------------------
void
Scheduler::ReadyToRun(Thread *thread) {
    ASSERT(kernel->interrupt->getLevel() == IntOff);
    DEBUG(dbgThread, "Putting thread on ready list: " << thread->getName());

    thread->setStatus(READY);
    readyList->Append(thread);          // always append (FIFO insertion)
}

//----------------------------------------------------------------------
// Scheduler::ReadyToRunPriority
//----------------------------------------------------------------------
void
Scheduler::ReadyToRunPriority(Thread *thread) {
    ReadyToRun(thread);   // same behavior – we select by priority later
}

//----------------------------------------------------------------------
// Scheduler::FindNextToRun
//----------------------------------------------------------------------
Thread*
Scheduler::FindNextToRun() {
    ASSERT(kernel->interrupt->getLevel() == IntOff);

    if (usePriorityScheduling) {
        return FindNextToRunPriority();
    }

    // Normal FIFO
    if (readyList->IsEmpty()) {
        return NULL;
    }
    return readyList->RemoveFront();
}

//----------------------------------------------------------------------
// Scheduler::FindNextToRunPriority
//      Scan list and pick thread with highest priority
//----------------------------------------------------------------------
Thread*
Scheduler::FindNextToRunPriority() {
    if (readyList->IsEmpty()) {
        return NULL;
    }

    Thread *best = NULL;
    int highest = -1;

    ListIterator<Thread*> iter(readyList);
    while (!iter.IsDone()) {
        Thread *t = iter.Item();
        int p = t->getPriority();

        if (p > highest) {
            highest = p;
            best = t;
        }
        iter.Next();
    }

    // Remove the selected thread
    readyList->Remove(best);

    return best;
}

//----------------------------------------------------------------------
// Scheduler::Run
//----------------------------------------------------------------------
void
Scheduler::Run(Thread *nextThread, bool finishing) {
    Thread *oldThread = kernel->currentThread;
    ASSERT(kernel->interrupt->getLevel() == IntOff);

    if (finishing) {
        ASSERT(toBeDestroyed == NULL);
        toBeDestroyed = oldThread;
    }

    if (oldThread->space != NULL) {
        oldThread->SaveUserState();
        oldThread->space->SaveState();
    }

    oldThread->CheckOverflow();

    kernel->currentThread = nextThread;
    nextThread->setStatus(RUNNING);

    DEBUG(dbgThread,
          "Switching from: " << oldThread->getName()
          << " to: " << nextThread->getName());

    SWITCH(oldThread, nextThread);

    ASSERT(kernel->interrupt->getLevel() == IntOff);

    DEBUG(dbgThread, "Now in thread: " << oldThread->getName());

    CheckToBeDestroyed();

    if (oldThread->space != NULL) {
        oldThread->RestoreUserState();
        oldThread->space->RestoreState();
    }
}

//----------------------------------------------------------------------
// Scheduler::CheckToBeDestroyed
//----------------------------------------------------------------------
void
Scheduler::CheckToBeDestroyed() {
    if (toBeDestroyed != NULL) {
        delete toBeDestroyed;
        toBeDestroyed = NULL;
    }
}

//----------------------------------------------------------------------
// Scheduler::Print
//----------------------------------------------------------------------
void
Scheduler::Print() {
    cout << "Ready list contents" 
         << (usePriorityScheduling ? " (priority mode)" : " (FIFO mode)")
         << ":\n";

    ListIterator<Thread*> iter(readyList);
    while (!iter.IsDone()) {
        Thread* t = iter.Item();
        cout << "  " << t->getName();
        if (usePriorityScheduling) {
            cout << " (prio " << t->getPriority() << ")";
        }
        cout << "\n";
        iter.Next();
    }
}