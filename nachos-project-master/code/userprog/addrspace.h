#ifndef ADDRSPACE_H
#define ADDRSPACE_H

#include "copyright.h"
#include "filesys.h"

#define UserStackSize 1024

struct PageInfo {
    int  fileOffset;
    int  validBytes;
    bool readOnly;
};

// Heap block header — stored in simulated user memory just before each allocation
// Layout: [size:4][free:4][next:4] = 12 bytes
struct HeapBlock {
    int  size;   // usable bytes (excluding this header)
    bool free;
    int  next;   // virtual addr of next HeapBlock, -1 = none
};

#define BLOCK_HEADER_SIZE 12

class AddrSpace {
   public:
    AddrSpace();
    AddrSpace(char *fileName);
    ~AddrSpace();

    void Execute();
    void SaveState();
    void RestoreState();

    ExceptionType Translate(unsigned int vaddr, unsigned int *paddr, int mode);
    void HandlePageFault(unsigned int vaddr);

    // Heap management — called from ksyscall.h
    int  SysMalloc(int size);
    void SysFree(int ptr);

   private:
    TranslationEntry *pageTable;
    unsigned int      numPages;

    OpenFile  *executable;
    PageInfo  *pageInfo;

    unsigned int heapStart;  // first virtual addr of heap (fixed after construction)
    unsigned int heapTop;    // current top of heap (grows/shrinks)

    // Internal helpers
    int  SysBreak(int delta);          // grow heap by delta bytes, return old heapTop or -1
    bool ShrinkHeap(unsigned int newTop); // shrink heap, free physical frames
    HeapBlock ReadBlock(int vaddr);
    void      WriteBlock(int vaddr, HeapBlock b);

    void InitRegisters();
};

#endif  // ADDRSPACE_H