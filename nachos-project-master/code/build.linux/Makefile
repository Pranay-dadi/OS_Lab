DEFINES =  -DUSE_TLB -DFILESYS_STUB -DRDATA -DSIM_FIX


#####################################################################
#
# You might want to play with the CFLAGS, but if you use -O it may
# break the thread system.  You might want to use -fno-inline if
# you need to call some inline functions from the debugger.

CFLAGS = -g -Wall $(INCPATH) $(DEFINES) $(HOSTCFLAGS) -DCHANGED -m32
LDFLAGS = -m32
CPP_AS_FLAGS= -m32

#####################################################################
CPP=/lib/cpp
CC = g++
LD = g++
AS = as
RM = /bin/rm

INCPATH = -I../network -I../filesys -I../userprog -I../threads -I../machine -I../lib -I-

PROGRAM = nachos

#
# Edit these lists as if you add files to the source directories.
# See the instructions at the top of the file for more information.
#

LIB_H = ../lib/bitmap.h\
	../lib/copyright.h\
	../lib/debug.h\
	../lib/hash.h\
	../lib/libtest.h\
	../lib/list.h\
	../lib/sysdep.h\
	../lib/utility.h

LIB_C = ../lib/bitmap.cc\
	../lib/debug.cc\
	../lib/hash.cc\
	../lib/libtest.cc\
	../lib/list.cc\
	../lib/sysdep.cc

LIB_O = bitmap.o debug.o libtest.o sysdep.o


MACHINE_H = ../machine/callback.h\
	../machine/interrupt.h\
	../machine/stats.h\
	../machine/timer.h\
	../machine/console.h\
	../machine/machine.h\
	../machine/mipssim.h\
	../machine/translate.h\
	../machine/network.h\
	../machine/disk.h

MACHINE_C = ../machine/interrupt.cc\
	../machine/stats.cc\
	../machine/timer.cc\
	../machine/console.cc\
	../machine/machine.cc\
	../machine/mipssim.cc\
	../machine/translate.cc\
	../machine/network.cc\
	../machine/disk.cc

MACHINE_O = interrupt.o stats.o timer.o console.o machine.o mipssim.o\
	translate.o network.o disk.o

THREAD_H = ../threads/alarm.h\
	../threads/kernel.h\
	../threads/main.h\
	../threads/scheduler.h\
	../threads/switch.h\
	../threads/synch.h\
	../threads/synchlist.h\
	../threads/thread.h\
	../threads/ptable.h\
	../threads/pcb.h\
	../threads/stable.h\
	../threads/sem.h\

THREAD_C = ../threads/alarm.cc\
	../threads/kernel.cc\
	../threads/main.cc\
	../threads/scheduler.cc\
	../threads/synch.cc\
	../threads/synchlist.cc\
	../threads/thread.cc\
	../threads/ptable.cc\
	../threads/pcb.cc\
	../threads/stable.cc\
	../threads/sem.cc\
	../threads/prioritytest.cc\

THREAD_O = alarm.o kernel.o main.o scheduler.o synch.o thread.o ptable.o pcb.o stable.o sem.o prioritytest.o

USERPROG_H = ../userprog/addrspace.h\
	../userprog/syscall.h\
	../userprog/synchconsole.h\
	../userprog/noff.h\
	../userprog/pipe.h\
    ../userprog/pipetable.h

USERPROG_C = ../userprog/addrspace.cc\
	../userprog/exception.cc\
	../userprog/synchconsole.cc\
	../userprog/pipe.cc\
    ../userprog/pipetable.cc

USERPROG_O = addrspace.o exception.o synchconsole.o pipe.o pipetable.o

FILESYS_H =../filesys/directory.h \
	../filesys/filehdr.h\
	../filesys/filesys.h \
	../filesys/openfile.h\
	../filesys/pbitmap.h\
	../filesys/synchdisk.h

FILESYS_C =../filesys/directory.cc\
	../filesys/filehdr.cc\
	../filesys/filesys.cc\
	../filesys/pbitmap.cc\
	../filesys/openfile.cc\
	../filesys/synchdisk.cc\

FILESYS_O =directory.o filehdr.o filesys.o pbitmap.o openfile.o synchdisk.o

NETWORK_H = ../network/post.h

NETWORK_C = ../network/post.cc

NETWORK_O = post.o

##################################################################
#  You probably don't want to change anything below this point in
#  the file unless you are comfortable with GNU make and know what
#  you are doing...
##################################################################

THREAD_S = ../threads/switch.s

HFILES = $(LIB_H) $(MACHINE_H) $(THREAD_H) $(USERPROG_H) $(FILESYS_H) $(NETWORK_H)
CFILES = $(LIB_C) $(MACHINE_C) $(THREAD_C) $(USERPROG_C) $(FILESYS_C) $(NETWORK_C)

C_OFILES = $(LIB_O) $(MACHINE_O) $(THREAD_O) $(USERPROG_O) $(FILESYS_O) $(NETWORK_O)

S_OFILES = switch.o
OFILES = $(C_OFILES) $(S_OFILES)

$(PROGRAM): $(OFILES)
	$(LD) $(OFILES) $(LDFLAGS) -o $(PROGRAM)

$(C_OFILES): %.o:
	$(CC) $(CFLAGS) -c $<

switch.o: ../threads/switch.S
	$(CC) $(CPP_AS_FLAGS) -P $(INCPATH) $(HOSTCFLAGS) -c ../threads/switch.S

depend: $(CFILES) $(HFILES)
	$(CC) $(INCPATH) $(DEFINES) $(HOSTCFLAGS) -DCHANGED -M $(CFILES) > makedep
	@echo '/^# DO NOT DELETE THIS LINE/+1,$$d' >eddep
	@echo '$$r makedep' >>eddep
	@echo 'w' >>eddep
	@echo 'q' >>eddep
	ed - Makefile.dep < eddep
	rm eddep makedep 
	@echo '# DEPENDENCIES MUST END AT END OF FILE' >> Makefile.dep
	@echo '# IF YOU PUT STUFF HERE IT WILL GO AWAY' >> Makefile.dep
	@echo '# see make depend above' >> Makefile.dep

clean:
	$(RM) -f $(OFILES)

distclean: clean
	$(RM) -f $(PROGRAM)
	$(RM) -f DISK_?
	$(RM) -f core
	$(RM) -f SOCKET_?
	@echo '/^# DO NOT DELETE THIS LINE/+1,$$d' >eddep
	@echo 'w' >>eddep
	@echo 'q' >>eddep
	ed - Makefile.dep < eddep
	rm eddep 
	@echo '# DEPENDENCIES MUST END AT END OF FILE' >> Makefile.dep
	@echo '# IF YOU PUT STUFF HERE IT WILL GO AWAY' >> Makefile.dep
	@echo '# see make depend above' >> Makefile.dep

include Makefile.dep
