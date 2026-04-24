/* ============================================================
 * errno.h
 * ------------------------------------------------------------
 * Kernel-side error codes — same values as vinixlibc errno.h
 * so set_errno_from() maps correctly to userspace.
 * ============================================================ */

#ifndef KERNEL_ERRNO_H
#define KERNEL_ERRNO_H

#define EIO      5    /* I/O error */
#define EAGAIN   11   /* Try again / resource temporarily unavailable */
#define ENOMEM   12   /* Out of memory */
#define EINVAL   22   /* Invalid argument */

#endif /* KERNEL_ERRNO_H */
