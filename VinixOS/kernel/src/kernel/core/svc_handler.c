/* ============================================================
 * svc_handler.c
 * ------------------------------------------------------------
 * Supervisor Call (SVC) Exception Handler
 * Implements the system call dispatcher and user/kernel boundary
 * ============================================================ */

#include "types.h"
#include "assert.h"
#include "trace.h"
#include "scheduler.h"
#include "uart.h"
#include "syscalls.h"
#include "mmu.h"
#include "vfs.h"
#include "string.h"
#include "proc.h"
#include "svc_context.h"

extern uint8_t _shell_payload_start;
extern uint8_t _shell_payload_end;

/*
 * Validation boundaries
 * Used to ensure user pointers are within allowed True User Space regions.
 */

/* ============================================================
 * Helper: Validate User Pointer
 * ============================================================
 * Enforces strict memory rules:
 * Pointers must point to the User Space region (0x40000000 -> +1MB).
 * This sandboxes User interactions preventing Kernel corruption.
 */
static int validate_user_pointer(const void *ptr, uint32_t len)
{
    uint32_t start = (uint32_t)ptr;
    uint32_t end = start + len;

    /* Check for overflow */
    if (end < start)
    {
        return E_PTR;
    }

    uint32_t allowed_start = USER_SPACE_VA;
    uint32_t allowed_end = USER_SPACE_VA + (USER_SPACE_MB * 1024 * 1024);

    /* Check bounds [start, end) within [allowed_start, allowed_end) */
    if (start >= allowed_start && end <= allowed_end)
    {
        return E_OK;
    }

    return E_PTR;
}

/* ============================================================
 * Syscall Handlers
 * ============================================================ */

/* sys_write(const void *buf, uint32_t len) */
static int32_t sys_write(struct svc_context *ctx)
{
    const void *buf = NULL;
    uint32_t len = 0;
    int fd = 1;

    /* ABI compatibility:
     * - Legacy userspace:   write(buf, len)        in r0, r1
     * - Compiler runtime:   write(fd, buf, count)  in r0, r1, r2 */
    if (validate_user_pointer((const void *)ctx->r0, (uint32_t)ctx->r1) == E_OK)
    {
        buf = (const void *)ctx->r0;
        len = (uint32_t)ctx->r1;
    }
    else if (validate_user_pointer((const void *)ctx->r1, (uint32_t)ctx->r2) == E_OK)
    {
        fd = (int)ctx->r0;
        buf = (const void *)ctx->r1;
        len = (uint32_t)ctx->r2;
    }
    else
    {
        uart_printf("[SVC] Security Violation: Invalid Ptr 0x%08x\n", (uint32_t)ctx->r0);
        return E_PTR;
    }

    /* Only stdout/stderr are supported for now */
    if (fd != 1 && fd != 2)
    {
        return E_ARG;
    }

    /* 2. Logic (Direct Driver Call for now) */
    /* Only allow reasonable length to prevent DoS */
    if (len > 256)
    {
        return E_ARG;
    }

    /* We can safely cast because we validated range */
    const char *str = (const char *)buf;
    for (uint32_t i = 0; i < len; i++)
    {
        if (str[i] == '\n')
        {
            uart_putc('\r');
        }
        uart_putc(str[i]);
    }

    return (int32_t)len;
}

/* sys_exit(int status) */
static int32_t sys_exit(struct svc_context *ctx)
{
    int32_t status = (int32_t)ctx->r0;
    struct task_struct *current = scheduler_current_task();

    uart_printf("[SVC] Task %d exiting with status %d\n", current->id, status);

    /* Keep interactive system alive:
     * shell task is reused as an app container (exec). If that app exits,
     * restore original shell payload and jump back to shell entrypoint. */
    if (current && strcmp(current->name, "User App (Shell)") == 0)
    {
        uint32_t payload_size = (uint32_t)&_shell_payload_end - (uint32_t)&_shell_payload_start;
        uint8_t *src = &_shell_payload_start;
        uint8_t *dst = (uint8_t *)USER_SPACE_VA;

        for (uint32_t i = 0; i < payload_size; i++)
        {
            dst[i] = src[i];
        }

        ctx->r0 = 0;
        ctx->r1 = 0;
        ctx->r2 = 0;
        ctx->r3 = 0;
        ctx->lr = USER_SPACE_VA;

        uart_printf("[SVC] Shell restarted\n");
        return E_OK;
    }

    /* Forked child: proper exit — zombie state + wake parent in wait(). */
    do_exit(status);
    return 0;
}

/* sys_yield() */
static int32_t sys_yield(struct svc_context *ctx)
{
    /*
     * CRITICAL FIX: Set need_reschedule flag BEFORE calling scheduler_yield
     *
     * Without this, voluntary yields may be ignored if the flag was already
     * cleared by a previous context switch. This causes tasks to get stuck
     * in busy-wait loops instead of properly yielding CPU to other tasks.
     *
     * Example failure scenario without this fix:
     * 1. Shell calls sys_yield() (need_reschedule=false from previous clear)
     * 2. sys_yield() calls scheduler_yield()
     * 3. scheduler_yield() sees need_reschedule=false, returns immediately
     * 4. Shell stuck in loop, never switches to Idle
     */
    extern volatile bool need_reschedule;
    need_reschedule = true;

    /* Voluntary Yield */
    scheduler_yield();
    return E_OK;
}

/* sys_read(void *buf, uint32_t len) */
static int32_t sys_read(struct svc_context *ctx)
{
    void *buf = NULL;
    uint32_t len = 0;
    int fd = 0;

    /* ABI compatibility:
     * - Legacy userspace: read(buf, len)         in r0, r1
     * - Compiler runtime: read(fd, buf, count)   in r0, r1, r2 */
    if (validate_user_pointer((void *)ctx->r0, (uint32_t)ctx->r1) == E_OK)
    {
        buf = (void *)ctx->r0;
        len = (uint32_t)ctx->r1;
    }
    else if (validate_user_pointer((void *)ctx->r1, (uint32_t)ctx->r2) == E_OK)
    {
        fd = (int)ctx->r0;
        buf = (void *)ctx->r1;
        len = (uint32_t)ctx->r2;
    }
    else
    {
        return E_PTR;
    }

    /* Only stdin is supported for now */
    if (fd != 0)
    {
        return E_ARG;
    }

    /* 1. Validation */
    int val_result = validate_user_pointer(buf, len);
    if (val_result != E_OK)
    {
        uart_printf("[SYS_READ] Validation FAILED: buf=0x%08x, len=%u, err=%d\n",
                    (uint32_t)buf, len, val_result);
        return E_PTR;
    }

    if (len == 0)
        return 0;

    /* Blocking read: wait_event() parks the task in uart_rx_wq until
     * the RX IRQ handler calls wake_up(). Only len=1 is supported. */
    char *c_buf = (char *)buf;

    wait_event(uart_rx_wq, uart_rx_available() > 0);

    int c = uart_getc();
    if (c == -1)
    {
        return 0;
    }
    *c_buf = (char)c;
    return 1;
}

/* sys_get_tasks(process_info_t *buf, uint32_t max_count) */
static int32_t sys_get_tasks(struct svc_context *ctx)
{
    void *buf = (void *)ctx->r0;
    uint32_t max_count = (uint32_t)ctx->r1;

    // Validate buffer (size = max_count * sizeof(process_info_t))
    uint32_t size = max_count * sizeof(process_info_t);
    if (validate_user_pointer(buf, size) != E_OK)
    {
        return E_PTR;
    }

    return scheduler_get_tasks(buf, max_count);
}

/* sys_get_meminfo(mem_info_t *buf) */
extern uint8_t _text_start[];
extern uint8_t _text_end[];
extern uint8_t _data_start[];
extern uint8_t _data_end[];
extern uint8_t _bss_start[];
extern uint8_t _bss_end[];
extern uint8_t _stack_start[];
extern uint8_t _svc_stack_top[];
extern uint8_t _kernel_end[];

static int32_t sys_get_meminfo(struct svc_context *ctx)
{
    mem_info_t *buf = (mem_info_t *)ctx->r0;

    if (validate_user_pointer(buf, sizeof(mem_info_t)) != E_OK)
    {
        return E_PTR;
    }

    buf->total = PLATFORM_DDR_SIZE_MB * 1024 * 1024;

    buf->kernel_text = (uint32_t)_text_end - (uint32_t)_text_start;
    buf->kernel_data = (uint32_t)_data_end - (uint32_t)_data_start;
    buf->kernel_bss = (uint32_t)_bss_end - (uint32_t)_bss_start;
    buf->kernel_stack = (uint32_t)_svc_stack_top - (uint32_t)_stack_start;

    uint32_t kernel_end = (uint32_t)_kernel_end;
    buf->free = buf->total - (kernel_end - PLATFORM_DDR_PA_BASE);

    return E_OK;
}

/* sys_open(const char *path, int flags) */
static int32_t sys_open(struct svc_context *ctx)
{
    const char *path = (const char *)ctx->r0;
    int flags = (int)ctx->r1;

    /* Validate path pointer (estimate max path length) */
    if (validate_user_pointer(path, MAX_PATH) != E_OK)
    {
        return E_PTR;
    }

    /* Call VFS layer */
    return vfs_open(path, flags);
}

/* sys_read_file(int fd, void *buf, uint32_t len) */
static int32_t sys_read_file(struct svc_context *ctx)
{
    int fd = (int)ctx->r0;
    void *buf = (void *)ctx->r1;
    uint32_t len = (uint32_t)ctx->r2;

    /* CRITICAL: Validate buffer + length range as requested */
    if (validate_user_pointer(buf, len) != E_OK)
    {
        return E_PTR;
    }

    /* Call VFS layer */
    return vfs_read(fd, buf, len);
}

static int32_t sys_write_file(struct svc_context *ctx)
{
    int fd = (int)ctx->r0;
    const void *buf = (const void *)ctx->r1;
    uint32_t len = (uint32_t)ctx->r2;

    if (validate_user_pointer(buf, len) != E_OK)
    {
        return E_PTR;
    }

    return vfs_write(fd, buf, len);
}

/* sys_close(int fd) */
static int32_t sys_close(struct svc_context *ctx)
{
    int fd = (int)ctx->r0;

    /* Call VFS layer */
    return vfs_close(fd);
}

/* sys_listdir(const char *path, file_info_t *entries, uint32_t max_entries) */
static int32_t sys_listdir(struct svc_context *ctx)
{
    const char *path = (const char *)ctx->r0;
    file_info_t *entries = (file_info_t *)ctx->r1;
    uint32_t max_entries = (uint32_t)ctx->r2;

    /* Validate path pointer */
    if (validate_user_pointer(path, MAX_PATH) != E_OK)
    {
        return E_PTR;
    }

    /* Validate entries buffer */
    uint32_t entries_size = max_entries * sizeof(file_info_t);
    if (validate_user_pointer(entries, entries_size) != E_OK)
    {
        return E_PTR;
    }

    /* Call VFS layer */
    return vfs_listdir(path, entries, max_entries);
}

/* sys_exec(const char *path) */
static int32_t sys_exec(struct svc_context *ctx)
{
    const char *path = (const char *)ctx->r0;

    if (validate_user_pointer(path, MAX_PATH) != E_OK) {
        return E_PTR;
    }

    return do_exec(path, ctx);
}

/* ============================================================
 * SVC Handler (Dispatcher)
 * ============================================================ */
void svc_handler(struct svc_context *ctx)
{
    /*
     * ABI:
     * R7 = Syscall Number
     * R0-R3 = Arguments
     * Return value -> R0
     */
    uint32_t syscall_num = ctx->r7;
    int32_t result = E_INVAL;

    static uint32_t svc_call_count = 0;
    svc_call_count++;

    /* DEBUG: Print every 5000 SVC calls */
    // if (svc_call_count % 5000 == 0) {
    //     uart_printf("[SVC] Call #%u: syscall=%u (YIELD=%u, READ=%u)\n",
    //                 svc_call_count, syscall_num, SYS_YIELD, SYS_READ);
    // }

    // TRACE_SCHED("SVC Entry: ID=%d, Args=0x%x, 0x%x", syscall_num, ctx->r0, ctx->r1);

    switch (syscall_num)
    {
    case SYS_WRITE:
        result = sys_write(ctx);
        break;

    case SYS_EXIT:
        result = sys_exit(ctx);
        break;

    case SYS_YIELD:
        result = sys_yield(ctx);
        break;

    case SYS_READ:
        result = sys_read(ctx);
        break;

    case SYS_GET_TASKS:
        result = sys_get_tasks(ctx);
        break;

    case SYS_GET_MEMINFO:
        result = sys_get_meminfo(ctx);
        break;

    case SYS_OPEN:
        result = sys_open(ctx);
        break;

    case SYS_READ_FILE:
        result = sys_read_file(ctx);
        break;

    case SYS_CLOSE:
        result = sys_close(ctx);
        break;

    case SYS_LISTDIR:
        result = sys_listdir(ctx);
        break;

    case SYS_EXEC:
        result = sys_exec(ctx);
        break;

    case SYS_WRITE_FILE:
        result = sys_write_file(ctx);
        break;

    case SYS_GETPID: {
        struct task_struct *t = scheduler_current_task();
        result = t ? t->pid : -1;
        break;
    }

    case SYS_GETPPID: {
        struct task_struct *t = scheduler_current_task();
        result = t ? t->ppid : -1;
        break;
    }

    case SYS_FORK:
        result = do_fork(ctx);
        break;

    case SYS_DUP: {
        result = vfs_dup((int)ctx->r0);
        break;
    }

    case SYS_DUP2: {
        result = vfs_dup2((int)ctx->r0, (int)ctx->r1);
        break;
    }

    case SYS_KILL: {
        int pid = (int)ctx->r0;
        int sig = (int)ctx->r1;
        /* MVP: any kill delivers SIGKILL. Exit status = 128 + sig for POSIX feel. */
        int exit_code = (sig > 0) ? (128 + sig) : 137;  /* 137 = 128 + 9 */
        result = do_kill_by_pid(pid, exit_code);
        break;
    }

    case SYS_WAIT: {
        int st = 0;
        int pid = do_wait(&st);
        if (pid >= 0 && ctx->r0 != 0)
        {
            int *user_status = (int *)ctx->r0;
            if (validate_user_pointer(user_status, sizeof(int)) == E_OK)
            {
                *user_status = st;
            }
        }
        result = pid;
        break;
    }

    default:
        uart_printf("[SVC] ERROR: Unknown Syscall %d\n", syscall_num);
        result = E_INVAL;
        break;
    }

    /* Write return value back to User Context R0 */
    ctx->r0 = result;
}