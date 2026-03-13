// pipetable.cc

#include "pipetable.h"
#include <string.h>

// Global singleton — initialised in kernel startup (kernel.cc or main.cc)
PipeTable *gPipeTable = NULL;

PipeTable::PipeTable()
{
    memset(entries, 0, sizeof(entries));
    nextFD = PIPE_FD_BASE;
}

PipeTable::~PipeTable()
{
    // Close any still-open entries
    for (int i = 0; i < MAX_PIPE_FDS; i++) {
        if (entries[i].inUse) {
            entries[i].inUse = false;
            // Only delete the PipeBuffer if BOTH ends are closed
            if (entries[i].pipe->IsWriteEndClosed() &&
                entries[i].pipe->IsReadEndClosed())
                delete entries[i].pipe;
        }
    }
}

int PipeTable::AllocSlot()
{
    for (int i = 0; i < MAX_PIPE_FDS; i++)
        if (!entries[i].inUse) return i;
    return -1;
}

int PipeTable::FDtoSlot(int fd)
{
    for (int i = 0; i < MAX_PIPE_FDS; i++)
        if (entries[i].inUse && (PIPE_FD_BASE + i) == fd) return i;
    return -1;
}

bool PipeTable::IsPipeFD(int fd)
{
    return FDtoSlot(fd) != -1;
}

int PipeTable::AllocPipe(int *readFD, int *writeFD)
{
    int rSlot = AllocSlot();
    if (rSlot == -1) return -1;

    entries[rSlot].inUse = true; // temporarily mark to avoid double-alloc
    int wSlot = AllocSlot();
    if (wSlot == -1) {
        entries[rSlot].inUse = false;
        return -1;
    }

    PipeBuffer *pb = new PipeBuffer();

    entries[rSlot].pipe       = pb;
    entries[rSlot].isWriteEnd = false;
    entries[rSlot].inUse      = true;

    entries[wSlot].pipe       = pb;
    entries[wSlot].isWriteEnd = true;
    entries[wSlot].inUse      = true;

    *readFD  = PIPE_FD_BASE + rSlot;
    *writeFD = PIPE_FD_BASE + wSlot;
    return 0;
}

int PipeTable::Read(int fd, char *buf, int numBytes)
{
    int slot = FDtoSlot(fd);
    if (slot == -1) return -1;
    if (entries[slot].isWriteEnd) return -1; // can't read write end
    return entries[slot].pipe->Read(buf, numBytes);
}

int PipeTable::Write(int fd, char *buf, int numBytes)
{
    int slot = FDtoSlot(fd);
    if (slot == -1) return -1;
    if (!entries[slot].isWriteEnd) return -1; // can't write read end
    return entries[slot].pipe->Write(buf, numBytes);
}

void PipeTable::Close(int fd)
{
    int slot = FDtoSlot(fd);
    if (slot == -1) return;

    PipeBuffer *pb = entries[slot].pipe;
    bool isWrite   = entries[slot].isWriteEnd;

    entries[slot].inUse = false;
    entries[slot].pipe  = NULL;

    if (isWrite)
        pb->CloseWriteEnd();
    else
        pb->CloseReadEnd();

    // Free shared buffer only when both ends are closed
    if (pb->IsWriteEndClosed() && pb->IsReadEndClosed())
        delete pb;
}