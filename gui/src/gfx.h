/*
 * Zapret GUI — gfx.h
 * Software "glass / carbon" renderer.
 *
 * Why not GDI+ gradients? They are 8-bit per channel without dithering,
 * which shows up as visible banding on dark tones, and a linear gloss
 * fade has a derivative jump at its end (a Mach band — the "hard edge"
 * of the old glossy buttons). Everything here is computed per pixel in
 * floating point, uses C1-continuous easing for every falloff and is
 * written back with triangular dithering, so gradients stay perfectly
 * smooth even across a few grey levels.
 *
 * Surfaces are plain 32bpp top-down DIB sections (ZgCanvas): their DC
 * can still be used for GDI text afterwards.
 */
#ifndef ZG_GFX_H
#define ZG_GFX_H

#include <windows.h>

/* ------------------------------------------------------------------ */
/* canvas                                                              */
/* ------------------------------------------------------------------ */
typedef struct ZgCanvas {
    int           w, h;
    unsigned int* px;     /* 0x00RRGGBB, top-down, stride == w          */
    HBITMAP       bmp;
    HDC           dc;
    HBITMAP       old;
} ZgCanvas;

BOOL zg_canvas_create(ZgCanvas* c, int w, int h);
void zg_canvas_destroy(ZgCanvas* c);

/* raw pixel copy between canvases (clipped) */
void zg_canvas_copy_rect(ZgCanvas* dst, int dx, int dy,
                         const ZgCanvas* src, int sx, int sy, int w, int h);
/* copy a rectangle of any DC (e.g. the screen) into a canvas */
void zg_canvas_grab_dc(ZgCanvas* dst, int dx, int dy, HDC src, int sx, int sy, int w, int h);
void zg_canvas_fill(ZgCanvas* c, COLORREF col);

/* ------------------------------------------------------------------ */
/* backdrop: the parent window keeps a pre-rendered picture of its      */
/* static surface; every child control copies its own crop of it as the */
/* background (shadow fringes then blend pixel-perfectly).              */
/* ------------------------------------------------------------------ */
void zg_backdrop_attach(HWND wnd, ZgCanvas* c);            /* c may be NULL */
BOOL zg_backdrop_fill(HWND ctrl, ZgCanvas* dst);           /* dst = ctrl-size canvas */

/* ------------------------------------------------------------------ */
/* surfaces                                                             */
/* ------------------------------------------------------------------ */

/* themed window background: gradient + soft ambient light + weave */
void zg_gfx_background(ZgCanvas* c);

/* glass panel style */
typedef struct {
    COLORREF tint;          /* glass colour                                */
    int      a_hi, a_lo;    /* tint alpha 0..255 at the top / bottom      */
    int      rim;           /* edge light alpha 0..255                    */
    int      sheen;         /* diagonal reflection alpha 0..255           */
    int      inset;         /* 1 = sunken field (log / edit)              */
    int      shadow_a;      /* drop shadow alpha 0..255 (0 = none)        */
    int      shadow_sigma;  /* px                                         */
    int      shadow_dy;     /* px                                         */
    int      opaque;        /* 1 = tint replaces the destination          */
} ZgGlass;

void zg_gfx_glass(ZgCanvas* c, int x, int y, int w, int h, int r, const ZgGlass* st);

/* ready-made styles for the active theme */
void zg_glass_style_card(ZgGlass* st);       /* status cards            */
void zg_glass_style_panel(ZgGlass* st);      /* big settings panel      */
void zg_glass_style_field(ZgGlass* st);      /* sunken log / edit field */
void zg_glass_style_popup(ZgGlass* st);      /* dropdown list (opaque)  */

/* engraved row separator */
void zg_gfx_separator(ZgCanvas* c, int x0, int x1, int y);

/* ------------------------------------------------------------------ */
/* controls (state values = ZGST_* from uidraw.h)                       */
/* ------------------------------------------------------------------ */
enum { ZGG_NORMAL = 0, ZGG_HOVER, ZGG_PRESSED, ZGG_DISABLED };

/* body = visible button rect (caller adds the shadow fringe around) */
void zg_gfx_button(ZgCanvas* c, int x, int y, int w, int h,
                   int primary, int scheme, int state);
void zg_gfx_toggle(ZgCanvas* c, int x, int y, int w, int h,
                   int on, int hover, int disabled);
void zg_gfx_chevron(ZgCanvas* c, float cx, float cy, float size, COLORREF col, int alpha);
void zg_gfx_gear(ZgCanvas* c, float cx, float cy, float size, COLORREF col, int alpha);

/* glass bead / dome (status dot, app icon background) */
void zg_gfx_dot(ZgCanvas* c, float cx, float cy, float r, COLORREF col, int glow);
void zg_gfx_icon_tile(ZgCanvas* c, int x, int y, int size);

/* hero: glass disc + chrome ring + (when active) soft green halo */
void zg_gfx_hero(ZgCanvas* c, float cx, float cy, float r, int active);

#endif /* ZG_GFX_H */
