/* ============================================================
 * payload.S
 * ------------------------------------------------------------
 * Embeds the init program. Kernel copies it to USER_SPACE_VA
 * on boot and runs it as PID 1. The shell (and everything else)
 * lives on the FAT32 rootfs and is exec'd by init.
 *
 * Symbol names are kept as _shell_payload_* for source-compat
 * with the boot path that predates the init model.
 * ============================================================ */

.syntax unified
.section .rodata

.global _shell_payload_start
.global _shell_payload_end

.align 4
_shell_payload_start:
    .incbin "../userspace/build/apps/init/init.bin"
_shell_payload_end:
