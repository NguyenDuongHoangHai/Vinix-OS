/* ============================================================
 * buffer_cache.h
 * ------------------------------------------------------------
 * 64 × 512 B sector cache with LRU eviction + write-back.
 * Fs drivers hold a buffer_head through one logical operation
 * and release it with brelse — the cache writes dirty entries
 * back to the block device on eviction or bsync().
 * ============================================================ */

#ifndef BUFFER_CACHE_H
#define BUFFER_CACHE_H

#include "types.h"
#include "block.h"

#define BCACHE_BUFFERS    64
#define BCACHE_BLOCK_SIZE 512

struct buffer_head {
    struct block_device *bdev;
    uint32_t             lba;
    uint8_t              data[BCACHE_BLOCK_SIZE];
    bool                 valid;
    bool                 dirty;
    uint32_t             last_used;   /* monotonically increasing counter */
};

void bcache_init(void);

/* Get a buffer for (bdev, lba). Loads from disk if not cached.
 * Returns NULL on I/O error. */
struct buffer_head *bread(struct block_device *bdev, uint32_t lba);

void brelse(struct buffer_head *bh);
void bmark_dirty(struct buffer_head *bh);
void binvalidate(struct block_device *bdev, uint32_t lba);  /* evict, no write-back */
int  bsync(void);                                           /* flush all dirty */

uint32_t bcache_hits(void);
uint32_t bcache_misses(void);

#endif /* BUFFER_CACHE_H */
