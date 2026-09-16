/*
 * Zapret GUI — controls.cpp
 * Implementation of custom-drawn controls (GDI+ for anti-aliased
 * rounded shapes and gradients):
 *   - ZgButton  : glossy chrome pill (primary) / chrome bar (flat) / link
 *   - ZgToggle  : iOS-style switch with chrome thumb
 *   - ZgCombo   : dropdown with styled chrome popup list
 *
 * Painting discipline: every control renders OPAQUELY into a memory
 * bitmap (local background replica -> chrome -> text) and blits it in
 * a single SRCCOPY. Nothing is ever drawn on top of stale window-DC
 * bits: common DCs are recycled by Windows and may carry foreign pixels,
 * which caused ghosting on hover and shadow accumulation. The GDI+ and
 * GDI phases remain strictly separated inside the memory DC.
 */
#include <windows.h>
#include <windowsx.h>
#include <stdio.h>
#include "common.h"
#include "theme.h"
#include "controls.h"
#include "uidraw.h"

#include <gdiplus.h>

/* ================================================================== */
/* shared helpers                                                      */
/* ================================================================== */

static void track_mouse_leave(HWND hwnd)
{
    TRACKMOUSEEVENT tme;
    tme.cbSize = sizeof(tme);
    tme.dwFlags = TME_LEAVE;
    tme.hwndTrack = hwnd;
    tme.dwHoverTime = 0;
    TrackMouseEvent(&tme);
}

/* per-channel brightness shift with clamping */
static COLORREF col_shift(COLORREF c, int d)
{
    int r = GetRValue(c) + d, g = GetGValue(c) + d, b = GetBValue(c) + d;
    if (r < 0)   r = 0;
    if (r > 255) r = 255;
    if (g < 0)   g = 0;
    if (g > 255) g = 255;
    if (b < 0)   b = 0;
    if (b > 255) b = 255;
    return RGB(r, g, b);
}

/* ================================================================== */
/* double-buffered paint target                                        */
/* ================================================================== */

/*
 * Creates a memory DC matching the control size. Drawing goes to
 * mem/dc (opaque, full-rect); zg_mem_finish blits it once. If any
 * allocation fails, painting falls back to the window DC directly.
 */
struct ZgMem {
    HDC     win;      /* DC from BeginPaint          */
    HDC     mem;      /* memory DC (or == win)       */
    HBITMAP bmp;      /* owned bitmap                */
    HBITMAP old;      /* original mem bitmap         */
};

static void zg_mem_begin(ZgMem* m, HWND hwnd, PAINTSTRUCT* ps)
{
    m->win = BeginPaint(hwnd, ps);
    m->mem = m->win;
    m->bmp = NULL;
    m->old = NULL;
    if (!m->win) return;

    RECT rc; GetClientRect(hwnd, &rc);
    int w = rc.right  < 1 ? 1 : rc.right;
    int h = rc.bottom < 1 ? 1 : rc.bottom;

    HDC dc = CreateCompatibleDC(m->win);
    HBITMAP bm = CreateCompatibleBitmap(m->win, w, h);
    if (dc && bm) {
        m->old = (HBITMAP)SelectObject(dc, bm);
        m->mem = dc;
        m->bmp = bm;
    } else {
        if (dc) DeleteDC(dc);
        if (bm) DeleteObject(bm);
    }
}

static void zg_mem_finish(ZgMem* m, HWND hwnd, PAINTSTRUCT* ps)
{
    if (m->mem != m->win) {
        RECT rc; GetClientRect(hwnd, &rc);
        int w = rc.right  < 1 ? 1 : rc.right;
        int h = rc.bottom < 1 ? 1 : rc.bottom;
        BitBlt(m->win, 0, 0, w, h, m->mem, 0, 0, SRCCOPY);
        SelectObject(m->mem, m->old);
        DeleteObject(m->bmp);
        DeleteDC(m->mem);
    }
    EndPaint(hwnd, ps);
}

/* ================================================================== */
/* per-control state structs                                           */
/* ================================================================== */

struct ZgBtnState {
    COLORREF bg_top, bg_bot;   /* MUST be the first members — zg_ctrl_set_bg */
    UINT     style;        /* ZGBTN_*          */
    int      scheme;       /* ZGBP_* (primary) */
    wchar_t  text[64];
    BOOL     hovering;
    BOOL     pressed;
    BOOL     disabled;
};

struct ZgTglState {
    COLORREF bg_top, bg_bot;   /* MUST be the first members — zg_ctrl_set_bg */
    BOOL on;
    BOOL hovering;
    BOOL disabled;
};

struct ZgCmbState {
    COLORREF     bg_top, bg_bot;  /* MUST be first — zg_ctrl_set_bg */
    int          count;
    int          capacity;
    int          selected;
    int          popup_scroll;
    int          hover_item;
    BOOL         open;
    HWND         hpopup;
    wchar_t    (*items)[48];   /* allocated right behind the struct */
};

struct ZgBtnInit { UINT style; int scheme; };

/* ================================================================== */
/* ZgButton                                                            */
/* ================================================================== */

static LRESULT CALLBACK ZgButtonProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    ZgBtnState* self = (ZgBtnState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_NCCREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        self = (ZgBtnState*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ZgBtnState));
        if (!self) return -1;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        ZgBtnInit* bi = (ZgBtnInit*)cs->lpCreateParams;
        self->bg_top = self->bg_bot = g_th.bg;
        self->style  = bi ? bi->style : ZGBTN_FLAT;
        self->scheme = bi ? bi->scheme : 0;
        wcsncpy(self->text, cs->lpszName ? cs->lpszName : L"", 63);
        self->text[63] = 0;
        return 1;
    }
    case WM_NCDESTROY:
        if (self) HeapFree(GetProcessHeap(), 0, self);
        return 0;

    case WM_ERASEBKGND:
        return 1;   /* the memory-DC paint covers the whole rect */

    case WM_PAINT: {
        if (!self) break;
        PAINTSTRUCT ps;
        ZgMem m;
        zg_mem_begin(&m, hwnd, &ps);
        HDC dc = m.mem;
        RECT rc; GetClientRect(hwnd, &rc);
        int w = rc.right, h = rc.bottom;
        if (w < 1 || h < 1) { zg_mem_finish(&m, hwnd, &ps); return 0; }

        /*
         * Common DCs are cached by Windows AND their bitmap contents
         * survive between users: a recycled DC may carry pixels of another
         * control. All painting therefore goes to a private memory bitmap
         * whose full rect is filled first. A Gdiplus::Graphics object also
         * leaves a DC in GM_ADVANCED with a world transform — reset it for
         * the direct-paint fallback path and never keep a Graphics object
         * alive while doing GDI calls below.
         */
        SetGraphicsMode(dc, GM_COMPATIBLE);
        SetBkMode(dc, TRANSPARENT);

        if (self->style == ZGBTN_LINK) {
            /* opaque background fill first — hover never ghosts again */
            {
                Gdiplus::Graphics g(dc);
                Gdiplus::SolidBrush bb(Gdiplus::Color(zg_argb(255, self->bg_top)));
                g.FillRectangle(&bb, 0, 0, w, h);
            }
            COLORREF c = self->hovering ? COL_GREEN_HOVER : COL_GREEN;
            SetTextColor(dc, c);
            HFONT f = (HFONT)SelectObject(dc, self->hovering ? g_font_body_b : g_font_body);
            RECT tr = rc;
            DrawTextW(dc, self->text, -1, &tr, DT_SINGLELINE | DT_RIGHT | DT_VCENTER);
            if (self->hovering) {
                SIZE sz; GetTextExtentPoint32W(dc, self->text, (int)wcslen(self->text), &sz);
                RECT line;
                line.left = rc.right - sz.cx;
                line.right = rc.right;
                line.top = (rc.top + rc.bottom) / 2 + sz.cy / 2 + 1;
                line.bottom = line.top + 1;
                HBRUSH b = CreateSolidBrush(c);
                FillRect(dc, &line, b);
                DeleteObject(b);
            }
            SelectObject(dc, f);
            zg_mem_finish(&m, hwnd, &ps);
            return 0;
        }

        /* body rect (inside the shadow fringe) + pressed offset */
        RECT b;
        zg_ctrl_body(0, 0, w, h, &b);
        int dy = self->pressed ? 1 : 0;

        int state = self->disabled ? ZGST_DISABLED
                  : self->pressed  ? ZGST_PRESSED
                  : self->hovering ? ZGST_HOVER
                  : ZGST_NORMAL;

        /* phase A: local background replica + GDI+ chrome (scoped Graphics) */
        {
            Gdiplus::Graphics g(dc);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            Gdiplus::LinearGradientBrush bb(Gdiplus::Point(0, 0), Gdiplus::Point(0, h),
                                            Gdiplus::Color(zg_argb(255, self->bg_top)),
                                            Gdiplus::Color(zg_argb(255, self->bg_bot)));
            g.FillRectangle(&bb, 0, 0, w, h);
            zg_draw_button(g, 0, 0, w, h, self->style, self->scheme, state);
        }

        /* phase B: GDI text */
        if (self->style == ZGBTN_PRIMARY) {
            LOGFONTW lf; GetObjectW(g_font_btn, sizeof(lf), &lf);
            lf.lfWeight = FW_SEMIBOLD;
            HFONT fb = CreateFontIndirectW(&lf);
            HFONT of = (HFONT)SelectObject(dc, fb);
            SetTextColor(dc, self->disabled ? RGB(0xCC,0xCC,0xCC) : RGB(255,255,255));
            RECT tr = { b.left, b.top + dy, b.right, b.bottom + dy };
            DrawTextW(dc, self->text, -1, &tr,
                      DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
            SelectObject(dc, of);
            DeleteObject(fb);
        } else {
            HFONT of = (HFONT)SelectObject(dc, self->disabled ? g_font_body : g_font_btn);
            SetTextColor(dc, self->disabled ? COL_MUTED2 : COL_TEXT);
            RECT tr = { b.left, b.top + dy, b.right, b.bottom + dy };
            DrawTextW(dc, self->text, -1, &tr,
                      DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            SelectObject(dc, of);
        }
        zg_mem_finish(&m, hwnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE:
        if (!self) break;
        if (!self->hovering) { self->hovering = TRUE; track_mouse_leave(hwnd); InvalidateRect(hwnd, NULL, TRUE); }
        return 0;

    case WM_MOUSELEAVE:
        if (self) { self->hovering = FALSE; self->pressed = FALSE; InvalidateRect(hwnd, NULL, TRUE); }
        return 0;

    case WM_LBUTTONDOWN:
        if (!self || self->disabled) return 0;
        self->pressed = TRUE;
        SetCapture(hwnd);
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;

    case WM_LBUTTONUP: {
        if (!self || self->disabled) { if (GetCapture() == hwnd) ReleaseCapture(); return 0; }
        BOOL was_pressed = self->pressed;
        if (GetCapture() == hwnd) ReleaseCapture();
        self->pressed = FALSE;
        RECT rc; GetClientRect(hwnd, &rc);
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (was_pressed && PtInRect(&rc, pt))
            SendMessageW(GetParent(hwnd), WM_ZGBUTTON, GetDlgCtrlID(hwnd), 0);
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }

    case WM_ENABLE:
        return 0; /* state managed via zg_button_set_disabled */
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void zg_button_set_text(HWND h, const wchar_t* text)
{
    ZgBtnState* self = (ZgBtnState*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (self) { wcsncpy(self->text, text, 63); self->text[63] = 0; }
    InvalidateRect(h, NULL, TRUE);
}

void zg_button_set_scheme(HWND h, int scheme)
{
    ZgBtnState* self = (ZgBtnState*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (self && self->scheme != scheme) { self->scheme = scheme; InvalidateRect(h, NULL, TRUE); }
}

void zg_button_set_disabled(HWND h, BOOL dis)
{
    ZgBtnState* self = (ZgBtnState*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (self && self->disabled != dis) { self->disabled = dis; InvalidateRect(h, NULL, TRUE); }
}

/* ================================================================== */
/* ZgToggle                                                            */
/* ================================================================== */

static LRESULT CALLBACK ZgToggleProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    ZgTglState* self = (ZgTglState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_NCCREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        self = (ZgTglState*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ZgTglState));
        if (!self) return -1;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        self->bg_top = self->bg_bot = g_th.bg;
        self->on = cs->lpCreateParams ? TRUE : FALSE;
        return 1;
    }
    case WM_NCDESTROY:
        if (self) HeapFree(GetProcessHeap(), 0, self);
        return 0;

    case WM_ERASEBKGND:
        return 1;   /* the memory-DC paint covers the whole rect */

    case WM_PAINT: {
        if (!self) break;
        PAINTSTRUCT ps;
        ZgMem m;
        zg_mem_begin(&m, hwnd, &ps);
        HDC dc = m.mem;
        RECT rc; GetClientRect(hwnd, &rc);
        int w = rc.right, h = rc.bottom;
        if (w < 1 || h < 1) { zg_mem_finish(&m, hwnd, &ps); return 0; }
        int pad = SC(2);
        if (h < 2 * pad + 6) pad = 0;
        int tw = w - 2 * pad, th = h - 2 * pad;   /* track */
        int r = th / 2;

        /* reset any GDI+ leftovers if painting falls back to the window DC */
        SetGraphicsMode(dc, GM_COMPATIBLE);

        Gdiplus::Graphics g(dc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        /* local background replica — the toggle rect is fully covered
         * by the track, the fill keeps the memory bitmap clean */
        {
            Gdiplus::LinearGradientBrush bb(Gdiplus::Point(0, 0), Gdiplus::Point(0, h),
                                            Gdiplus::Color(zg_argb(255, self->bg_top)),
                                            Gdiplus::Color(zg_argb(255, self->bg_bot)));
            g.FillRectangle(&bb, 0, 0, w, h);
        }

        Gdiplus::GraphicsPath* track = zg_round_path(pad, pad, tw - 1, th - 1, r);

        /* track body: vertical gradient + inner shadow (concave) */
        {
            COLORREF c1, c2;
            if (self->on) {
                c1 = self->disabled ? col_shift(g_th.green, -30) : g_th.green_hover;
                c2 = self->disabled ? col_shift(g_th.green, -60) : g_th.green;
                if (self->hovering && !self->disabled) { c1 = col_shift(c1, 14); c2 = col_shift(c2, 6); }
            } else {
                c1 = self->disabled ? col_shift(g_th.toggle_off, -20) : col_shift(g_th.toggle_off, 22);
                c2 = self->disabled ? col_shift(g_th.toggle_off, -35) : col_shift(g_th.toggle_off, -25);
                if (self->hovering && !self->disabled) c1 = col_shift(c1, 14);
            }
            Gdiplus::LinearGradientBrush tb(Gdiplus::Point(pad, pad),
                                            Gdiplus::Point(pad, pad + th),
                                            Gdiplus::Color(zg_argb(255, c1)),
                                            Gdiplus::Color(zg_argb(255, c2)));
            g.FillPath(&tb, track);

            /* inner top shadow — sunken groove */
            Gdiplus::LinearGradientBrush ish(Gdiplus::Point(pad, pad),
                                             Gdiplus::Point(pad, pad + th / 2),
                                             Gdiplus::Color(g_th.is_dark ? 70 : 40, 0, 0, 0),
                                             Gdiplus::Color(0, 0, 0, 0));
            ish.SetWrapMode(Gdiplus::WrapModeClamp);
            g.FillPath(&ish, track);

            Gdiplus::Pen rim(Gdiplus::Color(zg_argb(self->disabled ? 120 : 210, g_th.border)), 1.0f);
            g.DrawPath(&rim, track);
        }

        /* chrome thumb with a drop shadow */
        {
            int thumb_d = th - SC(8);
            if (thumb_d < 4) thumb_d = 4;
            int tx = self->on ? pad + tw - 1 - thumb_d - SC(3) : pad + SC(3);
            int ty = pad + (th - 1 - thumb_d) / 2;

            /* shadow under the thumb */
            Gdiplus::SolidBrush shb(Gdiplus::Color(g_th.is_dark ? 70 : 45, 0, 0, 0));
            g.FillEllipse(&shb, tx + 1, ty + 2, thumb_d, thumb_d);

            /* ball: white -> steel gradient */
            Gdiplus::GraphicsPath* ball = new Gdiplus::GraphicsPath();
            ball->AddEllipse(tx, ty, thumb_d, thumb_d);
            Gdiplus::LinearGradientBrush bb(Gdiplus::Point(tx, ty),
                                             Gdiplus::Point(tx, ty + thumb_d),
                                             Gdiplus::Color(255, 255, 255, 255),
                                             Gdiplus::Color(255, 0xD5, 0xDA, 0xE2));
            g.FillPath(&bb, ball);
            Gdiplus::Pen ring(Gdiplus::Color(160, 0x60, 0x66, 0x70), 1.0f);
            g.DrawPath(&ring, ball);
            delete ball;

            /* tiny specular dot */
            int sd = thumb_d / 3;
            if (sd > 2) {
                Gdiplus::SolidBrush spec(Gdiplus::Color(140, 255, 255, 255));
                g.FillEllipse(&spec, tx + thumb_d / 5, ty + thumb_d / 6, sd, sd);
            }
        }

        delete track;
        zg_mem_finish(&m, hwnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE:
        if (!self) break;
        if (!self->hovering) { self->hovering = TRUE; track_mouse_leave(hwnd); InvalidateRect(hwnd, NULL, FALSE); }
        return 0;

    case WM_MOUSELEAVE:
        if (self) { self->hovering = FALSE; InvalidateRect(hwnd, NULL, FALSE); }
        return 0;

    case WM_LBUTTONUP:
        if (!self || self->disabled) return 0;
        self->on = !self->on;
        InvalidateRect(hwnd, NULL, FALSE);
        SendMessageW(GetParent(hwnd), WM_ZGTOGGLE, GetDlgCtrlID(hwnd), (LPARAM)(self->on ? 1 : 0));
        return 0;

    case WM_SETCURSOR:
        SetCursor(LoadCursorW(NULL, IDC_HAND));
        return TRUE;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void zg_toggle_set(HWND h, BOOL on, BOOL notify)
{
    ZgTglState* self = (ZgTglState*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (self) self->on = on;
    InvalidateRect(h, NULL, FALSE);
    if (notify)
        SendMessageW(GetParent(h), WM_ZGTOGGLE, GetDlgCtrlID(h), (LPARAM)(on ? 1 : 0));
}

void zg_toggle_set_disabled(HWND h, BOOL dis)
{
    ZgTglState* self = (ZgTglState*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (self) self->disabled = dis;
    InvalidateRect(h, NULL, FALSE);
}

/* ================================================================== */
/* ZgCombo                                                             */
/* ================================================================== */

#define ZG_COMBO_ROW   30
#define ZG_COMBO_FRAME 4   /* chrome margin around the popup list */

static LRESULT CALLBACK ZgComboPopupProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

static void combo_open_popup(HWND hCombo)
{
    ZgCmbState* self = (ZgCmbState*)GetWindowLongPtrW(hCombo, GWLP_USERDATA);
    if (!self || self->open || self->count <= 0) return;

    RECT rc; GetWindowRect(hCombo, &rc);
    int vis = self->count < ZG_COMBO_MAXVIS ? self->count : ZG_COMBO_MAXVIS;
    int w = rc.right - rc.left;
    int m = SC(ZG_COMBO_FRAME);
    /* body size (matches the closed control), window adds the chrome margin */
    int h = vis * SC(ZG_COMBO_ROW) + 2 * SC(4) + 2;

    self->hover_item = -1;
    self->hpopup = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                                  L"ZgComboPopup", NULL,
                                  WS_POPUP | WS_VISIBLE,
                                  rc.left - m, rc.bottom + SC(6) - m,
                                  w + 2 * m, h + 2 * m,
                                  GetParent(hCombo), NULL, NULL, (LPVOID)hCombo);
    if (!self->hpopup) return;
    self->open = TRUE;

    if (self->selected >= ZG_COMBO_MAXVIS - 1)
        self->popup_scroll = self->selected - ZG_COMBO_MAXVIS + 2;
    else
        self->popup_scroll = 0;

    SetCapture(self->hpopup);
    InvalidateRect(self->hpopup, NULL, TRUE);
}

void zg_combo_close_popup(HWND hCombo)
{
    ZgCmbState* self = (ZgCmbState*)GetWindowLongPtrW(hCombo, GWLP_USERDATA);
    if (!self || !self->open) return;
    if (self->hpopup) {
        if (GetCapture() == self->hpopup) ReleaseCapture();
        DestroyWindow(self->hpopup);
        self->hpopup = NULL;
    }
    self->open = FALSE;
    InvalidateRect(hCombo, NULL, TRUE);
}

/* replace the item list (same or smaller count than allocated) */
BOOL zg_combo_set_items(HWND hCombo, const wchar_t* const* items, int count, int selected)
{
    ZgCmbState* self = (ZgCmbState*)GetWindowLongPtrW(hCombo, GWLP_USERDATA);
    if (!self || !items) return FALSE;
    if (count <= 0 || count > self->capacity) return FALSE;
    for (int i = 0; i < count; i++) {
        wcsncpy(self->items[i], items[i], 47);
        self->items[i][47] = 0;
    }
    self->count = count;
    self->selected = (selected >= 0 && selected < count) ? selected : -1;
    InvalidateRect(hCombo, NULL, TRUE);
    return TRUE;
}

static LRESULT CALLBACK ZgComboProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    ZgCmbState* self = (ZgCmbState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_NCCREATE: {
        typedef struct { const wchar_t* const* items; int count; int selected; } ComboInit;
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        ComboInit* ci = (ComboInit*)cs->lpCreateParams;
        int cnt = ci ? ci->count : 0;
        if (cnt < 0) cnt = 0;
        self = (ZgCmbState*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                      sizeof(ZgCmbState) + (size_t)(cnt > 0 ? cnt : 1) * 48 * sizeof(wchar_t));
        if (!self) return -1;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        self->bg_top = self->bg_bot = g_th.bg;
        self->count = cnt;
        self->capacity = cnt;
        self->selected = ci ? ci->selected : -1;
        self->items = (wchar_t (*)[48])(((BYTE*)self) + sizeof(ZgCmbState));
        for (int i = 0; i < cnt && ci; i++) {
            wcsncpy(self->items[i], ci->items[i], 47);
            self->items[i][47] = 0;
        }
        return 1;
    }
    case WM_NCDESTROY:
        if (self) HeapFree(GetProcessHeap(), 0, self);
        return 0;

    case WM_ERASEBKGND:
        return 1;   /* the memory-DC paint covers the whole rect */

    case WM_PAINT: {
        if (!self) break;
        PAINTSTRUCT ps;
        ZgMem m;
        zg_mem_begin(&m, hwnd, &ps);
        HDC dc = m.mem;
        RECT rc; GetClientRect(hwnd, &rc);
        int w = rc.right, h = rc.bottom;
        if (w < 1 || h < 1) { zg_mem_finish(&m, hwnd, &ps); return 0; }

        /* reset any GDI+ leftovers if painting falls back to the window DC */
        SetGraphicsMode(dc, GM_COMPATIBLE);

        BOOL hovering = (GetCapture() == hwnd);

        /* phase A: local background replica + GDI+ chrome panel (scoped Graphics) */
        {
            Gdiplus::Graphics g(dc);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            Gdiplus::LinearGradientBrush bb(Gdiplus::Point(0, 0), Gdiplus::Point(0, h),
                                            Gdiplus::Color(zg_argb(255, self->bg_top)),
                                            Gdiplus::Color(zg_argb(255, self->bg_bot)));
            g.FillRectangle(&bb, 0, 0, w, h);
            int state = hovering ? ZGST_HOVER : ZGST_NORMAL;
            zg_draw_button(g, 0, 0, w, h, ZGBTN_FLAT, 0, state);
        }

        /* phase B: GDI text + chevron (inside the body rect) */
        RECT b;
        zg_ctrl_body(0, 0, w, h, &b);

        SetBkMode(dc, TRANSPARENT);
        HFONT of = (HFONT)SelectObject(dc, g_font_body);
        SetTextColor(dc, COL_TEXT);
        RECT tr = { b.left + SC(10), b.top, b.right - SC(24), b.bottom };
        DrawTextW(dc, self->selected >= 0 ? self->items[self->selected] : L"",
                  -1, &tr, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
        SelectObject(dc, of);

        /* chevron */
        int cx = b.right - SC(16), cy = (b.top + b.bottom) / 2, s = SC(4);
        POINT pts[3] = { { cx - s, cy - s/2 }, { cx + s, cy - s/2 }, { cx, cy + s/2 } };
        HPEN pen = CreatePen(PS_SOLID, 1, COL_MUTED);
        HBRUSH br = CreateSolidBrush(hovering ? COL_TEXT : COL_MUTED);
        HPEN ofp = (HPEN)SelectObject(dc, pen);
        HBRUSH ofb = (HBRUSH)SelectObject(dc, br);
        Polygon(dc, pts, 3);
        SelectObject(dc, ofp); SelectObject(dc, ofb);
        DeleteObject(pen); DeleteObject(br);

        zg_mem_finish(&m, hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN:
        SetCapture(hwnd);
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;

    case WM_LBUTTONUP: {
        if (GetCapture() == hwnd) ReleaseCapture();
        InvalidateRect(hwnd, NULL, TRUE);
        if (self && self->open) zg_combo_close_popup(hwnd);
        else combo_open_popup(hwnd);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (self && !self->open) InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_SETCURSOR:
        SetCursor(LoadCursorW(NULL, IDC_HAND));
        return TRUE;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int zg_combo_get_sel(HWND h)
{
    ZgCmbState* self = (ZgCmbState*)GetWindowLongPtrW(h, GWLP_USERDATA);
    return self ? self->selected : -1;
}

void zg_combo_set_sel(HWND h, int idx, BOOL notify)
{
    ZgCmbState* self = (ZgCmbState*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (self) self->selected = idx;
    InvalidateRect(h, NULL, TRUE);
    if (notify)
        SendMessageW(GetParent(h), WM_ZGCOMBO, GetDlgCtrlID(h), (LPARAM)idx);
}

/* ---- popup window -------------------------------------------------- */

static LRESULT CALLBACK ZgComboPopupProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    HWND hCombo = (HWND)GetPropW(hwnd, L"ZgOwner");
    ZgCmbState* self = hCombo ? (ZgCmbState*)GetWindowLongPtrW(hCombo, GWLP_USERDATA) : NULL;

    switch (msg) {
    case WM_NCCREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        SetPropW(hwnd, L"ZgOwner", (HANDLE)cs->lpCreateParams);
        return 1;
    }
    case WM_NCDESTROY:
        RemovePropW(hwnd, L"ZgOwner");
        return 0;

    case WM_ERASEBKGND:
        return 1;  /* painted in WM_PAINT */

    case WM_PAINT: {
        if (!self) break;
        PAINTSTRUCT ps;
        ZgMem m;
        zg_mem_begin(&m, hwnd, &ps);
        HDC dc = m.mem;
        RECT rc; GetClientRect(hwnd, &rc);
        int w = rc.right, h = rc.bottom;
        if (w < 1 || h < 1) { zg_mem_finish(&m, hwnd, &ps); return 0; }

        /* reset any GDI+ leftovers if painting falls back to the window DC */
        SetGraphicsMode(dc, GM_COMPATIBLE);

        int mgn = SC(ZG_COMBO_FRAME);
        int bw = w - 2 * mgn, bh = h - 2 * mgn;   /* body inside the margin */

        /* phase A: local background replica + chrome shell */
        {
            Gdiplus::Graphics g(dc);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            /* the ring around the panel approximates the surface the popup
             * floats over (the owner combo's own background) */
            Gdiplus::LinearGradientBrush bb(Gdiplus::Point(0, 0), Gdiplus::Point(0, h),
                                            Gdiplus::Color(zg_argb(255, self->bg_top)),
                                            Gdiplus::Color(zg_argb(255, self->bg_bot)));
            g.FillRectangle(&bb, 0, 0, w, h);
            zg_draw_shadow(g, mgn, mgn, bw, bh, SC(8), SC(3), 110);
            zg_draw_chrome_panel(g, mgn, mgn, bw, bh, SC(8),
                                 g_th.card_top, g_th.card_bot, g_th.border, 0);
        }

        /* phase B: rows (GDI) */
        SetBkMode(dc, TRANSPARENT);
        int rowh = SC(ZG_COMBO_ROW);
        int pad = SC(4);
        int vw = bw - 2 * pad;   /* row width inside the body */

        for (int i = 0; i < self->count; i++) {
            int row_idx = i - self->popup_scroll;
            if (row_idx < 0 || row_idx >= ZG_COMBO_MAXVIS) continue;
            int y = mgn + pad + row_idx * rowh;
            BOOL hovered = (self->hover_item == i);
            BOOL selected = (self->selected == i);

            if (selected) {
                HBRUSH sb = CreateSolidBrush(COL_PANEL_HOVER);
                RECT sr = { mgn + pad, y, mgn + pad + vw, y + rowh };
                FillRect(dc, &sr, sb);
                DeleteObject(sb);
                HBRUSH ab = CreateSolidBrush(COL_GREEN);
                RECT abr = { mgn + pad, y + SC(7), mgn + pad + SC(3), y + rowh - SC(7) };
                FillRect(dc, &abr, ab);
                DeleteObject(ab);
            } else if (hovered) {
                HBRUSH hb = CreateSolidBrush(g_th.popup_hover);
                RECT hr2 = { mgn + pad, y, mgn + pad + vw, y + rowh };
                FillRect(dc, &hr2, hb);
                DeleteObject(hb);
            }

            HFONT of = (HFONT)SelectObject(dc, selected ? g_font_body_b : g_font_body);
            SetTextColor(dc, COL_TEXT);
            RECT tr = { mgn + pad + SC(10), y, mgn + pad + vw - SC(6), y + rowh };
            DrawTextW(dc, self->items[i], -1, &tr,
                      DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
            SelectObject(dc, of);
        }

        /* scrollbar */
        int vis = self->count < ZG_COMBO_MAXVIS ? self->count : ZG_COMBO_MAXVIS;
        if (self->count > vis) {
            int track_y0 = mgn + pad, track_y1 = h - mgn - pad;
            int thumb_h = (track_y1 - track_y0) * vis / self->count;
            int thumb_y = track_y0 + (track_y1 - track_y0 - thumb_h)
                          * self->popup_scroll / (self->count - vis);
            HBRUSH tb = CreateSolidBrush(COL_GRAY_DIM);
            RECT tbr = { w - mgn - SC(4), thumb_y, w - mgn - SC(1), thumb_y + thumb_h };
            FillRect(dc, &tbr, tb);
            DeleteObject(tb);
        }

        zg_mem_finish(&m, hwnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (!self) break;
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int rowh = SC(ZG_COMBO_ROW);
        int m = SC(ZG_COMBO_FRAME);
        int vis = self->count < ZG_COMBO_MAXVIS ? self->count : ZG_COMBO_MAXVIS;
        int idx = -1;
        int r = (pt.y - m - SC(4)) / rowh;
        if (r >= 0 && r < vis) {
            idx = self->popup_scroll + r;
            if (idx >= self->count) idx = -1;
        }
        if (self->hover_item != idx) {
            self->hover_item = idx;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        if (!self) break;
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        int vis = self->count < ZG_COMBO_MAXVIS ? self->count : ZG_COMBO_MAXVIS;
        int maxscroll = self->count - vis;
        self->popup_scroll -= delta / WHEEL_DELTA;
        if (self->popup_scroll < 0) self->popup_scroll = 0;
        if (self->popup_scroll > maxscroll) self->popup_scroll = maxscroll;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_LBUTTONDOWN:
        return 0;  /* keep capture, select on button-up */

    case WM_LBUTTONUP: {
        if (!self) break;
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        RECT rc; GetClientRect(hwnd, &rc);
        if (!PtInRect(&rc, pt)) {
            zg_combo_close_popup(hCombo);
            return 0;
        }
        int rowh = SC(ZG_COMBO_ROW);
        int m = SC(ZG_COMBO_FRAME);
        int vis = self->count < ZG_COMBO_MAXVIS ? self->count : ZG_COMBO_MAXVIS;
        int r = (pt.y - m - SC(4)) / rowh;
        if (r >= 0 && r < vis) {
            int idx = self->popup_scroll + r;
            if (idx >= 0 && idx < self->count)
                zg_combo_set_sel(hCombo, idx, TRUE);
        }
        zg_combo_close_popup(hCombo);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ================================================================== */
/* factories / registration                                            */
/* ================================================================== */

/*
 * Local background replica for the shadow fringe. All control state
 * structs begin with { bg_top, bg_bot } — this shared setter works for
 * buttons, toggles and combos alike. The main window computes the color
 * the parent surface has at the control's position (solid bg or the
 * settings-panel gradient) and pushes it here after layout/theme/DPI
 * changes. Controls fill their memory bitmap with it before drawing
 * the chrome, so the fringe blends into the parent surface.
 */
struct ZgBgHdr { COLORREF bg_top, bg_bot; };

void zg_ctrl_set_bg(HWND h, COLORREF top, COLORREF bottom)
{
    ZgBgHdr* p = (ZgBgHdr*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (p && (p->bg_top != top || p->bg_bot != bottom)) {
        p->bg_top = top;
        p->bg_bot = bottom;
        InvalidateRect(h, NULL, FALSE);
    }
}

void zg_register_controls(void)
{
    WNDCLASSW wc;
    HINSTANCE hi = GetModuleHandleW(NULL);

    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc   = ZgButtonProc;
    wc.hInstance     = hi;
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"ZgButton";
    RegisterClassW(&wc);

    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc   = ZgToggleProc;
    wc.hInstance     = hi;
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"ZgToggle";
    RegisterClassW(&wc);

    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc   = ZgComboProc;
    wc.hInstance     = hi;
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"ZgCombo";
    RegisterClassW(&wc);

    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc   = ZgComboPopupProc;
    wc.hInstance     = hi;
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"ZgComboPopup";
    RegisterClassW(&wc);
}

HWND zg_button_create(HWND parent, int id, const wchar_t* text, UINT style, int scheme)
{
    ZgBtnInit bi; bi.style = style; bi.scheme = scheme;
    return CreateWindowExW(0, L"ZgButton", text,
                           WS_CHILD | WS_VISIBLE,
                           0, 0, 10, 10, parent, (HMENU)(INT_PTR)id,
                           GetModuleHandleW(NULL), (LPVOID)&bi);
}

HWND zg_toggle_create(HWND parent, int id, BOOL initial)
{
    return CreateWindowExW(0, L"ZgToggle", L"",
                           WS_CHILD | WS_VISIBLE,
                           0, 0, 10, 10, parent, (HMENU)(INT_PTR)id,
                           GetModuleHandleW(NULL), (LPVOID)(INT_PTR)(initial ? 1 : 0));
}

HWND zg_combo_create(HWND parent, int id, const wchar_t* const* items, int count, int selected)
{
    typedef struct { const wchar_t* const* items; int count; int selected; } ComboInit;
    ComboInit ci;
    ci.items = items; ci.count = count; ci.selected = selected;
    return CreateWindowExW(0, L"ZgCombo", L"",
                           WS_CHILD | WS_VISIBLE,
                           0, 0, 10, 10, parent, (HMENU)(INT_PTR)id,
                           GetModuleHandleW(NULL), &ci);
}
