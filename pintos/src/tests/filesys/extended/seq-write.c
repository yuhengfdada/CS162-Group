/* Try sequential read twice, and see whether hit rate improves. */

#include "tests/lib.h"
#include "tests/main.h"
#include <random.h>
#include <syscall.h>

void
test_main (void)
{
    char buffer[16];
    create("test.txt", 65536);
    int fd = open("test.txt");
    for (int i = 0; i < 4069; i += 1) {
      random_bytes(buffer, sizeof buffer);
      for(int j = 0; j < 16; j+= 1){
        write(fd, (buffer+j), 1);
      }
    }

    msg("Total access: %d", access_count());
    CHECK((access_count() - hit_count()) < 1024, "Bufcache coalesce writes successfully" );
    close(fd);
}
