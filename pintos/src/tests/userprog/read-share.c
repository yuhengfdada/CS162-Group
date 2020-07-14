/* Try opening the same file twice and reading from two file descriptors respectively. */

#include "tests/userprog/sample.inc"
#include "tests/lib.h"
#include "tests/main.h"
#include <syscall.h>
#include <string.h>

void
test_main (void) 
{
    int fd_1;
    int fd_2;
    char buffer_1[10];
    char buffer_2[10];
    char buffer_3[10];

    CHECK ((fd_1 = open("sample.txt")) > 1, "open \"sample.txt\"");
    CHECK ((fd_2 = open("sample.txt")) > 1, "open \"sample.txt\"");

    read (fd_1, buffer_1, 1);
    read (fd_2, buffer_2, 1);
    read (fd_1, buffer_3, 1);

    if (buffer_1[0] != buffer_2[0]) {
        fail ("different file descriptors should be independent");
    }

    if (buffer_1[0] == buffer_3[0]) {
        fail ("same file descriptor should be dependent");
    }
}
