/*
 * Zapret GUI — theme.h
 * Runtime-switchable palettes (dark / light / midnight), fonts and
 * layout metrics (design units, base 96 DPI).
 *
 * The palette lives in the ZgTheme struct; the classic COL_* macros
 * resolve into the active theme so all painting code works unchanged
 * after zg_theme_apply().
 */
#ifndef ZG_THEME_H
#define ZG_THEME_H

#include <windows.h>

/* ------------------------------------------------------------------ */
/* Theme ids                                                            */
/* ------------------------------------------------------------------ */
enum {
    ZG_THEME_DARK = 0,     /* как раньше — тёмная (по умолчанию) */
    ZG_THEME_LIGHT,        /* светлая                              */
    ZG_THEME_MIDNIGHT,     /* глубокая сине-чёрная                 */
    ZG_THEME_COUNT
};

/* ------------------------------------------------------------------ */
/* Palette (filled by zg_theme_apply)                                   */
/* ------------------------------------------------------------------ */
typedef struct {
    /* base */
    COLORREF bg;               /* main background                   */
    COLORREF bg_dark;          /* log area                          */
    COLORREF panel;            /* cards / panels / combos           */
    COLORREF panel_hover;      /* hover fill                        */
    COLORREF panel_down;      /* pressed fill                      */
    COLORREF border;           /* subtle borders                    */
    COLORREF border_dim;
    COLORREF text;
    COLORREF muted;
    COLORREF muted2;
    COLORREF green;            /* accent                            */
    COLORREF green_dim;
    COLORREF green_hover;
    COLORREF purple;           /* gradient start                    */
    COLORREF coral;            /* gradient end                      */
    COLORREF gray;
    COLORREF gray_dim;
    COLORREF red;
    COLORREF yellow;
    COLORREF toggle_off;
    /* extended tokens */
    COLORREF hdr;              /* section headers + card titles     */
    COLORREF ver;              /* version text, top-right           */
    COLORREF log_info;         /* log text colors                   */
    COLORREF log_time;
    COLORREF hero_disc;        /* hero circle fill                 */
    COLORREF hero_ring_off;    /* inactive ring                    */
    COLORREF hero_bolt_off;    /* inactive bolt                    */
    COLORREF popup_hover;      /* combo popup hovered row          */
    COLORREF shadow;           /* shadow tint                      */
    /* chrome gradients */
    COLORREF btn_top,    btn_bot;      /* flat chrome — normal     */
    COLORREF btn_top_h,  btn_bot_h;    /* hover                    */
    COLORREF btn_top_p,  btn_bot_p;    /* pressed                  */
    COLORREF card_top,   card_bot;     /* cards / popup            */
    COLORREF setp_top,   setp_bot;     /* settings panel           */
    BOOL     is_dark;                  /* dark titlebar / overlays */
} ZgTheme;

extern ZgTheme g_th;
extern int     g_theme;        /* current ZG_THEME_* id */

void zg_theme_apply(int id);

/* compat macros — resolve into the ACTIVE theme */
#define COL_BG          (g_th.bg)
#define COL_BG_DARK     (g_th.bg_dark)
#define COL_PANEL       (g_th.panel)
#define COL_PANEL_HOVER (g_th.panel_hover)
#define COL_PANEL_DOWN  (g_th.panel_down)
#define COL_BORDER      (g_th.border)
#define COL_BORDER_DIM  (g_th.border_dim)
#define COL_TEXT        (g_th.text)
#define COL_MUTED       (g_th.muted)
#define COL_MUTED2       (g_th.muted2)
#define COL_GREEN        (g_th.green)
#define COL_GREEN_DIM    (g_th.green_dim)
#define COL_GREEN_HOVER  (g_th.green_hover)
#define COL_PURPLE       (g_th.purple)
#define COL_CORAL        (g_th.coral)
#define COL_GRAY         (g_th.gray)
#define COL_GRAY_DIM     (g_th.gray_dim)
#define COL_RED          (g_th.red)
#define COL_YELLOW       (g_th.yellow)
#define COL_TOGGLE_OFF   (g_th.toggle_off)
#define COL_HDR          (g_th.hdr)
#define COL_LOG_INFO     (g_th.log_info)
#define COL_LOG_TIME     (g_th.log_time)

/* ------------------------------------------------------------------ */
/* Layout — client size in design units (window slightly larger than   */
/* the 561x745 reference). All metrics in design units, scaled at      */
/* runtime by the current DPI.                                         */
/* ------------------------------------------------------------------ */
#define DU_WIN_W        660
#define DU_WIN_H        996     /* +96: две новые строки настроек      */

#define DU_PAD          24      /* side padding                        */
#define DU_CONTENT_W    (DU_WIN_W - 2*DU_PAD)   /* 612                   */

/* header */
#define DU_ICON_SIZE    44
#define DU_ICON_X       DU_PAD
#define DU_ICON_Y       24
#define DU_TITLE_X      84
#define DU_TITLE_Y      26
#define DU_SUB_Y        58
#define DU_VER_Y        26
#define DU_UPDLINK_Y    50

/* hero */
#define DU_HERO_TOP     104
#define DU_CIRCLE_CX    (DU_WIN_W/2)
#define DU_CIRCLE_CY    148
#define DU_CIRCLE_R     36
#define DU_STAT_Y       194
#define DU_STAT_H       30
#define DU_SUBTXT_Y     226
#define DU_PRIMARY_W    360
#define DU_PRIMARY_H    44
#define DU_PRIMARY_X    ((DU_WIN_W - DU_PRIMARY_W)/2)
#define DU_PRIMARY_Y    252
#define DU_HERO_BOTTOM  300

/* status cards */
#define DU_CARDS_Y      316
#define DU_CARD_H       64
#define DU_CARD_GAP     12
#define DU_CARD_W       ((DU_CONTENT_W - 2*DU_CARD_GAP)/3)

/* settings: 7 rows now (strategy, game, svc, upd, ipset, lang, theme) */
#define DU_SET_HDR_Y    392
#define DU_SET_Y        416
#define DU_SET_ROWS     7
#define DU_SET_H        (6 + DU_SET_ROWS*48 + 2)   /* 344              */
#define DU_ROW_H        48
#define DU_ROW0_Y       (DU_SET_Y + 6)
#define DU_CTRL_H       32
#define DU_COMBO_W      240
#define DU_COMBO_W2     200     /* game / ipset                         */
#define DU_COMBO_W3     170     /* language / theme                     */

/* log */
#define DU_LOG_HDR_Y    (DU_SET_Y + DU_SET_H + 16)   /* 776             */
#define DU_LOG_Y        (DU_LOG_HDR_Y + 24)          /* 800             */
#define DU_LOG_H        128

/* footer */
#define DU_FOOT_Y       (DU_LOG_Y + DU_LOG_H + 16)   /* 944             */
#define DU_FOOT_H       46
#define DU_FOOT_GAP     10

/* chrome shadow fringe added around shadowed controls (buttons, combos) */
#define DU_SHADOW        4

/* ------------------------------------------------------------------ */
/* DPI helpers                                                          */
/* ------------------------------------------------------------------ */
extern int g_scale_pct;   /* e.g. 100, 125, 150 */

static inline int SC(int v) { return MulDiv(v, g_scale_pct, 100); }

/* COLORREF -> 0xAARRGGBB (Gdiplus::ARGB) */
static inline DWORD zg_argb(int a, COLORREF c)
{
    return ((DWORD)(BYTE)(a) << 24) | ((DWORD)GetRValue(c) << 16)
         | ((DWORD)GetGValue(c) << 8) | (DWORD)GetBValue(c);
}

/* ------------------------------------------------------------------ */
/* Fonts (created once in main.cpp)                                    */
/* ------------------------------------------------------------------ */
extern HFONT g_font_title;    /* Segoe UI Semibold 20                  */
extern HFONT g_font_status;   /* Segoe UI Bold     20                  */
extern HFONT g_font_body;    /* Segoe UI         10                   */
extern HFONT g_font_body_b;  /* Segoe UI Semibold 10                   */
extern HFONT g_font_small;   /* Segoe UI         8.5                  */
extern HFONT g_font_h2;      /* Segoe UI Semibold 9  (section headers) */
extern HFONT g_font_btn;     /* Segoe UI Semibold 10                   */
extern HFONT g_font_mono;    /* Consolas          9                   */

void theme_create_fonts(void);
void theme_destroy_fonts(void);

#endif /* ZG_THEME_H */
