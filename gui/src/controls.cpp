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
#include "gfx.h"

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




/* ------------------------------------------------------------------ */
/* canvas paint target: backdrop crop -> software render -> one blit   */
/* ------------------------------------------------------------------ */
struct ZgPaint { HDC win; ZgCanvas cv; BOOL ok; int w, h; };

static BOOL zp_begin(ZgPaint* p, HWND hwnd, PAINTSTRUCT* ps)
{
    p->win = BeginPaint(hwnd, ps);
    RECT rc; GetClientRect(hwnd, &rc);
    p->w = rc.right; p->h = rc.bottom;
    p->ok = FALSE;
    if (p->w < 1 || p->h < 1) return FALSE;
    if (!zg_canvas_create(&p->cv, p->w, p->h)) return FALSE;
    if (!zg_backdrop_fill(hwnd, &p->cv)) zg_canvas_fill(&p->cv, g_th.bg);
    SetBkMode(p->cv.dc, TRANSPARENT);
    p->ok = TRUE;
    return TRUE;
}

static void zp_end(ZgPaint* p, HWND hwnd, PAINTSTRUCT* ps)
{
    if (p->ok) {
        GdiFlush();
        BitBlt(p->win, 0, 0, p->w, p->h, p->cv.dc, 0, 0, SRCCOPY);
        zg_canvas_destroy(&p->cv);
    }
    EndPaint(hwnd, ps);
}

/* text with a soft 1px shadow (shadow colour = dark tone of the surface) */
static void zp_text(HDC dc, const wchar_t* t, RECT r, UINT fmt, COLORREF col, COLORREF shadow, int sdy)
{
    if (sdy) {
        RECT sr = r; OffsetRect(&sr, 0, sdy);
        SetTextColor(dc, shadow);
        DrawTextW(dc, t, -1, &sr, fmt);
    }
    SetTextColor(dc, col);
    DrawTextW(dc, t, -1, &r, fmt);
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
        PAINTSTRUCT ps; ZgPaint zp;
        if (!zp_begin(&zp, hwnd, &ps)) { zp_end(&zp, hwnd, &ps); return 0; }
        HDC dc = zp.cv.dc;
        int w = zp.w, h = zp.h;

        if (self->style == ZGBTN_LINK) {
            RECT rc = { 0, 0, w, h };
            COLORREF c = self->hovering ? COL_GREEN_HOVER : COL_GREEN;
            SetTextColor(dc, c);
            HFONT f = (HFONT)SelectObject(dc, self->hovering ? g_font_body_b : g_font_body);
            RECT tr = rc;
            DrawTextW(dc, self->text, -1, &tr, DT_SINGLELINE | DT_RIGHT | DT_VCENTER);
            if (self->hovering) {
                SIZE sz; GetTextExtentPoint32W(dc, self->text, (int)wcslen(self->text), &sz);
                RECT line;
                line.left = rc.right - sz.cx; line.right = rc.right;
                line.top = (rc.top + rc.bottom) / 2 + sz.cy / 2 + 1; line.bottom = line.top + 1;
                HBRUSH b = CreateSolidBrush(c); FillRect(dc, &line, b); DeleteObject(b);
            }
            SelectObject(dc, f);
            zp_end(&zp, hwnd, &ps);
            return 0;
        }

        RECT b; zg_ctrl_body(0, 0, w, h, &b);
        int dy = self->pressed ? 1 : 0;
        int state = self->disabled ? ZGG_DISABLED : self->pressed ? ZGG_PRESSED
                  : self->hovering ? ZGG_HOVER : ZGG_NORMAL;
        zg_gfx_button(&zp.cv, b.left, b.top, b.right - b.left, b.bottom - b.top,
                      self->style == ZGBTN_PRIMARY, self->scheme, state);
        if (self->style == ZGBTN_GEAR) {
            zg_gfx_gear(&zp.cv, (b.left + b.right) * 0.5f, (b.top + b.bottom) * 0.5f + dy,
                        (float)SC(17), self->disabled ? COL_MUTED2 : COL_TEXT, 235);
            zp_end(&zp, hwnd, &ps);
            return 0;
        }
        GdiFlush();

        RECT tr = { b.left, b.top + dy, b.right, b.bottom + dy };
        if (self->style == ZGBTN_PRIMARY) {
            LOGFONTW lf; GetObjectW(g_font_btn, sizeof(lf), &lf);
            lf.lfWeight = FW_SEMIBOLD;
            HFONT fb = CreateFontIndirectW(&lf);
            HFONT of = (HFONT)SelectObject(dc, fb);
            COLORREF sh = (self->scheme == ZGBP_GREEN) ? RGB(0x0B, 0x4D, 0x1C) : RGB(0x3A, 0x22, 0x66);
            zp_text(dc, self->text, tr, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX,
                    self->disabled ? RGB(0xCC,0xCC,0xCC) : RGB(255,255,255), sh, 1);
            SelectObject(dc, of); DeleteObject(fb);
        } else {
            HFONT of = (HFONT)SelectObject(dc, self->disabled ? g_font_body : g_font_btn);
            zp_text(dc, self->text, tr, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX,
                    self->disabled ? COL_MUTED2 : COL_TEXT,
                    g_th.is_dark ? RGB(0x10,0x11,0x14) : RGB(0xFF,0xFF,0xFF), g_th.is_dark ? 1 : 0);
            SelectObject(dc, of);
        }
        zp_end(&zp, hwnd, &ps);
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
        PAINTSTRUCT ps; ZgPaint zp;
        if (!zp_begin(&zp, hwnd, &ps)) { zp_end(&zp, hwnd, &ps); return 0; }
        int pad = SC(2);
        if (zp.h < 2 * pad + 6) pad = 0;
        zg_gfx_toggle(&zp.cv, pad, pad, zp.w - 2 * pad, zp.h - 2 * pad,
                      self->on, self->hovering, self->disabled);
        zp_end(&zp, hwnd, &ps);
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
static ZgCanvas g_popbg;   /* screen pixels under the open popup (for its shadow fringe) */

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
    {
        int pw_ = w + 2 * m, ph_ = h + 2 * m;
        zg_canvas_destroy(&g_popbg);
        if (zg_canvas_create(&g_popbg, pw_, ph_)) {
            HDC sdc = GetDC(NULL);
            zg_canvas_grab_dc(&g_popbg, 0, 0, sdc, rc.left - m, rc.bottom + SC(6) - m, pw_, ph_);
            ReleaseDC(NULL, sdc);
        }
    }
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
        PAINTSTRUCT ps; ZgPaint zp;
        if (!zp_begin(&zp, hwnd, &ps)) { zp_end(&zp, hwnd, &ps); return 0; }
        HDC dc = zp.cv.dc;
        BOOL hovering = (GetCapture() == hwnd) || self->open;
        RECT b; zg_ctrl_body(0, 0, zp.w, zp.h, &b);
        zg_gfx_button(&zp.cv, b.left, b.top, b.right - b.left, b.bottom - b.top,
                      0, 0, hovering ? ZGG_HOVER : ZGG_NORMAL);
        zg_gfx_chevron(&zp.cv, (float)(b.right - SC(16)), (float)(b.top + b.bottom) * 0.5f,
                       (float)SC(4), hovering ? COL_TEXT : COL_MUTED, 235);
        GdiFlush();
        HFONT of = (HFONT)SelectObject(dc, g_font_body);
        RECT tr = { b.left + SC(11), b.top, b.right - SC(26), b.bottom };
        zp_text(dc, self->selected >= 0 ? self->items[self->selected] : L"", tr,
                DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS, COL_TEXT,
                g_th.is_dark ? RGB(0x10,0x11,0x14) : RGB(0xFF,0xFF,0xFF), g_th.is_dark ? 1 : 0);
        SelectObject(dc, of);
        zp_end(&zp, hwnd, &ps);
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
        PAINTSTRUCT ps; ZgPaint zp;
        HDC wdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        int w = rc.right, h = rc.bottom;
        ZgCanvas cv;
        if (w < 1 || h < 1 || !zg_canvas_create(&cv, w, h)) { EndPaint(hwnd, &ps); return 0; }
        (void)zp;
        if (g_popbg.px && g_popbg.w == w && g_popbg.h == h) zg_canvas_copy_rect(&cv, 0, 0, &g_popbg, 0, 0, w, h);
        else zg_canvas_fill(&cv, g_th.bg);
        HDC dc = cv.dc; SetBkMode(dc, TRANSPARENT);

        int mgn = SC(ZG_COMBO_FRAME);
        int bw = w - 2 * mgn, bh = h - 2 * mgn;
        int rowh = SC(ZG_COMBO_ROW), pad = SC(4), vw = bw - 2 * pad;

        ZgGlass gs; zg_glass_style_popup(&gs);
        zg_gfx_glass(&cv, mgn, mgn, bw, bh, SC(9), &gs);

        /* rows: glass highlight pills (selected = accent tinted) */
        for (int i = 0; i < self->count; i++) {
            int ri = i - self->popup_scroll;
            if (ri < 0 || ri >= ZG_COMBO_MAXVIS) continue;
            int y = mgn + pad + ri * rowh;
            BOOL hov = (self->hover_item == i), sel = (self->selected == i);
            if (!sel && !hov) continue;
            ZgGlass rg; memset(&rg, 0, sizeof(rg));
            rg.tint = sel ? g_th.green : RGB(255, 255, 255);
            rg.a_hi = sel ? (g_th.is_dark ? 70 : 60) : (g_th.is_dark ? 40 : 90);
            rg.a_lo = sel ? (g_th.is_dark ? 30 : 30) : (g_th.is_dark ? 14 : 40);
            rg.rim = sel ? 150 : 90; rg.sheen = 40;
            zg_gfx_glass(&cv, mgn + pad, y + SC(1), vw, rowh - SC(2), SC(6), &rg);
        }
        GdiFlush();

        for (int i = 0; i < self->count; i++) {
            int ri = i - self->popup_scroll;
            if (ri < 0 || ri >= ZG_COMBO_MAXVIS) continue;
            int y = mgn + pad + ri * rowh;
            BOOL sel = (self->selected == i);
            HFONT of = (HFONT)SelectObject(dc, sel ? g_font_body_b : g_font_body);
            RECT tr = { mgn + pad + SC(12), y, mgn + pad + vw - SC(8), y + rowh };
            zp_text(dc, self->items[i], tr, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS,
                    COL_TEXT, RGB(0,0,0), 0);
            SelectObject(dc, of);
        }

        int vis = self->count < ZG_COMBO_MAXVIS ? self->count : ZG_COMBO_MAXVIS;
        if (self->count > vis) {
            int ty0 = mgn + pad, ty1 = h - mgn - pad;
            int th = (ty1 - ty0) * vis / self->count;
            int tyy = ty0 + (ty1 - ty0 - th) * self->popup_scroll / (self->count - vis);
            ZgGlass tg; memset(&tg, 0, sizeof(tg));
            tg.tint = g_th.muted; tg.a_hi = 150; tg.a_lo = 110; tg.rim = 60;
            zg_gfx_glass(&cv, w - mgn - SC(6), tyy, SC(3), th, SC(1), &tg);
        }
        GdiFlush();
        BitBlt(wdc, 0, 0, w, h, dc, 0, 0, SRCCOPY);
        zg_canvas_destroy(&cv);
        EndPaint(hwnd, &ps);
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
