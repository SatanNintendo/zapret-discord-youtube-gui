/*
 * Zapret GUI — theme.h
 * Colors, fonts and layout metrics (design units, base 96 DPI).
 * Design recreated from the reference screenshot:
 *   dark #1E1E1E background, #2D2D2D panels, neon-green accent,
 *   purple->coral gradient primary button, Segoe UI / Consolas.
 */
#ifndef ZG_THEME_H
#define ZG_THEME_H

#include <windows.h>

/* ------------------------------------------------------------------ */
/* Palette                                                             */
/* ------------------------------------------------------------------ */
#define COL_BG          RGB(0x1E,0x1E,0x1E)   /* main background          */
#define COL_BG_DARK     RGB(0x18,0x18,0x18)   /* log area                 */
#define COL_PANEL       RGB(0x2D,0x2D,0x2D)   /* cards / panels / combos   */
#define COL_PANEL_HOVER RGB(0x38,0x38,0x38)   /* hover fill               */
#define COL_PANEL_DOWN  RGB(0x33,0x33,0x33)   /* pressed fill             */
#define COL_BORDER      RGB(0x3E,0x3E,0x3E)   /* subtle borders           */
#define COL_BORDER_DIM  RGB(0x33,0x33,0x33)
#define COL_TEXT        RGB(0xFF,0xFF,0xFF)
#define COL_MUTED       RGB(0xAA,0xAA,0xAA)
#define COL_MUTED2      RGB(0x8A,0x8A,0x8A)
#define COL_GREEN       RGB(0x32,0xCD,0x32)   /* "LimeGreen" accent       */
#define COL_GREEN_DIM   RGB(0x27,0x99,0x27)
#define COL_GREEN_HOVER RGB(0x4A,0xE0,0x4A)
#define COL_PURPLE      RGB(0x7B,0x68,0xEE)   /* gradient start           */
#define COL_CORAL       RGB(0xFF,0x63,0x47)   /* gradient end             */
#define COL_GRAY        RGB(0x80,0x80,0x80)
#define COL_GRAY_DIM    RGB(0x55,0x55,0x55)
#define COL_RED         RGB(0xFF,0x5C,0x5C)
#define COL_YELLOW      RGB(0xE0,0xC0,0x50)
#define COL_TOGGLE_OFF  RGB(0x55,0x55,0x55)

/* ------------------------------------------------------------------ */
/* Layout — client size in design units (window slightly larger than   */
/* the 561x745 reference). All metrics in design units, scaled at       */
/* runtime by the current DPI.                                         */
/* ------------------------------------------------------------------ */
#define DU_WIN_W        660
#define DU_WIN_H        900

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

/* settings */
#define DU_SET_HDR_Y    392
#define DU_SET_Y        416
#define DU_SET_H        248
#define DU_ROW_H        48
#define DU_ROW0_Y       (DU_SET_Y + 6)
#define DU_CTRL_W       220
#define DU_CTRL_H       32
#define DU_COMBO_W      240

/* log */
#define DU_LOG_HDR_Y    682
#define DU_LOG_Y        706
#define DU_LOG_H        128

/* footer */
#define DU_FOOT_Y       850
#define DU_FOOT_H       46
#define DU_FOOT_GAP     10

/* ------------------------------------------------------------------ */
/* DPI helpers                                                         */
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
/* Fonts (created once in main.c)                                      */
/* ------------------------------------------------------------------ */
extern HFONT g_font_title;    /* Segoe UI Semibold 20                  */
extern HFONT g_font_status;   /* Segoe UI Bold     20                  */
extern HFONT g_font_body;    /* Segoe UI         10                   */
extern HFONT g_font_body_b;  /* Segoe UI Semibold 10                  */
extern HFONT g_font_small;   /* Segoe UI         8.5                  */
extern HFONT g_font_h2;      /* Segoe UI Semibold 9  (section headers) */
extern HFONT g_font_btn;     /* Segoe UI Semibold 10                   */
extern HFONT g_font_mono;    /* Consolas          9                   */

void theme_create_fonts(void);
void theme_destroy_fonts(void);

#endif /* ZG_THEME_H */
