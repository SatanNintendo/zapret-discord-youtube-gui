/*
 * Zapret GUI — netops.h
 * WinHTTP operations: version check, ipset list update, hosts check.
 * All run on a worker thread; results are marshalled to the UI thread.
 */
#ifndef ZG_NETOPS_H
#define ZG_NETOPS_H

#include <windows.h>

/* runs on the worker thread; logs via zg_log_from_worker */
void zg_net_version_check(void);
void zg_net_ipset_update(void);
void zg_net_hosts_check(void);

/* call from any thread — posts a message to the UI thread for appending */
void zg_log_from_worker(int level, const wchar_t* fmt, ...);

#endif /* ZG_NETOPS_H */
