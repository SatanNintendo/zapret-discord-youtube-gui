/*
 * Zapret GUI — zapret.h
 * Core logic: strategy discovery & parsing, bypass launch/stop,
 * Windows service management, filter states, status polling.
 * Mirrors the behaviour of upstream general*.bat and service.bat.
 */
#ifndef ZG_ZAPRET_H
#define ZG_ZAPRET_H

#include <windows.h>

/* status snapshot, refreshed by timer */
typedef struct {
    BOOL  winws_running;      /* any winws.exe process alive            */
    BOOL  own_child_alive;     /* winws.exe started by this GUI          */
    BOOL  svc_installed;       /* "zapret" service exists                */
    BOOL  svc_running;         /* "zapret" service state == RUNNING      */
    BOOL  windivert_running;   /* WinDivert / WinDivert14 RUNNING        */
    wchar_t svc_strategy[80];  /* from HKLM ...\Services\zapret reg value */
    wchar_t local_version[32];/* LOCAL_VERSION parsed from service.bat  */
} ZgStatus;

/* how the effective zapret base dir was determined */
enum {
    ZG_DET_NONE = 0,   /* nothing found — base == exe dir, files missing  */
    ZG_DET_CONFIG,     /* remembered in %APPDATA%\\ZapretGUI\\settings.ini */
    ZG_DET_AUTO_EXE,   /* the exe dir itself                              */
    ZG_DET_AUTO_PARENT,/* a parent / *zapret* subfolder found by search   */
};

/* global app paths (base dir = effective zapret folder) */
typedef struct {
    wchar_t exe_dir[MAX_PATH];     /* effective zapret base dir (no trailing \) */
    wchar_t real_exe_dir[MAX_PATH];/* where ZapretGUI.exe actually runs from     */
    wchar_t bin_dir[MAX_PATH];     /* ...\bin\                             */
    wchar_t lists_dir[MAX_PATH];   /* ...\lists\                           */
    wchar_t utils_dir[MAX_PATH];  /* ...\utils\                           */
    wchar_t winws_path[MAX_PATH]; /* ...\bin\winws.exe                    */
    BOOL   files_ok;               /* winws.exe present                    */
    int    detect;                 /* ZG_DET_* — how exe_dir was chosen     */
} ZgPaths;

extern ZgPaths g_paths;

void   zg_paths_init(void);
BOOL   zg_set_base_dir(const wchar_t* dir);     /* rebase + recheck; returns files_ok */
BOOL   zg_dir_has_winws(const wchar_t* dir);     /* <dir>\bin\winws.exe present?      */
BOOL   zg_autosearch_dir(wchar_t* out, DWORD cap); /* config + exe + parents + subdirs */
const wchar_t* zg_search_report(void);          /* dirs checked by the last search  */
void   zg_save_dir_config(const wchar_t* dir);   /* remember dir in %APPDATA%        */
BOOL   zg_load_dir_config(wchar_t* out, DWORD cap);

/* strategies */
int    zg_find_strategies(wchar_t*** out_list);          /* natural sort, caller frees via zg_free_strategies */
void   zg_free_strategies(wchar_t** list, int count);
BOOL   zg_parse_strategy(const wchar_t* bat_name, wchar_t* args, size_t args_cap);

/* filters / settings persisted as files (like service.bat does) */
int    zg_gamefilter_get(void);                          /* ZG_GAME_*  */
BOOL   zg_gamefilter_set(int mode);
int    zg_ipset_get(void);                               /* ZG_IPSET_* */
BOOL   zg_ipset_set(int mode);
BOOL   zg_checkupdates_enabled(void);
BOOL   zg_checkupdates_set(BOOL enable);
void   zg_ensure_user_lists(void);

/* running the bypass */
BOOL   zg_launch_bypass(const wchar_t* strategy_name, wchar_t* err, size_t err_cap);
BOOL   zg_stop_bypass(int* pStoppedSvc);                /* returns FALSE on hard error */
void   zg_kill_own_child(void);

/* windows service helpers */
BOOL   zg_service_install(const wchar_t* strategy_name, wchar_t* err, size_t err_cap);
BOOL   zg_service_remove(wchar_t* err, size_t err_cap);
BOOL   zg_service_stop_only(void);

/* status */
void   zg_status_refresh(ZgStatus* st);
BOOL   zg_winws_running(void);
const wchar_t* zg_local_version(void);
void   zg_local_version_invalidate(void);

/* misc helpers */
void   zg_run_hidden(const wchar_t* exe, const wchar_t* params);
void   zg_open_console_bat(const wchar_t* arg);         /* e.g. L"diag" */
wchar_t* zg_read_file_w(const wchar_t* path, DWORD* out_chars);   /* UTF-8 -> W, caller HeapFree */

#endif /* ZG_ZAPRET_H */
