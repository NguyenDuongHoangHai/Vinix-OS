/* ============================================================
 * net/http.h
 * ------------------------------------------------------------
 * HTTP/1.0 static server — kernel task, no userspace socket API.
 * Chưa sử dụng — chờ Phase 4 (Application layer).
 * ============================================================ */

#ifndef NET_HTTP_H
#define NET_HTTP_H

#include "types.h"

void http_server_init(uint16_t port);
void http_server_task_entry(void);

#endif /* NET_HTTP_H */
