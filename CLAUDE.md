# CLAUDE.md — VinixOS Project Guide

---

## 1. Hard Constraints

MUST follow these rules at all times. No exceptions.

**Memory — MUST NOT:**

- Use `malloc`/`free` — static allocation only
- Use large stack-allocated buffers (kernel stack is small)

**Standard Library — MUST NOT:**

- `#include <stdio.h>`, `<stdlib.h>` in kernel code
- Use `printf()` — use `uart_printf()` instead
- Use any libc function — use `libc/src/string.c` internal only

**Hardware Access — MUST:**

- Access registers only via `mmio_read32(addr)` / `mmio_write32(addr, val)`
- Take all register addresses and bit definitions from AM335x TRM — DO NOT guess

**Toolchain — MUST NOT:**

- Mix `arm-none-eabi` and `arm-linux-gnueabihf` toolchains
- Build kernel before userspace (`userspace` → `kernel`, always)

**VinCC Subset C — these features DO NOT EXIST in the compiler:**

- `struct`, `union`, `enum`, `float`, `double`
- `++` / `--` operators — use `i = i + 1`
- Variadic functions, standard library
- More than 4 function parameters (r0–r3, AAPCS)

**Design — MUST NOT:**

- Use Linux BSP or any commercial SDK
- Use emulator — runs on real hardware only
- Add abstraction layers that are not necessary

---

## 2. Project Context

**VinixOS** là bare-metal ARM software platform xây từ đầu, chạy trực tiếp trên phần cứng thật (BeagleBone Black — SoC AM3358, CPU ARMv7-A Cortex-A8). Không dùng Linux, không emulator, không SDK thương mại.

Project gồm 2 components: **VinixOS** (kernel + bootloader + userspace, viết bằng C và ARM Assembly) và **VinCC** (cross-compiler Subset C → ARMv7-A ELF32, viết bằng Python).

**Toolchain:**

- Kernel / Bootloader → `arm-none-eabi-gcc` (bare-metal, no libc)
- VinCC runtime / output → `arm-linux-gnueabihf-as`, `arm-linux-gnueabihf-ld`

**Build commands:**

```bash
# Sau khi thay đổi userspace code — chạy lệnh này TRƯỚC:
make -C VinixOS userspace

# Sau khi thay đổi kernel code, hoặc sau khi build userspace:
make -C VinixOS kernel

# VinCC runtime (khi thay đổi crt0.S, syscalls.S, divmod.S):
cd CrossCompiler && make runtime

# Compile chương trình với VinCC:
python3 -m toolchain.main -o <out> <src.c>
```

---

## 3. Project Structure

```text
Vinix-OS/
├── VinixOS/
│   ├── bootloader/             ← MLO, chạy ở SRAM 0x402F0400
│   ├── kernel/
│   │   ├── src/
│   │   │   ├── arch/arm/       ← entry.S, MMU, context_switch.S, exception vectors
│   │   │   ├── drivers/        ← uart, timer, intc, i2c, lcdc, fb, tda19988, watchdog
│   │   │   ├── kernel/         ← main, scheduler, syscalls, MMU, VFS, RAMFS
│   │   │   └── ui/             ← boot_screen (boot log, splash, home launcher)
│   │   ├── include/            ← kernel headers
│   │   ├── libc/               ← string.c, types.h (nội bộ, không dùng stdlib)
│   │   └── linker/kernel.ld
│   ├── userspace/              ← shell app, crt0.S, syscall wrappers
│   ├── initfs/                 ← files nhúng vào RAMFS lúc build
│   └── docs/                   ← 9 tài liệu kỹ thuật
│
├── CrossCompiler/
│   ├── toolchain/
│   │   ├── frontend/           ← Lexer, Parser, Semantic Analyzer
│   │   ├── middleend/ir/       ← IR Generator (3-address code)
│   │   ├── backend/armv7a/     ← Code Generator, Register Allocator
│   │   └── runtime/            ← crt0.S, syscalls.S, divmod.S, app.ld
│   └── docs/                   ← 5 tài liệu kỹ thuật
│
├── docs_trainingAI/            ← AM335x TRM + ARM Assembly reference
│   └── index.md                ← tra cứu chapter theo subsystem
│
├── scripts/                    ← flash_sdcard.sh, setup-environment.sh
├── CLAUDE.md
└── Makefile
```

---

## 4. Coding Style & Conventions

### C — Cấu trúc file

Mỗi `.c` MUST theo đúng thứ tự:

1. File header banner
2. `#include` (chỉ dùng `""`, không dùng `<>` trừ stdarg)
3. `#define` — nhóm theo chức năng, mỗi nhóm có comment header
4. `static` variables
5. Static (internal) functions
6. Public functions

File header — MUST có, không được bỏ:

```c
/* ============================================================
 * filename.c
 * ------------------------------------------------------------
 * Brief one-line description
 * ============================================================ */
```

Section separator — dùng để chia nhóm logic trong file:

```c
/* ============================================================
 * Section Title
 * ============================================================ */
```

### C — Indentation & Braces

- 4 spaces, MUST NOT dùng tab
- Hàm: dấu `{` xuống dòng mới
- `if` / `while` / `for`: dấu `{` cùng dòng
- Khai báo local variables ở đầu block, trước code

```c
void uart_init(void)
{
    uint32_t val;

    if (condition) {
        /* ... */
    }
}
```

### C — Comments

- MUST dùng `/* */` cho mọi comment trong code thật
- `//` CHỈ dùng để comment out debug code — MUST NOT xóa, giữ lại để trace
- Comment giải thích **tại sao**, không phải **cái gì**

MUST dùng `CRITICAL:` cho code nhạy cảm hardware/interrupt:

```c
/* CRITICAL: Must switch to SVC mode before calling context_switch */
```

`#define` groups MUST có comment header:

```c
/* UART Register Offsets */
#define UART_RHR    0x00

/* LSR bits */
#define LSR_DR      (1 << 0)
```

### C — Function Documentation

Public functions MUST dùng Javadoc:

```c
/**
 * Short description.
 *
 * @param name  Description
 * @return      0 on success, -1 on error
 */
```

Hàm phức tạp (ISR, critical path) MUST dùng section separator kèm CONTRACT:

```c
/* ============================================================
 * timer_irq_handler - DMTimer2 overflow ISR
 * ============================================================
 *
 * CONTRACT:
 * - Must clear interrupt status at peripheral
 * - Must be fast — no blocking operations allowed
 * CRITICAL: Runs in IRQ mode
 */
```

### C — Log Format

MUST có prefix `[MODULE]` viết hoa trong mọi `uart_printf`:

```c
uart_printf("[TIMER] Initializing DMTimer2...\n");
uart_printf("[SCHED] ERROR: No tasks to run!\n");
```

### C — Naming

- Variables & functions: `snake_case` — e.g., `task_count`, `uart_init`
- Public functions: `module_verb_noun` — e.g., `scheduler_yield`, `intc_enable_irq`
- Macros / constants: `UPPER_SNAKE_CASE` — e.g., `UART0_BASE`, `LSR_DR`
- Structs: `module_thing` / `module_thing_t` — e.g., `task_struct`, `process_info_t`
- Public symbols MUST có module prefix: `uart_*`, `scheduler_*`, `intc_*`

### Assembly (`.S`)

File header MUST giống C banner. Phần mô tả ghi rõ: assumptions, flow, register usage.

Mỗi logical block MUST có section separator và đánh số bước:

```asm
    /* ========================================================
     * 1. Clear .bss section
     * ========================================================
     * MMU is OFF — subtract VA_OFFSET to get PA.
     * ======================================================== */
    ldr     r0, =_bss_start
```

Inline comment MUST dùng `@`, NOT `#`:

```asm
cps     #0x1F               @ Switch to System Mode
ldr     sp, =_svc_stack_top @ Reload SVC stack to VA
```

`.extern` MUST khai báo ở cuối file, sau toàn bộ code.

---

## 5. Driver Development Workflow

CRITICAL: Before writing any new driver, verify ALL items in this checklist.

**IF ANY ITEM IS MISSING — STOP IMMEDIATELY. DO NOT GUESS. Report to user:**

```text
Để viết driver cho [peripheral], tôi cần bạn cung cấp từ docs_trainingAI/:
- [item còn thiếu] → AM335x TRM Ch.XX
- [item còn thiếu] → AM335x TRM Ch.XX
```

**Checklist bắt buộc:**

| Thông tin | Nguồn |
| --- | --- |
| Base address của peripheral | AM335x TRM Ch.02 — Memory Map |
| Register offsets + bit definitions | TRM chapter của peripheral |
| Clock enable sequence (CM_PER / CM_WKUP) | AM335x TRM Ch.08 — PRCM |
| IRQ number (nếu dùng interrupt) | AM335x TRM Ch.06 — Interrupts |
| Pin mux / CONF_* (nếu cần) | AM335x TRM Ch.09 — Control Module |
| Reset sequence | TRM chapter của peripheral |

MUST NOT copy register addresses from other projects without verifying against AM335x TRM.

---

## 6. Debug Workflow

No JTAG in normal workflow. **`uart_printf` is the ONLY debug tool.**

### Khi user báo bug

MUST hỏi đủ các thông tin sau trước khi phân tích hoặc đề xuất fix:

- Toàn bộ UART log từ đầu boot
- Dòng log cuối cùng trước khi hang / crash
- Loại exception nếu có (Data Abort / Prefetch Abort / Undefined Instruction)
- Thao tác thực hiện trước khi bug xảy ra

DO NOT đoán nguyên nhân khi chưa có UART log.

### Debug bằng uart_printf

Thêm checkpoint print trước và sau mỗi bước nghi ngờ:

```c
uart_printf("[MODULE] Step X: before\n");
/* operation */
uart_printf("[MODULE] Step X: after — reg = 0x%08x\n", mmio_read32(REG));
```

MUST print readback value sau khi ghi register, không chỉ print giá trị vừa ghi:

```c
mmio_write32(BASE + OFFSET, value);
uart_printf("[DRV] wrote 0x%08x, readback = 0x%08x\n",
            value, mmio_read32(BASE + OFFSET));
```

Sau khi fix: comment out debug prints bằng `//`, MUST NOT xóa.

### Exception / Abort

Khi gặp Data Abort hoặc Prefetch Abort, MUST yêu cầu user cung cấp:

```text
Bạn có thể chạy lại và cung cấp:
1. Toàn bộ UART log
2. DFAR — Data Fault Address Register
3. DFSR — Data Fault Status Register
4. PC tại thời điểm abort
```

Nếu exception handler chưa print các register đó — đề xuất thêm vào `exception_handlers.c` trước khi debug tiếp.
