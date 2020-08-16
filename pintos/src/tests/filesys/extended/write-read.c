/* Tests the effectiveness of the cache by doing the following:
   Writes a file to disk that takes up half of the cache size
   (this is because room must be left for the inode_disk structs
   which must be read from disk in addition to the file itself).
   Then closes and reopens the file, and invalidates the cache.
   Then reads the entire file (cold cache), keeping track of cache 
   access statistics. Then closes and reopens the file, and re-reads 
   the entire file. It then compares the access statistics from the 
   second read to the first read, and checks for an increase in the 
   cache hit rate.  */

#include <random.h>
#include <stdio.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"
#include "threads/fixed-point.h"

#define BLOCK_SECTOR_SIZE 512
#define BUF_SIZE (BLOCK_SECTOR_SIZE * 200)

static char buf[BUF_SIZE];
static long long num_disk_reads;
static long long num_disk_writes;

void
test_main (void)
{
  int test_fd;
  char *opt_write_file_name = "opt_write_test";
  CHECK (create (opt_write_file_name, 0), 
    "create \"%s\"", opt_write_file_name);
  CHECK ((test_fd = open (opt_write_file_name)) > 1, 
    "open \"%s\"", opt_write_file_name);

  /* Write opt_write_test file of size BLOCK_SECTOR_SIZE * 200 */
  random_bytes (buf, sizeof buf);
  CHECK (write (test_fd, buf, sizeof buf) == BUF_SIZE,
   "write %d bytes to \"%s\"", (int) BUF_SIZE, opt_write_file_name);

  /* Invalidate cache */
  invcache ();
  msg ("invcache");

   /* Get baseline disk statistics */
  CHECK (diskstat (&num_disk_reads, &num_disk_writes) == 0, 
  	"baseline disk statistics");

  /* Save baseline disk stats for comparison */
  long long base_disk_reads = num_disk_reads;
  long long base_disk_writes = num_disk_writes;

  /* Read full file (cold cache) */
  CHECK (read (test_fd, buf, sizeof buf) == BUF_SIZE,
   "read %d bytes from \"%s\"", (int) BUF_SIZE, opt_write_file_name);

   /* Get new disk statistics */
  CHECK (diskstat (&num_disk_reads, &num_disk_writes) == 0, 
  	"get new disk statistics");

  /* Check that hit rate improved */
  CHECK (num_disk_reads - ((long long ) 10) <= base_disk_reads, 
    "old reads: %lld, old writes: %lld, new reads: %lld, new writes: %lld", 
  	base_disk_reads, base_disk_writes, num_disk_reads, num_disk_writes);

  msg ("close \"%s\"", opt_write_file_name);
  close (test_fd);
}
