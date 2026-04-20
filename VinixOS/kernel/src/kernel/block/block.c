/* ============================================================
 * block.c
 * ------------------------------------------------------------
 * Flat block-device registry.
 * ============================================================ */

/* INVARIANT: single consumer per device — no locking. Add a
 * spinlock if concurrent mounts ever appear. */

#include "block.h"
#include "string.h"
#include "uart.h"
#include "syscalls.h"

#define MAX_BLOCK_DEVS 4

static struct block_device *bdevs[MAX_BLOCK_DEVS];
static int num_bdevs = 0;

int block_register(struct block_device *bdev)
{
    if (num_bdevs >= MAX_BLOCK_DEVS) return E_FAIL;
    bdevs[num_bdevs++] = bdev;
    uart_printf("[BLK] registered %s (%u sectors x %u B)\n",
                bdev->name, bdev->total_sectors, bdev->sector_size);
    return E_OK;
}

struct block_device *block_find(const char *name)
{
    for (int i = 0; i < num_bdevs; i++) {
        if (bdevs[i] && strcmp(bdevs[i]->name, name) == 0) return bdevs[i];
    }
    return 0;
}

int block_read(struct block_device *bdev, uint32_t lba, uint32_t count, void *buf)
{
    if (!bdev || !bdev->ops || !bdev->ops->read_sectors) return E_FAIL;
    return bdev->ops->read_sectors(bdev, lba, count, buf);
}

int block_write(struct block_device *bdev, uint32_t lba, uint32_t count, const void *buf)
{
    if (!bdev || !bdev->ops || !bdev->ops->write_sectors) return E_PERM;
    return bdev->ops->write_sectors(bdev, lba, count, buf);
}
