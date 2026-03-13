/* test/pipetest.c
 * Tests the Pipe system call.
 * Written in C89 style (all declarations before statements)
 * to be compatible with gcc 2.95.2 used by the NachOS cross-compiler.
 */

#include "syscall.h"

#define BUFSIZE 64

int main()
{
    /* ALL declarations must come first in C89 */
    int readFD;
    int writeFD;
    int written;
    int bytesRead;
    char msg[18];
    char buf[BUFSIZE];

    /* Build message manually (no string literals assigned to char[]) */
    msg[0]='H'; msg[1]='e'; msg[2]='l'; msg[3]='l'; msg[4]='o';
    msg[5]=' '; msg[6]='f'; msg[7]='r'; msg[8]='o'; msg[9]='m';
    msg[10]=' '; msg[11]='p'; msg[12]='i'; msg[13]='p'; msg[14]='e';
    msg[15]='!'; msg[16]='\n'; msg[17]='\0';

    /* Create the pipe */
    if (Pipe(&readFD, &writeFD) < 0) {
        Exit(1);
    }

    /* Write into the write end */
    written = Write(msg, 17, writeFD);

    /* Close write end so the reader sees EOF */
    Close(writeFD);

    /* Read from the read end */
    bytesRead = Read(buf, BUFSIZE, readFD);

    /* Echo what we received to stdout (fd 1) */
    if (bytesRead > 0) {
        Write(buf, bytesRead, 1);
    }

    Close(readFD);
    Exit(0);
    return 0;
}