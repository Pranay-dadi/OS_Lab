#ifndef ADDRSPACE_H
#define ADDRSPACE_H

#include "copyright.h"
#include "filesys.h"

#define UserStackSize 1024

// ---------------------------------------------------------------------------
// PageInfo — describes where each virtual page lives in the executable file.
// Used by HandlePageFault for demand paging.
// ---------------------------------------------------------------------------
struct PageInfo {
    int  fileOffset;   // -1 means zero-fill (stack / uninit data)
    int  validBytes;   // how many bytes to read from file (rest is zero)
    bool readOnly;     // true for .rodata segment
};

// ---------------------------------------------------------------------------
// HeapBlock — header for each allocation managed by SysMalloc / SysFree.
// Stored in simulated user memory, 12 bytes wide:
//   [size:4][free:4][next:4]
// ---------------------------------------------------------------------------
struct HeapBlock {
    int  size;   // usable bytes (excluding this header)
    bool free;
    int  next;   // virtual addr of next HeapBlock header, -1 = none
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

    // Demand-paging handler — called by ExceptionHandler on PageFaultException.
    void HandlePageFault(unsigned int vaddr);

    // Heap management — called from ksyscall.h
    int  SysMalloc(int size);
    void SysFree(int ptr);

    // ------------------------------------------------------------------
    // TLB helpers (used by ExceptionHandler and scheduler context switch)
    // ------------------------------------------------------------------
    int                NumPages()    const { return numPages; }
    TranslationEntry  *GetPageTable()      { return pageTable; }
    TranslationEntry  *FindPTE(int vpn);   // NULL if vpn out of range
    void               SaveTLBState();     // copy use/dirty: TLB -> pageTable
    void               ClearTLB();         // invalidate every TLB slot

   private:
    TranslationEntry *pageTable;
    unsigned int      numPages;

    OpenFile  *executable;   // kept open for on-demand loading
    PageInfo  *pageInfo;     // per-page segment classification

    unsigned int heapStart;  // first virtual byte of heap (fixed after ctor)
    unsigned int heapTop;    // current top of heap (grows/shrinks)

    // Internal heap helpers
    int       SysBreak(int delta);
    bool      ShrinkHeap(unsigned int newTop);
    HeapBlock ReadBlock(int vaddr);
    void      WriteBlock(int vaddr, HeapBlock b);

    void InitRegisters();
};

#endif  // ADDRSPACE_H