/*
 * Zapret GUI — netops.cpp
 * WinHTTP operations (mirrors the update behaviour of service.bat):
 *   - version check  : .service/version.txt  vs LOCAL_VERSION
 *   - ipset update   : .service/ipset-service.txt -> lists/ipset-all.txt
 *   - hosts check    : .service/hosts -> compare with system hosts file
 */
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <winhttp.h>
#include <shellapi.h>
#include "common.h"
#include "zapret.h"
#include "netops.h"

#define UPSTREAM_BASE  L"raw.githubusercontent.com"
#define UPSTREAM_REPO  L"/Flowseal/zapret-discord-youtube/refs/heads/main/"
#define RELEASES_URL   L"https://github.com/Flowseal/zapret-discord-youtube/releases/latest"

/* ================================================================== */
/* logging from worker threads                                         */
/* ================================================================== */

void zg_log_from_worker(int level, const wchar_t* fmt, ...)
{
    wchar_t buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(buf, 1023, fmt, ap);
    va_end(ap);
    buf[1023] = 0;

    /* marshal to the UI thread (freed there) */
    wchar_t* copy = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, (wcslen(buf) + 1) * sizeof(wchar_t));
    if (!copy) return;
    wcscpy(copy, buf);
    HWND hMain = FindWindowW(ZG_WND_CLASS, NULL);
    if (hMain) SendMessageW(hMain, WM_APP_LOG, (WPARAM)level, (LPARAM)copy);
    else HeapFree(GetProcessHeap(), 0, copy);
}

/* ================================================================== */
/* tiny HTTP GET via WinHTTP (returns heap buffer, sets size)          */
/* ================================================================== */

static char* http_get(const wchar_t* path, DWORD* out_size, DWORD* out_status)
{
    *out_size = 0;
    *out_status = 0;

    HINTERNET hSession = WinHttpOpen(L"ZapretGUI/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return NULL;

    /* TLS + redirects */
    DWORD flags = WINHTTP_FLAG_SECURE;
    HINTERNET hConnect = WinHttpConnect(hSession, UPSTREAM_BASE, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return NULL; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path,
                                            NULL, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return NULL; }

    /* cache-buster like the bat scripts do */
    wchar_t headers[128];
    _snwprintf(headers, 127, L"Cache-Control: no-cache\r\nPragma: no-cache\r\n");
    headers[127] = 0;

    BOOL ok = WinHttpSendRequest(hRequest, headers, (DWORD)-1L,
                                 WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
           && WinHttpReceiveResponse(hRequest, NULL);

    char* data = NULL;
    if (ok) {
        DWORD status = 0, sz = sizeof(status);
        WinHttpQueryHeaders(hRequest,
                            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX,
                            &status, &sz, WINHTTP_NO_HEADER_INDEX);
        *out_status = status;
        if (status == 200) {
            DWORD cap = 1024 * 1024, len = 0;
            data = (char*)HeapAlloc(GetProcessHeap(), 0, cap);
            if (data) {
                DWORD avail = 0;
                while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0) {
                    if (len + avail + 1 > cap) {
                        char* nb = (char*)HeapReAlloc(GetProcessHeap(), 0, data, cap * 2);
                        if (!nb) { data = NULL; break; }
                        data = nb; cap *= 2;
                    }
                    DWORD rd = 0;
                    if (!WinHttpReadData(hRequest, data + len, avail, &rd) || rd == 0) break;
                    len += rd;
                }
                if (data) {
                    data[len] = 0;
                    *out_size = len;
                }
            }
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return data;
}

/* ================================================================== */
/* version check                                                       */
/* ================================================================== */

void zg_net_version_check(void)
{
    zg_log_from_worker(ZLOG_INFO, L"Проверка обновлений zapret…");

    const wchar_t* ver_c = zg_local_version();
    wchar_t ver[32];
    wcsncpy(ver, ver_c, 31);
    ver[31] = 0;

    DWORD size = 0, status = 0;
    char* resp = http_get(UPSTREAM_REPO L".service/version.txt", &size, &status);
    if (!resp || status != 200 || size == 0) {
        zg_log_from_worker(ZLOG_WARN,
            L"Не удалось получить версию из репозитория (проверка пропущена)");
        if (resp) HeapFree(GetProcessHeap(), 0, resp);
        return;
    }
    /* trim */
    char* end = resp + size;
    while (end > resp && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ')) *(--end) = 0;
    for (char* q = resp; *q; q++)
        if (*q == '\n' || *q == '\r') { *q = 0; break; }

    wchar_t remote[32];
    MultiByteToWideChar(CP_UTF8, 0, resp, -1, remote, 32);
    HeapFree(GetProcessHeap(), 0, resp);

    if (wcscmp(ver, remote) == 0) {
        zg_log_from_worker(ZLOG_OK, L"Установлена последняя версия: %s", ver);
    } else {
        zg_log_from_worker(ZLOG_WARN,
            L"Доступна новая версия: %s (установлена %s). Открываю страницу релиза…",
            remote, ver);
        ShellExecuteW(NULL, L"open", RELEASES_URL, NULL, NULL, SW_SHOWNORMAL);
    }
}

/* ================================================================== */
/* ipset update                                                        */
/* ================================================================== */

void zg_net_ipset_update(void)
{
    zg_log_from_worker(ZLOG_INFO, L"Обновление списка IPSet…");

    DWORD size = 0, status = 0;
    char* resp = http_get(UPSTREAM_REPO L".service/ipset-service.txt", &size, &status);
    if (!resp || status != 200 || size == 0) {
        zg_log_from_worker(ZLOG_ERR,
            L"Не удалось скачать список IPSet (код %lu)", status);
        if (resp) HeapFree(GetProcessHeap(), 0, resp);
        return;
    }

    wchar_t listp[MAX_PATH];
    _snwprintf(listp, MAX_PATH, L"%sipset-all.txt", g_paths.lists_dir);
    listp[MAX_PATH-1] = 0;

    HANDLE h = CreateFileW(listp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        zg_log_from_worker(ZLOG_ERR, L"Не удалось записать lists\\ipset-all.txt");
        HeapFree(GetProcessHeap(), 0, resp);
        return;
    }
    DWORD wr = 0;
    WriteFile(h, resp, size, &wr, NULL);
    CloseHandle(h);

    /* count lines for the log message */
    int lines = 1;
    for (DWORD i = 0; i < size; i++)
        if (resp[i] == '\n') lines++;
    HeapFree(GetProcessHeap(), 0, resp);

    zg_log_from_worker(ZLOG_OK, L"Список IPSet обновлён (%d строк, %lu байт)", lines, size);
}

/* ================================================================== */
/* hosts check                                                         */
/* ================================================================== */

/* case-insensitive substring search in a wide buffer */
static const wchar_t* wcasestr(const wchar_t* hay, const wchar_t* needle)
{
    if (!*needle) return hay;
    for (; *hay; hay++) {
        const wchar_t* a = hay, *b = needle;
        while (*a && *b && towlower(*a) == towlower(*b)) { a++; b++; }
        if (!*b) return hay;
    }
    return NULL;
}

void zg_net_hosts_check(void)
{
    zg_log_from_worker(ZLOG_INFO, L"Проверка файла hosts…");

    DWORD size = 0, status = 0;
    char* resp = http_get(UPSTREAM_REPO L".service/hosts", &size, &status);
    if (!resp || status != 200 || size == 0) {
        zg_log_from_worker(ZLOG_ERR,
            L"Не удалось скачать hosts из репозитория (код %lu)", status);
        if (resp) HeapFree(GetProcessHeap(), 0, resp);
        return;
    }

    /* save to %TEMP%\zapret_hosts.txt like service.bat does */
    wchar_t tmp[MAX_PATH], tempdir[MAX_PATH];
    GetTempPathW(MAX_PATH, tempdir);
    _snwprintf(tmp, MAX_PATH, L"%szapret_hosts.txt", tempdir);
    tmp[MAX_PATH-1] = 0;

    HANDLE h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        zg_log_from_worker(ZLOG_ERR, L"Не удалось создать временный файл");
        HeapFree(GetProcessHeap(), 0, resp);
        return;
    }
    DWORD wr = 0;
    WriteFile(h, resp, size, &wr, NULL);
    CloseHandle(h);

    /* convert to wide for comparison */
    int wlen = MultiByteToWideChar(CP_UTF8, 0, resp, (int)size, NULL, 0);
    wchar_t* whosts = (wchar_t*)HeapAlloc(GetProcessHeap(), 0,
                                          ((size_t)wlen + 1) * sizeof(wchar_t));
    if (!whosts) { HeapFree(GetProcessHeap(), 0, resp); return; }
    MultiByteToWideChar(CP_UTF8, 0, resp, (int)size, whosts, wlen);
    whosts[wlen] = 0;
    HeapFree(GetProcessHeap(), 0, resp);

    /* first & last non-empty lines of downloaded hosts */
    wchar_t first[256] = L"", last[256] = L"";
    wchar_t* p = whosts;
    BOOL got_first = FALSE;
    while (*p) {
        while (*p == L'\r' || *p == L'\n') p++;
        if (!*p) break;
        wchar_t* eol = wcspbrk(p, L"\r\n");
        size_t len = eol ? (size_t)(eol - p) : wcslen(p);
        if (len > 0 && len < 255) {
            if (!got_first) {
                wcsncpy(first, p, len); first[len] = 0; got_first = TRUE;
            }
            wcsncpy(last, p, len); last[len] = 0;
        }
        if (!eol) break;
        p = eol;
    }

    /* read system hosts */
    wchar_t sysp[MAX_PATH];
    GetSystemDirectoryW(sysp, MAX_PATH);
    wcscat(sysp, L"\\drivers\\etc\\hosts");

    BOOL needs_update = FALSE;
    DWORD n = 0;
    wchar_t* sysh = zg_read_file_w(sysp, &n);
    if (!sysh) {
        needs_update = TRUE;
        zg_log_from_worker(ZLOG_WARN, L"Не удалось прочитать системный hosts");
    } else {
        if (first[0] && !wcasestr(sysh, first)) {
            zg_log_from_worker(ZLOG_WARN,
                L"Первая строка из репозитория не найдена в hosts");
            needs_update = TRUE;
        }
        if (last[0] && !wcasestr(sysh, last)) {
            zg_log_from_worker(ZLOG_WARN,
                L"Последняя строка из репозитория не найдена в hosts");
            needs_update = TRUE;
        }
        HeapFree(GetProcessHeap(), 0, sysh);
    }
    HeapFree(GetProcessHeap(), 0, whosts);

    if (needs_update) {
        zg_log_from_worker(ZLOG_TIME, L"—");
        zg_log_from_worker(ZLOG_WARN,
            L"Hosts требует обновления: скопируйте строки из открывшегося файла");
        zg_log_from_worker(ZLOG_WARN,
            L"в %ls (файл hosts — открыть от имени администратора)", sysp);
        zg_log_from_worker(ZLOG_TIME, L"—");
        /* open notepad + explorer like service.bat :hosts_update */
        ShellExecuteW(NULL, L"open", L"notepad.exe", tmp, NULL, SW_SHOWNORMAL);

        wchar_t sel[MAX_PATH + 64];
        _snwprintf(sel, MAX_PATH + 63, L"/select,\"%s\"", sysp);
        sel[MAX_PATH + 63] = 0;
        HINSTANCE hr = ShellExecuteW(NULL, L"open", L"explorer.exe", sel, NULL, SW_SHOWNORMAL);
        if ((INT_PTR)hr <= 32) {
            /* fallback: just open the folder */
            wchar_t* slash = wcsrchr(sysp, L'\\');
            if (slash) *slash = 0;
            ShellExecuteW(NULL, L"open", sysp, NULL, NULL, SW_SHOWNORMAL);
        }
    } else {
        zg_log_from_worker(ZLOG_OK, L"Hosts актуален");
        DeleteFileW(tmp);
    }
}
