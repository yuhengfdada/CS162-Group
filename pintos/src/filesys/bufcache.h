#ifndef FILESYS_BUFCACHE_H
#define FILESYS_BUFCACHE_H

#include "devices/block.h"

void bufcache_init(void); 
void bufcache_read (block_sector_t sector, void* buffer, size_t offset, size_t length); 
void bufcache_write(block_sector_t sector, const void* buffer, size_t offset, size_t length); 
void bufcache_flush(void);

int bufcache_hit_count(void);
int bufcache_access_count(void);

#endif
