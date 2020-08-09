#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "filesys/bufcache.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "lib/kernel/bitmap.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
#define NUM_DIRECT 123
#define NUM_S_INDIRECT 128
#define NUM_D_INDIRECT 16384 // = 128 * 128
#define NUM_PTR_PER_BLOCK 128
#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */
    uint32_t isdir;
    block_sector_t direct[NUM_DIRECT];
    block_sector_t s_indirect;
    block_sector_t d_indirect;
    unsigned magic;                     /* Magic number. */
  };

struct indirect_block{
  block_sector_t block[NUM_PTR_PER_BLOCK];
};

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
/* since using buffer cache, no longer require inode_disk enbeded in here */
struct inode
  {
    block_sector_t sector;              /* Sector number of disk location. */
                                        /* help find the block in bufcache */
    struct lock inode_lock;
    struct list_elem elem;              /* Element in inode list. */
    
    int open_cnt;                       /* Number of openers. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    bool removed;                       /* True if deleted, false otherwise. */
    bool extending;
    
    struct condition until_not_extending;
    struct condition until_no_writers;
  };

/* functions for allocating an inode */
static bool inode_allocate (struct inode_disk *disk_inode, off_t length);
static bool inode_allocate_sector (block_sector_t *sector_num);

/* functions for deallocating an inode */
static void inode_deallocate (struct inode *inode);
static void inode_deallocate_indirect (block_sector_t sector_num, size_t cnt);
static void inode_deallocate_doubly_indirect (block_sector_t sector_num, size_t cnt);


/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos)
{
  ASSERT (inode != NULL);
  /* read in entir disk_inode */
  struct inode_disk* disk_inode = get_inode_disk(inode);
  block_sector_t sector = -1;
  if (pos >= disk_inode->length){
    free(disk_inode);
    return sector;
  }

  off_t block_index = pos / BLOCK_SECTOR_SIZE;

  /* direct block */
  if (block_index < NUM_DIRECT){
    return disk_inode->direct[block_index];
  }

  /* indirect block */
  else if (block_index < NUM_DIRECT + NUM_PTR_PER_BLOCK){
    /* remove direct block bias */
    block_index -= NUM_DIRECT;
    struct indirect_block indirect_block;
    bufcache_read(disk_inode->s_indirect, &indirect_block, 0, BLOCK_SECTOR_SIZE);
    return indirect_block.block[block_index];
  }

  /* doubly indirect block */
  else {
    /* remove direct and indirect block bias */
    block_index -= (NUM_DIRECT + NUM_S_INDIRECT);
    /* get doubly indirect and indirect block index */
    int did_index = block_index / NUM_PTR_PER_BLOCK;
    int id_index  = block_index % NUM_PTR_PER_BLOCK;

    struct indirect_block indirect_block;
    /* Read doubly indirect block, then indirect block */
    bufcache_read(disk_inode->d_indirect, &indirect_block, 0, BLOCK_SECTOR_SIZE);
    bufcache_read(indirect_block.block[did_index], &indirect_block, 0, BLOCK_SECTOR_SIZE);
    
    return indirect_block.block[id_index];
  }
  /* ToDO: may optimize bufcache_read only desire block_sector_t not entire block */
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
inode_create (block_sector_t sector, off_t length, bool isdir)
{
  bool success = false;

  /* Length cannot be negative */
  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof(struct inode_disk) == BLOCK_SECTOR_SIZE);

  /* init disk_inode */
  struct inode_disk disk_inode;
  disk_inode.isdir = isdir;
  disk_inode.length = length;
  disk_inode.magic  = INODE_MAGIC;
  if (inode_allocate (&disk_inode, length)){
    bufcache_write(sector, &disk_inode, 0, BLOCK_SECTOR_SIZE);
    success = true;
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
  lock_init (&inode->inode_lock);
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
  if (--inode->open_cnt == 0){
    /* Remove from inode list and release lock. */
    list_remove (&inode->elem);

    /* Deallocate blocks if removed. */
    if (inode->removed){
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
      int min_left = MIN(inode_left, sector_left);

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      bufcache_read(sector_idx, (void*)(buffer + bytes_read), sector_ofs, chunk_size);

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

  lock_acquire(&inode->inode_lock);
  if (inode->deny_write_cnt){
    lock_release(&inode->inode_lock);
    return 0;
  }

  /* If new size of the file is past EOF, extend file */
  if (byte_to_sector (inode, offset + size - 1) == (size_t)-1)
    {
      inode->extending = true;

      /* Get inode_disk */
      struct inode_disk* disk_inode = get_inode_disk(inode);

      /* Allocate more sectors */
      if (!inode_allocate (disk_inode, offset + size))
        {
          return bytes_written;
        }

      /* Update inode_disk */
      disk_inode->length = offset + size;
      bufcache_write(inode->sector, (void *)(disk_inode), 0, BLOCK_SECTOR_SIZE);
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
      int min_left = MIN(inode_left, sector_left);

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      bufcache_write(sector_idx, (void*)(buffer+bytes_written), sector_ofs, chunk_size);

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
/* read inode_disk from buffercache into heap, remember to free */
struct inode_disk *
get_inode_disk (const struct inode *inode)
{
  ASSERT (inode != NULL);
  struct inode_disk *disk_inode = malloc (sizeof *disk_inode);
  bufcache_read (inode->sector, (void *)disk_inode, 0, BLOCK_SECTOR_SIZE);
  return disk_inode;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  ASSERT (inode != NULL);
  struct inode_disk *disk_inode = get_inode_disk (inode);
  off_t len = disk_inode->length;
  free (disk_inode);
  return len;
}

/* Returns is_dir of INODE's data. */
bool
inode_is_dir (const struct inode *inode)
{
  ASSERT (inode != NULL);
  struct inode_disk *disk_inode = get_inode_disk (inode);
  bool isdir = disk_inode->isdir;
  free (disk_inode);
  return isdir;
}

/* Returns removed of INODE's data. */
bool
inode_is_removed (const struct inode *inode)
{
  ASSERT (inode != NULL);
  return inode->removed;
}


/* Attempts allocating sectors in the order of direct->indirect->d.indirect */
static bool
inode_allocate (struct inode_disk *disk_inode, off_t length)
{
  ASSERT (disk_inode != NULL);
  if (length < 0) return false;

  /* Get number of sectors needed */
  size_t num_sectors = bytes_to_sectors (length);
  /* bit map for roll back */
  struct bitmap* task = bitmap_create(num_sectors);
  bitmap_set_all(task, false);
  /* temp store ptr in this array while allocating them */
  block_sector_t disk_sectors[num_sectors];

  /* Allocate Direct Blocks */
  for (size_t i = 0; i < num_sectors; i++){
    if (!inode_allocate_sector (disk_sectors[i])){
      goto roll_back;
    }
    bitmap_set(task, i, true);
  }
  /* reach here means all sector allocated successfully */
  
  size_t num;
  /* TODO: set pointers in inode to these sector */
  /* set direct pointers */
  num = MIN(num_sectors, NUM_DIRECT);
  for(int i = 0; i < num; i++){
    disk_inode->direct[i] = disk_sectors[i];
  }
  num_sectors -= num;
  if(num_sectors == 0){
    goto done;
  }

  /* set single indirect pointers */
  num = MIN(num_sectors, NUM_S_INDIRECT);
  inode_allocate_sector(&disk_inode->s_indirect);/* allocate s_indirect block */
  /* read in s_indirect block */
  struct indirect_block s_indirect;
  bufcache_read(disk_inode->s_indirect, &s_indirect, 0, BLOCK_SECTOR_SIZE);
  
  for(int i = 0; i < num; i++){
    s_indirect.block[i] = disk_sectors[i + NUM_DIRECT];
  }
  
  bufcache_write(disk_inode->s_indirect, &s_indirect, 0, BLOCK_SECTOR_SIZE);
  num_sectors -= num;
  if(num_sectors == 0){
    goto done;
  }
  
  /* set double indirect pointers */
  num = MIN(num_sectors, NUM_D_INDIRECT);
  struct indirect_block d_indirect_table, d_indirect;
  inode_allocate_sector(&disk_inode->d_indirect);  /*allocate d_indirect_table block */

  size_t num_d_indirect = DIV_ROUND_UP(num, NUM_PTR_PER_BLOCK);
  for(int j = 0; j < num_d_indirect; j++){  /* for each d_indirect block */
    inode_allocate_sector(&d_indirect_table.block[j]);
    size_t num_in_this_block = MIN(NUM_PTR_PER_BLOCK, num - j*NUM_PTR_PER_BLOCK);
    for(int i = 0; i < num_in_this_block; i++){
      d_indirect.block[i] = disk_sectors[i + j*NUM_PTR_PER_BLOCK + NUM_S_INDIRECT + NUM_DIRECT];
    }
    bufcache_write(d_indirect_table.block[j], &d_indirect, 0, BLOCK_SECTOR_SIZE); /*write back the d_indirect */
    memset(&d_indirect, 0, BLOCK_SECTOR_SIZE);
  }
  bufcache_write(disk_inode->d_indirect, &d_indirect_table, 0, BLOCK_SECTOR_SIZE);

done:
  bitmap_destroy(task);
  return true;


  /* when something goes wrong, we roll back based on bitmap task */
roll_back:

  for(int i = 0; i < num_sectors; i++){
    if(bitmap_test(task, i)){
      free_map_release(disk_sectors[i], 1);
    }
  }

  bitmap_destroy(task);
  return false;
}

static bool
inode_allocate_sector (block_sector_t *sector_num)
{
  static char buffer[BLOCK_SECTOR_SIZE];
  if (!*sector_num)
    {
      if (!free_map_allocate (1, sector_num))
        return false;
      bufcache_write(*sector_num, buffer, 0, BLOCK_SECTOR_SIZE);
    }
  return true;
}