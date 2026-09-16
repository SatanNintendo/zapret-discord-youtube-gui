/*
 * Zapret GUI — lang.h
 * UI string table with runtime language switching (Russian / English).
 *
 * Every user-visible string goes through zg_str(S_*). Log templates may
 * contain printf-style placeholders (%s, %d, %lu, %ls); the parameter
 * order is identical in both languages.
 */
#ifndef ZG_LANG_H
#define ZG_LANG_H

enum {
    ZG_LANG_RU = 0,
    ZG_LANG_EN,
    ZG_LANG_COUNT
};

extern int g_lang;                       /* current ZG_LANG_* id */
void zg_lang_set(int lang);              /* no UI refresh — caller does that */
const wchar_t* zg_str(int id);           /* string for the active language */

/* per-language label sets (re-read on language switch) */
const wchar_t* const* zg_game_labels(void);   /* 4 items */
const wchar_t* const* zg_ipset_labels(void); /* 3 items */
const wchar_t* const* zg_lang_names(void);    /* 2 items, shown as-is */
int               zg_lang_name_count(void);

/* string ids */
enum {
    S_WINDOW_TITLE = 0,
    S_SUBTITLE,
    S_CHECK_UPDATES,
    S_CLEAR,
    S_SEC_SETTINGS,
    S_SEC_LOG,
    S_BTN_DIAG,
    S_BTN_HOSTS,
    S_BTN_IPSET,
    S_BTN_TESTS,
    S_BTN_TURN_ON,
    S_BTN_TURN_OFF,
    S_BTN_PICK_DIR,

    /* hero status */
    S_ST_ON,
    S_ST_OFF,
    S_ST_NOFILES,
    S_SUB_NOFILES,
    S_SUB_SVC,
    S_SUB_OWN,
    S_SUB_MANUAL,
    S_SUB_SVCINST,
    S_SUB_IDLE,

    /* cards */
    S_CARD_WINWS,
    S_CARD_WINWS_ON,
    S_CARD_WINWS_OFF,
    S_CARD_SVC,
    S_CARD_SVC_RUN,
    S_CARD_SVC_INST,
    S_CARD_SVC_NONE,
    S_CARD_WD,
    S_CARD_WD_ON,
    S_CARD_WD_OFF,

    /* settings rows */
    S_LBL_STRAT,
    S_LBL_GAME,
    S_LBL_SVC,
    S_LBL_UPD,
    S_LBL_IPSET,
    S_LBL_LANG,
    S_LBL_THEME,

    /* theme names */
    S_TH_DARK,
    S_TH_LIGHT,
    S_TH_MIDNIGHT,

    /* tray */
    S_TRAY_TIP_ON,
    S_TRAY_TIP_OFF,
    S_TRAY_TIP_NOFILES,
    S_TRAY_BALLOON_TITLE,
    S_TRAY_BALLOON,
    S_TRAY_OPEN,
    S_TRAY_EXIT,

    /* missing-folder dialog */
    S_DLG_TITLE,
    S_DLG_MAIN,
    S_DLG_CONTENT,        /* %s = exe dir */
    S_DLG_BTN_PICK,
    S_DLG_BTN_SEARCH,
    S_DLG_BTN_CONTINUE,
    S_DLG_FB_TEXT,        /* fallback MessageBox, %s = exe dir */
    S_DLG_PICK_TITLE,     /* folder picker dialog title */
    S_MB_BAD_DIR,
    S_MB_NOT_FOUND,

    /* log messages */
    S_LOG_STARTED,        /* %s version, %s exe dir */
    S_LOG_NOFILES_ERR,
    S_LOG_SEARCH_REPORT,  /* %s */
    S_LOG_DIR_CFG,        /* %s */
    S_LOG_DIR_AUTO,       /* %s */
    S_LOG_DIR_SET,        /* %s */
    S_LOG_VER_STRATS,     /* %s, %d */
    S_LOG_CUR_STRAT,      /* %s */
    S_LOG_DIR_BAD,        /* %s */
    S_LOG_RETRY_SEARCH,
    S_LOG_NOT_FOUND,      /* %s */
    S_LOG_STOPPING,
    S_LOG_STOPPED_SVC,
    S_LOG_STOPPED,
    S_LOG_NO_STRAT,
    S_LOG_LAUNCHING,      /* %s */
    S_LOG_STARTED_OK,
    S_LOG_STARTED_FAIL,
    S_LOG_OPEN_DIAG,
    S_LOG_RUN_TESTS,
    S_LOG_NO_STRAT_SVC,
    S_LOG_SVC_INSTALLING, /* %s */
    S_LOG_SVC_REMOVING,
    S_LOG_SVC_INSTALLED,  /* %s */
    S_LOG_SVC_INSTALL_ERR, /* %s */
    S_LOG_SVC_REMOVED,
    S_LOG_SVC_REMOVE_ERR, /* %s */
    S_LOG_UPD_ON,
    S_LOG_UPD_OFF,
    S_LOG_UPD_ERR,
    S_LOG_STRAT_SEL,      /* %s */
    S_LOG_RESTART_HINT,
    S_LOG_GAME_SET,        /* %s */
    S_LOG_GAME_ERR,
    S_LOG_RESTART_HINT2,
    S_LOG_IPSET_SET,      /* %s */
    S_LOG_IPSET_ERR,
    S_LOG_GUI_CLOSED,
    S_LOG_HIDDEN,
    S_LOG_LANG_RU,
    S_LOG_LANG_EN,
    S_LOG_THEME_SET,      /* %s */

    /* core errors (zapret.cpp) */
    S_ERR_NO_WINWS,
    S_ERR_SVC_RUNNING,
    S_ERR_WINWS_RUNNING,
    S_ERR_PARSE_STRAT,    /* %s */
    S_ERR_LAUNCH_WINWS,   /* %lu */
    S_ERR_NO_WINWS_DIR,   /* %s */
    S_ERR_SCM_ACCESS,     /* %lu */
    S_ERR_SVC_EXISTS,
    S_ERR_CREATE_SVC,     /* %lu */
    S_ERR_START_SVC,      /* %lu */
    S_ERR_NO_SEARCH,

    /* network messages (netops.cpp) */
    S_NET_CHECK_VER,
    S_NET_VER_FAIL,
    S_NET_VER_LATEST,     /* %s */
    S_NET_VER_NEW,        /* %s, %s */
    S_NET_IPSET_UPD,
    S_NET_IPSET_FAIL,     /* %lu */
    S_NET_IPSET_WRITE,
    S_NET_IPSET_OK,       /* %d, %lu */
    S_NET_HOSTS_CHECK,
    S_NET_HOSTS_FAIL,     /* %lu */
    S_NET_TMP_FAIL,
    S_NET_HOSTS_READ,
    S_NET_HOSTS_FIRST,
    S_NET_HOSTS_LAST,
    S_NET_HOSTS_UPDATE,
    S_NET_HOSTS_NOTE,     /* %ls */
    S_NET_HOSTS_OK,

    ZG_STR_COUNT   /* must stay last */
};

#endif /* ZG_LANG_H */
