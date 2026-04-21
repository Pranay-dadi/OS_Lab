#include "copyright.h"
#include "main.h"
#include "addrspace.h"
#include "machine.h"
#include "noff.h"
#include "synch.h"

// ---------------------------------------------------------------------------
// SwapHeader — byte-swap noff header to host byte order when needed.
// ---------------------------------------------------------------------------
static void SwapHeader(NoffHeader *noffH) {
    noffH->noffMagic                    = WordToHost(noffH->noffMagic);
    noffH->code.size                    = WordToHost(noffH->code.size);
    noffH->code.virtualAddr             = WordToHost(noffH->code.virtualAddr);
    noffH->code.inFileAddr              = WordToHost(noffH->code.inFileAddr);
    noffH->initData.size                = WordToHost(noffH->initData.size);
    noffH->initData.virtualAddr         = WordToHost(noffH->initData.virtualAddr);
    noffH->initData.inFileAddr          = WordToHost(noffH->initData.inFileAddr);
#ifdef RDATA
    noffH->readonlyData.size            = WordToHost(noffH->readonlyData.size);
    noffH->readonlyData.virtualAddr     = WordToHost(noffH->readonlyData.virtualAddr);
    noffH->readonlyData.inFileAddr      = WordToHost(noffH->readonlyData.inFileAddr);
#endif
    noffH->uninitData.size              = WordToHost(noffH->uninitData.size);
    noffH->uninitData.virtualAddr       = WordToHost(noffH->uninitData.virtualAddr);
    noffH->uninitData.inFileAddr        = WordToHost(noffH->uninitData.inFileAddr);
}

static inline int minInt(int a, int b) { return a < b ? a : b; }

// ---------------------------------------------------------------------------
// ClassifyPage — identify which segment owns a virtual page.
// ---------------------------------------------------------------------------
static bool ClassifyPage(int pageStart,
                         int segVA, int segSize, int segFileBase,
                         bool readOnly,
                         PageInfo *pi)
{
    if (segSize <= 0) return false;
    if (pageStart <  segVA) return false;
    if (pageStart >= segVA + segSize) return false;

    int offsetInSeg    = pageStart - segVA;
    pi->fileOffset     = segFileBase + offsetInSeg;
    pi->validBytes     = minInt(PageSize, segSize - offsetInSeg);
    pi->readOnly       = readOnly;
    return true;
}

// ===========================================================================
// AddrSpace() — default constructor
// ===========================================================================
AddrSpace::AddrSpace()
    : pageTable(NULL), numPages(0), executable(NULL), pageInfo(NULL)
{}

// ===========================================================================
// AddrSpace(fileName) — demand-paging constructor
// ===========================================================================
AddrSpace::AddrSpace(char *fileName)
    : pageTable(NULL), numPages(0), executable(NULL), pageInfo(NULL)
{
    NoffHeader   noffH;
    unsigned int size;

    executable = kernel->fileSystem->Open(fileName);
    if (executable == NULL) {
        DEBUG(dbgFile, "Cannot open executable: " << fileName);
        return;
    }

    executable->ReadAt((char *)&noffH, sizeof(noffH), 0);
    if ((noffH.noffMagic != NOFFMAGIC) &&
        (WordToHost(noffH.noffMagic) == NOFFMAGIC))
        SwapHeader(&noffH);
    ASSERT(noffH.noffMagic == NOFFMAGIC);

    DEBUG(dbgAddr, "noff segments for " << fileName << ":");
    DEBUG(dbgAddr, "  code      va=0x" << hex << noffH.code.virtualAddr
          << " size=" << dec << noffH.code.size
          << " fileOff=0x" << hex << noffH.code.inFileAddr << dec);
#ifdef RDATA
    DEBUG(dbgAddr, "  rodata    va=0x" << hex << noffH.readonlyData.virtualAddr
          << " size=" << dec << noffH.readonlyData.size
          << " fileOff=0x" << hex << noffH.readonlyData.inFileAddr << dec);
#endif
    DEBUG(dbgAddr, "  initData  va=0x" << hex << noffH.initData.virtualAddr
          << " size=" << dec << noffH.initData.size
          << " fileOff=0x" << hex << noffH.initData.inFileAddr << dec);
    DEBUG(dbgAddr, "  uninit    va=0x" << hex << noffH.uninitData.virtualAddr
          << " size=" << dec << noffH.uninitData.size << dec);

    size =  noffH.code.size
          + noffH.initData.size
          + noffH.uninitData.size
          + UserStackSize;
#ifdef RDATA
    size += noffH.readonlyData.size;
#endif

    numPages = divRoundUp(size, PageSize);
    size     = numPages * PageSize;

    // Find top of file-backed segments to locate heap start.
    unsigned int segTop = 0;
    if (noffH.code.size > 0) {
        unsigned int end = noffH.code.virtualAddr + noffH.code.size;
        if (end > segTop) segTop = end;
    }
    if (noffH.initData.size > 0) {
        unsigned int end = noffH.initData.virtualAddr + noffH.initData.size;
        if (end > segTop) segTop = end;
    }
    if (noffH.uninitData.size > 0 && noffH.uninitData.virtualAddr < 0x10000000u) {
        unsigned int end = noffH.uninitData.virtualAddr + noffH.uninitData.size;
        if (end > segTop) segTop = end;
    }
#ifdef RDATA
    if (noffH.readonlyData.size > 0) {
        unsigned int end = noffH.readonlyData.virtualAddr + noffH.readonlyData.size;
        if (end > segTop) segTop = end;
    }
#endif

    heapStart = divRoundUp(segTop, PageSize) * PageSize;
    heapTop   = heapStart;

    DEBUG(dbgAddr, "AddrSpace: " << numPages << " pages (" << size
          << " bytes) for " << fileName);

    // All PTEs start invalid — pages are loaded on demand.
    pageTable = new TranslationEntry[numPages];
    for (unsigned int i = 0; i < numPages; i++) {
        pageTable[i].virtualPage  = i;
        pageTable[i].physicalPage = (unsigned int)-1;
        pageTable[i].valid        = FALSE;
        pageTable[i].use          = FALSE;
        pageTable[i].dirty        = FALSE;
        pageTable[i].readOnly     = FALSE;
    }

    // Build per-page segment classification for HandlePageFault.
    pageInfo = new PageInfo[numPages];
    for (unsigned int i = 0; i < numPages; i++) {
        int pageStart = (int)(i * PageSize);

        if (ClassifyPage(pageStart,
                         noffH.code.virtualAddr, noffH.code.size,
                         noffH.code.inFileAddr, false, &pageInfo[i])) {
            DEBUG(dbgAddr, "  page " << i << " CODE  fileOff="
                  << pageInfo[i].fileOffset << " bytes=" << pageInfo[i].validBytes);
            continue;
        }
#ifdef RDATA
        if (ClassifyPage(pageStart,
                         noffH.readonlyData.virtualAddr, noffH.readonlyData.size,
                         noffH.readonlyData.inFileAddr, true, &pageInfo[i])) {
            DEBUG(dbgAddr, "  page " << i << " RODATA fileOff="
                  << pageInfo[i].fileOffset << " bytes=" << pageInfo[i].validBytes);
            continue;
        }
#endif
        if (ClassifyPage(pageStart,
                         noffH.initData.virtualAddr, noffH.initData.size,
                         noffH.initData.inFileAddr, false, &pageInfo[i])) {
            DEBUG(dbgAddr, "  page " << i << " DATA  fileOff="
                  << pageInfo[i].fileOffset << " bytes=" << pageInfo[i].validBytes);
            continue;
        }

        // Zero-fill: uninit data or stack
        pageInfo[i].fileOffset = -1;
        pageInfo[i].validBytes = 0;
        pageInfo[i].readOnly   = false;
        DEBUG(dbgAddr, "  page " << i << " ZERO-FILL");
    }
}

// ===========================================================================
// ~AddrSpace — release only frames that were faulted in.
// ===========================================================================
AddrSpace::~AddrSpace() {
    if (pageTable != NULL) {
        kernel->addrLock->P();
        for (unsigned int i = 0; i < numPages; i++) {
            if (pageTable[i].valid) {
                kernel->gPhysPageBitMap->Clear(pageTable[i].physicalPage);
                DEBUG(dbgAddr, "  freed phys page " << pageTable[i].physicalPage);
            }
        }
        kernel->addrLock->V();
        delete[] pageTable;
        pageTable = NULL;
    }
    delete[] pageInfo;
    pageInfo = NULL;
    if (executable != NULL) {
        delete executable;
        executable = NULL;
    }
}

// ===========================================================================
// HandlePageFault — allocate a frame and load the missing page.
//
// Called from ExceptionHandler when a PTE is invalid.
// After this returns the PTE is valid; ExceptionHandler then fills the TLB
// (in USE_TLB mode) and returns WITHOUT advancing the PC so the faulting
// instruction is retried.
// ===========================================================================
void AddrSpace::HandlePageFault(unsigned int vaddr) {
    unsigned int vpn = vaddr / PageSize;

    if (vpn >= numPages) {
        cerr << "HandlePageFault: vaddr=0x" << hex << vaddr << dec
             << " vpn=" << vpn << " out of range (numPages=" << numPages << ")\n";
        kernel->interrupt->Halt();
        return;
    }

    kernel->addrLock->P();

    // Double-check: another thread may have loaded this page while we waited.
    if (pageTable[vpn].valid) {
        kernel->addrLock->V();
        DEBUG(dbgAddr, "HandlePageFault: vpn=" << vpn
              << " already loaded (race avoided)");
        return;
    }

    int ppn = kernel->gPhysPageBitMap->FindAndSet();
    if (ppn == -1) {
        cerr << "HandlePageFault: out of physical memory!\n";
        kernel->addrLock->V();
        kernel->interrupt->Halt();
        return;
    }

    DEBUG(dbgAddr, "PageFault: vpn=" << vpn << " -> ppn=" << ppn
          << "  vaddr=0x" << hex << vaddr << dec);

    char *frame = &kernel->machine->mainMemory[ppn * PageSize];
    bzero(frame, PageSize);

    if (pageInfo[vpn].fileOffset != -1 &&
        pageInfo[vpn].validBytes  >  0  &&
        executable               != NULL)
    {
        executable->ReadAt(frame,
                           pageInfo[vpn].validBytes,
                           pageInfo[vpn].fileOffset);
        DEBUG(dbgAddr, "  loaded " << pageInfo[vpn].validBytes
              << " bytes from fileOff=" << pageInfo[vpn].fileOffset);
    }

    pageTable[vpn].physicalPage = (unsigned int)ppn;
    pageTable[vpn].valid        = TRUE;
    pageTable[vpn].use          = FALSE;
    pageTable[vpn].dirty        = FALSE;
    pageTable[vpn].readOnly     = pageInfo[vpn].readOnly;

    kernel->stats->numPageFaults++;

    kernel->addrLock->V();
}

// ===========================================================================
// TLB helpers
// ===========================================================================

// FindPTE — return pointer to pageTable[vpn], or NULL if vpn is illegal.
TranslationEntry *AddrSpace::FindPTE(int vpn) {
    if (vpn < 0 || (unsigned int)vpn >= numPages)
        return NULL;
    return &pageTable[vpn];
}

// SaveTLBState — before a process stops running, copy use/dirty bits from
// any valid TLB entries back into this address space's page table so that
// information accumulated by the hardware is not lost.
void AddrSpace::SaveTLBState() {
#ifdef USE_TLB
    Machine *machine = kernel->machine;
    for (int i = 0; i < TLBSize; i++) {
        TranslationEntry &te = machine->tlb[i];
        if (!te.valid)
            continue;
        TranslationEntry *pte = FindPTE(te.virtualPage);
        if (pte != NULL) {
            pte->use   = pte->use   || te.use;
            pte->dirty = pte->dirty || te.dirty;
        }
    }
#endif
}

// ClearTLB — invalidate every TLB slot.
// Called on context switch so the incoming process does not see stale
// translations from the outgoing one.
void AddrSpace::ClearTLB() {
#ifdef USE_TLB
    for (int i = 0; i < TLBSize; i++)
        kernel->machine->tlb[i].valid = FALSE;
#endif
}

// ===========================================================================
// Heap helpers
// ===========================================================================
HeapBlock AddrSpace::ReadBlock(int vaddr) {
    HeapBlock b;
    int word;
    if (!kernel->machine->ReadMem(vaddr,     4, &word)) kernel->machine->ReadMem(vaddr,     4, &word);
    b.size = word;
    if (!kernel->machine->ReadMem(vaddr + 4, 4, &word)) kernel->machine->ReadMem(vaddr + 4, 4, &word);
    b.free = (bool)word;
    if (!kernel->machine->ReadMem(vaddr + 8, 4, &word)) kernel->machine->ReadMem(vaddr + 8, 4, &word);
    b.next = word;
    return b;
}

void AddrSpace::WriteBlock(int vaddr, HeapBlock b) {
    if (!kernel->machine->WriteMem(vaddr,     4, b.size))       kernel->machine->WriteMem(vaddr,     4, b.size);
    if (!kernel->machine->WriteMem(vaddr + 4, 4, (int)b.free))  kernel->machine->WriteMem(vaddr + 4, 4, (int)b.free);
    if (!kernel->machine->WriteMem(vaddr + 8, 4, b.next))       kernel->machine->WriteMem(vaddr + 8, 4, b.next);
}

int AddrSpace::SysBreak(int delta) {
    unsigned int oldTop = heapTop;
    unsigned int newTop = heapTop + (unsigned int)delta;

    if (newTop >= numPages * PageSize - UserStackSize)
        return -1;

    unsigned int firstNewPage = divRoundUp(oldTop, PageSize);
    unsigned int lastNewPage  = divRoundUp(newTop, PageSize);

    kernel->addrLock->P();
    for (unsigned int p = firstNewPage; p < lastNewPage; p++) {
        if (p >= numPages) { kernel->addrLock->V(); return -1; }
        if (!pageTable[p].valid) {
            int ppn = kernel->gPhysPageBitMap->FindAndSet();
            if (ppn == -1) { kernel->addrLock->V(); return -1; }
            bzero(&kernel->machine->mainMemory[ppn * PageSize], PageSize);
            pageTable[p].physicalPage = (unsigned int)ppn;
            pageTable[p].valid        = TRUE;
            pageTable[p].readOnly     = FALSE;
            pageTable[p].use          = FALSE;
            pageTable[p].dirty        = FALSE;
        }
    }
    kernel->addrLock->V();

    heapTop = newTop;
    return (int)oldTop;
}

bool AddrSpace::ShrinkHeap(unsigned int newTop) {
    if (newTop < heapStart || newTop > heapTop)
        return false;

    unsigned int firstFreePage = divRoundUp(newTop,   PageSize);
    unsigned int lastUsedPage  = divRoundUp(heapTop,  PageSize);

    kernel->addrLock->P();
    for (unsigned int p = firstFreePage; p < lastUsedPage; p++) {
        if (p < numPages && pageTable[p].valid) {
            kernel->gPhysPageBitMap->Clear(pageTable[p].physicalPage);
            pageTable[p].valid        = FALSE;
            pageTable[p].physicalPage = (unsigned int)-1;
            DEBUG(dbgAddr, "ShrinkHeap: freed phys frame for vpage " << p);
        }
    }
    kernel->addrLock->V();

    heapTop = newTop;
    DEBUG(dbgAddr, "ShrinkHeap: heapTop now " << heapTop);
    return true;
}

int AddrSpace::SysMalloc(int size) {
    if (size <= 0) return 0;

    if (heapTop > heapStart) {
        int cur = (int)heapStart;
        while (cur != -1) {
            HeapBlock b = ReadBlock(cur);
            if (b.free && b.size >= size) {
                b.free = false;
                WriteBlock(cur, b);
                DEBUG(dbgAddr, "SysMalloc: reused block at " << cur
                      << " size=" << b.size);
                return cur + BLOCK_HEADER_SIZE;
            }
            cur = b.next;
        }
    }

    int headerAddr = SysBreak(BLOCK_HEADER_SIZE + size);
    if (headerAddr == -1) {
        DEBUG(dbgAddr, "SysMalloc: out of memory for size=" << size);
        return 0;
    }

    if ((unsigned int)headerAddr > heapStart) {
        int cur = (int)heapStart;
        while (true) {
            HeapBlock tmp = ReadBlock(cur);
            if (tmp.next == -1) {
                tmp.next = headerAddr;
                WriteBlock(cur, tmp);
                break;
            }
            cur = tmp.next;
        }
    }

    HeapBlock b;
    b.size = size;
    b.free = false;
    b.next = -1;
    WriteBlock(headerAddr, b);

    DEBUG(dbgAddr, "SysMalloc: new block at " << headerAddr
          << " size=" << size << " heapTop=" << heapTop);
    return headerAddr + BLOCK_HEADER_SIZE;
}

void AddrSpace::SysFree(int ptr) {
    if (ptr == 0) return;

    int headerAddr = ptr - BLOCK_HEADER_SIZE;

    if ((unsigned int)headerAddr < heapStart ||
        (unsigned int)headerAddr >= heapTop) {
        DEBUG(dbgAddr, "SysFree: invalid pointer " << ptr);
        return;
    }

    HeapBlock b = ReadBlock(headerAddr);
    b.free = true;
    WriteBlock(headerAddr, b);
    DEBUG(dbgAddr, "SysFree: freed block at " << headerAddr
          << " size=" << b.size);

    // Forward coalesce
    int cur = (int)heapStart;
    while (cur != -1) {
        HeapBlock curr = ReadBlock(cur);
        if (curr.free && curr.next != -1) {
            HeapBlock nxt = ReadBlock(curr.next);
            if (nxt.free) {
                curr.size += BLOCK_HEADER_SIZE + nxt.size;
                curr.next  = nxt.next;
                WriteBlock(cur, curr);
                continue;
            }
        }
        cur = curr.next;
    }

    // Tail-trim
    while (true) {
        if (heapTop == heapStart) break;

        int prev = -1;
        int cur2 = (int)heapStart;
        while (true) {
            HeapBlock tmp = ReadBlock(cur2);
            if (tmp.next == -1) break;
            prev = cur2;
            cur2 = tmp.next;
        }

        HeapBlock last = ReadBlock(cur2);
        if (!last.free) break;

        if (prev == -1) {
            ShrinkHeap(heapStart);
        } else {
            HeapBlock prevBlock = ReadBlock(prev);
            prevBlock.next = -1;
            WriteBlock(prev, prevBlock);
            ShrinkHeap((unsigned int)cur2);
        }

        DEBUG(dbgAddr, "SysFree: trimmed tail block at " << cur2
              << " heapTop now " << heapTop);
    }
}

// ===========================================================================
// Execute / InitRegisters
// ===========================================================================
void AddrSpace::Execute() {
    kernel->currentThread->space = this;
    this->InitRegisters();
    this->RestoreState();
    kernel->machine->Run();
    ASSERTNOTREACHED();
}

void AddrSpace::InitRegisters() {
    Machine *machine = kernel->machine;
    for (int i = 0; i < NumTotalRegs; i++)
        machine->WriteRegister(i, 0);
    machine->WriteRegister(PCReg,     0);
    machine->WriteRegister(NextPCReg, 4);
    machine->WriteRegister(StackReg,  numPages * PageSize - 16);
    DEBUG(dbgAddr, "Stack pointer = " << numPages * PageSize - 16);
}

// ===========================================================================
// SaveState — called by the scheduler when this process is switched OUT.
//
// With USE_TLB: flush TLB use/dirty bits back into the page table so no
// bit updates made by the hardware while this process ran are lost.
// Without USE_TLB: nothing to do (machine->pageTable already points here).
// ===========================================================================
void AddrSpace::SaveState() {
#ifdef USE_TLB
    SaveTLBState();
#endif
    // Non-TLB mode: machine->pageTable is still valid; nothing extra needed.
}

// ===========================================================================
// RestoreState — called by the scheduler when this process is switched IN.
//
// Without USE_TLB: install this process's page table in the hardware register.
// With USE_TLB:    clear the machine page-table pointer (hardware must not use
//                  it) and flush stale TLB entries from the previous process.
// ===========================================================================
void AddrSpace::RestoreState() {
#ifdef USE_TLB
    // In TLB mode the hardware translates through tlb[], not pageTable.
    // Set pageTable to NULL so any accidental use is caught immediately.
    kernel->machine->pageTable     = NULL;
    kernel->machine->pageTableSize = 0;
    // Invalidate all TLB slots — the previous process's translations must
    // not leak into this one.
    ClearTLB();
#else
    kernel->machine->pageTable     = pageTable;
    kernel->machine->pageTableSize = numPages;
#endif
}

// ===========================================================================
// Translate — software translation path (used when USE_TLB is NOT defined;
// kept for completeness and for internal helpers that call ReadMem/WriteMem).
// ===========================================================================
ExceptionType AddrSpace::Translate(unsigned int vaddr, unsigned int *paddr,
                                   int isReadWrite) {
    unsigned int vpn    = vaddr / PageSize;
    unsigned int offset = vaddr % PageSize;

    if (vpn >= numPages)
        return AddressErrorException;

    TranslationEntry *pte = &pageTable[vpn];

    if (!pte->valid)
        return PageFaultException;

    if (isReadWrite && pte->readOnly)
        return ReadOnlyException;

    unsigned int pfn = pte->physicalPage;
    if (pfn >= (unsigned int)NumPhysPages) {
        DEBUG(dbgAddr, "Illegal physical page " << pfn);
        return BusErrorException;
    }

    pte->use = TRUE;
    if (isReadWrite) pte->dirty = TRUE;

    *paddr = pfn * PageSize + offset;
    ASSERT(*paddr < (unsigned int)MemorySize);
    return NoException;
}