/*
 * Zapret GUI — controls.cpp
 * Implementation of custom-drawn dark-theme controls (GDI+ for
 * anti-aliased rounded shapes and gradients).
 */
#include <windows.h>
#include <windowsx.h>
#include <stdio.h>
#include "common.h"
#include "controls.h"

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

static Gdiplus::GraphicsPath* round_rect_path(int x, int y, int w, int h, int r)
{
    Gdiplus::GraphicsPath* p = new Gdiplus::GraphicsPath();
    int d = r * 2;
    if (r <= 0) { p->AddRectangle(Gdiplus::Rect(x, y, w, h)); return p; }
    p->AddArc(x,     y,     d, d, 180, 90);
    p->AddArc(x+w-d, y,     d, d, 270, 90);
    p->AddArc(x+w-d, y+h-d, d, d,   0, 90);
    p->AddArc(x,     y+h-d, d, d,  90, 90);
    p->CloseFigure();
    return p;
}

/* ================================================================== */
/* per-control state structs                                           */
/* ================================================================== */

struct ZgBtnState {
    UINT     style;        /* ZGBTN_*          */
    int      scheme;       /* ZGBP_* (primary) */
    wchar_t  text[64];
    BOOL     hovering;
    BOOL     pressed;
    BOOL     disabled;
};

struct ZgTglState {
    BOOL on;
    BOOL hovering;
    BOOL disabled;
};

struct ZgCmbState {
    int          count;
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
        self->style  = bi ? bi->style : ZGBTN_FLAT;
        self->scheme = bi ? bi->scheme : 0;
        wcsncpy(self->text, cs->lpszName ? cs->lpszName : L"", 63);
        self->text[63] = 0;
        return 1;
    }
    case WM_NCDESTROY:
        if (self) HeapFree(GetProcessHeap(), 0, self);
        return 0;

    case WM_PAINT: {
        if (!self) break;
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        int w = rc.right, h = rc.bottom;

        /*
         * Common DCs are cached by Windows: their state SURVIVES between
         * BeginPaint/EndPaint calls. A Gdiplus::Graphics object leaves the
         * DC in GM_ADVANCED with a world transform, which corrupted every
         * subsequent paint (missing pill, sheared text). Reset it here, and
         * never keep a Graphics object alive while doing GDI calls below.
         */
        SetGraphicsMode(hdc, GM_COMPATIBLE);
        SetBkMode(hdc, TRANSPARENT);

        if (self->style == ZGBTN_LINK) {
            COLORREF c = self->hovering ? COL_GREEN_HOVER : COL_GREEN;
            SetTextColor(hdc, c);
            HFONT f = (HFONT)SelectObject(hdc, self->hovering ? g_font_body_b : g_font_body);
            RECT tr = rc;
            DrawTextW(hdc, self->text, -1, &tr, DT_SINGLELINE | DT_RIGHT | DT_VCENTER);
            if (self->hovering) {
                SIZE sz; GetTextExtentPoint32W(hdc, self->text, (int)wcslen(self->text), &sz);
                RECT line;
                line.left = rc.right - sz.cx;
                line.right = rc.right;
                line.top = (rc.top + rc.bottom) / 2 + sz.cy / 2 + 1;
                line.bottom = line.top + 1;
                HBRUSH b = CreateSolidBrush(c);
                FillRect(hdc, &line, b);
                DeleteObject(b);
            }
            SelectObject(hdc, f);
            EndPaint(hwnd, &ps);
            return 0;
        }

        if (self->style == ZGBTN_PRIMARY) {
            /* phase A: GDI+ shapes (scoped Graphics) */
            {
                Gdiplus::Graphics g(hdc);
                g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                Gdiplus::GraphicsPath* path = round_rect_path(0, 0, w - 1, h - 1, h / 2);
                Gdiplus::Color c1, c2;
                if (self->scheme == ZGBP_GREEN) {
                    if (self->pressed)       { c1 = Gdiplus::Color(255, 0x1E, 0x7A, 0x2E); c2 = Gdiplus::Color(255, 0x24, 0x8F, 0x37); }
                    else if (self->hovering) { c1 = Gdiplus::Color(255, 0x2F, 0xB0, 0x44); c2 = Gdiplus::Color(255, 0x39, 0xC4, 0x4F); }
                    else                     { c1 = Gdiplus::Color(255, 0x2A, 0xA0, 0x3A); c2 = Gdiplus::Color(255, 0x32, 0xC0, 0x50); }
                } else {
                    if (self->pressed)       { c1 = Gdiplus::Color(255, 0x5E, 0x4E, 0xC0); c2 = Gdiplus::Color(255, 0xD8, 0x50, 0x38); }
                    else if (self->hovering) { c1 = Gdiplus::Color(255, 0x8F, 0x7C, 0xFF); c2 = Gdiplus::Color(255, 0xFF, 0x7E, 0x5E); }
                    else                     { c1 = Gdiplus::Color(255, 0x7B, 0x68, 0xEE); c2 = Gdiplus::Color(255, 0xFF, 0x63, 0x47); }
                }
                Gdiplus::LinearGradientBrush br(Gdiplus::Point(0, 0), Gdiplus::Point(w, 0), c1, c2);
                g.FillPath(&br, path);
                if (self->disabled) {
                    Gdiplus::SolidBrush dim(Gdiplus::Color(150, 0x20, 0x20, 0x20));
                    g.FillPath(&dim, path);
                }
                delete path;
            }

            /* phase B: GDI text */
            LOGFONTW lf; GetObjectW(g_font_btn, sizeof(lf), &lf);
            lf.lfWeight = FW_SEMIBOLD;
            HFONT fb = CreateFontIndirectW(&lf);
            HFONT of = (HFONT)SelectObject(hdc, fb);
            SetTextColor(hdc, self->disabled ? RGB(0xCC,0xCC,0xCC) : RGB(255,255,255));
            RECT tr = { 0, 0, w, h };
            DrawTextW(hdc, self->text, -1, &tr,
                      DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
            SelectObject(hdc, of);
            DeleteObject(fb);
            EndPaint(hwnd, &ps);
            return 0;
        }

        /* flat */
        {
            Gdiplus::Graphics g(hdc);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            Gdiplus::GraphicsPath* path = round_rect_path(0, 0, w - 1, h - 1, SC(8));
            Gdiplus::SolidBrush brFill(self->pressed ? COL_PANEL_DOWN
                                       : self->hovering ? COL_PANEL_HOVER : COL_PANEL);
            g.FillPath(&brFill, path);
            Gdiplus::Pen penBorder(Gdiplus::Color(zg_argb(self->disabled ? 120 : 230, COL_BORDER)));
            g.DrawPath(&penBorder, path);
            delete path;
        }

        HFONT of = (HFONT)SelectObject(hdc, self->disabled ? g_font_body : g_font_btn);
        SetTextColor(hdc, self->disabled ? COL_MUTED2 : COL_TEXT);
        RECT tr = { 0, 0, w, h };
        DrawTextW(hdc, self->text, -1, &tr,
                  DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(hdc, of);
        EndPaint(hwnd, &ps);
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
        self->on = cs->lpCreateParams ? TRUE : FALSE;
        return 1;
    }
    case WM_NCDESTROY:
        if (self) HeapFree(GetProcessHeap(), 0, self);
        return 0;

    case WM_PAINT: {
        if (!self) break;
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        int w = rc.right, h = rc.bottom;
        int r = h / 2;

        /* reset any GDI+ leftovers on the cached common DC */
        SetGraphicsMode(hdc, GM_COMPATIBLE);

        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        Gdiplus::Color track;
        if (self->on)
            track = Gdiplus::Color(self->disabled ? 140 : 255, 0x32, 0xCD, 0x32);
        else
            track = Gdiplus::Color(self->disabled ? 120 : 255, 0x55, 0x55, 0x55);
        if (self->hovering && !self->disabled)
            track = self->on ? Gdiplus::Color(255, 0x4A, 0xE0, 0x4A)
                             : Gdiplus::Color(255, 0x66, 0x66, 0x66);

        Gdiplus::GraphicsPath* p = round_rect_path(0, 0, w - 1, h - 1, r);
        Gdiplus::SolidBrush br(track);
        g.FillPath(&br, p);
        delete p;

        int thumb_d = h - SC(8);
        int tx = self->on ? w - thumb_d - SC(4) : SC(4);
        int ty = (h - thumb_d) / 2;
        Gdiplus::SolidBrush tb(self->disabled ? Gdiplus::Color(255, 0xB8, 0xB8, 0xB8)
                                               : Gdiplus::Color(255, 255, 255, 255));
        g.FillEllipse(&tb, tx, ty, thumb_d, thumb_d);

        EndPaint(hwnd, &ps);
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

#define ZG_COMBO_ROW 30

static LRESULT CALLBACK ZgComboPopupProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

static void combo_open_popup(HWND hCombo)
{
    ZgCmbState* self = (ZgCmbState*)GetWindowLongPtrW(hCombo, GWLP_USERDATA);
    if (!self || self->open || self->count <= 0) return;

    RECT rc; GetWindowRect(hCombo, &rc);
    int vis = self->count < ZG_COMBO_MAXVIS ? self->count : ZG_COMBO_MAXVIS;
    int w = rc.right - rc.left;
    int h = vis * SC(ZG_COMBO_ROW) + 2 * SC(4) + 2;

    self->hover_item = -1;
    self->hpopup = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                                  L"ZgComboPopup", NULL,
                                  WS_POPUP | WS_VISIBLE,
                                  rc.left, rc.bottom + SC(6), w, h,
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

static LRESULT CALLBACK ZgComboProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    ZgCmbState* self = (ZgCmbState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_NCCREATE: {
        typedef struct { const wchar_t* const* items; int count; int selected; } ComboInit;
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        ComboInit* ci = (ComboInit*)cs->lpCreateParams;
        int cnt = ci ? ci->count : 0;
        self = (ZgCmbState*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                      sizeof(ZgCmbState) + (size_t)(cnt > 0 ? cnt : 1) * 48 * sizeof(wchar_t));
        if (!self) return -1;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        self->count = cnt;
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

    case WM_PAINT: {
        if (!self) break;
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        int w = rc.right, h = rc.bottom;

        /* reset any GDI+ leftovers on the cached common DC */
        SetGraphicsMode(hdc, GM_COMPATIBLE);

        BOOL hovering = (GetCapture() == hwnd);
        COLORREF fill = hovering ? COL_PANEL_HOVER : COL_PANEL;

        /* phase A: GDI+ panel (scoped Graphics) */
        {
            Gdiplus::Graphics g(hdc);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            Gdiplus::GraphicsPath* path = round_rect_path(0, 0, w - 1, h - 1, SC(8));
            Gdiplus::SolidBrush brFill(fill);
            g.FillPath(&brFill, path);
            Gdiplus::Pen penBorder(Gdiplus::Color(zg_argb(230, COL_BORDER)));
            g.DrawPath(&penBorder, path);
            delete path;
        }

        /* phase B: GDI text + chevron */
        SetBkMode(hdc, TRANSPARENT);
        HFONT of = (HFONT)SelectObject(hdc, g_font_body);
        SetTextColor(hdc, COL_TEXT);
        RECT tr = { SC(12), 0, w - SC(30), h };
        DrawTextW(hdc, self->selected >= 0 ? self->items[self->selected] : L"",
                  -1, &tr, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
        SelectObject(hdc, of);

        /* chevron */
        int cx = w - SC(17), cy = h / 2, s = SC(4);
        POINT pts[3] = { { cx - s, cy - s/2 }, { cx + s, cy - s/2 }, { cx, cy + s/2 } };
        HPEN pen = CreatePen(PS_SOLID, 1, COL_MUTED);
        HBRUSH br = CreateSolidBrush(COL_MUTED);
        HPEN ofp = (HPEN)SelectObject(hdc, pen);
        HBRUSH ofb = (HBRUSH)SelectObject(hdc, br);
        Polygon(hdc, pts, 3);
        SelectObject(hdc, ofp); SelectObject(hdc, ofb);
        DeleteObject(pen); DeleteObject(br);

        EndPaint(hwnd, &ps);
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
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        int w = rc.right, h = rc.bottom;

        /* reset any GDI+ leftovers on the cached common DC */
        SetGraphicsMode(hdc, GM_COMPATIBLE);

        HBRUSH bg = CreateSolidBrush(COL_PANEL);
        RECT full = { 0, 0, w, h };
        FillRect(hdc, &full, bg);
        DeleteObject(bg);

        SetBkMode(hdc, TRANSPARENT);
        int rowh = SC(ZG_COMBO_ROW);
        int pad = SC(4);

        for (int i = 0; i < self->count; i++) {
            int row_idx = i - self->popup_scroll;
            if (row_idx < 0 || row_idx >= ZG_COMBO_MAXVIS) continue;
            int y = pad + row_idx * rowh;
            BOOL hovered = (self->hover_item == i);
            BOOL selected = (self->selected == i);

            if (selected) {
                HBRUSH sb = CreateSolidBrush(COL_PANEL_HOVER);
                RECT sr = { SC(2), y, w - SC(2), y + rowh };
                FillRect(hdc, &sr, sb);
                DeleteObject(sb);
                HBRUSH ab = CreateSolidBrush(COL_GREEN);
                RECT abr = { 0, y + SC(7), SC(3), y + rowh - SC(7) };
                FillRect(hdc, &abr, ab);
                DeleteObject(ab);
            } else if (hovered) {
                HBRUSH hb = CreateSolidBrush(RGB(0x35, 0x35, 0x35));
                RECT hr2 = { SC(2), y, w - SC(2), y + rowh };
                FillRect(hdc, &hr2, hb);
                DeleteObject(hb);
            }

            HFONT of = (HFONT)SelectObject(hdc, selected ? g_font_body_b : g_font_body);
            SetTextColor(hdc, COL_TEXT);
            RECT tr = { SC(14), y, w - SC(8), y + rowh };
            DrawTextW(hdc, self->items[i], -1, &tr,
                      DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
            SelectObject(hdc, of);
        }

        HPEN pen = CreatePen(PS_SOLID, 1, COL_BORDER);
        HPEN ofp = (HPEN)SelectObject(hdc, pen);
        HBRUSH nb = (HBRUSH)GetStockObject(NULL_BRUSH);
        HBRUSH ofb = (HBRUSH)SelectObject(hdc, nb);
        Rectangle(hdc, 0, 0, w, h);
        SelectObject(hdc, ofp); SelectObject(hdc, ofb);
        DeleteObject(pen);

        int vis = self->count < ZG_COMBO_MAXVIS ? self->count : ZG_COMBO_MAXVIS;
        if (self->count > vis) {
            int track_y0 = pad, track_y1 = h - pad;
            int thumb_h = (track_y1 - track_y0) * vis / self->count;
            int thumb_y = track_y0 + (track_y1 - track_y0 - thumb_h)
                          * self->popup_scroll / (self->count - vis);
            HBRUSH tb = CreateSolidBrush(COL_GRAY_DIM);
            RECT tbr = { w - SC(5), thumb_y, w - SC(1), thumb_y + thumb_h };
            FillRect(hdc, &tbr, tb);
            DeleteObject(tb);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (!self) break;
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int rowh = SC(ZG_COMBO_ROW);
        int vis = self->count < ZG_COMBO_MAXVIS ? self->count : ZG_COMBO_MAXVIS;
        int idx = -1;
        int r = (pt.y - SC(4)) / rowh;
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
        int vis = self->count < ZG_COMBO_MAXVIS ? self->count : ZG_COMBO_MAXVIS;
        int r = (pt.y - SC(4)) / rowh;
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
