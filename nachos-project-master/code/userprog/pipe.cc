// pipe.cc

#include "pipe.h"
#include "main.h"
#include <string.h>

PipeBuffer::PipeBuffer()
{
    readPos        = 0;
    writePos       = 0;
    count          = 0;
    writeEndClosed = false;
    readEndClosed  = false;
    lock     = new Lock("PipeLock");
    notEmpty = new Condition("PipeNotEmpty");
    notFull  = new Condition("PipeNotFull");
}

PipeBuffer::~PipeBuffer()
{
    delete lock;
    delete notEmpty;
    delete notFull;
}

int PipeBuffer::Write(char *buffer, int numBytes)
{
    if (numBytes <= 0 || buffer == NULL) return -1;
    lock->Acquire();

    if (readEndClosed) { lock->Release(); return -1; }

    int written = 0;
    while (written < numBytes) {
        while (count == PIPE_BUFFER_SIZE) {
            if (readEndClosed) {
                lock->Release();
                return written == 0 ? -1 : written;
            }
            notFull->Wait(lock);
        }
        buf[writePos] = buffer[written];
        writePos = (writePos + 1) % PIPE_BUFFER_SIZE;
        count++;
        written++;
        notEmpty->Signal(lock);
    }

    lock->Release();
    return written;
}

int PipeBuffer::Read(char *buffer, int numBytes)
{
    if (numBytes <= 0 || buffer == NULL) return -1;
    lock->Acquire();

    while (count == 0) {
        if (writeEndClosed) { lock->Release(); return 0; } // EOF
        notEmpty->Wait(lock);
    }

    int bytesRead = 0;
    while (bytesRead < numBytes && count > 0) {
        buffer[bytesRead] = buf[readPos];
        readPos = (readPos + 1) % PIPE_BUFFER_SIZE;
        count--;
        bytesRead++;
        notFull->Signal(lock);
    }

    lock->Release();
    return bytesRead;
}

void PipeBuffer::CloseWriteEnd()
{
    lock->Acquire();
    writeEndClosed = true;
    notEmpty->Broadcast(lock); // wake readers so they see EOF
    lock->Release();
}

void PipeBuffer::CloseReadEnd()
{
    lock->Acquire();
    readEndClosed = true;
    notFull->Broadcast(lock);  // wake writers so they see broken pipe
    lock->Release();
}