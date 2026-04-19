/* ============================================================
 * mmu_lab.c
 * ------------------------------------------------------------
 * L2 page-table smoke test.
 * ============================================================ */

#include "mmu.h"
#include "mmu_lab.h"
#include "uart.h"
#include "types.h"

/* Lab alias — picked outside every active mapping. Section
 * 0x50000000 is not used by kernel, user, peripherals, or FB. */
#define MMU_LAB_ALIAS_VA  0x50000000

/* L2 table: 256 entries * 4 bytes = 1 KB. Must be 1 KB aligned. */
static uint32_t lab_l2[256]
    __attribute__((aligned(1024), used, section(".data")));

/* Test page: 4 KB aligned, remapped through lab_l2. */
static uint32_t lab_page[1024]
    __attribute__((aligned(4096), used, section(".data")));

static inline uint32_t va_to_pa(uint32_t va) { return va - VA_OFFSET; }

void mmu_lab_run(void)
{
    uint32_t *alias_va = (uint32_t *)MMU_LAB_ALIAS_VA;
    uint32_t  page_pa  = va_to_pa((uint32_t)lab_page);

    uart_printf("[MMU_LAB] === L2 smoke test ===\n");
    uart_printf("[MMU_LAB] lab_l2  VA=0x%x PA=0x%x\n",
                (uint32_t)lab_l2, va_to_pa((uint32_t)lab_l2));
    uart_printf("[MMU_LAB] lab_page VA=0x%x PA=0x%x\n",
                (uint32_t)lab_page, page_pa);

    for (int i = 0; i < 256; i++)
    {
        lab_l2[i] = 0;
    }

    /* Signature through kernel VA — before any alias exists. */
    lab_page[0] = 0xDEADBEEF;
    __asm__ __volatile__("dsb\n\t" ::: "memory");

    mmu_install_page_table(MMU_LAB_ALIAS_VA, lab_l2, MMU_DOMAIN_KERNEL);
    lab_l2[0] = (page_pa & 0xFFFFF000) | MMU_L2_SMALL_KERN_RW;

    __asm__ __volatile__("dsb\n\t" ::: "memory");
    mmu_flush_tlb();

    uint32_t via_alias = *(volatile uint32_t *)alias_va;
    uart_printf("[MMU_LAB] wrote 0x%x via kernel VA, read via alias = 0x%x\n",
                lab_page[0], via_alias);
    if (via_alias != 0xDEADBEEF)
    {
        uart_printf("[MMU_LAB] FAIL: alias read mismatch\n");
        return;
    }

    *(volatile uint32_t *)alias_va = 0xCAFEBABE;
    __asm__ __volatile__("dsb\n\t" ::: "memory");

    uint32_t via_kernel = lab_page[0];
    uart_printf("[MMU_LAB] wrote 0xCAFEBABE via alias, read via kernel VA = 0x%x\n",
                via_kernel);
    if (via_kernel != 0xCAFEBABE)
    {
        uart_printf("[MMU_LAB] FAIL: kernel read mismatch\n");
        return;
    }

    lab_l2[0] = 0;
    __asm__ __volatile__("dsb\n\t" ::: "memory");
    mmu_flush_tlb();

    uart_printf("[MMU_LAB] PASS — L2 mapping + TLB flush verified\n");
}
