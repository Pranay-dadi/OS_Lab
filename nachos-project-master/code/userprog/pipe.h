// pipe.h
// A unidirectional in-memory pipe buffer.
// Named PipeBuffer to avoid conflict with the Pipe() syscall function.

#ifndef PIPE_H
#define PIPE_H

#include "synch.h"

#define PIPE_BUFFER_SIZE 1024

class PipeBuffer {
public:
    PipeBuffer();
    ~PipeBuffer();

    // Write numBytes from buffer into pipe. Blocks if full.
    // Returns bytes written, or -1 if read end is closed.
    int Write(char *buffer, int numBytes);

    // Read up to numBytes from pipe into buffer. Blocks if empty.
    // Returns bytes read, 0 on EOF (write end closed + empty), -1 on error.
    int Read(char *buffer, int numBytes);

    void CloseWriteEnd();
    void CloseReadEnd();

    bool IsWriteEndClosed() { return writeEndClosed; }
    bool IsReadEndClosed()  { return readEndClosed;  }

private:
    char buf[PIPE_BUFFER_SIZE];
    int  readPos;
    int  writePos;
    int  count;

    bool writeEndClosed;
    bool readEndClosed;

    Lock      *lock;
    Condition *notEmpty;
    Condition *notFull;
};

#endif // PIPE_H