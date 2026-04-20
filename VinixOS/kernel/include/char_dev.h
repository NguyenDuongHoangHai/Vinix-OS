/* ============================================================
 * char_dev.h
 * ------------------------------------------------------------
 * Minimal character-device registry (backing devfs).
 * ============================================================ */

#ifndef CHAR_DEV_H
#define CHAR_DEV_H

#include "types.h"

struct char_device {
    const char *name;   /* without leading slash, e.g. "null" */
    int (*read)(void *priv, uint32_t offset, void *buf, uint32_t len);
    int (*write)(void *priv, uint32_t offset, const void *buf, uint32_t len);
    void *priv;
};

int  char_dev_register(const struct char_device *dev);
int  char_dev_count(void);
const struct char_device *char_dev_at(uint32_t index);
int  char_dev_find(const char *name);

#endif /* CHAR_DEV_H */
