#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/bufcache.h"
#include "threads/synch.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* Identify number of direct blocks and indirect blocks in a sector. */
#define DIRECT_BLOCK_COUNT 123
#define INDIRECT_BLOCK_COUNT 128

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    block_sector_t direct_blocks[DIRECT_BLOCK_COUNT];     /* Pointer to direct blocks. */
    block_sector_t indirect_block;                        /* Pointer to an indirect block. */
    block_sector_t doubly_indirect_block;                 /* Pointer to a doubly indirect block. */
    uint32_t isdir;                                       /* Whether it is a directory or file. */
    off_t length;                                         /* File size in bytes. */
    unsigned magic;                                       /* Magic number. */
  };

/* Struct definition for indirect blocks. */
struct indirect_block {
  block_sector_t blocks[INDIRECT_BLOCK_COUNT];
};

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    bool extended;                      /* Whether the file is extended or not. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct lock inode_lock;             // not sure
    struct condition until_not_extending; // 
    struct condition until_no_writers; // no read and write at the same time (might not be necessary)
  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos)
{
  ASSERT (inode != NULL);
  block_sector_t rv = -1;
  struct inode_disk *disk_inode = (struct inode_disk *)malloc(sizeof(struct inode_disk));
  bufcache_read(inode_get_inumber(inode), disk_inode, 0, BLOCK_SECTOR_SIZE);

  if (pos < disk_inode->length) {
    off_t index = pos / BLOCK_SECTOR_SIZE;

    /* Scenario 1: Direct blocks are sufficient. */
    if (index < DIRECT_BLOCK_COUNT) {
      rv = disk_inode->direct_blocks[index];
    }
    /* Scenario 2: Direct blocks and indirect blocks are sufficient. */
    else if (index < DIRECT_BLOCK_COUNT + INDIRECT_BLOCK_COUNT) {
      off_t remaining_index = index - DIRECT_BLOCK_COUNT;
      struct indirect_block block_indirect;
      bufcache_read(disk_inode->indirect_block, &block_indirect, 0, BLOCK_SECTOR_SIZE);
      rv = block_indirect.blocks[remaining_index];
    }
    /* Scenario 3: Direct blocks, indirect blocks, and doubly indirect blocks are sufficient. */
    else {
      off_t remaining_index = index - DIRECT_BLOCK_COUNT - INDIRECT_BLOCK_COUNT;
      struct indirect_block first_level_block_indirect;
      bufcache_read(disk_inode->doubly_indirect_block, &first_level_block_indirect, 0, BLOCK_SECTOR_SIZE);
      struct indirect_block second_level_block_indirect;
      bufcache_read(first_level_block_indirect.blocks[remaining_index / INDIRECT_BLOCK_COUNT], &second_level_block_indirect, 0, BLOCK_SECTOR_SIZE);
      rv = second_level_block_indirect.blocks[remaining_index % INDIRECT_BLOCK_COUNT];
    }
  }

  free(disk_inode);
  return rv;
}

/* The following functions are thin wrappers around free_map_allocate(). */
static bool inode_allocate_sector (block_sector_t *sector);
static bool inode_allocate_indirect (block_sector_t *sector, size_t count);
static bool inode_allocate_doubly_indirect (block_sector_t *sector, size_t count);
static bool inode_allocate (struct inode_disk *disk_inode, off_t length);

/* The following functions are thin wrappers around free_map_release(). */
static void inode_deallocate_sector (block_sector_t sector);
static void inode_deallocate_indirect (block_sector_t sector, size_t count);
static void inode_deallocate_doubly_indirect (block_sector_t sector, size_t count);
static void inode_deallocate (struct inode *inode);

/* Allocate a sector for a direct pointer. */
static bool inode_allocate_sector (block_sector_t *sector) {
  char buffer[BLOCK_SECTOR_SIZE];
  if (!*sector) {
    if (!free_map_allocate(1, sector)) {
      return false;
    }
    bufcache_write(*sector, buffer, 0, BLOCK_SECTOR_SIZE);
  }
  return true;
}

/* Allocate a sector for an indirect pointer. */
static bool inode_allocate_indirect (block_sector_t *sector, size_t count) {
  /* First try to allocate the first level sector. */
  if (!inode_allocate_sector(sector)) {
    return false;
  }

  struct indirect_block block_indirect;
  bufcache_read(*sector, &block_indirect, 0, BLOCK_SECTOR_SIZE);

  /* Allocate COUNT of data sectors. */
  for (size_t i = 0; i < count; i += 1) {
    if (!inode_allocate_sector(&block_indirect.blocks[i])) {
      return false;
    }
  }

  bufcache_write(*sector, &block_indirect, 0, BLOCK_SECTOR_SIZE);
  return true;
}

/* Allocate a sector for a doubly indirect pointer. */
static bool inode_allocate_doubly_indirect (block_sector_t *sector, size_t count) {
  /* First try to allocate the first level sector. */
  if (!inode_allocate_sector(sector)) {
    return false;
  }

  struct indirect_block first_level_block_indirect;
  bufcache_read(*sector, &first_level_block_indirect, 0, BLOCK_SECTOR_SIZE);

  size_t num_second_level_blocks = DIV_ROUND_UP(count, INDIRECT_BLOCK_COUNT);
  for (size_t i = 0; i < num_second_level_blocks; i += 1) {
    size_t num_to_allocate = count < INDIRECT_BLOCK_COUNT? count : INDIRECT_BLOCK_COUNT;
    if (!inode_allocate_indirect(&first_level_block_indirect.blocks[i], num_to_allocate)) {
      return false;
    }
    count -= num_to_allocate;
  }

  bufcache_write(*sector, &first_level_block_indirect, 0, BLOCK_SECTOR_SIZE);
  return true;
}

/* Allocate all the inodes needed given the length of the file. */
static bool inode_allocate (struct inode_disk *disk_inode, off_t length) {
  /* Basic check. */
  ASSERT (disk_inode != NULL);
  if (length < 0) {
    return false;
  }

  /* Total number of sector needed and number of sectors to allocate in each level. */
  size_t remaining_num_sectors = bytes_to_sectors(length);
  size_t num_to_allocate;

  /* Allocate direct blocks. */
  num_to_allocate = remaining_num_sectors < DIRECT_BLOCK_COUNT? remaining_num_sectors : DIRECT_BLOCK_COUNT;
  for (size_t i = 0; i < num_to_allocate; i += 1) {
    if (!inode_allocate_sector(&disk_inode->direct_blocks[i])) {
      return false;
    }
  }
  remaining_num_sectors -= num_to_allocate;
  if (remaining_num_sectors == 0) {
    return true;
  }

  /* Allocate indirect blocks. */
  num_to_allocate = remaining_num_sectors < INDIRECT_BLOCK_COUNT? remaining_num_sectors : INDIRECT_BLOCK_COUNT;
  if (!inode_allocate_indirect(&disk_inode->indirect_block, num_to_allocate)) {
    return false;
  }
  remaining_num_sectors -= num_to_allocate;
  if (remaining_num_sectors == 0) {
    return true;
  }

  /* Allocate doubly indirect blocks. */
  num_to_allocate = remaining_num_sectors < INDIRECT_BLOCK_COUNT * INDIRECT_BLOCK_COUNT? remaining_num_sectors : INDIRECT_BLOCK_COUNT * INDIRECT_BLOCK_COUNT;
  if (!inode_allocate_doubly_indirect(&disk_inode->doubly_indirect_block, num_to_allocate)) {
    return false;
  }
  remaining_num_sectors -= num_to_allocate;

  ASSERT(remaining_num_sectors == 0);
  return true;
}

/* Deallocate a sector for a direct pointer. */
static void inode_deallocate_sector (block_sector_t sector) {
  free_map_release(sector, 1);
}

/* Dealllocate sectors for an indrect pointer. */
static void inode_deallocate_indirect (block_sector_t sector, size_t count) {
  struct indirect_block block_indirect;
  bufcache_read(sector, &block_indirect, 0, BLOCK_SECTOR_SIZE);

  for (size_t i = 0; i < count; i += 1) {
    inode_deallocate_sector(block_indirect.blocks[i]);
  }

  inode_deallocate_sector(sector);
}

/* Deallocate sectors for a doubly indirect pointer. */
static void inode_deallocate_doubly_indirect (block_sector_t sector, size_t count) {
  struct indirect_block first_level_block_indirect;
  bufcache_read(sector, &first_level_block_indirect, 0, BLOCK_SECTOR_SIZE);

  size_t num_second_level_blocks = DIV_ROUND_UP(count, INDIRECT_BLOCK_COUNT);
  for (size_t i = 0; i < num_second_level_blocks; i += 1) {
    size_t num_to_deallocate = count < INDIRECT_BLOCK_COUNT? count: INDIRECT_BLOCK_COUNT;
    inode_deallocate_indirect(first_level_block_indirect.blocks[i], num_to_deallocate);
    count -= num_to_deallocate;
  }

  inode_deallocate_sector(sector);
}

/* Deallocate all sectors for a given node. */
static void inode_deallocate (struct inode *inode) {
  /* Basic check. */
  ASSERT(inode != NULL);

  /* Get the corresponding disk inode. */
  struct inode_disk *disk_inode = (struct inode_disk *)malloc(sizeof(struct inode_disk));
  bufcache_read(inode_get_inumber(inode), disk_inode, 0, BLOCK_SECTOR_SIZE);

  /* Total number of sector needed to free and number of sectors to deallocate in each level. */
  size_t remaining_num_sectors = bytes_to_sectors(disk_inode->length);
  size_t num_to_deallocate;

  /* Deallocate direct blocks. */
  num_to_deallocate = remaining_num_sectors < DIRECT_BLOCK_COUNT? remaining_num_sectors : DIRECT_BLOCK_COUNT;
  for (size_t i = 0; i < num_to_deallocate; i += 1) {
    inode_deallocate_sector(disk_inode->direct_blocks[i]);
  }
  remaining_num_sectors -= num_to_deallocate;
  if (remaining_num_sectors == 0) {
    free(disk_inode);
    return;
  }

  /* Allocate indirect blocks. */
  num_to_deallocate = remaining_num_sectors < INDIRECT_BLOCK_COUNT? remaining_num_sectors : INDIRECT_BLOCK_COUNT;
  inode_deallocate_indirect(disk_inode->indirect_block, num_to_deallocate);
  remaining_num_sectors -= num_to_deallocate;
  if (remaining_num_sectors == 0) {
    free(disk_inode);
    return;
  }

  /* Allocate doubly indirect blocks. */
  num_to_deallocate = remaining_num_sectors < INDIRECT_BLOCK_COUNT * INDIRECT_BLOCK_COUNT? remaining_num_sectors : INDIRECT_BLOCK_COUNT * INDIRECT_BLOCK_COUNT;
  inode_deallocate_doubly_indirect(disk_inode->doubly_indirect_block, num_to_deallocate);
  remaining_num_sectors -= num_to_deallocate;

  ASSERT(remaining_num_sectors == 0);
  free(disk_inode);
  return;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void)
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      if (inode_allocate(disk_inode, length)) {
        bufcache_write(sector, disk_inode, 0, BLOCK_SECTOR_SIZE);
        success = true;
      }
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e))
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector)
        {
          inode_reopen (inode);
          return inode;
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init(&inode->inode_lock);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode)
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);

      /* Deallocate blocks if removed. */
      if (inode->removed)
        {
          free_map_release (inode->sector, 1);
          inode_deallocate(inode);
        }

      free (inode);
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode)
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset)
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;

  while (size > 0)
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      bufcache_read(sector_idx, (void *)(buffer + bytes_read), sector_ofs, chunk_size);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset)
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  if (inode->deny_write_cnt)
    return 0;
  
  /* File extension. */
  if (byte_to_sector(inode, offset + size - 1) == (size_t)-1) {
    struct inode_disk *disk_inode = (struct inode_disk *)malloc(sizeof(struct inode_disk));
    bufcache_read(inode_get_inumber(inode), disk_inode, 0, BLOCK_SECTOR_SIZE);

    if (!inode_allocate(disk_inode, offset + size)) {
      free(disk_inode);
      return bytes_written;
    }

    disk_inode->length = offset + size;
    bufcache_write(inode_get_inumber(inode), (void *)disk_inode, 0, BLOCK_SECTOR_SIZE);
    free(disk_inode);
  }

  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      bufcache_write(sector_idx, (void *)(buffer + bytes_written), sector_ofs, chunk_size);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode)
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode)
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  struct inode_disk *disk_inode = (struct inode_disk *)malloc(sizeof(struct inode_disk));
  bufcache_read(inode_get_inumber(inode), disk_inode, 0, BLOCK_SECTOR_SIZE);
  off_t length = disk_inode->length;
  free(disk_inode);
  return length;
}
