// pipetable.h
//
// A global table that maps file-descriptor integers to PipeBuffer objects.
//
// Why we need this instead of openFiles[]:
//   In this NachOS repo, Thread has no openFiles[] array.
//   All file I/O goes through kernel->fileSystem with integer FDs.
//   We intercept pipe FDs BEFORE they reach the filesystem.
//
// FD allocation strategy:
//   Regular filesystem FDs start from 2 (0=stdin, 1=stdout).
//   Pipe FDs start from PIPE_FD_BASE (100) so they never clash
//   with filesystem FDs.

#ifndef PIPETABLE_H
#define PIPETABLE_H

#include "pipe.h"

#define PIPE_FD_BASE   100   // pipe FDs start here
#define MAX_PIPE_FDS   64    // max simultaneous pipe endpoints

struct PipeEntry {
    PipeBuffer *pipe;       // shared buffer (same pointer for both ends)
    bool        isWriteEnd; // true = write end, false = read end
    bool        inUse;
};

class PipeTable {
public:
    PipeTable();
    ~PipeTable();

    // Allocate two new FDs (read + write ends) for a fresh pipe.
    // Returns 0 on success, -1 if table is full.
    int  AllocPipe(int *readFD, int *writeFD);

    // Returns true if fd belongs to a pipe.
    bool IsPipeFD(int fd);

    // Read/Write/Close — only call after IsPipeFD() returns true.
    int  Read (int fd, char *buf, int numBytes);
    int  Write(int fd, char *buf, int numBytes);
    void Close(int fd);

private:
    PipeEntry entries[MAX_PIPE_FDS];
    int nextFD; // next candidate FD to allocate

    int  AllocSlot();         // find a free slot index
    int  FDtoSlot(int fd);    // fd -> slot index (-1 if not found)
};

// Global singleton — defined in pipe.cc, declared here.
extern PipeTable *gPipeTable;

#endif // PIPETABLE_H