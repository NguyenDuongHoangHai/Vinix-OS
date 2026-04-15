/* ============================================================
 * vfs.c
 * ------------------------------------------------------------
 * Virtual File System Implementation
 * ============================================================ */

#include "vfs.h"
#include "syscalls.h"
#include "uart.h"
#include "string.h"
#include "types.h"

/* Forward declaration */
static struct vfs_operations *vfs_find_fs(const char *path);

/* ============================================================
 * File Descriptor Table
 * ============================================================ */

struct vfs_fd {
    bool     in_use;        /* FD allocated? */
    uint32_t file_index;    /* Index into underlying fs file table */
    uint32_t offset;        /* Current read/write position */
    int      flags;         /* Open flags (O_RDONLY, O_WRONLY, O_RDWR, ...) */
};

static struct vfs_fd fd_table[MAX_FDS];

/* ============================================================
 * VFS Mount Table
 * ============================================================ */

struct vfs_mount {
    const char *mount_point;
    struct vfs_operations *fs_ops;
    bool in_use;
};

#define MAX_MOUNTS 4
static struct vfs_mount mount_table[MAX_MOUNTS];

/* ============================================================
 * VFS Initialization
 * ============================================================ */

void vfs_init(void)
{
    uart_printf("[VFS] Initializing Virtual File System...\n");
    
    /* Initialize FD table */
    for (int i = 0; i < MAX_FDS; i++) {
        fd_table[i].in_use = false;
        fd_table[i].file_index = 0;
        fd_table[i].offset = 0;
        fd_table[i].flags = 0;
    }
    
    /* Initialize mount table */
    for (int i = 0; i < MAX_MOUNTS; i++) {
        mount_table[i].mount_point = NULL;
        mount_table[i].fs_ops = NULL;
        mount_table[i].in_use = false;
    }
    
    uart_printf("[VFS] Initialization complete\n");
}

/**
 * Mount filesystem at mount point
 */
int vfs_mount(const char *mount_point, struct vfs_operations *fs_ops)
{
    uart_printf("[VFS] Mounting filesystem at '%s'...\n", mount_point);
    
    /* Find free mount slot */
    int slot = -1;
    for (int i = 0; i < MAX_MOUNTS; i++) {
        if (!mount_table[i].in_use) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        uart_printf("[VFS] ERROR: No free mount slots\n");
        return E_FAIL;
    }
    
    /* Register filesystem */
    mount_table[slot].mount_point = mount_point;
    mount_table[slot].fs_ops = fs_ops;
    mount_table[slot].in_use = true;
    
    uart_printf("[VFS] Filesystem mounted at '%s'\n", mount_point);
    return E_OK;
}

/**
 * Find filesystem for path
 */
static struct vfs_operations *vfs_find_fs(const char *path)
{
    /* Simple implementation: only support root "/" */
    for (int i = 0; i < MAX_MOUNTS; i++) {
        if (mount_table[i].in_use && 
            strcmp(mount_table[i].mount_point, "/") == 0) {
            return mount_table[i].fs_ops;
        }
    }
    return NULL;
}

/* ============================================================
 * File Operations
 * ============================================================ */

/**
 * Open a file
 */
int vfs_open(const char *path, int flags)
{
    int access = flags & O_ACCMODE;

    if (access != O_RDONLY && access != O_WRONLY && access != O_RDWR) {
        return E_ARG;
    }

    struct vfs_operations *fs_ops = vfs_find_fs(path);
    if (!fs_ops) {
        return E_NOENT;
    }

    if ((access == O_WRONLY || access == O_RDWR) && fs_ops->write == NULL) {
        return E_PERM;
    }

    const char *filename = path;
    if (*filename == '/') {
        filename++;
    }

    int file_index = fs_ops->lookup(filename);

    if (file_index < 0) {
        if ((flags & O_CREAT) && fs_ops->create != NULL) {
            file_index = fs_ops->create(filename);
            if (file_index < 0) {
                return file_index;
            }
        } else {
            return E_NOENT;
        }
    } else if (flags & O_TRUNC) {
        if (fs_ops->truncate != NULL) {
            int r = fs_ops->truncate(file_index, 0);
            if (r < 0) return r;
        }
    }

    /* FDs 0-2 reserved for stdin/stdout/stderr */
    int fd = -1;
    for (int i = 3; i < MAX_FDS; i++) {
        if (!fd_table[i].in_use) {
            fd = i;
            break;
        }
    }

    if (fd < 0) {
        return E_MFILE;
    }

    fd_table[fd].in_use = true;
    fd_table[fd].file_index = file_index;
    fd_table[fd].offset = 0;
    fd_table[fd].flags = flags;

    return fd;
}

/**
 * Read from file descriptor
 */
int vfs_read(int fd, void *buf, uint32_t len)
{
    /* Validate FD */
    if (fd < 0 || fd >= MAX_FDS || !fd_table[fd].in_use) {
        return E_BADF;
    }
    
    /* Find filesystem (assume root for now) */
    struct vfs_operations *fs_ops = vfs_find_fs("/");
    if (!fs_ops) {
        return E_FAIL;
    }
    
    /* Read from filesystem */
    int bytes_read = fs_ops->read(
        fd_table[fd].file_index,
        fd_table[fd].offset,
        buf,
        len
    );
    
    if (bytes_read > 0) {
        fd_table[fd].offset += bytes_read;
    }
    
    return bytes_read;
}

int vfs_write(int fd, const void *buf, uint32_t len)
{
    if (fd < 0 || fd >= MAX_FDS || !fd_table[fd].in_use) {
        return E_BADF;
    }

    int access = fd_table[fd].flags & O_ACCMODE;
    if (access != O_WRONLY && access != O_RDWR) {
        return E_PERM;
    }

    struct vfs_operations *fs_ops = vfs_find_fs("/");
    if (!fs_ops || fs_ops->write == NULL) {
        return E_PERM;
    }

    int bytes_written = fs_ops->write(
        fd_table[fd].file_index,
        fd_table[fd].offset,
        buf,
        len
    );

    if (bytes_written > 0) {
        fd_table[fd].offset += bytes_written;
    }

    return bytes_written;
}

/**
 * Close file descriptor
 */
int vfs_close(int fd)
{
    /* Validate FD */
    if (fd < 0 || fd >= MAX_FDS || !fd_table[fd].in_use) {
        return E_BADF;
    }
    
    /* Free FD */
    fd_table[fd].in_use = false;
    fd_table[fd].file_index = 0;
    fd_table[fd].offset = 0;
    fd_table[fd].flags = 0;

    return E_OK;
}

/**
 * List directory contents
 */
int vfs_listdir(const char *path, void *entries, uint32_t max_entries)
{
    file_info_t *file_entries = (file_info_t *)entries;
    
    /* Find filesystem for path */
    struct vfs_operations *fs_ops = vfs_find_fs(path);
    if (!fs_ops) {
        return E_NOENT;
    }
    
    /* Only support root directory */
    if (strcmp(path, "/") != 0) {
        return E_NOENT;
    }
    
    /* Get file count */
    int file_count = fs_ops->get_file_count();
    if (file_count < 0) {
        return file_count;
    }
    
    /* Fill entries */
    int count = 0;
    for (int i = 0; i < file_count && count < (int)max_entries; i++) {
        int ret = fs_ops->get_file_info(
            i,
            file_entries[count].name,
            &file_entries[count].size
        );
        
        if (ret == E_OK) {
            count++;
        }
    }
    
    return count;
}
