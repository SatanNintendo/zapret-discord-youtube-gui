/*
 * Zapret GUI — controls.h
 * Custom-drawn dark-theme controls: buttons (flat / gradient pill),
 * iOS-style toggle switches, dropdown combos with styled popup list.
 */
#ifndef ZG_CONTROLS_H
#define ZG_CONTROLS_H

#include <windows.h>

/* ---- ZgButton ---------------------------------------------------- */
/* styles */
#define ZGBTN_FLAT     0   /* #2D2D2D rounded rect, subtle border       */
#define ZGBTN_PRIMARY  1   /* gradient pill, bold uppercase text       */
#define ZGBTN_LINK     2   /* transparent text link (accent color)    */

/* primary button color schemes */
#define ZGBP_GREEN     0   /* "ВКЛЮЧИТЬ ОБХОД"                          */
#define ZGBP_PURPLE    1   /* "ВЫКЛЮЧИТЬ ОБХОД" (purple->coral)        */

void zg_register_controls(void);

HWND zg_button_create(HWND parent, int id, const wchar_t* text, UINT style, int scheme);
void zg_button_set_text(HWND h, const wchar_t* text);
void zg_button_set_scheme(HWND h, int scheme);
void zg_button_set_disabled(HWND h, BOOL dis);

/* ---- ZgToggle ---------------------------------------------------- */
HWND zg_toggle_create(HWND parent, int id, BOOL initial);
void zg_toggle_set(HWND h, BOOL on, BOOL notify_parent);
void zg_toggle_set_disabled(HWND h, BOOL dis);

/* ---- ZgCombo ----------------------------------------------------- */
#define ZG_COMBO_MAXVIS 8

HWND zg_combo_create(HWND parent, int id, const wchar_t* const* items, int count, int selected);
int  zg_combo_get_sel(HWND h);
void zg_combo_set_sel(HWND h, int idx, BOOL notify_parent);
void zg_combo_close_popup(HWND h);
BOOL zg_combo_set_items(HWND h, const wchar_t* const* items, int count, int selected);

#endif /* ZG_CONTROLS_H */
