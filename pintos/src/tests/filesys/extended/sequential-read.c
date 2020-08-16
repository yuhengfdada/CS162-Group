/* Try sequential read twice, and see whether hit rate improves. */

#include "tests/lib.h"
#include "tests/main.h"
#include <random.h>
#include <syscall.h>

void
test_main (void)
{
    msg("what you gonna do bro\n");
    // char buffer[200];

    // msg("1\n");

    // create("test.txt", 200);
    // int fd = open("test.txt");
    // random_bytes(buffer, sizeof buffer);
    // write(fd, buffer, 200);
    // close(fd);

    // msg("2\n");
    
    // reset();

    // msg("3\n");

    // /* First sequential read. */
    // int fd_1 = open("test.txt");

    // for (int i = 0; i < 10; i += 1) {
    //     read(fd_1, buffer, 10);
    // }

    // msg("4\n");

    // int first_hit_count = hit_count();
    // int first_access_count = access_count();

    // close(fd_1);


    // /* Second sequential read. */
    // int fd_2 = open("test.txt");

    // for (int i = 0; i < 10; i += 1) {
    //     read(fd_2, buffer, 10);
    // }

    // int second_hit_count = hit_count() - first_hit_count;
    // int second_access_count = access_count() - first_access_count;

    // close(fd_2);


    // msg("First read hit count: %d\n", first_hit_count);
    // msg("First read access acount: %d\n", first_access_count);
    // msg("Second read hit count : %d\n", second_hit_count);
    // msg("Second read access acount: %d\n", second_access_count);
}
