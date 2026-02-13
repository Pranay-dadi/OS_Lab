#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "copyright.h"
#include "list.h"
#include "thread.h"

class Scheduler {
public:
    Scheduler();
    ~Scheduler();

    void ReadyToRun(Thread* thread);
    Thread* FindNextToRun();
    void Run(Thread* nextThread, bool finishing);
    void CheckToBeDestroyed();
    void Print();

    void EnablePriorityScheduling(bool enable);
    void ReadyToRunPriority(Thread* thread);

private:
    List<Thread*>* readyList;
    Thread* toBeDestroyed;
    bool usePriorityScheduling;

    Thread* FindNextToRunPriority();
};

#endif