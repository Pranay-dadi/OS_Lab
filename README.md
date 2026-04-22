# Instructions\

# Install building tools like make, g++
bash install_building_tools.sh\

# Build the nachos
bash build_nachos.sh\

# Build the coff2noff translator
bash coff2noff.sh\

# Build & run tests in code/test folder
bash build_test.sh\

test drectory -make clean\
              -make\
build.linux - make clean\
            - make\
            - ./nachos -x ../test/abs\
            - ./nachos -x ../test/sleep\
            - ./nachos -P\
            - ./nachos -x ../test/pipetest\
            - ./nachos -x ../test/demandpage_test\
            - ./nachos -x ../test/malloc_test\
For syscalls  - Add c code in test\
              - Then Makefile in test (2 parts) \
              - Then start.S in test\
              - Then kyscall.h in userprog\
              - Then syscall.h in userprog\
              - Then exception.cc in userproq (2 parts)\
              - Then files in threads, userprog, and other directories.\
For Diff file see from end as previous diff file content may be present in the current diff file.
