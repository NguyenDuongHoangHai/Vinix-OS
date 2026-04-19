/* ============================================================
 * wait.c
 * ------------------------------------------------------------
 * do_exit  — mark task ZOMBIE, wake parent.
 * do_wait  — block until any child zombies, reap it.
 * ============================================================ */

#include "proc.h"
#include "task.h"
#include "scheduler.h"
#include "wait_queue.h"
#include "mmu.h"
#include "page_alloc.h"
#include "slab.h"
#include "uart.h"
#include "types.h"

/* Single global queue — every parent waiting in wait() blocks here.
 * wake_up_all is not needed because each waiter rechecks its own
 * children list via wait_event(). */
static wait_queue_head_t parent_wq = { .head = 0 };

extern volatile bool need_reschedule;

static int has_zombie_child(int ppid)
{
    for (uint32_t i = 0; i < MAX_TASKS; i++)
    {
        struct task_struct *t = tasks_array_get(i);
        if (t != 0 && t->ppid == ppid && t->state == TASK_STATE_ZOMBIE)
        {
            return 1;
        }
    }
    return 0;
}

void do_exit(int status)
{
    struct task_struct *me = scheduler_current_task();
    if (me == 0) return;

    me->exit_status = status;
    me->state       = TASK_STATE_ZOMBIE;

    /* Wake every parent waiter — they individually re-check their own
     * children list. Simple pattern, fine at MAX_TASKS=5. */
    while (parent_wq.head != 0)
    {
        wake_up(&parent_wq);
    }

    /* Yield — scheduler picks someone else. We never come back. */
    need_reschedule = true;
    scheduler_yield();

    /* Fallback if a bug lets us return. */
    while (1) { }
}

int do_wait(int *status_out)
{
    struct task_struct *me = scheduler_current_task();
    if (me == 0) return -1;

    wait_event(parent_wq, has_zombie_child(me->pid));

    for (uint32_t i = 0; i < MAX_TASKS; i++)
    {
        struct task_struct *t = tasks_array_get(i);
        if (t != 0 && t->ppid == me->pid && t->state == TASK_STATE_ZOMBIE)
        {
            int pid = t->pid;
            int st  = t->exit_status;

            /* Release resources allocated in do_fork. */
            if (t->pgd_pa != 0 && t->pgd_pa != mmu_kernel_pgd_pa())
            {
                mmu_free_pgd(t->pgd_pa);
            }
            /* Note: user memory + kernel stack tracked separately — leak
             * for now. Proper bookkeeping needs a per-task resource list,
             * deferred to a follow-up batch. */

            scheduler_release_slot(i);
            kfree(t);

            if (status_out != 0) *status_out = st;
            return pid;
        }
    }

    return -1;
}
