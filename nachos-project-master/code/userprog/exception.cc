#include "copyright.h"
#include "main.h"
#include "syscall.h"
#include "ksyscall.h"
// openfile_pipe.h intentionally NOT included — pipe dispatch is handled
// entirely through gPipeTable inside SysRead / SysWrite / SysClose.

// ---------------------------------------------------------------------------
// Utility: copy a C-string from user space into a kernel-allocated buffer.
// ---------------------------------------------------------------------------
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
// DEMAND PAGING:
//   PageFaultException is caught here. We call HandlePageFault() which
//   allocates a physical frame and loads the missing page from the
//   executable. We then RETURN WITHOUT advancing the PC, so Machine::Run()
//   retries the faulting instruction — this time finding a valid PTE.
// ===========================================================================
void ExceptionHandler(ExceptionType which) {
    int type = kernel->machine->ReadRegister(2);
    DEBUG(dbgSys, "Exception " << which << " type=" << type << "\n");

    switch (which) {

        // -------------------------------------------------------------------
        case NoException:
            kernel->interrupt->setStatus(SystemMode);
            DEBUG(dbgSys, "Switch to system mode\n");
            return;   // return — do NOT fall through to ASSERTNOTREACHED()

        // -------------------------------------------------------------------
        // DEMAND PAGING — handle transparently, retry the instruction
        // -------------------------------------------------------------------
        case PageFaultException: {
            unsigned int badVAddr =
                (unsigned int)kernel->machine->ReadRegister(BadVAddrReg);
            DEBUG(dbgAddr, "PageFaultException vaddr=" << badVAddr);

            if (kernel->currentThread->space == NULL) {
                cerr << "PageFaultException with no address space!\n";
                SysHalt();
                ASSERTNOTREACHED();
            }

            // Load the missing page; page table entry becomes valid.
            kernel->currentThread->space->HandlePageFault(badVAddr);

            // Do NOT call move_program_counter().
            // The PC still points at the faulting instruction.
            // Machine::Run() will retry it now that the page is present.
            return;
        }

        // -------------------------------------------------------------------
        // Fatal hardware exceptions
        // -------------------------------------------------------------------
        case ReadOnlyException:
            cerr << "ReadOnlyException vaddr="
                 << kernel->machine->ReadRegister(BadVAddrReg) << "\n";
            SysHalt(); ASSERTNOTREACHED();

        case BusErrorException:
            cerr << "BusErrorException vaddr="
                 << kernel->machine->ReadRegister(BadVAddrReg) << "\n";
            SysHalt(); ASSERTNOTREACHED();

        case AddressErrorException:
            cerr << "AddressErrorException vaddr="
                 << kernel->machine->ReadRegister(BadVAddrReg) << "\n";
            SysHalt(); ASSERTNOTREACHED();

        case OverflowException:
            cerr << "OverflowException\n";
            SysHalt(); ASSERTNOTREACHED();

        case IllegalInstrException:
            cerr << "IllegalInstrException\n";
            SysHalt(); ASSERTNOTREACHED();

        case NumExceptionTypes:
            cerr << "NumExceptionTypes exception\n";
            SysHalt(); ASSERTNOTREACHED();

        // -------------------------------------------------------------------
        // System calls
        // -------------------------------------------------------------------
        case SyscallException:
            switch (type) {
                case SC_Halt:             return handle_SC_Halt();
                case SC_Add:              return handle_SC_Add();
                case SC_Abs:             return handle_SC_Abs();
                case SC_Sleep:            return handle_SC_Sleep();

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

                case SC_ReadNum:          return handle_SC_ReadNum();
                case SC_PrintNum:         return handle_SC_PrintNum();
                case SC_ReadChar:         return handle_SC_ReadChar();
                case SC_PrintChar:        return handle_SC_PrintChar();
                case SC_RandomNum:        return handle_SC_RandomNum();
                case SC_ReadString:       return handle_SC_ReadString();
                case SC_PrintString:      return handle_SC_PrintString();
                case SC_CreateFile:       return handle_SC_CreateFile();
                case SC_Open:             return handle_SC_Open();
                case SC_Close:            return handle_SC_Close();
                case SC_Read:             return handle_SC_Read();
                case SC_Write:            return handle_SC_Write();
                case SC_Seek:             return handle_SC_Seek();
                case SC_Exec:             return handle_SC_Exec();
                case SC_Join:             return handle_SC_Join();
                case SC_Exit:             return handle_SC_Exit();
                case SC_CreateSemaphore:  return handle_SC_CreateSemaphore();
                case SC_Wait:             return handle_SC_Wait();
                case SC_Signal:           return handle_SC_Signal();
                case SC_GetPid:           return handle_SC_GetPid();

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
                    SysHalt();
                    ASSERTNOTREACHED();
            }
            break;

        default:
            cerr << "Unexpected exception " << (int)which << "\n";
            SysHalt();
            ASSERTNOTREACHED();
    }
    ASSERTNOTREACHED();
}