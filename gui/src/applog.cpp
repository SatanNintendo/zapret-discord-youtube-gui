/*
 * Zapret GUI — applog.cpp
 * Colored event log based on RICHEDIT50W (msftedit.dll).
 * Timestamped lines; color per severity; auto-scroll to the end;
 * trimmed when it grows beyond ~200 KB.
 */
#include <windows.h>
#include <stdio.h>
#include <richedit.h>
#include "common.h"
#include "applog.h"

#ifndef MSFTEDIT_CLASS
#define MSFTEDIT_CLASS L"RICHEDIT50W"
#endif

#define LOG_COLOR_INFO  COL_LOG_INFO
#define LOG_COLOR_OK    COL_GREEN
#define LOG_COLOR_WARN  COL_YELLOW
#define LOG_COLOR_ERR   COL_RED
#define LOG_COLOR_TIME  COL_LOG_TIME

HWND zg_log_create(HWND parent, int x, int y, int w, int h)
{
    static HMODULE hMsft = NULL;
    if (!hMsft) hMsft = LoadLibraryW(L"msftedit.dll");
    if (!hMsft) return NULL;

    HWND hLog = CreateWindowExW(0,
                                MSFTEDIT_CLASS, L"",
                                WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                                ES_READONLY | ES_MULTILINE |
                                ES_AUTOVSCROLL | ES_NOHIDESEL,
                                x, y, w, h, parent, (HMENU)(INT_PTR)900,
                                GetModuleHandleW(NULL), NULL);
    if (!hLog) return NULL;

    SendMessageW(hLog, EM_SETBKGNDCOLOR, 0, (LPARAM)COL_BG_DARK);
    SendMessageW(hLog, EM_SETEVENTMASK, 0, 0);
    SendMessageW(hLog, EM_SETLIMITTEXT, 0, 0);   /* max capacity */
    SendMessageW(hLog, EM_SETTEXTMODE, (WPARAM)TM_MULTILEVELUNDO, 0);

    zg_log_apply_theme(hLog);

    return hLog;
}

/* re-apply the palette-dependent styling (background + default font) */
void zg_log_apply_theme(HWND hLog)
{
    if (!hLog) return;
    SendMessageW(hLog, EM_SETBKGNDCOLOR, 0, (LPARAM)COL_BG_DARK);

    CHARFORMAT2W cf;
    ZeroMemory(&cf, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR | CFM_FACE | CFM_SIZE;
    cf.crTextColor = COL_LOG_INFO;
    cf.yHeight = MulDiv(9 * 20, g_scale_pct, 100);   /* 9pt, DPI-scaled */
    wcscpy(cf.szFaceName, L"Consolas");
    SendMessageW(hLog, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
}

static void log_append_formatted(HWND hLog, COLORREF color, const wchar_t* stamp,
                                 const wchar_t* text)
{
    /* timestamp part */
    GETTEXTLENGTHEX gtl;
    gtl.flags = GTL_DEFAULT;
    gtl.codepage = 1200;
    LRESULT len = SendMessageW(hLog, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0);
    if (len < 0) len = 0;

    if (len > 200000) {
        /* trim old half */
        SendMessageW(hLog, EM_SETSEL, 0, 100000);
        SendMessageW(hLog, EM_REPLACESEL, FALSE, (LPARAM)L"");
        len = 100000;
    }

    SendMessageW(hLog, EM_SETSEL, (WPARAM)len, (LPARAM)len);

    CHARFORMAT2W cf;
    ZeroMemory(&cf, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR;
    cf.crTextColor = color;
    SendMessageW(hLog, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

    wchar_t line[1400];
    _snwprintf(line, 1398, L"[%s]  %s\r\n", stamp, text);
    line[1398] = 0;
    SendMessageW(hLog, EM_REPLACESEL, FALSE, (LPARAM)line);

    /* scroll to end */
    SendMessageW(hLog, EM_SCROLLCARET, 0, 0);
    SendMessageW(hLog, EM_SETSEL, (WPARAM)0, (LPARAM)0);
}

void zg_log_append(HWND hLog, int level, const wchar_t* text)
{
    if (!hLog || !text) return;

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t stamp[16];
    _snwprintf(stamp, 15, L"%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
    stamp[15] = 0;

    COLORREF c;
    switch (level) {
    case ZLOG_OK:   c = LOG_COLOR_OK;   break;
    case ZLOG_WARN: c = LOG_COLOR_WARN; break;
    case ZLOG_ERR:  c = LOG_COLOR_ERR;  break;
    case ZLOG_TIME: c = LOG_COLOR_TIME; break;
    default:        c = LOG_COLOR_INFO; break;
    }
    log_append_formatted(hLog, c, stamp, text);
}

void zg_log_clear(HWND hLog)
{
    if (!hLog) return;
    SendMessageW(hLog, EM_SETSEL, 0, -1);
    SendMessageW(hLog, EM_REPLACESEL, FALSE, (LPARAM)L"");
}
