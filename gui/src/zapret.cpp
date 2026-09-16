/*
 * Zapret GUI — zapret.c
 * Core logic: strategy discovery & parsing, bypass launch/stop,
 * Windows service management, filter states, status polling.
 *
 * The behaviour mirrors upstream Flowseal/zapret-discord-youtube:
 *   - general*.bat  : launch winws.exe with a strategy
 *   - service.bat   : install/remove the "zapret" service, filters, etc.
 */
#include <windows.h>
#include <stdio.h>
#include <ctype.h>
#include <wctype.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <shlobj.h>
#include "common.h"
#include "zapret.h"
#include "lang.h"

ZgPaths g_paths;

static HANDLE g_child     = NULL;   /* winws.exe started by this GUI */
static DWORD  g_child_pid = 0;

/* ================================================================== */
/* helpers                                                             */
/* ================================================================== */

static void path_join(wchar_t* dst, size_t cap, const wchar_t* dir, const wchar_t* leaf)
{
    _snwprintf(dst, cap, L"%s\\%s", dir, leaf);
    dst[cap - 1] = 0;
}

/* read a whole file, convert UTF-8 -> UTF-16; caller HeapFree()s result */
wchar_t* zg_read_file_w(const wchar_t* path, DWORD* out_chars)
{
    *out_chars = 0;
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || sz.QuadPart > 8 * 1024 * 1024) {
        CloseHandle(h); return NULL;
    }
    char* buf = (char*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)sz.QuadPart + 1);
    if (!buf) { CloseHandle(h); return NULL; }
    DWORD rd = 0;
    if (!ReadFile(h, buf, (DWORD)sz.QuadPart, &rd, NULL) || rd == 0) {
        HeapFree(GetProcessHeap(), 0, buf); CloseHandle(h); return NULL;
    }
    CloseHandle(h);
    buf[rd] = 0;
    /* skip UTF-8 BOM */
    char* p = buf;
    if (rd >= 3 && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF)
        p += 3;
    int need = MultiByteToWideChar(CP_UTF8, 0, p, -1, NULL, 0);
    if (need <= 0) { HeapFree(GetProcessHeap(), 0, buf); return NULL; }
    wchar_t* w = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, (size_t)need * sizeof(wchar_t));
    if (!w) { HeapFree(GetProcessHeap(), 0, buf); return NULL; }
    MultiByteToWideChar(CP_UTF8, 0, p, -1, w, need);
    HeapFree(GetProcessHeap(), 0, buf);
    *out_chars = (DWORD)wcslen(w);
    return w;
}

static wchar_t* read_file_bytes_as_w(const wchar_t* path, DWORD* out_chars)
{
    /* same as zg_read_file_w — kept as an alias for call sites below */
    return zg_read_file_w(path, out_chars);
}

/* replace all occurrences of `from` with `to` in-place (from/to different length ok) */
static void wstr_replace_all(wchar_t* s, size_t cap, const wchar_t* from, const wchar_t* to)
{
    size_t fl = wcslen(from), tl = wcslen(to);
    if (!fl) return;
    wchar_t* pos = s;
    while ((pos = wcsstr(pos, from)) != NULL) {
        size_t rest_len = wcslen(pos + fl) + 1;              /* incl. NUL */
        if ((size_t)(pos - s) + tl + rest_len > cap) break;  /* won't fit */
        memmove(pos + tl, pos + fl, rest_len * sizeof(wchar_t));
        wcsncpy(pos, to, tl);
        pos += tl;
    }
}

/* ================================================================== */
/* paths: locate the zapret folder                                     */
/* ================================================================== */

/* dirs examined by the last autosearch — shown in the log so the
 * user sees exactly where the app looked before picking manually   */
static wchar_t g_search_report[900] = L"";

static void search_report_add(const wchar_t* dir)
{
    if (!dir || !dir[0]) return;
    size_t used = wcslen(g_search_report);
    size_t need = wcslen(dir) + 4;
    if (used + need + 1 >= sizeof(g_search_report) / sizeof(wchar_t))
        return;  /* report is full — keep the newest entries out, fine */
    if (used) { g_search_report[used++] = L';'; g_search_report[used++] = L' '; }
    wcscpy(g_search_report + used, dir);
}

const wchar_t* zg_search_report(void)
{
    return g_search_report[0] ? g_search_report : zg_str(S_ERR_NO_SEARCH);
}

BOOL zg_dir_has_winws(const wchar_t* dir)
{
    if (!dir || !dir[0]) return FALSE;
    wchar_t p[MAX_PATH];
    _snwprintf(p, MAX_PATH, L"%s\\bin\\winws.exe", dir);
    p[MAX_PATH - 1] = 0;
    DWORD attr = GetFileAttributesW(p);
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

/* set the effective zapret base dir and all derived paths */
BOOL zg_set_base_dir(const wchar_t* dir)
{
    if (!dir || !dir[0]) return FALSE;
    wcsncpy(g_paths.exe_dir, dir, MAX_PATH - 1);
    g_paths.exe_dir[MAX_PATH - 1] = 0;

    /* strip trailing backslashes (keep "C:\" root as-is is impossible —
     * "C:\" would become "C:", which Windows still resolves fine) */
    size_t n = wcslen(g_paths.exe_dir);
    while (n > 0 && g_paths.exe_dir[n - 1] == L'\\') g_paths.exe_dir[--n] = 0;
    if (n == 0) { g_paths.exe_dir[0] = L'.'; g_paths.exe_dir[1] = 0; }

    _snwprintf(g_paths.bin_dir,   MAX_PATH, L"%s\\bin\\",   g_paths.exe_dir);
    _snwprintf(g_paths.lists_dir, MAX_PATH, L"%s\\lists\\", g_paths.exe_dir);
    _snwprintf(g_paths.utils_dir, MAX_PATH, L"%s\\utils\\", g_paths.exe_dir);
    _snwprintf(g_paths.winws_path, MAX_PATH, L"%s\\bin\\winws.exe", g_paths.exe_dir);
    g_paths.bin_dir[MAX_PATH-1] = g_paths.lists_dir[MAX_PATH-1] = 0;
    g_paths.utils_dir[MAX_PATH-1] = g_paths.winws_path[MAX_PATH-1] = 0;

    g_paths.files_ok = zg_dir_has_winws(g_paths.exe_dir);

    /* local version cache must be re-parsed from the new service.bat */
    zg_local_version_invalidate();
    return g_paths.files_ok;
}

/* ---- remembered dir (%APPDATA%\ZapretGUI\settings.ini, [zapret] dir) ---- */

static void dir_config_path(wchar_t* out, DWORD cap)
{
    wchar_t ad[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, ad)))
        _snwprintf(out, cap, L"%s\\ZapretGUI\\settings.ini", ad);
    else
        _snwprintf(out, cap, L"%s\\gui-settings.ini",
                   g_paths.real_exe_dir[0] ? g_paths.real_exe_dir : L".");
    out[cap - 1] = 0;
}

void zg_save_dir_config(const wchar_t* dir)
{
    wchar_t p[MAX_PATH]; dir_config_path(p, MAX_PATH);
    wchar_t d[MAX_PATH];
    wcsncpy(d, p, MAX_PATH - 1); d[MAX_PATH - 1] = 0;
    wchar_t* slash = wcsrchr(d, L'\\');
    if (slash) { *slash = 0; CreateDirectoryW(d, NULL); }
    WritePrivateProfileStringW(L"zapret", L"dir", dir, p);
}

BOOL zg_load_dir_config(wchar_t* out, DWORD cap)
{
    wchar_t p[MAX_PATH]; dir_config_path(p, MAX_PATH);
    GetPrivateProfileStringW(L"zapret", L"dir", L"", out, cap, p);
    /* drop a possible trailing backslash */
    size_t n = wcslen(out);
    while (n > 0 && out[n - 1] == L'\\') out[--n] = 0;
    return out[0] != 0;
}

/* scan `root`'s immediate subfolders whose name contains "zapret"
 * (covers zapret-discord-youtube-vX.X sitting next to the exe)      */
static BOOL search_zapret_subdirs(const wchar_t* root, wchar_t* out, DWORD cap)
{
    wchar_t pat[MAX_PATH];
    _snwprintf(pat, MAX_PATH, L"%s\\*zapret*", root);
    pat[MAX_PATH - 1] = 0;

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    BOOL ok = FALSE;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;

        wchar_t cand[MAX_PATH];
        _snwprintf(cand, MAX_PATH, L"%s\\%s", root, fd.cFileName);
        cand[MAX_PATH - 1] = 0;
        search_report_add(cand);
        if (zg_dir_has_winws(cand)) {
            wcsncpy(out, cand, cap - 1); out[cap - 1] = 0;
            ok = TRUE;
            break;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return ok;
}

/*
 * Full auto-search. Candidate order:
 *   1. dir remembered in %APPDATA%\ZapretGUI\settings.ini
 *   2. the exe dir itself
 *   3. exe dir's parents (up to 3 levels up)
 *   4. each of those dirs' subfolders matching *zapret*
 * A candidate is valid when <dir>\bin\winws.exe exists.
 */
BOOL zg_autosearch_dir(wchar_t* out, DWORD cap)
{
    if (out && cap) out[0] = 0;
    g_search_report[0] = 0;
    if (!out || !cap) return FALSE;

    /* 1) remembered dir */
    wchar_t saved[MAX_PATH];
    if (zg_load_dir_config(saved, MAX_PATH) && saved[0]) {
        search_report_add(saved);
        if (zg_dir_has_winws(saved)) {
            wcsncpy(out, saved, cap - 1); out[cap - 1] = 0;
            return TRUE;
        }
    }

    /* 2-3) exe dir and its parents */
    wchar_t cur[MAX_PATH];
    wcsncpy(cur, g_paths.real_exe_dir, MAX_PATH - 1);
    cur[MAX_PATH - 1] = 0;

    for (int level = 0; level <= 3 && cur[0]; level++) {
        search_report_add(cur);
        if (zg_dir_has_winws(cur)) {
            wcsncpy(out, cur, cap - 1); out[cap - 1] = 0;
            return TRUE;
        }
        /* 4) subfolders with "zapret" in the name */
        if (search_zapret_subdirs(cur, out, cap))
            return TRUE;

        /* go one level up */
        wchar_t* slash = wcsrchr(cur, L'\\');
        if (!slash) break;
        *slash = 0;
        /* "C:\foo" -> "C:" is fine (Windows treats it as the drive),
         * but "\\server" (UNC) must not be cut further — stop there  */
        if (cur[0] == L'\\' && !cur[1]) break;
    }
    return FALSE;
}

void zg_paths_init(void)
{
    ZgPaths zero;
    ZeroMemory(&zero, sizeof(zero));
    g_paths = zero;

    wchar_t self[MAX_PATH];
    GetModuleFileNameW(NULL, self, MAX_PATH);
    wchar_t* slash = wcsrchr(self, L'\\');
    if (slash) *slash = 0;
    wcsncpy(g_paths.real_exe_dir, self, MAX_PATH - 1);
    g_paths.real_exe_dir[MAX_PATH - 1] = 0;

    wchar_t base[MAX_PATH] = L"";
    g_paths.detect = ZG_DET_NONE;

    if (zg_autosearch_dir(base, MAX_PATH)) {
        if (_wcsicmp(base, g_paths.real_exe_dir) == 0)
            g_paths.detect = ZG_DET_AUTO_EXE;
        else {
            wchar_t saved[MAX_PATH];
            if (zg_load_dir_config(saved, MAX_PATH) &&
                _wcsicmp(base, saved) == 0)
                g_paths.detect = ZG_DET_CONFIG;
            else
                g_paths.detect = ZG_DET_AUTO_PARENT;
        }
    } else {
        wcscpy(base, self);   /* fallback: exe dir, files_ok = FALSE */
    }

    zg_set_base_dir(base);
}

/* storage for version string (parsed lazily, reset on rebase) */
static wchar_t g_local_version[32] = L"";

void zg_local_version_invalidate(void)
{
    g_local_version[0] = 0;
}

const wchar_t* zg_local_version(void)
{
    if (g_local_version[0]) return g_local_version;
    wchar_t sb[MAX_PATH]; path_join(sb, MAX_PATH, g_paths.exe_dir, L"service.bat");
    DWORD n;
    wchar_t* txt = read_file_bytes_as_w(sb, &n);
    if (txt) {
        wchar_t* p = wcsstr(txt, L"LOCAL_VERSION=");
        if (p) {
            p += wcslen(L"LOCAL_VERSION=");
            int i = 0;
            while (*p && *p != L'"' && *p != L'\r' && *p != L'\n' && i < 15)
                g_local_version[i++] = *p++;
            g_local_version[i] = 0;
        }
        HeapFree(GetProcessHeap(), 0, txt);
    }
    if (!g_local_version[0]) wcscpy(g_local_version, L"?");
    return g_local_version;
}

/* ================================================================== */
/* strategy discovery (natural sort)                                   */
/* ================================================================== */

static int wnatcmp(const wchar_t* a, const wchar_t* b)
{
    while (*a && *b) {
        if (iswdigit(*a) && iswdigit(*b)) {
            /* compare full digit runs numerically */
            const wchar_t *da = a, *db = b;
            while (iswdigit(*a)) a++;
            while (iswdigit(*b)) b++;
            size_t la = (size_t)(a - da), lb = (size_t)(b - db);
            if (la != lb) return (la < lb) ? -1 : 1;
            int r = CompareStringW(LOCALE_INVARIANT, 0, da, (int)la, db, (int)lb) - CSTR_EQUAL;
            if (r) return r < 0 ? -1 : 1;
        } else {
            if (*a != *b) return (*a < *b) ? -1 : 1;
            a++; b++;
        }
    }
    if (*a) return 1;
    if (*b) return -1;
    return 0;
}

static int __cdecl strat_cmp(const void* pa, const void* pb)
{
    const wchar_t* a = *(const wchar_t**)pa;
    const wchar_t* b = *(const wchar_t**)pb;
    int r = wnatcmp(a, b);
    if (r) return r;
    return wcscmp(a, b);
}

int zg_find_strategies(wchar_t*** out_list)
{
    *out_list = NULL;
    int cap = 32, count = 0;
    wchar_t** list = (wchar_t**)HeapAlloc(GetProcessHeap(), 0, cap * sizeof(wchar_t*));
    if (!list) return 0;

    wchar_t pattern[MAX_PATH];
    _snwprintf(pattern, MAX_PATH, L"%s\\*.bat", g_paths.exe_dir);
    pattern[MAX_PATH-1] = 0;

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (_wcsnicmp(fd.cFileName, L"service", 7) == 0) continue;  /* skip service.bat */
            if (count == cap) {
                cap *= 2;
                wchar_t** nl = (wchar_t**)HeapReAlloc(GetProcessHeap(), 0, list, cap * sizeof(wchar_t*));
                if (!nl) break;
                list = nl;
            }
            size_t len = wcslen(fd.cFileName);
            wchar_t* copy = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, (len + 1) * sizeof(wchar_t));
            if (!copy) continue;
            wcscpy(copy, fd.cFileName);
            list[count++] = copy;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }

    if (count > 1) qsort(list, (size_t)count, sizeof(wchar_t*), strat_cmp);
    *out_list = list;
    return count;
}

void zg_free_strategies(wchar_t** list, int count)
{
    if (!list) return;
    for (int i = 0; i < count; i++) HeapFree(GetProcessHeap(), 0, list[i]);
    HeapFree(GetProcessHeap(), 0, list);
}

/* ================================================================== */
/* strategy .bat parsing — mirrors service.bat :service_install parser */
/* ================================================================== */

static void expand_vars(wchar_t* tok, size_t cap)
{
    /* game filter ports */
    int mode = zg_gamefilter_get();
    const wchar_t* gf_tcp = L"12";
    const wchar_t* gf_udp = L"12";
    if (mode == ZG_GAME_ALL)      { gf_tcp = L"1024-65535"; gf_udp = L"1024-65535"; }
    else if (mode == ZG_GAME_TCP) { gf_tcp = L"1024-65535"; }
    else if (mode == ZG_GAME_UDP) { gf_udp = L"1024-65535"; }

    wstr_replace_all(tok, cap, L"%BIN%", g_paths.bin_dir);
    wstr_replace_all(tok, cap, L"%LISTS%", g_paths.lists_dir);
    wstr_replace_all(tok, cap, L"%GameFilterTCP%", gf_tcp);
    wstr_replace_all(tok, cap, L"%GameFilterUDP%", gf_udp);
    wstr_replace_all(tok, cap, L"%GameFilter%", gf_tcp);
    /* caret-escaped exclamation mark used inside bat strategies */
    wstr_replace_all(tok, cap, L"^!", L"!");
}

BOOL zg_parse_strategy(const wchar_t* bat_name, wchar_t* args, size_t args_cap)
{
    args[0] = 0;
    if (!bat_name || !bat_name[0]) return FALSE;

    wchar_t path[MAX_PATH];
    path_join(path, MAX_PATH, g_paths.exe_dir, bat_name);

    DWORD n;
    wchar_t* txt = read_file_bytes_as_w(path, &n);
    if (!txt) return FALSE;

    /* walk logical lines (join ".... ^" continuations), find winws.exe line */
    wchar_t logical[16384];
    logical[0] = 0;
    BOOL capture = FALSE, found = FALSE;

    wchar_t* cur = txt;
    while (*cur && !found) {
        /* extract one physical line */
        wchar_t* eol = wcspbrk(cur, L"\r\n");
        size_t len = eol ? (size_t)(eol - cur) : wcslen(cur);
        wchar_t buf[8192];
        if (len >= 8192) len = 8191;
        wcsncpy(buf, cur, len);
        buf[len] = 0;

        /* trim right */
        wchar_t* e = buf + wcslen(buf);
        while (e > buf && *(e - 1) == L' ') *(--e) = 0;

        BOOL cont = (e > buf && *(e - 1) == L'^');
        if (cont) *(--e) = 0;                      /* strip trailing caret */

        if (!capture) {
            if (wcsstr(buf, L"winws.exe")) {
                capture = TRUE;
                wcscpy(logical, buf);
            }
        } else {
            /* guard the concat: logical + " " + buf must fit */
            if (wcslen(logical) + wcslen(buf) + 2 >=
                sizeof(logical) / sizeof(logical[0])) {
                break;
            }
            if (logical[0]) wcscat(logical, L" ");
            wcscat(logical, buf);
        }
        if (capture && !cont) { found = TRUE; break; }

        if (!eol) break;
        cur = eol + ((*eol == L'\r' && eol[1] == L'\n') ? 2 : 1);
    }

    BOOL ok = FALSE;
    if (found && logical[0]) {
        /* skip up to and including 'winws.exe' and its closing quote */
        wchar_t* p = wcsstr(logical, L"winws.exe");
        if (p) {
            p += wcslen(L"winws.exe");
            if (*p == L'"') p++;
            while (*p == L' ') p++;

            /* tokenize respecting quotes */
            while (*p) {
                wchar_t tok[2048];
                int tl = 0;
                while (*p == L' ') p++;
                BOOL inq = FALSE, got = FALSE;
                while (*p && tl < 2000) {
                    if (*p == L'"') inq = !inq;
                    else if (*p == L' ' && !inq) break;
                    tok[tl++] = *p++;
                    got = TRUE;
                }
                tok[tl] = 0;
                if (!got) break;

                wchar_t exp[4096];
                wcsncpy(exp, tok, 4095);
                exp[4095] = 0;
                expand_vars(exp, 4096);

                if (args[0]) wcscat(args, L" ");
                if (wcslen(args) + wcslen(exp) + 1 < args_cap)
                    wcscat(args, exp);
                else { ok = FALSE; break; }
            }
            ok = (args[0] != 0);
        }
    }

    HeapFree(GetProcessHeap(), 0, txt);
    return ok;
}

/* ================================================================== */
/* file-backed settings (game filter / ipset / update checks)           */
/* ================================================================== */

static void utils_path(wchar_t* out, size_t cap, const wchar_t* name)
{
    _snwprintf(out, cap, L"%s%s", g_paths.utils_dir, name);
    out[cap - 1] = 0;
}

int zg_gamefilter_get(void)
{
    wchar_t p[MAX_PATH]; utils_path(p, MAX_PATH, L"game_filter.enabled");
    DWORD n;
    wchar_t* txt = read_file_bytes_as_w(p, &n);
    if (!txt) return ZG_GAME_OFF;
    int mode = ZG_GAME_OFF;
    wchar_t first[32]; int i = 0;
    wchar_t* s = txt;
    while (*s && *s != L'\r' && *s != L'\n' && i < 15) first[i++] = *s++;
    first[i] = 0;
    if (_wcsicmp(first, L"all") == 0)      mode = ZG_GAME_ALL;
    else if (_wcsicmp(first, L"tcp") == 0) mode = ZG_GAME_TCP;
    else if (_wcsicmp(first, L"udp") == 0) mode = ZG_GAME_UDP;
    HeapFree(GetProcessHeap(), 0, txt);
    return mode;
}

BOOL zg_gamefilter_set(int mode)
{
    wchar_t p[MAX_PATH]; utils_path(p, MAX_PATH, L"game_filter.enabled");
    if (mode == ZG_GAME_OFF)
        return DeleteFileW(p) || GetLastError() == ERROR_FILE_NOT_FOUND;

    const wchar_t* s = (mode == ZG_GAME_ALL) ? L"all\r\n"
                    : (mode == ZG_GAME_TCP) ? L"tcp\r\n" : L"udp\r\n";
    HANDLE h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    DWORD wr;
    BOOL ok = WriteFile(h, s, (DWORD)(wcslen(s) * sizeof(wchar_t)), &wr, NULL);
    CloseHandle(h);
    return ok;
}

int zg_ipset_get(void)
{
    wchar_t p[MAX_PATH];
    _snwprintf(p, MAX_PATH, L"%sipset-all.txt", g_paths.lists_dir);
    p[MAX_PATH-1] = 0;
    DWORD n;
    wchar_t* txt = read_file_bytes_as_w(p, &n);
    if (!txt) return ZG_IPSET_ANY;         /* missing file acts like empty => any */

    /* first meaningful line */
    wchar_t* s = txt;
    while (*s == L'\r' || *s == L'\n' || *s == L' ' || *s == L'\t') s++;
    if (!*s) { HeapFree(GetProcessHeap(), 0, txt); return ZG_IPSET_ANY; }

    int lines = 0;
    for (wchar_t* q = txt; *q; q++)
        if (*q == L'\n') lines++;
    if (*txt && txt[wcslen(txt) - 1] != L'\n') lines++;

    int res;
    if (lines == 0) res = ZG_IPSET_ANY;
    else if (lines == 1 && wcsncmp(s, L"203.0.113.113/32", 17) == 0) res = ZG_IPSET_NONE;
    else res = ZG_IPSET_LOADED;

    HeapFree(GetProcessHeap(), 0, txt);
    return res;
}

BOOL zg_ipset_set(int mode)
{
    wchar_t listp[MAX_PATH], backup[MAX_PATH];
    _snwprintf(listp, MAX_PATH, L"%sipset-all.txt", g_paths.lists_dir);
    _snwprintf(backup, MAX_PATH, L"%sipset-all.txt.backup", g_paths.lists_dir);
    listp[MAX_PATH-1] = backup[MAX_PATH-1] = 0;

    int cur = zg_ipset_get();
    if (cur == mode) return TRUE;

    if (mode == ZG_IPSET_NONE) {
        /* keep the loaded list in .backup, write marker */
        if (cur == ZG_IPSET_LOADED) {
            if (!MoveFileW(listp, backup)) {
                /* backup may already exist — replace it */
                DeleteFileW(backup);
                if (!MoveFileW(listp, backup)) return FALSE;
            }
        }
        HANDLE h = CreateFileW(listp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) return FALSE;
        DWORD wr;
        BOOL ok = WriteFile(h, "203.0.113.113/32\r\n", 18, &wr, NULL);
        CloseHandle(h);
        return ok;
    }
    if (mode == ZG_IPSET_ANY) {
        HANDLE h = CreateFileW(listp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) return FALSE;
        CloseHandle(h);
        return TRUE;
    }
    /* mode == LOADED */
    if (cur == ZG_IPSET_NONE) {
        if (GetFileAttributesW(backup) == INVALID_FILE_ATTRIBUTES) return FALSE;
        DeleteFileW(listp);
        if (!MoveFileW(backup, listp)) return FALSE;
    }
    return TRUE;
}

BOOL zg_checkupdates_enabled(void)
{
    wchar_t p[MAX_PATH]; utils_path(p, MAX_PATH, L"check_updates.enabled");
    return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES;
}

BOOL zg_checkupdates_set(BOOL enable)
{
    wchar_t p[MAX_PATH]; utils_path(p, MAX_PATH, L"check_updates.enabled");
    if (enable) {
        HANDLE h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) return FALSE;
        DWORD wr;
        BOOL ok = WriteFile(h, "ENABLED\r\n", 9, &wr, NULL);
        CloseHandle(h);
        return ok;
    }
    return DeleteFileW(p) || GetLastError() == ERROR_FILE_NOT_FOUND;
}

void zg_ensure_user_lists(void)
{
    struct { const wchar_t* name; const char* content; } files[] = {
        { L"ipset-exclude-user.txt", "203.0.113.113/32\r\n" },
        { L"list-general-user.txt",  "# Never leave this file empty\r\ndomain.example.abc\r\n" },
        { L"list-exclude-user.txt",  "domain.example.abc\r\n" },
    };
    for (int i = 0; i < 3; i++) {
        wchar_t p[MAX_PATH];
        _snwprintf(p, MAX_PATH, L"%s%s", g_paths.lists_dir, files[i].name);
        p[MAX_PATH-1] = 0;
        if (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES) continue;
        HANDLE h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) continue;
        DWORD wr;
        WriteFile(h, files[i].content, (DWORD)strlen(files[i].content), &wr, NULL);
        CloseHandle(h);
    }
}

/* ================================================================== */
/* process helpers                                                     */
/* ================================================================== */

void zg_run_hidden(const wchar_t* exe, const wchar_t* params)
{
    STARTUPINFOW si; PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si)); ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);
    wchar_t cmd[1024];
    _snwprintf(cmd, 1024, L"\"%s\" %s", exe, params ? params : L"");
    cmd[1023] = 0;
    if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE,
                       CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                       NULL, g_paths.exe_dir, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

void zg_open_console_bat(const wchar_t* arg)
{
    STARTUPINFOW si; PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si)); ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);
    wchar_t cmd[2048];
    _snwprintf(cmd, 2048, L"cmd.exe /c \"\"%s\\service.bat\" %s\"", g_paths.exe_dir, arg);
    cmd[2047] = 0;
    if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE,
                       CREATE_NEW_CONSOLE | CREATE_UNICODE_ENVIRONMENT,
                       NULL, g_paths.exe_dir, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

BOOL zg_winws_running(void)
{
    BOOL found = FALSE;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return FALSE;
    PROCESSENTRY32W pe; pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"winws.exe") == 0) { found = TRUE; break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

void zg_kill_own_child(void)
{
    if (g_child) {
        TerminateProcess(g_child, 0);
        CloseHandle(g_child);
        g_child = NULL;
        g_child_pid = 0;
    }
}

static void kill_all_winws(void)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe; pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"winws.exe") == 0) {
                HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pe.th32ProcessID);
                if (h) {
                    TerminateProcess(h, 0);
                    WaitForSingleObject(h, 3000);
                    CloseHandle(h);
                }
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    zg_kill_own_child();
}

/* ================================================================== */
/* SCM helpers                                                         */
/* ================================================================== */

static SC_HANDLE open_scm(DWORD access)
{
    return OpenSCManagerW(NULL, SERVICES_ACTIVE_DATABASEW, access);
}

static DWORD query_service_state(SC_HANDLE hSvc, BOOL* exists)
{
    *exists = FALSE;
    QUERY_SERVICE_CONFIGW* dummy = NULL; /* not used */
    (void)dummy;
    SERVICE_STATUS_PROCESS ssp; DWORD need = 0;
    if (!QueryServiceStatusEx(hSvc, SC_STATUS_PROCESS_INFO,
                              (LPBYTE)&ssp, sizeof(ssp), &need)) {
        DWORD e = GetLastError();
        if (e == ERROR_SERVICE_NOT_ACTIVE || e == ERROR_INVALID_HANDLE) {
            /* service handle valid but not running / stale */
            *exists = TRUE;
            return SERVICE_STOPPED;
        }
        return 0xFFFFFFFF;
    }
    *exists = TRUE;
    return ssp.dwCurrentState;
}

static BOOL wait_service_state(SC_HANDLE hSvc, DWORD target, DWORD timeout_ms)
{
    for (;;) {
        SERVICE_STATUS_PROCESS ssp; DWORD need = 0;
        if (!QueryServiceStatusEx(hSvc, SC_STATUS_PROCESS_INFO,
                                  (LPBYTE)&ssp, sizeof(ssp), &need))
            break;
        if (ssp.dwCurrentState == target) return TRUE;
        if (ssp.dwCurrentState == SERVICE_STOPPED && target != SERVICE_STOPPED) return FALSE;
        if (timeout_ms == 0) return FALSE;
        DWORD step = ssp.dwWaitHint;
        if (step == 0 || step > 500) step = 500;
        Sleep(step);
        if (step >= timeout_ms) { timeout_ms = 0; } else { timeout_ms -= step; }
    }
    return FALSE;
}

/* ================================================================== */
/* status                                                              */
/* ================================================================== */

void zg_status_refresh(ZgStatus* st)
{
    ZeroMemory(st, sizeof(*st));
    st->winws_running = zg_winws_running();

    if (g_child && WaitForSingleObject(g_child, 0) == WAIT_TIMEOUT)
        st->own_child_alive = TRUE;

    /* zapret service */
    SC_HANDLE hScm = open_scm(SC_MANAGER_CONNECT);
    if (hScm) {
        SC_HANDLE hSvc = OpenServiceW(hScm, L"zapret", SERVICE_QUERY_STATUS | SERVICE_START | SERVICE_STOP | DELETE);
        if (hSvc) {
            BOOL exists = FALSE;
            DWORD state = query_service_state(hSvc, &exists);
            if (exists) {
                st->svc_installed = TRUE;
                st->svc_running = (state == SERVICE_RUNNING);
            }
            CloseServiceHandle(hSvc);
        }
        /* WinDivert driver */
        const wchar_t* drvNames[2] = { L"WinDivert", L"WinDivert14" };
        for (int i = 0; i < 2 && !st->windivert_running; i++) {
            SC_HANDLE hDrv = OpenServiceW(hScm, drvNames[i], SERVICE_QUERY_STATUS);
            if (hDrv) {
                BOOL ex2 = FALSE;
                DWORD s2 = query_service_state(hDrv, &ex2);
                if (ex2 && s2 == SERVICE_RUNNING) st->windivert_running = TRUE;
                CloseServiceHandle(hDrv);
            }
        }
        CloseServiceHandle(hScm);
    }

    /* strategy name from registry (like :get_strategy_name) */
    HKEY hk;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SYSTEM\\CurrentControlSet\\Services\\zapret",
                      0, KEY_QUERY_VALUE, &hk) == ERROR_SUCCESS) {
        wchar_t buf[80]; DWORD sz = sizeof(buf); DWORD type = 0;
        if (RegQueryValueExW(hk, L"zapret-discord-youtube", NULL, &type,
                             (LPBYTE)buf, &sz) == ERROR_SUCCESS && type == REG_SZ) {
            buf[79] = 0;
            wcsncpy(st->svc_strategy, buf, 79);
            st->svc_strategy[79] = 0;
        }
        RegCloseKey(hk);
    }

    const wchar_t* lv = zg_local_version();
    wcsncpy(st->local_version, lv, 31);
    st->local_version[31] = 0;
}

/* ================================================================== */
/* launch / stop bypass                                                */
/* ================================================================== */

BOOL zg_launch_bypass(const wchar_t* strategy_name, wchar_t* err, size_t err_cap)
{
    err[0] = 0;
    if (!g_paths.files_ok) {
        _snwprintf(err, err_cap, L"%s", zg_str(S_ERR_NO_WINWS));
        return FALSE;
    }

    /* do not run alongside the service (matches general.bat :test_service) */
    ZgStatus st; zg_status_refresh(&st);
    if (st.svc_running) {
        _snwprintf(err, err_cap, L"%s", zg_str(S_ERR_SVC_RUNNING));
        return FALSE;
    }
    if (st.winws_running) {
        _snwprintf(err, err_cap, L"%s", zg_str(S_ERR_WINWS_RUNNING));
        return FALSE;
    }

    zg_ensure_user_lists();

    /* enable TCP timestamps (like :tcp_enable) — fire and forget */
    zg_run_hidden(L"netsh", L"interface tcp set global timestamps=enabled");

    /* upstream "check_updates soft" run (self-guards on the enabled flag) */
    {
        STARTUPINFOW si; PROCESS_INFORMATION pi;
        ZeroMemory(&si, sizeof(si)); ZeroMemory(&pi, sizeof(pi));
        si.cb = sizeof(si);
        wchar_t cmd[2048];
        _snwprintf(cmd, 2048, L"cmd.exe /c \"\"%s\\service.bat\" check_updates soft\"", g_paths.exe_dir);
        cmd[2047] = 0;
        if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE,
                           CREATE_NO_WINDOW, NULL, g_paths.exe_dir, &si, &pi)) {
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        }
    }

    wchar_t args[16384];
    if (!zg_parse_strategy(strategy_name, args, 16384)) {
        _snwprintf(err, err_cap, zg_str(S_ERR_PARSE_STRAT), strategy_name);
        return FALSE;
    }

    wchar_t cmdline[17000];
    _snwprintf(cmdline, 17000, L"\"%s\" %s", g_paths.winws_path, args);
    cmdline[16999] = 0;

    zg_kill_own_child();

    STARTUPINFOW si; PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si)); ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);
    if (!CreateProcessW(NULL, cmdline, NULL, NULL, FALSE,
                        CREATE_NO_WINDOW, NULL, g_paths.bin_dir, &si, &pi)) {
        _snwprintf(err, err_cap, zg_str(S_ERR_LAUNCH_WINWS), GetLastError());
        return FALSE;
    }
    CloseHandle(pi.hThread);
    g_child = pi.hProcess;      /* keep to track / terminate later */
    g_child_pid = pi.dwProcessId;
    return TRUE;
}

BOOL zg_stop_bypass(int* pStoppedSvc)
{
    if (pStoppedSvc) *pStoppedSvc = 0;
    BOOL stoppedSvc = FALSE;

    SC_HANDLE hScm = open_scm(SC_MANAGER_ALL_ACCESS);
    if (!hScm) hScm = open_scm(SC_MANAGER_CONNECT);
    if (hScm) {
        SC_HANDLE hSvc = OpenServiceW(hScm, L"zapret",
                                      SERVICE_QUERY_STATUS | SERVICE_STOP);
        if (hSvc) {
            SERVICE_STATUS_PROCESS ssp; DWORD need = 0;
            if (QueryServiceStatusEx(hSvc, SC_STATUS_PROCESS_INFO,
                                     (LPBYTE)&ssp, sizeof(ssp), &need) &&
                ssp.dwCurrentState == SERVICE_RUNNING) {
                SERVICE_STATUS ss;
                if (ControlService(hSvc, SERVICE_CONTROL_STOP, &ss)) {
                    wait_service_state(hSvc, SERVICE_STOPPED, 15000);
                    stoppedSvc = TRUE;
                }
            }
            CloseServiceHandle(hSvc);
        }
        CloseServiceHandle(hScm);
    }

    kill_all_winws();
    if (pStoppedSvc) *pStoppedSvc = stoppedSvc;
    return TRUE;
}

/* ================================================================== */
/* service install / remove / stop                                     */
/* ================================================================== */

static void netsh_timestamps(void)
{
    zg_run_hidden(L"netsh", L"interface tcp set global timestamps=enabled");
}

BOOL zg_service_install(const wchar_t* strategy_name, wchar_t* err, size_t err_cap)
{
    err[0] = 0;
    if (!g_paths.files_ok) {
        _snwprintf(err, err_cap, zg_str(S_ERR_NO_WINWS_DIR), g_paths.exe_dir);
        return FALSE;
    }

    wchar_t args[16384];
    if (!zg_parse_strategy(strategy_name, args, 16384)) {
        _snwprintf(err, err_cap, zg_str(S_ERR_PARSE_STRAT), strategy_name);
        return FALSE;
    }

    netsh_timestamps();
    zg_ensure_user_lists();

    /* remove any previous instance (like net stop + sc delete in service.bat) */
    zg_kill_own_child();
    {
        SC_HANDLE hScm = open_scm(SC_MANAGER_ALL_ACCESS);
        if (hScm) {
            SC_HANDLE hSvc = OpenServiceW(hScm, L"zapret",
                                          SERVICE_STOP | DELETE | SERVICE_QUERY_STATUS);
            if (hSvc) {
                SERVICE_STATUS_PROCESS ssp; DWORD need = 0;
                if (QueryServiceStatusEx(hSvc, SC_STATUS_PROCESS_INFO,
                                         (LPBYTE)&ssp, sizeof(ssp), &need) &&
                    ssp.dwCurrentState == SERVICE_RUNNING) {
                    SERVICE_STATUS ss;
                    ControlService(hSvc, SERVICE_CONTROL_STOP, &ss);
                    wait_service_state(hSvc, SERVICE_STOPPED, 15000);
                }
                DeleteService(hSvc);
                CloseServiceHandle(hSvc);
                Sleep(300);
            }
            CloseServiceHandle(hScm);
        }
    }
    kill_all_winws();

    wchar_t binpath[17000];
    _snwprintf(binpath, 17000, L"\"%s\" %s", g_paths.winws_path, args);
    binpath[16999] = 0;

    SC_HANDLE hScm = open_scm(SC_MANAGER_ALL_ACCESS);
    if (!hScm) {
        _snwprintf(err, err_cap, zg_str(S_ERR_SCM_ACCESS), GetLastError());
        return FALSE;
    }

    SC_HANDLE hSvc = CreateServiceW(hScm, L"zapret", L"zapret",
                                    SERVICE_ALL_ACCESS,
                                    SERVICE_WIN32_OWN_PROCESS,
                                    SERVICE_AUTO_START,
                                    SERVICE_ERROR_NORMAL,
                                    binpath,
                                    NULL, NULL, NULL, NULL, NULL);
    if (!hSvc) {
        DWORD e = GetLastError();
        CloseServiceHandle(hScm);
        if (e == ERROR_SERVICE_EXISTS) {
            _snwprintf(err, err_cap, L"%s", zg_str(S_ERR_SVC_EXISTS));
        } else {
            _snwprintf(err, err_cap, zg_str(S_ERR_CREATE_SVC), e);
        }
        return FALSE;
    }

    SERVICE_DESCRIPTIONW sd;
    sd.lpDescription = (LPWSTR)L"Zapret DPI bypass software";
    ChangeServiceConfig2W(hSvc, SERVICE_CONFIG_DESCRIPTION, &sd);

    if (!StartServiceW(hSvc, 0, NULL)) {
        DWORD e = GetLastError();
        if (e != ERROR_SERVICE_ALREADY_RUNNING) {
            /* created but failed to start — remove it to stay consistent */
            DeleteService(hSvc);
            CloseServiceHandle(hSvc);
            CloseServiceHandle(hScm);
            _snwprintf(err, err_cap, zg_str(S_ERR_START_SVC), e);
            return FALSE;
        }
    }
    CloseServiceHandle(hSvc);
    CloseServiceHandle(hScm);

    /* remember strategy name (like reg add ... /v zapret-discord-youtube) */
    wchar_t name_no_ext[80];
    wcsncpy(name_no_ext, strategy_name, 79);
    name_no_ext[79] = 0;
    wchar_t* dot = wcsrchr(name_no_ext, L'.');
    if (dot && _wcsicmp(dot, L".bat") == 0) *dot = 0;

    HKEY hk;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE,
                        L"SYSTEM\\CurrentControlSet\\Services\\zapret",
                        0, NULL, 0, KEY_SET_VALUE, NULL, &hk, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(hk, L"zapret-discord-youtube", 0, REG_SZ,
                       (const BYTE*)name_no_ext,
                       (DWORD)((wcslen(name_no_ext) + 1) * sizeof(wchar_t)));
        RegCloseKey(hk);
    }
    return TRUE;
}

static void remove_service_by_name(const wchar_t* name, DWORD access)
{
    SC_HANDLE hScm = open_scm(SC_MANAGER_ALL_ACCESS);
    if (!hScm) hScm = open_scm(SC_MANAGER_CONNECT);
    if (!hScm) return;
    SC_HANDLE hSvc = OpenServiceW(hScm, name, access | SERVICE_QUERY_STATUS | SERVICE_STOP | DELETE);
    if (hSvc) {
        SERVICE_STATUS_PROCESS ssp; DWORD need = 0;
        if (QueryServiceStatusEx(hSvc, SC_STATUS_PROCESS_INFO,
                                 (LPBYTE)&ssp, sizeof(ssp), &need) &&
            ssp.dwCurrentState == SERVICE_RUNNING) {
            SERVICE_STATUS ss;
            ControlService(hSvc, SERVICE_CONTROL_STOP, &ss);
            wait_service_state(hSvc, SERVICE_STOPPED, 10000);
        }
        DeleteService(hSvc);
        CloseServiceHandle(hSvc);
    }
    CloseServiceHandle(hScm);
}

BOOL zg_service_remove(wchar_t* err, size_t err_cap)
{
    (void)err; (void)err_cap;   /* removal steps report via return code */
    err[0] = 0;
    remove_service_by_name(L"zapret", 0);
    kill_all_winws();
    remove_service_by_name(L"WinDivert", 0);
    remove_service_by_name(L"WinDivert14", 0);
    return TRUE;
}

BOOL zg_service_stop_only(void)
{
    int stopped = 0;
    return zg_stop_bypass(&stopped);
}
