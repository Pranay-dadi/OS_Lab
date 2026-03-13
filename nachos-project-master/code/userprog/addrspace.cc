#include "copyright.h"
#include "main.h"
#include "addrspace.h"
#include "machine.h"
#include "noff.h"
#include "synch.h"

// ---------------------------------------------------------------------------
// SwapHeader — byte-swap noff header to host byte order when needed.
// Handles the optional readonlyData segment (compiled in with -DRDATA).
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
// Helper: classify one virtual page against one noff segment.
//
//   pageStart   — first virtual byte of the page  (i * PageSize)
//   segVA       — segment's virtualAddr field
//   segSize     — segment's size field
//   segFileBase — segment's inFileAddr field
//   readOnly    — TRUE for the readonlyData segment
//   pi          — PageInfo to fill in on a hit
//
// Returns TRUE if the page belongs to this segment (pi filled in),
// FALSE otherwise (pi unchanged).
// ---------------------------------------------------------------------------
static bool ClassifyPage(int pageStart,
                         int segVA, int segSize, int segFileBase,
                         bool readOnly,
                         PageInfo *pi)
{
    if (segSize <= 0) return false;                    // segment is empty
    if (pageStart <  segVA) return false;              // page starts before seg
    if (pageStart >= segVA + segSize) return false;    // page starts after seg

    int offsetInSeg    = pageStart - segVA;
    pi->fileOffset     = segFileBase + offsetInSeg;
    pi->validBytes     = minInt(PageSize, segSize - offsetInSeg);
    pi->readOnly       = readOnly;
    return true;
}

// ===========================================================================
// AddrSpace()  — default constructor (unused path; kept for compatibility)
// ===========================================================================
AddrSpace::AddrSpace()
    : pageTable(NULL), numPages(0), executable(NULL), pageInfo(NULL)
{}

// ===========================================================================
// AddrSpace(fileName)  — DEMAND-PAGING constructor
//
//  • Reads the noff header to learn segment layout.
//  • Allocates the page table; marks every PTE invalid.
//  • Builds pageInfo[] so HandlePageFault knows where each page lives.
//  • Does NOT allocate any physical frames yet.
//  • Keeps the executable file open for on-demand loading.
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

    // Read and validate the noff header
    executable->ReadAt((char *)&noffH, sizeof(noffH), 0);
    if ((noffH.noffMagic != NOFFMAGIC) &&
        (WordToHost(noffH.noffMagic) == NOFFMAGIC))
        SwapHeader(&noffH);
    ASSERT(noffH.noffMagic == NOFFMAGIC);

    // -----------------------------------------------------------------------
    // Print segment map for debugging
    // -----------------------------------------------------------------------
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

    // -----------------------------------------------------------------------
    // Compute virtual address space size
    // -----------------------------------------------------------------------
    size =  noffH.code.size
          + noffH.initData.size
          + noffH.uninitData.size
          + UserStackSize;
#ifdef RDATA
    size += noffH.readonlyData.size;
#endif

    numPages = divRoundUp(size, PageSize);
    size     = numPages * PageSize;

    DEBUG(dbgAddr, "AddrSpace: " << numPages << " pages (" << size
          << " bytes) for " << fileName);

    // -----------------------------------------------------------------------
    // Allocate page table — all entries INVALID until HandlePageFault fires
    // -----------------------------------------------------------------------
    pageTable = new TranslationEntry[numPages];
    for (unsigned int i = 0; i < numPages; i++) {
        pageTable[i].virtualPage  = i;
        pageTable[i].physicalPage = (unsigned int)-1;  // invalid sentinel
        pageTable[i].valid        = FALSE;
        pageTable[i].use          = FALSE;
        pageTable[i].dirty        = FALSE;
        pageTable[i].readOnly     = FALSE;
    }

    // -----------------------------------------------------------------------
    // Build pageInfo[] — classify every virtual page into one segment.
    //
    // Priority order matters for pages that could straddle boundaries:
    //   1. code (execute permission)
    //   2. readonlyData (.rodata — string literals live here with -DRDATA)
    //   3. initData (.data)
    //   4. zero-fill (uninitData, stack)
    // -----------------------------------------------------------------------
    pageInfo = new PageInfo[numPages];

    for (unsigned int i = 0; i < numPages; i++) {
        int pageStart = (int)(i * PageSize);

        // --- 1. code segment ---
        if (ClassifyPage(pageStart,
                         noffH.code.virtualAddr,
                         noffH.code.size,
                         noffH.code.inFileAddr,
                         false,
                         &pageInfo[i])) {
            DEBUG(dbgAddr, "  page " << i << " CODE  fileOff="
                  << pageInfo[i].fileOffset
                  << " bytes=" << pageInfo[i].validBytes);
            continue;
        }

#ifdef RDATA
        // --- 2. read-only data segment (.rodata / string literals) ---
        if (ClassifyPage(pageStart,
                         noffH.readonlyData.virtualAddr,
                         noffH.readonlyData.size,
                         noffH.readonlyData.inFileAddr,
                         true,   // readOnly = TRUE
                         &pageInfo[i])) {
            DEBUG(dbgAddr, "  page " << i << " RODATA fileOff="
                  << pageInfo[i].fileOffset
                  << " bytes=" << pageInfo[i].validBytes);
            continue;
        }
#endif

        // --- 3. initialised data segment (.data) ---
        if (ClassifyPage(pageStart,
                         noffH.initData.virtualAddr,
                         noffH.initData.size,
                         noffH.initData.inFileAddr,
                         false,
                         &pageInfo[i])) {
            DEBUG(dbgAddr, "  page " << i << " DATA  fileOff="
                  << pageInfo[i].fileOffset
                  << " bytes=" << pageInfo[i].validBytes);
            continue;
        }

        // --- 4. zero-fill: uninitialised data or stack ---
        pageInfo[i].fileOffset = -1;
        pageInfo[i].validBytes = 0;
        pageInfo[i].readOnly   = false;
        DEBUG(dbgAddr, "  page " << i << " ZERO-FILL");
    }

    // Executable file stays open — HandlePageFault reads from it on demand.
    // addrLock / gPhysPageBitMap are NOT touched here (no frames yet).
}

// ===========================================================================
// ~AddrSpace
//
// Release only the physical frames that were actually faulted in (valid PTEs).
// Close the executable.
// ===========================================================================
AddrSpace::~AddrSpace() {
    if (pageTable != NULL) {
        kernel->addrLock->P();
        for (unsigned int i = 0; i < numPages; i++) {
            if (pageTable[i].valid) {
                kernel->gPhysPageBitMap->Clear(pageTable[i].physicalPage);
                DEBUG(dbgAddr, "  freed phys page "
                      << pageTable[i].physicalPage);
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
// HandlePageFault
//
// Called from ExceptionHandler when Translate() returns PageFaultException.
// Steps:
//   1. Derive VPN from the faulting virtual address.
//   2. Acquire addrLock to serialise frame allocation.
//   3. Double-check: another context may have loaded this page while we waited.
//   4. FindAndSet() a free physical frame.
//   5. Zero the frame.
//   6. If page is file-backed, read validBytes from the executable.
//   7. Mark PTE valid.
//   8. Increment the page-fault statistics counter.
//   9. Release addrLock and return.
//
// The PC is NOT advanced by ExceptionHandler, so Machine::Run() will retry
// the faulting instruction and this time Translate() will succeed.
// ===========================================================================
void AddrSpace::HandlePageFault(unsigned int vaddr) {
    unsigned int vpn = vaddr / PageSize;

    // Sanity-check
    if (vpn >= numPages) {
        cerr << "HandlePageFault: vaddr=0x" << hex << vaddr << dec
             << " vpn=" << vpn << " out of range (numPages=" << numPages << ")\n";
        kernel->interrupt->Halt();
        return;
    }

    // Serialise frame allocation
    kernel->addrLock->P();

    // Double-check: page may have been loaded by another thread while waiting
    if (pageTable[vpn].valid) {
        kernel->addrLock->V();
        DEBUG(dbgAddr, "HandlePageFault: vpn=" << vpn
              << " already loaded (race avoided)");
        return;
    }

    // Allocate a free physical frame
    int ppn = kernel->gPhysPageBitMap->FindAndSet();
    if (ppn == -1) {
        cerr << "HandlePageFault: out of physical memory! "
             << "(page replacement not yet implemented)\n";
        kernel->addrLock->V();
        kernel->interrupt->Halt();
        return;
    }

    DEBUG(dbgAddr, "PageFault: vpn=" << vpn << " -> ppn=" << ppn
          << "  vaddr=0x" << hex << vaddr << dec);

    // Zero the entire frame (handles stack / uninit data and pads partial pages)
    char *frame = &kernel->machine->mainMemory[ppn * PageSize];
    bzero(frame, PageSize);

    // Load file-backed content if this page has any
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

    // Update the PTE
    pageTable[vpn].physicalPage = (unsigned int)ppn;
    pageTable[vpn].valid        = TRUE;
    pageTable[vpn].use          = FALSE;
    pageTable[vpn].dirty        = FALSE;
    pageTable[vpn].readOnly     = pageInfo[vpn].readOnly;

    // Increment the NachOS page-fault statistics counter
    kernel->stats->numPageFaults++;

    kernel->addrLock->V();
}

// ===========================================================================
// Execute / InitRegisters / SaveState / RestoreState / Translate
// (unchanged in structure; Translate returns PageFaultException for invalid
//  pages, which triggers HandlePageFault via ExceptionHandler)
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
    // Stack pointer at top of address space minus a safety margin
    machine->WriteRegister(StackReg, numPages * PageSize - 16);
    DEBUG(dbgAddr, "Stack pointer = " << numPages * PageSize - 16);
}

void AddrSpace::SaveState() {}

void AddrSpace::RestoreState() {
    kernel->machine->pageTable     = pageTable;
    kernel->machine->pageTableSize = numPages;
}

ExceptionType AddrSpace::Translate(unsigned int vaddr, unsigned int *paddr,
                                   int isReadWrite) {
    unsigned int vpn    = vaddr / PageSize;
    unsigned int offset = vaddr % PageSize;

    if (vpn >= numPages)
        return AddressErrorException;

    TranslationEntry *pte = &pageTable[vpn];

    if (!pte->valid)
        return PageFaultException;   // handled by ExceptionHandler

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