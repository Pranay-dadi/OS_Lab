#ifndef ADDRSPACE_H
#define ADDRSPACE_H

#include "copyright.h"
#include "filesys.h"

#define UserStackSize 1024  // increase this as necessary!

// ---------------------------------------------------------------------------
// PageInfo — records where a virtual page's content lives in the noff file.
//
//   fileOffset == -1  →  zero-fill (uninitialised data or stack)
//   validBytes        →  how many bytes to copy from the file into the frame;
//                         the rest of the PageSize frame is zeroed.
//   readOnly          →  TRUE for pages from the readonlyData (.rodata) segment
// ---------------------------------------------------------------------------
struct PageInfo {
    int  fileOffset;  // byte offset inside the noff executable (-1 = zero-fill)
    int  validBytes;  // bytes to read from the file  (0 .. PageSize)
    bool readOnly;    // set for read-only data segment pages
};

class AddrSpace {
   public:
    AddrSpace();                // default constructor (kept for compatibility)
    AddrSpace(char *fileName);  // build page table from noff file (demand paging)
    ~AddrSpace();               // free physical frames that were allocated

    void Execute();       // hand control to the user program
    void SaveState();     // save / restore address-space state on context switch
    void RestoreState();

    // Translate vaddr → paddr.  Returns PageFaultException when the page is
    // not yet resident; ExceptionHandler will call HandlePageFault and retry.
    ExceptionType Translate(unsigned int vaddr, unsigned int *paddr, int mode);

    // Load the faulting virtual page into a free physical frame.
    // Called by ExceptionHandler on PageFaultException.
    void HandlePageFault(unsigned int vaddr);

   private:
    TranslationEntry *pageTable;  // one entry per virtual page
    unsigned int      numPages;   // total number of virtual pages

    OpenFile  *executable;  // kept open so pages can be loaded on demand
    PageInfo  *pageInfo;    // one PageInfo per virtual page

    void InitRegisters();   // initialise user-level CPU registers
};

#endif  // ADDRSPACE_H