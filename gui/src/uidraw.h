/*
 * Zapret GUI — uidraw.h
 * Chrome drawing primitives shared by the main window and the custom
 * controls: rounded paths, soft drop shadows, glossy 3D panels and
 * buttons with gradients, bevels and specular highlights.
 *
 * All functions draw through a SCOPED Gdiplus::Graphics (phase A of
 * the two-phase painting discipline — see main.cpp WM_PAINT).
 */
#ifndef ZG_UIDRAW_H
#define ZG_UIDRAW_H

#include <windows.h>
#include <gdiplus.h>

/* button / control states */
enum {
    ZGST_NORMAL = 0,
    ZGST_HOVER,
    ZGST_PRESSED,
    ZGST_DISABLED
};

/* rounded rectangle path; caller deletes (Gdiplus::GraphicsPath*) */
Gdiplus::GraphicsPath* zg_round_path(int x, int y, int w, int h, int r);

/*
 * Soft drop shadow: `depth` layered rounded strokes around/below the
 * shape, each with alpha/depth — the body painted afterwards covers
 * the centre, only the soft fringe remains visible.
 */
void zg_draw_shadow(Gdiplus::Graphics& g, int x, int y, int w, int h,
                    int r, int depth, int alpha);

/*
 * Chrome panel: vertical gradient fill + optional specular gloss on the
 * upper half + light inner top bevel + outer border.
 */
void zg_draw_chrome_panel(Gdiplus::Graphics& g, int x, int y, int w, int h,
                          int r, COLORREF top, COLORREF bot,
                          COLORREF border, int gloss_alpha);

/*
 * Full 3D chrome button (ZGBTN_PRIMARY / ZGBTN_FLAT). (x,y,w,h) is the
 * CONTROL window rect; the visible body is inset by a shadow fringe.
 * state = ZGST_*. The caller draws the text over the body rect returned
 * by zg_ctrl_body() (+1px down when pressed).
 */
void zg_draw_button(Gdiplus::Graphics& g, int x, int y, int w, int h,
                   UINT style, int scheme, int state);

/* visible body rect of a shadowed control (client coordinates) */
void zg_ctrl_body(int x, int y, int w, int h, RECT* out);

#endif /* ZG_UIDRAW_H */
