/*
 * Zapret GUI — applog.h
 * Colored event log based on RICHEDIT50W.
 */
#ifndef ZG_APPLOG_H
#define ZG_APPLOG_H

#include <windows.h>

HWND zg_log_create(HWND parent, int x, int y, int w, int h);
void zg_log_append(HWND hLog, int level, const wchar_t* text);
void zg_log_clear(HWND hLog);
void zg_log_apply_theme(HWND hLog);   /* re-style colors after a theme switch */

#endif /* ZG_APPLOG_H */
