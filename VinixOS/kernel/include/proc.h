/* ============================================================
 * proc.h
 * ------------------------------------------------------------
 * fork/wait/exit — process lifecycle.
 * ============================================================ */

#ifndef PROC_H
#define PROC_H

#include "types.h"

struct svc_context;

int  do_fork(struct svc_context *parent_ctx);
void do_exit(int status);
int  do_wait(int *status_out);
int  do_kill_by_pid(int pid, int exit_status);

#endif
