/* ============================================================
 * char_dev.c
 * ------------------------------------------------------------
 * Flat registry of character devices. devfs walks this list.
 * ============================================================ */

#include "char_dev.h"
#include "string.h"

#define MAX_CHAR_DEVS 16

static struct char_device devs[MAX_CHAR_DEVS];
static uint32_t dev_count = 0;

int char_dev_register(const struct char_device *d)
{
    if (dev_count >= MAX_CHAR_DEVS) return -1;
    devs[dev_count] = *d;
    return (int)dev_count++;
}

int char_dev_count(void)
{
    return (int)dev_count;
}

const struct char_device *char_dev_at(uint32_t index)
{
    if (index >= dev_count) return 0;
    return &devs[index];
}

int char_dev_find(const char *name)
{
    for (uint32_t i = 0; i < dev_count; i++) {
        if (strcmp(devs[i].name, name) == 0) return (int)i;
    }
    return -1;
}
