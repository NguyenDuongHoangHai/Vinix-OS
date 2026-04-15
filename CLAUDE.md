# CLAUDE.md — VinixOS Project Guide

---

## 1. Hard Constraints

BẮT BUỘC tuân thủ mọi lúc. Không có ngoại lệ.

**Memory — KHÔNG được:**

- Dùng `malloc`/`free` — chỉ static allocation
- Dùng buffer lớn trên stack (kernel stack nhỏ)

**Standard Library — KHÔNG được:**

- `#include <stdio.h>`, `<stdlib.h>` trong kernel code
- Dùng `printf()` — phải dùng `uart_printf()`
- Dùng bất cứ libc function nào — chỉ dùng `libc/src/string.c` nội bộ

**Hardware Access — BẮT BUỘC:**

- Truy cập register chỉ qua `mmio_read32(addr)` / `mmio_write32(addr, val)`
- Lấy register address và bit definition từ AM335x TRM — KHÔNG được đoán

**Toolchain — KHÔNG được:**

- Mix `arm-none-eabi` và `arm-linux-gnueabihf`
- Build kernel trước userspace (thứ tự: `userspace` → `kernel`, luôn luôn)

**VinCC Subset C — CHỈ áp dụng cho user program biên dịch bằng VinCC. KHÔNG áp dụng cho kernel (kernel dùng `arm-none-eabi-gcc` full C):**

- `struct`, `union`, `enum`, `float`, `double`
- `++` / `--` — dùng `i = i + 1`
- Variadic function, standard library
- Hơn 4 tham số hàm (r0–r3 theo AAPCS)

**Design — KHÔNG được:**

- Dùng Linux BSP hoặc SDK thương mại
- Dùng emulator — chỉ chạy trên hardware thật
- Thêm abstraction layer không cần thiết

**Reference Code (`docs_trainingAI/drivers/*/source/`) — BẮT BUỘC:**

- Reference source (omap_hsmmc.c, FatFs, ...) CHỈ dùng để hiểu logic và sequence
- Viết lại từ đầu theo convention của VinixOS (naming, logging, register access, static allocation)
- KHÔNG được copy nguyên block code — mỗi dòng phải được hiểu và adapt
- Reference code thường dùng Linux API, malloc, pattern không tương thích với VinixOS — luôn verify trước khi adapt

---

## 2. Project Context

**VinixOS** — bare-metal ARM platform tự xây từ đầu, chạy trực tiếp trên BeagleBone Black (SoC AM3358, Cortex-A8, ARMv7-A). Không Linux, không emulator, không SDK thương mại.

Hai component:
- **VinixOS** — kernel + bootloader + userspace (C + ARM assembly)
- **VinCC** — Python cross-compiler: Subset C → ARMv7-A ELF32

**Toolchain:**
- Kernel/bootloader → `arm-none-eabi-gcc` (bare-metal, no libc)
- VinCC runtime/output → `arm-linux-gnueabihf-as`, `arm-linux-gnueabihf-ld`

**Thứ tự build — luôn luôn:**

```bash
make -C VinixOS userspace   # TRƯỚC — kernel nhúng shell payload
make -C VinixOS kernel      # SAU  — kernel lấy shell mới nhất
```

### Memory Map (quick reference)

| Region | VA | PA | Thuộc tính |
| --- | --- | --- | --- |
| User space | `0x40000000` | `0x80500000` | 1 MB, RW, cached |
| Kernel DDR | `0xC0000000` | `0x80000000` | 5 MB, kernel-only, cached |
| L4 WKUP (PRCM, UART0, WDT1, Control Module) | `0x44E00000` | identity | 1 MB, device, Strongly Ordered |
| L4 PER (INTC, DMTimer, I2C, MMC0) | `0x48000000` | identity | 4 MB, device, Strongly Ordered |
| Framebuffer | `0x80800000` | identity | 4 MB, non-cacheable |

### Knowledge Base

Tài liệu hardware thẩm quyền ở `docs_trainingAI/`:

- `project_context.md` — đọc trước khi bắt đầu session mới
- `am335x/` — AM335x TRM (27 chapter)
- `arm-arch/` — ARM architecture reference
- `hardware-beagleboneblack/` — BBB P8/P9 pinout + schematic
- `drivers/<name>/index.md` — pointer theo từng driver (TRM chapter liên quan + reference source)

LUÔN đọc `drivers/<name>/index.md` trước khi viết driver mới.

---

## 3. Concurrency & State

VinixOS có **scheduler preemptive round-robin**. Nhiều task có thể chạy, nhưng hiện tại chỉ shell chạm tới filesystem / driver.

**Invariant hiện tại (có thể tin):**

- Chỉ task shell gọi syscall đụng FS / MMC / driver
- Mọi driver I/O đều polled (chưa có IRQ-driven I/O)
- Buffer global static (ví dụ `sector_buf`, `fat_cache`) an toàn vì chỉ có 1 consumer duy nhất — được serial hóa

**TRƯỚC KHI thêm task thứ hai chạm vào bất kỳ thứ nào ở trên:**

1. PHẢI thêm lock quanh shared mutable state, HOẶC
2. PHẢI disable preemption quanh critical section, HOẶC
3. PHẢI document rõ lý do task mới không thể race với caller hiện có

**Không bao giờ giả định static buffer là an toàn** chỉ vì "hiện tại không có gì khác dùng nó". Document invariant trong comment ngay chỗ declare, và verify lại trước khi thêm caller mới.

---

## 4. Comments

**Mặc định: không viết comment.**

Chỉ viết comment khi một trong các trigger sau xảy ra:

1. Ràng buộc hardware không hiển nhiên (thứ tự theo spec, reset sau error, clock dependency)
2. Invariant tinh tế mà reader tương lai có thể phá (buffer sharing, yêu cầu init order)
3. Marker `CRITICAL:` cho code interrupt-mode hoặc nhạy cảm
4. Section separator để navigate file >300 dòng

KHÔNG viết comment để:

- Giải thích dòng code làm gì khi code đã nói rồi
- Lặp lại điều mà tên register hoặc tên function đã nói
- Mô tả task/phase hiện tại ("Phase 1 stub", "TODO sprint 3")
- Tham chiếu PR / issue / fix cũ

### Good vs bad — ví dụ cụ thể

❌ **BAD** (giải thích cái gì):
```c
/* Clear status register */
mmio_write32(MMC_STAT, 0xFFFFFFFF);

/* Set command argument */
mmio_write32(MMC_ARG, arg);

/* Wait for TC (Transfer Complete) */
while ((mmio_read32(MMC_STAT) & MMC_STAT_TC) == 0) { ... }
```

✓ **GOOD** (giải thích tại sao — hardware spec hoặc invariant):
```c
/* SD spec: voltage must be set BEFORE power. Reversed order leaves
 * the card in undefined state. */
mmio_write32(MMC_HCTL, MMC_HCTL_SDVS_3_3V);
mmio_write32(MMC_HCTL, mmio_read32(MMC_HCTL) | MMC_HCTL_SDBP);

/* CRITICAL: Must switch to SVC mode before calling context_switch */
cps     #0x13
```

### Javadoc cho public function

**Mặc định: không viết Javadoc.** Tên function tốt + parameter list là tài liệu đủ.

Chỉ viết Javadoc khi:

- Function có precondition không hiển nhiên ("phải gọi sau X")
- Có side effect mà signature không lộ ra ("flush write cache")
- Giá trị return cần làm rõ ("trả E_AGAIN nếu queue đầy")

❌ **BAD** (lặp lại signature):
```c
/**
 * Close file descriptor
 * @param fd File descriptor
 * @return 0 on success, negative error code on failure
 */
int vfs_close(int fd);
```

✓ **GOOD** (1 dòng precondition có giá trị):
```c
/* Does NOT flush write buffers — caller must sync first. */
int vfs_close(int fd);
```

### Debug print

Khi tạm comment out trong lúc bring-up: dùng `//` để grep lại được. Xóa hẳn sau khi feature ổn định.

### Logging format

BẮT BUỘC prefix mọi `uart_printf` bằng `[MODULE]` viết hoa:

```c
uart_printf("[TIMER] Initializing DMTimer2...\n");
uart_printf("[SCHED] ERROR: No tasks to run!\n");
```

---

## 5. Coding Style

4 space, không bao giờ dùng tab. Snake_case cho variable/function, UPPER_SNAKE cho macro.

**Public symbol BẮT BUỘC có module prefix:** `uart_*`, `scheduler_*`, `intc_*`, `vfs_*`, ...

**File layout** (mọi file `.c`):

1. File header banner
2. `#include` (dùng `""`, không `<>` trừ `<stdarg.h>`)
3. `#define` cho register/field local
4. `static` state (variable + forward decl)
5. Static helper
6. Public function

**Template file header:**
```c
/* ============================================================
 * filename.c
 * ------------------------------------------------------------
 * Mô tả 1 dòng
 * ============================================================ */
```

**Braces:**
- Function: `{` xuống dòng mới
- Control flow (`if`/`while`/`for`): `{` cùng dòng

**Assembly file:** inline comment dùng `@`, không phải `#`. Banner giống C.

**Mọi thứ khác** (spacing, line wrap, include order): giữ nhất quán với file xung quanh trong cùng module. Không tranh luận về style trong PR.

---

## 6. Driver Development Workflow

CRITICAL: Trước khi viết driver mới, verify TẤT CẢ item trong checklist.

**NẾU THIẾU BẤT KỲ ITEM NÀO — DỪNG NGAY. KHÔNG ĐOÁN. Report lại cho user:**

```text
Để viết driver cho [peripheral], tôi cần bạn cung cấp từ docs_trainingAI/:
- [item còn thiếu] → AM335x TRM Ch.XX
- [item còn thiếu] → AM335x TRM Ch.XX
```

**Checklist bắt buộc:**

| Thông tin | Nguồn |
| --- | --- |
| Base address của peripheral | AM335x TRM Ch.02 — Memory Map |
| Register offset + bit definition | TRM chapter của peripheral |
| Clock enable sequence (CM_PER / CM_WKUP) | AM335x TRM Ch.08 — PRCM |
| IRQ number (nếu dùng interrupt) | AM335x TRM Ch.06 — Interrupts |
| Pin mux / CONF_* (nếu cần) | AM335x TRM Ch.09 — Control Module |
| Reset sequence | TRM chapter của peripheral |

KHÔNG được copy register address từ project khác mà không verify với AM335x TRM.

---

## 7. Debug Workflow

Không có JTAG trong workflow bình thường. **`uart_printf` là công cụ debug DUY NHẤT.**

### Khi user báo bug

BẮT BUỘC gom đủ các thông tin sau TRƯỚC KHI phân tích hoặc đề xuất fix:

- Toàn bộ UART log từ đầu boot
- Dòng log cuối cùng trước khi hang / crash
- Loại exception nếu có (Data Abort / Prefetch Abort / Undefined)
- Thao tác user làm ngay trước khi bug xảy ra

**KHÔNG đoán nguyên nhân khi chưa có UART log.**

### Debug bằng uart_printf

Thêm checkpoint print trước và sau bước nghi ngờ:

```c
uart_printf("[MODULE] Step X: before\n");
/* operation */
uart_printf("[MODULE] Step X: after — reg = 0x%08x\n", mmio_read32(REG));
```

BẮT BUỘC print readback của register, không chỉ print giá trị vừa ghi:

```c
mmio_write32(BASE + OFFSET, value);
uart_printf("[DRV] wrote 0x%08x, readback = 0x%08x\n",
            value, mmio_read32(BASE + OFFSET));
```

### Exception / Abort

Khi gặp Data Abort hoặc Prefetch Abort, yêu cầu user cung cấp:

```text
Chạy lại và cung cấp:
1. Toàn bộ UART log
2. DFAR — Data Fault Address Register
3. DFSR — Data Fault Status Register
4. PC tại thời điểm abort
```

Nếu exception handler chưa print các register đó — đề xuất thêm vào `exception_handlers.c` TRƯỚC khi tiếp tục debug.

---

## 8. Definition of Done

Task **chưa xong** cho đến khi tất cả item phù hợp dưới đây được thỏa mãn.

**Driver / kernel feature:**

- Compile sạch (không warning)
- User đã confirm thành công trên BeagleBone Black thật — không bao giờ tự giả định
- Boot log sạch (không có dòng error bất ngờ)

**Refactor / cleanup:**

- Compile sạch
- Không thay đổi behavior — kernel binary size bằng hoặc nhỏ hơn là dấu hiệu tốt
- Nói rõ với user rằng thay đổi preserve behavior để họ có thể skip re-test

**Bug fix:**

- Xác định được root cause (không chỉ suppress triệu chứng)
- UART log cho thấy failure mode đã biến mất
- User không báo regression ở khu vực liên quan

**KHÔNG BAO GIỜ tuyên bố "complete" hoặc "works" chỉ dựa trên compile thành công.** Luôn nói "build sạch, bạn test trên hardware giúp" và chờ.

---

## 9. Scope Discipline

Match phạm vi hành động chính xác với điều user hỏi.

**Khi user nói "fix X", KHÔNG đụng Y** ngay cả khi Y có vấn đề tương tự. Ghi nhận Y như một follow-up riêng.

**Khi không chắc file có trong scope hay không, HỎI** — không extend task im lặng. "Nhân tiện tôi cũng..." là con đường dẫn đến over-refactor.

**Refactor đụng hơn 5 file cần user confirm rõ ràng** trước khi bắt đầu. Trình bày scope estimate trước, chờ go-ahead.

**Prefer rolling cleanup hơn big-bang refactor.** Khi một vấn đề style/quality tồn tại khắp codebase, chỉ fix nó trong file đang sửa vì lý do khác.

Khi feature mới đụng đến architecture (scheduler, VFS, MMU, syscall ABI), dừng lại và propose design trước khi viết code.
