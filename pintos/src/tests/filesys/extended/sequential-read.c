/* Try sequential read twice, and see whether hit rate improves. */

#include "tests/lib.h"
#include "tests/main.h"
#include <random.h>
#include <syscall.h>

void
test_main (void)
{
    void *buffer;
    random_bytes(buffer, 4096 * 16);
    CHECK (create())

}

