/* ============================================================
 * devfs.c
 * ------------------------------------------------------------
 * Thin adapter that plugs char_device callbacks into the VFS
 * operations table. Each registered device becomes a file whose
 * file_index is its slot in char_dev's registry.
 * ============================================================ */

#include "devfs.h"
#include "char_dev.h"
#include "uart.h"
#include "syscalls.h"
#include "wait_queue.h"
#include "string.h"

/* ------------------------------------------------------------
 * /dev/null — silently discards writes, returns EOF on read.
 * ------------------------------------------------------------ */
static int null_read(void *p, uint32_t off, void *buf, uint32_t len)
{
    (void)p; (void)off; (void)buf; (void)len;
    return 0;
}

static int null_write(void *p, uint32_t off, const void *buf, uint32_t len)
{
    (void)p; (void)off; (void)buf;
    return (int)len;
}

/* ------------------------------------------------------------
 * /dev/tty — wraps the UART. Read blocks on uart_rx_wq; write
 * expands \n → \r\n like the legacy sys_write path.
 * ------------------------------------------------------------ */
static int tty_read(void *p, uint32_t off, void *buf, uint32_t len)
{
    (void)p; (void)off;
    if (len == 0) return 0;

    char *out = buf;
    wait_event(uart_rx_wq, uart_rx_available() > 0);

    int c = uart_getc();
    if (c < 0) return 0;
    out[0] = (char)c;
    return 1;
}

static int tty_write(void *p, uint32_t off, const void *buf, uint32_t len)
{
    (void)p; (void)off;
    const char *s = buf;
    for (uint32_t i = 0; i < len; i++) {
        if (s[i] == '\n') uart_putc('\r');
        uart_putc(s[i]);
    }
    return (int)len;
}

/* ------------------------------------------------------------
 * VFS adapter — indexes into char_dev registry.
 * ------------------------------------------------------------ */
static int devfs_lookup(const char *name)
{
    return char_dev_find(name);
}

static int devfs_read(int idx, uint32_t offset, void *buf, uint32_t len)
{
    const struct char_device *d = char_dev_at((uint32_t)idx);
    if (!d || !d->read) return E_FAIL;
    return d->read(d->priv, offset, buf, len);
}

static int devfs_write(int idx, uint32_t offset, const void *buf, uint32_t len)
{
    const struct char_device *d = char_dev_at((uint32_t)idx);
    if (!d || !d->write) return E_PERM;
    return d->write(d->priv, offset, buf, len);
}

static int devfs_get_file_count(void)
{
    return char_dev_count();
}

static int devfs_get_file_info(int index, char *name_out, uint32_t *size_out)
{
    const struct char_device *d = char_dev_at((uint32_t)index);
    if (!d) return E_NOENT;

    int i = 0;
    while (d->name[i] && i < 31) { name_out[i] = d->name[i]; i++; }
    name_out[i] = '\0';
    if (size_out) *size_out = 0;
    return E_OK;
}

static int devfs_listdir(const char *path, void *entries, uint32_t max)
{
    /* devfs is flat — the only valid path is the mount root. */
    if (path && *path != '\0') return E_NOENT;

    file_info_t *out = (file_info_t *)entries;
    uint32_t written = 0;
    int total = char_dev_count();

    for (int i = 0; i < total && written < max; i++) {
        const struct char_device *d = char_dev_at((uint32_t)i);
        if (!d) continue;

        int k = 0;
        while (d->name[k] && k < 31) { out[written].name[k] = d->name[k]; k++; }
        out[written].name[k] = '\0';
        out[written].size = 0;
        written++;
    }
    return (int)written;
}

static struct vfs_operations devfs_ops = {
    .lookup         = devfs_lookup,
    .read           = devfs_read,
    .get_file_count = devfs_get_file_count,
    .get_file_info  = devfs_get_file_info,
    .listdir        = devfs_listdir,
    .create         = 0,
    .write          = devfs_write,
    .truncate       = 0,
};

static const struct char_device null_dev = {
    .name  = "null",
    .read  = null_read,
    .write = null_write,
    .priv  = 0,
};

static const struct char_device tty_dev = {
    .name  = "tty",
    .read  = tty_read,
    .write = tty_write,
    .priv  = 0,
};

struct vfs_operations *devfs_init(void)
{
    char_dev_register(&tty_dev);
    char_dev_register(&null_dev);
    uart_printf("[DEVFS] registered /dev/tty, /dev/null\n");
    return &devfs_ops;
}
