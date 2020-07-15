/* give a general test for syscall seek */
#include "tests/userprog/sample.inc"
#include "tests/lib.h"
#include "tests/main.h"
#include <syscall.h>
#include <string.h>

void
test_main (void) 
{
    int fd;
    char buffer_1[5];
    char buffer_2[5];

    CHECK ((fd = open("sample.txt")) > 1, "open \"sample.txt\"");

    seek (fd, 5);
    read (fd, buffer_1, 2);
    seek (fd, 5);
    read (fd, buffer_2, 2);

    if (buffer_1[0] != buffer_2[0] || buffer_1[1] != buffer_2[1]) {
        fail ("seek failed");
    }
}