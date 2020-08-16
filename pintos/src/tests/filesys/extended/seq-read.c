/* Try sequential read twice, and see whether hit rate improves. */

#include "tests/lib.h"
#include "tests/main.h"
#include <random.h>
#include <syscall.h>

void
test_main (void)
{
    char buffer[10];

    create("test.txt", 1000);
    int fd = open("test.txt");
    for (int i = 0; i < 100; i += 1) {
        random_bytes(buffer, sizeof buffer);
        write(fd, buffer, 10);
    }

    reset();

    /* First sequential read. */
    for (int i = 0; i < 10; i += 1) {
        read(fd, buffer, 10);
    }

    int first_hit_count = hit_count();
    int first_access_count = access_count();

    /* Second sequential read. */
    seek(fd, 0);
    for (int i = 0; i < 10; i += 1) {
        read(fd, buffer, 10);
    }

    int second_hit_count = hit_count() - first_hit_count;
    int second_access_count = access_count() - first_access_count;

    msg("First read hit count: %d", first_hit_count);
    msg("First read access acount: %d", first_access_count);
    msg("Second read hit count : %d", second_hit_count);
    msg("Second read access acount: %d", second_access_count);
    close(fd);
}
