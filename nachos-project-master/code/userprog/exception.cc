#include "copyright.h"
#include "main.h"
#include "syscall.h"
#include "ksyscall.h"

// ===========================================================================
// TLB statistics counters
//
// These are global to this translation unit.  PrintTLBStats() prints them
// just before the machine halts.
// ===========================================================================
#ifdef USE_TLB
static int gTLBMissCount        = 0;  // total TLB misses handled
static int gTLBInvalidFillCount = 0;  // misses filled into an invalid slot
static int gTLBReplaceCount     = 0;  // misses that evicted a valid slot
static int gTLBDirtyBackCount   = 0;  // times a dirty bit was saved on evict

// Round-robin replacement cursor
static int nextTLBSlot = 0;

// ---------------------------------------------------------------------------
// SelectTLBVictim — choose a TLB slot to overwrite.
//   Prefer an invalid (empty) slot; fall back to round-robin.
// ---------------------------------------------------------------------------
static int SelectTLBVictim() {
    for (int i = 0; i < TLBSize; i++) {
        if (!kernel->machine->tlb[i].valid) {
            gTLBInvalidFillCount++;
            return i;
        }
    }
    // All slots are valid — round-robin eviction.
    gTLBReplaceCount++;
    int victim = nextTLBSlot;
    nextTLBSlot = (nextTLBSlot + 1) % TLBSize;
    return victim;
}

// ---------------------------------------------------------------------------
// SaveBackTLBEntry — before overwriting a TLB slot, copy its use/dirty bits
//                    back into the owning page-table entry.
// ---------------------------------------------------------------------------
static void SaveBackTLBEntry(AddrSpace *space, TranslationEntry &te) {
    if (!te.valid || space == NULL)
        return;
    TranslationEntry *pte = space->FindPTE(te.virtualPage);
    if (pte != NULL) {
        if (te.dirty && !pte->dirty)
            gTLBDirtyBackCount++;
        pte->use   = pte->use   || te.use;
        pte->dirty = pte->dirty || te.dirty;
    }
}

// ---------------------------------------------------------------------------
// PrintTLBStats — called just before the machine halts.
// The guard ensures it prints exactly once even if multiple exit paths fire.
// ---------------------------------------------------------------------------
static bool gStatsPrinted = false;
static void PrintTLBStats() {
    if (gStatsPrinted) return;
    gStatsPrinted = true;
    cout << "\n===== TLB Statistics =====\n";
    cout << "  Total TLB misses         : " << gTLBMissCount        << "\n";
    cout << "  Filled into invalid slot : " << gTLBInvalidFillCount  << "\n";
    cout << "  Evicted valid slot       : " << gTLBReplaceCount      << "\n";
    cout << "  Dirty bits saved on evict: " << gTLBDirtyBackCount    << "\n";
    cout << "==========================\n";
}
#else
// Stubs so the rest of the file compiles without USE_TLB.
static void PrintTLBStats() {}
#endif  // USE_TLB

// ===========================================================================
// Utility: copy a C-string from user space into a kernel-allocated buffer.
// ===========================================================================
char* stringUser2System(int addr, int convert_length = -1) {
    int  length = 0;
    bool stop   = false;
    char *str;
    do {
        int oneChar;
        kernel->machine->ReadMem(addr + length, 1, &oneChar);
        length++;
        stop = ((oneChar == '\0' && convert_length == -1) ||
                length == convert_length);
    } while (!stop);
    str = new char[length];
    for (int i = 0; i < length; i++) {
        int oneChar;
        kernel->machine->ReadMem(addr + i, 1, &oneChar);
        str[i] = (unsigned char)oneChar;
    }
    return str;
}

void StringSys2User(char *str, int addr, int convert_length = -1) {
    int length = (convert_length == -1 ? (int)strlen(str) : convert_length);
    for (int i = 0; i < length; i++)
        kernel->machine->WriteMem(addr + i, 1, str[i]);
    kernel->machine->WriteMem(addr + length, 1, '\0');
}

// ---------------------------------------------------------------------------
// Advance the PC past the current syscall instruction.
// MUST NOT be called for page faults — the faulting instruction must retry.
// ---------------------------------------------------------------------------
void move_program_counter() {
    kernel->machine->WriteRegister(PrevPCReg,
                                   kernel->machine->ReadRegister(PCReg));
    kernel->machine->WriteRegister(PCReg,
                                   kernel->machine->ReadRegister(NextPCReg));
    kernel->machine->WriteRegister(NextPCReg,
                                   kernel->machine->ReadRegister(NextPCReg) + 4);
}

void handle_not_implemented_SC(int type) {
    DEBUG(dbgSys, "Not yet implemented syscall " << type << "\n");
    return move_program_counter();
}

void handle_SC_Halt() {
    DEBUG(dbgSys, "Shutdown, initiated by user program.\n");
    PrintTLBStats();
    SysHalt();
    ASSERTNOTREACHED();
}

void handle_SC_Add() {
    int result = SysAdd((int)kernel->machine->ReadRegister(4),
                        (int)kernel->machine->ReadRegister(5));
    kernel->machine->WriteRegister(2, result);
    return move_program_counter();
}

void handle_SC_Abs() {
    int result = SysAbs((int)kernel->machine->ReadRegister(4));
    kernel->machine->WriteRegister(2, result);
    return move_program_counter();
}

void handle_SC_Sleep() {
    SysSleep(kernel->machine->ReadRegister(4));
    return move_program_counter();
}

void handle_SC_Malloc() {
    int size = kernel->machine->ReadRegister(4);
    kernel->machine->WriteRegister(2, SysMalloc(size));
    return move_program_counter();
}

void handle_SC_Free() {
    SysFree(kernel->machine->ReadRegister(4));
    kernel->machine->WriteRegister(2, 0);
    return move_program_counter();
}

void handle_SC_ReadNum() {
    kernel->machine->WriteRegister(2, SysReadNum());
    return move_program_counter();
}

void handle_SC_PrintNum() {
    SysPrintNum(kernel->machine->ReadRegister(4));
    return move_program_counter();
}

void handle_SC_ReadChar() {
    kernel->machine->WriteRegister(2, (int)SysReadChar());
    return move_program_counter();
}

void handle_SC_PrintChar() {
    SysPrintChar((char)kernel->machine->ReadRegister(4));
    return move_program_counter();
}

void handle_SC_RandomNum() {
    kernel->machine->WriteRegister(2, SysRandomNum());
    return move_program_counter();
}

#define MAX_READ_STRING_LENGTH 255
void handle_SC_ReadString() {
    int memPtr = kernel->machine->ReadRegister(4);
    int length = kernel->machine->ReadRegister(5);
    if (length > MAX_READ_STRING_LENGTH) { SysHalt(); }
    char *buffer = SysReadString(length);
    StringSys2User(buffer, memPtr);
    delete[] buffer;
    return move_program_counter();
}

void handle_SC_PrintString() {
    int   memPtr = kernel->machine->ReadRegister(4);
    char *buffer = stringUser2System(memPtr);
    SysPrintString(buffer, strlen(buffer));
    delete[] buffer;
    return move_program_counter();
}

void handle_SC_CreateFile() {
    int   virtAddr = kernel->machine->ReadRegister(4);
    char *fileName = stringUser2System(virtAddr);
    kernel->machine->WriteRegister(2, SysCreateFile(fileName) ? 0 : -1);
    delete[] fileName;
    return move_program_counter();
}

void handle_SC_Open() {
    int   virtAddr = kernel->machine->ReadRegister(4);
    char *fileName = stringUser2System(virtAddr);
    int   type     = kernel->machine->ReadRegister(5);
    kernel->machine->WriteRegister(2, SysOpen(fileName, type));
    delete fileName;
    return move_program_counter();
}

void handle_SC_Close() {
    kernel->machine->WriteRegister(2,
        SysClose(kernel->machine->ReadRegister(4)));
    return move_program_counter();
}

void handle_SC_Read() {
    int   virtAddr  = kernel->machine->ReadRegister(4);
    int   charCount = kernel->machine->ReadRegister(5);
    char *buffer    = stringUser2System(virtAddr, charCount);
    int   fileId    = kernel->machine->ReadRegister(6);
    kernel->machine->WriteRegister(2, SysRead(buffer, charCount, fileId));
    StringSys2User(buffer, virtAddr, charCount);
    delete[] buffer;
    return move_program_counter();
}

void handle_SC_Write() {
    int   virtAddr  = kernel->machine->ReadRegister(4);
    int   charCount = kernel->machine->ReadRegister(5);
    char *buffer    = stringUser2System(virtAddr, charCount);
    int   fileId    = kernel->machine->ReadRegister(6);
    kernel->machine->WriteRegister(2, SysWrite(buffer, charCount, fileId));
    StringSys2User(buffer, virtAddr, charCount);
    delete[] buffer;
    return move_program_counter();
}

void handle_SC_Seek() {
    kernel->machine->WriteRegister(2,
        SysSeek(kernel->machine->ReadRegister(4),
                kernel->machine->ReadRegister(5)));
    return move_program_counter();
}

void handle_SC_Exec() {
    int   virtAddr = kernel->machine->ReadRegister(4);
    char *name     = stringUser2System(virtAddr);
    if (name == NULL) {
        kernel->machine->WriteRegister(2, -1);
        return move_program_counter();
    }
    kernel->machine->WriteRegister(2, SysExec(name));
    return move_program_counter();
}

void handle_SC_Join() {
    kernel->machine->WriteRegister(2,
        SysJoin(kernel->machine->ReadRegister(4)));
    return move_program_counter();
}

void handle_SC_Exit() {
    // Print stats when the last (or only) process exits via exit().
    // The gStatsPrinted guard in PrintTLBStats prevents double-printing
    // when both SC_Exit and SC_Halt are reached (e.g. multi-process runs).
    PrintTLBStats();
    kernel->machine->WriteRegister(2,
        SysExit(kernel->machine->ReadRegister(4)));
    return move_program_counter();
}

void handle_SC_CreateSemaphore() {
    int   virtAddr = kernel->machine->ReadRegister(4);
    int   semval   = kernel->machine->ReadRegister(5);
    char *name     = stringUser2System(virtAddr);
    if (name == NULL) {
        kernel->machine->WriteRegister(2, -1);
        return move_program_counter();
    }
    kernel->machine->WriteRegister(2, SysCreateSemaphore(name, semval));
    delete[] name;
    return move_program_counter();
}

void handle_SC_Wait() {
    int   virtAddr = kernel->machine->ReadRegister(4);
    char *name     = stringUser2System(virtAddr);
    if (name == NULL) {
        kernel->machine->WriteRegister(2, -1);
        return move_program_counter();
    }
    kernel->machine->WriteRegister(2, SysWait(name));
    delete[] name;
    return move_program_counter();
}

void handle_SC_Signal() {
    int   virtAddr = kernel->machine->ReadRegister(4);
    char *name     = stringUser2System(virtAddr);
    if (name == NULL) {
        kernel->machine->WriteRegister(2, -1);
        return move_program_counter();
    }
    kernel->machine->WriteRegister(2, SysSignal(name));
    delete[] name;
    return move_program_counter();
}

void handle_SC_GetPid() {
    kernel->machine->WriteRegister(2, SysGetPid());
    return move_program_counter();
}

// ===========================================================================
// ExceptionHandler
//
// PageFaultException handling covers two cases:
//
//   CASE A — PTE is INVALID (page not yet in memory):
//     Call HandlePageFault() to allocate a frame and load the page.
//     Then (in USE_TLB mode) install the now-valid PTE into the TLB.
//     Return WITHOUT advancing the PC — the faulting instruction retries.
//
//   CASE B — PTE is VALID but translation not in TLB (pure TLB miss):
//     (Only possible in USE_TLB mode.)
//     Install the PTE directly into the TLB.
//     Return WITHOUT advancing the PC.
//
// Both cases end with the TLB populated and the PC unchanged.
// ===========================================================================
void ExceptionHandler(ExceptionType which) {
    int type = kernel->machine->ReadRegister(2);
    DEBUG(dbgSys, "Exception " << which << " type=" << type << "\n");

    switch (which) {

        // -------------------------------------------------------------------
        case NoException:
            kernel->interrupt->setStatus(SystemMode);
            DEBUG(dbgSys, "Switch to system mode\n");
            return;

        // -------------------------------------------------------------------
        // PageFaultException — unified handler for TLB miss + demand paging
        // -------------------------------------------------------------------
        case PageFaultException: {
            unsigned int badVAddr =
                (unsigned int)kernel->machine->ReadRegister(BadVAddrReg);
            DEBUG(dbgAddr, "PageFaultException vaddr=0x" << hex << badVAddr << dec);

            AddrSpace *space = kernel->currentThread->space;
            if (space == NULL) {
                cerr << "PageFaultException with no address space!\n";
                PrintTLBStats();
                SysHalt();
                ASSERTNOTREACHED();
            }

            int vpn = (int)((unsigned int)badVAddr / PageSize);

            // --- Step 1: bounds check ---
            TranslationEntry *pte = space->FindPTE(vpn);
            if (pte == NULL) {
                cerr << "Illegal virtual address 0x" << hex << badVAddr << dec
                     << " (vpn=" << vpn << " out of range)\n";
                PrintTLBStats();
                SysHalt();
                ASSERTNOTREACHED();
            }

            // --- Step 2: if page not in memory, demand-load it ---
            if (!pte->valid) {
                DEBUG(dbgAddr, "  demand-loading vpn=" << vpn);
                space->HandlePageFault(badVAddr);
                // pte->valid is now TRUE (HandlePageFault set it).
                // pte pointer still valid — it points into space's pageTable.
            }

            // --- Step 3 (USE_TLB only): fill TLB slot ---
#ifdef USE_TLB
            gTLBMissCount++;

            int victim = SelectTLBVictim();
            // Save use/dirty bits of the evicted entry before overwriting.
            SaveBackTLBEntry(space, kernel->machine->tlb[victim]);

            // Install translation.
            kernel->machine->tlb[victim]       = *pte;
            kernel->machine->tlb[victim].valid = TRUE;

            DEBUG(dbgAddr, "  TLB[" << victim << "] <- vpn=" << pte->virtualPage
                  << " ppn=" << pte->physicalPage);
#endif

            // Do NOT call move_program_counter().
            // The faulting instruction is retried automatically.
            return;
        }

        // -------------------------------------------------------------------
        // Fatal hardware exceptions
        // -------------------------------------------------------------------
        case ReadOnlyException:
            cerr << "ReadOnlyException vaddr="
                 << kernel->machine->ReadRegister(BadVAddrReg) << "\n";
            PrintTLBStats();
            SysHalt();
            ASSERTNOTREACHED();

        case BusErrorException:
            cerr << "BusErrorException vaddr="
                 << kernel->machine->ReadRegister(BadVAddrReg) << "\n";
            PrintTLBStats();
            SysHalt();
            ASSERTNOTREACHED();

        case AddressErrorException:
            cerr << "AddressErrorException vaddr="
                 << kernel->machine->ReadRegister(BadVAddrReg) << "\n";
            PrintTLBStats();
            SysHalt();
            ASSERTNOTREACHED();

        case OverflowException:
            cerr << "OverflowException\n";
            PrintTLBStats();
            SysHalt();
            ASSERTNOTREACHED();

        case IllegalInstrException:
            cerr << "IllegalInstrException\n";
            PrintTLBStats();
            SysHalt();
            ASSERTNOTREACHED();

        case NumExceptionTypes:
            cerr << "NumExceptionTypes exception\n";
            PrintTLBStats();
            SysHalt();
            ASSERTNOTREACHED();

        // -------------------------------------------------------------------
        // System calls
        // -------------------------------------------------------------------
        case SyscallException:
            switch (type) {
                case SC_Halt:            return handle_SC_Halt();
                case SC_Add:             return handle_SC_Add();
                case SC_Abs:             return handle_SC_Abs();
                case SC_Sleep:           return handle_SC_Sleep();

                case SC_Pipe: {
                    int readFDptr  = kernel->machine->ReadRegister(4);
                    int writeFDptr = kernel->machine->ReadRegister(5);
                    int readFD = -1, writeFD = -1;
                    int result = SysPipe(&readFD, &writeFD);
                    if (result == 0) {
                        kernel->machine->WriteMem(readFDptr,  4, readFD);
                        kernel->machine->WriteMem(writeFDptr, 4, writeFD);
                        kernel->machine->WriteRegister(2, 0);
                    } else {
                        kernel->machine->WriteRegister(2, -1);
                    }
                    kernel->machine->WriteRegister(PrevPCReg,
                        kernel->machine->ReadRegister(PCReg));
                    kernel->machine->WriteRegister(PCReg,
                        kernel->machine->ReadRegister(NextPCReg));
                    kernel->machine->WriteRegister(NextPCReg,
                        kernel->machine->ReadRegister(NextPCReg) + 4);
                    return;
                }

                case SC_Malloc:          return handle_SC_Malloc();
                case SC_Free:            return handle_SC_Free();
                case SC_ReadNum:         return handle_SC_ReadNum();
                case SC_PrintNum:        return handle_SC_PrintNum();
                case SC_ReadChar:        return handle_SC_ReadChar();
                case SC_PrintChar:       return handle_SC_PrintChar();
                case SC_RandomNum:       return handle_SC_RandomNum();
                case SC_ReadString:      return handle_SC_ReadString();
                case SC_PrintString:     return handle_SC_PrintString();
                case SC_CreateFile:      return handle_SC_CreateFile();
                case SC_Open:            return handle_SC_Open();
                case SC_Close:           return handle_SC_Close();
                case SC_Read:            return handle_SC_Read();
                case SC_Write:           return handle_SC_Write();
                case SC_Seek:            return handle_SC_Seek();
                case SC_Exec:            return handle_SC_Exec();
                case SC_Join:            return handle_SC_Join();
                case SC_Exit:            return handle_SC_Exit();
                case SC_CreateSemaphore: return handle_SC_CreateSemaphore();
                case SC_Wait:            return handle_SC_Wait();
                case SC_Signal:          return handle_SC_Signal();
                case SC_GetPid:          return handle_SC_GetPid();

                case SC_Create:
                case SC_Remove:
                case SC_ThreadFork:
                case SC_ThreadYield:
                case SC_ExecV:
                case SC_ThreadExit:
                case SC_ThreadJoin:
                    return handle_not_implemented_SC(type);

                default:
                    cerr << "Unexpected syscall " << type << "\n";
                    PrintTLBStats();
                    SysHalt();
                    ASSERTNOTREACHED();
            }
            break;

        default:
            cerr << "Unexpected exception " << (int)which << "\n";
            PrintTLBStats();
            SysHalt();
            ASSERTNOTREACHED();
    }
    ASSERTNOTREACHED();
}