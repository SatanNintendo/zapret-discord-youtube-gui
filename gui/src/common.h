/*
 * Zapret GUI — common.h
 * Shared declarations for all modules.
 */
#ifndef ZG_COMMON_H
#define ZG_COMMON_H

#include <windows.h>
#include "theme.h"
#include "lang.h"

#define ZG_APP_NAME     L"Zapret GUI"
#define ZG_GUI_VERSION  L"1.1.1"
#define ZG_WND_CLASS    L"ZapretGuiWnd"
#define ZG_TIMER_STATUS 1

/* custom control messages */
#define WM_ZGBUTTON  (WM_APP + 1)   /* wparam = id                          */
#define WM_ZGTOGGLE  (WM_APP + 2)   /* wparam = id, lparam = new state       */
#define WM_ZGCOMBO   (WM_APP + 3)   /* wparam = id, lparam = selected index  */
#define WM_APP_LOG   (WM_APP + 4)   /* wparam = level, lparam = wchar_t*     */
#define WM_APP_OPDONE (WM_APP + 5)  /* wparam = op id, lparam = result       */
#define WM_APP_TRAY   (WM_APP + 6)  /* tray icon callback (classic format)   */

/* tray menu commands */
#define ZG_TRAY_CMD_OPEN 1
#define ZG_TRAY_CMD_EXIT 2

/* control ids */
#define IDC_BTN_PRIMARY   100
#define IDC_BTN_DIAG      101
#define IDC_BTN_HOSTS     102
#define IDC_BTN_IPSETUPD  103
#define IDC_BTN_TESTS     104
#define IDC_BTN_CHECKUPD  105
#define IDC_BTN_LOGCLEAR  106
#define IDC_COMBO_STRAT   200
#define IDC_COMBO_GAME    201
#define IDC_COMBO_IPSET   202
#define IDC_COMBO_LANG    203
#define IDC_COMBO_THEME   204
#define IDC_TOGGLE_SVC    300
#define IDC_TOGGLE_UPD    301

/* log levels */
enum { ZLOG_INFO = 0, ZLOG_OK, ZLOG_WARN, ZLOG_ERR, ZLOG_TIME };

/* async operations */
enum {
    OP_NONE = 0,
    OP_VERSION_CHECK,
    OP_IPSET_UPDATE,
    OP_HOSTS_CHECK,
    OP_SVC_INSTALL,
    OP_SVC_REMOVE,
    OP_SVC_STOP
};

/* game filter modes (mirror utils/game_filter.enabled) */
enum { ZG_GAME_OFF = 0, ZG_GAME_UDP, ZG_GAME_TCP, ZG_GAME_ALL };

/* ipset modes (mirror lists/ipset-all.txt states) */
enum { ZG_IPSET_LOADED = 0, ZG_IPSET_NONE, ZG_IPSET_ANY };

#endif /* ZG_COMMON_H */
