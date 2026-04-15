/* ============================================================
 * payload.S
 * ------------------------------------------------------------
 * Embeds the shell user application binary into the kernel's
 * read-only data section.
 * ============================================================ */

.syntax unified
.section .rodata

.global _shell_payload_start
.global _shell_payload_end

.align 4
_shell_payload_start:
    .incbin "../userspace/build/apps/shell/shell.bin"
_shell_payload_end:
