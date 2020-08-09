#include "devices/block.h"



void bufcache_init(void); 
void bufcache_read (block_sector_t sector,       void* buffer, size_t offset, size_t length); 
void bufcache_write(block_sector_t sector, const void* buffer, size_t offset, size_t length); 
void bufcache_flush(void);