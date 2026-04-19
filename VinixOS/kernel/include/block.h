/* ============================================================
 * block.h
 * ------------------------------------------------------------
 * Block-device abstraction. MMC, future USB-storage, any sector
 * target registers here; filesystems talk bread()/bwrite()
 * instead of driver-specific sector calls.
 * ============================================================ */

#ifndef BLOCK_H
#define BLOCK_H

#include "types.h"

struct block_device;

struct block_operations {
    int (*read_sectors)(struct block_device *bdev, uint32_t lba,
                        uint32_t count, void *buf);
    int (*write_sectors)(struct block_device *bdev, uint32_t lba,
                         uint32_t count, const void *buf);
};

struct block_device {
    const char                    *name;          /* e.g. "mmc0" */
    uint32_t                       sector_size;   /* bytes, usually 512 */
    uint32_t                       total_sectors;
    const struct block_operations *ops;
    void                          *priv;
};

int block_register(struct block_device *bdev);
struct block_device *block_find(const char *name);
int block_read(struct block_device *bdev, uint32_t lba, uint32_t count, void *buf);
int block_write(struct block_device *bdev, uint32_t lba, uint32_t count, const void *buf);

#endif /* BLOCK_H */
