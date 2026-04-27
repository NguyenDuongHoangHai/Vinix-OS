/* ============================================================
 * vinix/mmc/host.h
 * ------------------------------------------------------------
 * MMC subsystem — host controller vtable. Drivers fill
 * struct mmc_host + struct mmc_host_ops, call mmc_add_host;
 * mmc-core handles card detection, identification, and the
 * gendisk wiring through mmc_block.
 *
 * omap_hsmmc currently does its own block_device registration —
 * Phase 2.7 will tease the generic core out so the driver only
 * exposes mmc_host_ops.
 * ============================================================ */

#ifndef VINIX_MMC_HOST_H
#define VINIX_MMC_HOST_H

#include "types.h"

struct mmc_host;
struct mmc_command;
struct mmc_data;

struct mmc_request {
    struct mmc_command *cmd;
    struct mmc_data    *data;
};

struct mmc_host_ops {
    void (*request)   (struct mmc_host *host, struct mmc_request *mrq);
    int  (*get_cd)    (struct mmc_host *host);   /* card detect */
    int  (*get_ro)    (struct mmc_host *host);   /* read-only */
    void (*set_ios)   (struct mmc_host *host);
};

struct mmc_host {
    const char                  *name;
    void                        *priv;
    const struct mmc_host_ops   *ops;
    uint32_t                     f_min;     /* Hz */
    uint32_t                     f_max;
    uint32_t                     ocr_avail;
};

struct mmc_host *mmc_alloc_host(int extra_priv, const char *name);
int              mmc_add_host  (struct mmc_host *host);
void             mmc_remove_host(struct mmc_host *host);

#endif /* VINIX_MMC_HOST_H */
