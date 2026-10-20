/*
 * Zapret GUI — portsdlg.cpp
 * Themed modal dialog (own message loop) with two borderless EDIT controls
 * sitting on sunken glass plates. Validation mirrors service.bat
 * :validate_game_filter_range through zg_gamefilter_range_ok().
 */
#include <windows.h>
#include <stdio.h>
#include "common.h"
#include "controls.h"
#include "gfx.h"
#include "portsdlg.h"

#define IDC_PD_TCP    10
#define IDC_PD_UDP    11
#define IDC_PD_RESET  20
#define IDC_PD_CANCEL 21
#define IDC_PD_SAVE   22

typedef struct {
    ZgGameFilter* gf;
    BOOL  done, saved;
    HWND  hTcp, hUdp;
    ZgCanvas bd;
    HBRUSH   editBrush;
    wchar_t  err[200];
} PdState;

static const wchar_t* PD_CLASS = L"ZgPortsDlg";

/* ---- geometry (design units) ---- */
#define PD_W      460
#define PD_H      282
#define PD_PAD    24

static int fld_y(int row) { return SC(114 + row * 66); }
static RECT fld_rect(int row)
{
    RECT r = { SC(PD_PAD), fld_y(row), SC(PD_W - PD_PAD), fld_y(row) + SC(34) };
    return r;
}

static void pd_build_backdrop(PdState* s, HWND hwnd)
{
    RECT rc; GetClientRect(hwnd, &rc);
    zg_backdrop_attach(hwnd, NULL);
    zg_canvas_destroy(&s->bd);
    if (!zg_canvas_create(&s->bd, rc.right, rc.bottom)) return;
    zg_gfx_background(&s->bd);
    ZgGlass fld; zg_glass_style_field(&fld);
    for (int i = 0; i < 2; i++) {
        RECT r = fld_rect(i);
        zg_gfx_glass(&s->bd, r.left, r.top, r.right - r.left, r.bottom - r.top, SC(9), &fld);
    }
    GdiFlush();
    zg_backdrop_attach(hwnd, &s->bd);
}

static void pd_place(PdState* s, HWND hwnd)
{
    int pad = SC(DU_SHADOW);
    for (int i = 0; i < 2; i++) {
        RECT r = fld_rect(i);
        int eh = SC(20);
        SetWindowPos(i ? s->hUdp : s->hTcp, NULL, r.left + SC(12), r.top + ((r.bottom - r.top) - eh) / 2,
                     r.right - r.left - SC(24), eh, SWP_NOZORDER);
    }
    int by = SC(234), bh = SC(34);
    struct { int id; int x, w; } B[3] = {
        { IDC_PD_RESET,  SC(PD_PAD),                 SC(136) },
        { IDC_PD_CANCEL, SC(PD_W - PD_PAD - 120 - 10 - 124), SC(120) },
        { IDC_PD_SAVE,   SC(PD_W - PD_PAD - 124),    SC(124) },
    };
    for (int i = 0; i < 3; i++)
        SetWindowPos(GetDlgItem(hwnd, B[i].id), NULL, B[i].x - pad, by - pad,
                     B[i].w + 2 * pad, bh + 2 * pad, SWP_NOZORDER);
}

static void pd_try_save(PdState* s, HWND hwnd)
{
    wchar_t t[ZG_GF_RANGE_MAX + 40], u[ZG_GF_RANGE_MAX + 40], ot[ZG_GF_RANGE_MAX], ou[ZG_GF_RANGE_MAX];
    GetWindowTextW(s->hTcp, t, ZG_GF_RANGE_MAX + 39);
    GetWindowTextW(s->hUdp, u, ZG_GF_RANGE_MAX + 39);
    if (!zg_gamefilter_range_ok(t, ot, ZG_GF_RANGE_MAX)) {
        _snwprintf(s->err, 199, zg_str(S_PORTS_BAD), zg_str(S_PORTS_TCP)); s->err[199] = 0;
        SetFocus(s->hTcp); SendMessageW(s->hTcp, EM_SETSEL, 0, -1);
        InvalidateRect(hwnd, NULL, FALSE); return;
    }
    if (!zg_gamefilter_range_ok(u, ou, ZG_GF_RANGE_MAX)) {
        _snwprintf(s->err, 199, zg_str(S_PORTS_BAD), zg_str(S_PORTS_UDP)); s->err[199] = 0;
        SetFocus(s->hUdp); SendMessageW(s->hUdp, EM_SETSEL, 0, -1);
        InvalidateRect(hwnd, NULL, FALSE); return;
    }
    wcscpy(s->gf->tcp, ot); wcscpy(s->gf->udp, ou);
    s->saved = TRUE; s->done = TRUE;
}

static LRESULT CALLBACK PdProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    PdState* s = (PdState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_NCCREATE:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW*)lp)->lpCreateParams);
        return DefWindowProcW(hwnd, msg, wp, lp);

    case WM_CREATE: {
        s = (PdState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        s->editBrush = CreateSolidBrush(g_th.bg_dark);
        s->hTcp = CreateWindowExW(0, L"EDIT", s->gf->tcp, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                  0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)IDC_PD_TCP, GetModuleHandleW(NULL), NULL);
        s->hUdp = CreateWindowExW(0, L"EDIT", s->gf->udp, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                  0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)IDC_PD_UDP, GetModuleHandleW(NULL), NULL);
        for (int i = 0; i < 2; i++) {
            HWND e = i ? s->hUdp : s->hTcp;
            SendMessageW(e, WM_SETFONT, (WPARAM)g_font_body, TRUE);
            SendMessageW(e, EM_LIMITTEXT, ZG_GF_RANGE_MAX - 1, 0);
        }
        zg_button_create(hwnd, IDC_PD_RESET,  zg_str(S_PORTS_RESET),  ZGBTN_FLAT, 0);
        zg_button_create(hwnd, IDC_PD_CANCEL, zg_str(S_PORTS_CANCEL), ZGBTN_FLAT, 0);
        zg_button_create(hwnd, IDC_PD_SAVE,   zg_str(S_PORTS_SAVE),   ZGBTN_PRIMARY, ZGBP_GREEN);
        pd_place(s, hwnd);
        return 0;
    }
    case WM_SIZE:
        if (s) { pd_build_backdrop(s, hwnd); pd_place(s, hwnd); }
        return 0;

    case WM_ERASEBKGND: return 1;

    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)wp;
        SetTextColor(dc, COL_TEXT);
        SetBkColor(dc, g_th.bg_dark);
        return (LRESULT)(s ? s->editBrush : GetStockObject(BLACK_BRUSH));
    }

    case WM_PAINT: {
        PAINTSTRUCT ps; HDC wdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        ZgCanvas cv;
        if (s && s->bd.px && zg_canvas_create(&cv, rc.right, rc.bottom)) {
            zg_canvas_copy_rect(&cv, 0, 0, &s->bd, 0, 0, rc.right, rc.bottom);
            HDC dc = cv.dc; SetBkMode(dc, TRANSPARENT);

            RECT tr = { SC(PD_PAD), SC(18), SC(PD_W - PD_PAD), SC(44) };
            HFONT of = (HFONT)SelectObject(dc, g_font_title);
            SetTextColor(dc, COL_TEXT);
            DrawTextW(dc, zg_str(S_PORTS_TITLE), -1, &tr, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
            SelectObject(dc, g_font_body);
            RECT hr = { SC(PD_PAD), SC(50), SC(PD_W - PD_PAD), SC(92) };
            SetTextColor(dc, COL_MUTED);
            DrawTextW(dc, zg_str(S_PORTS_HINT), -1, &hr, DT_WORDBREAK | DT_LEFT);
            SelectObject(dc, g_font_h2);
            SetTextColor(dc, COL_HDR);
            for (int i = 0; i < 2; i++) {
                RECT lr = { SC(PD_PAD), fld_y(i) - SC(22), SC(PD_W - PD_PAD), fld_y(i) - SC(4) };
                DrawTextW(dc, zg_str(i ? S_PORTS_UDP : S_PORTS_TCP), -1, &lr, DT_SINGLELINE | DT_LEFT | DT_VCENTER);
            }
            if (s->err[0]) {
                RECT er = { SC(PD_PAD), SC(206), SC(PD_W - PD_PAD), SC(228) };
                SelectObject(dc, g_font_body);
                SetTextColor(dc, COL_RED);
                DrawTextW(dc, s->err, -1, &er, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
            }
            SelectObject(dc, of);
            GdiFlush();
            BitBlt(wdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
            zg_canvas_destroy(&cv);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ZGBUTTON:
        if (!s) return 0;
        if (wp == IDC_PD_SAVE) pd_try_save(s, hwnd);
        else if (wp == IDC_PD_CANCEL) s->done = TRUE;
        else if (wp == IDC_PD_RESET) {
            SetWindowTextW(s->hTcp, L"1024-65535");
            SetWindowTextW(s->hUdp, L"1024-65535");
            s->err[0] = 0; InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_COMMAND:
        if (s && HIWORD(wp) == EN_CHANGE && s->err[0]) { s->err[0] = 0; InvalidateRect(hwnd, NULL, FALSE); }
        return 0;

    case WM_CLOSE:
        if (s) s->done = TRUE;
        return 0;

    case WM_DESTROY:
        zg_backdrop_attach(hwnd, NULL);
        if (s) { zg_canvas_destroy(&s->bd); if (s->editBrush) DeleteObject(s->editBrush); }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void pd_dark_titlebar(HWND hwnd)
{
    typedef HRESULT (WINAPI *FN)(HWND, DWORD, LPCVOID, DWORD);
    HMODULE m = LoadLibraryW(L"dwmapi.dll");
    if (!m) return;
    FN f = (FN)(void*)GetProcAddress(m, "DwmSetWindowAttribute");
    if (f) { BOOL d = g_th.is_dark; if (FAILED(f(hwnd, 20, &d, sizeof(d)))) f(hwnd, 19, &d, sizeof(d)); }
    FreeLibrary(m);
}

BOOL zg_ports_dialog(HWND owner, ZgGameFilter* gf)
{
    static BOOL reg = FALSE;
    if (!reg) {
        WNDCLASSW wc; ZeroMemory(&wc, sizeof(wc));
        wc.lpfnWndProc = PdProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.lpszClassName = PD_CLASS;
        RegisterClassW(&wc);
        reg = TRUE;
    }

    PdState st; ZeroMemory(&st, sizeof(st));
    st.gf = gf;

    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    RECT r = { 0, 0, SC(PD_W), SC(PD_H) };
    AdjustWindowRect(&r, style, FALSE);
    int w = r.right - r.left, h = r.bottom - r.top;
    RECT orc; GetWindowRect(owner, &orc);
    int x = orc.left + ((orc.right - orc.left) - w) / 2, y = orc.top + ((orc.bottom - orc.top) - h) / 2;

    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, PD_CLASS, zg_str(S_PORTS_TITLE), style,
                                x, y, w, h, owner, NULL, GetModuleHandleW(NULL), &st);
    if (!hwnd) return FALSE;
    pd_dark_titlebar(hwnd);
    pd_build_backdrop(&st, hwnd);
    pd_place(&st, hwnd);

    EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    SetFocus(st.hTcp);
    SendMessageW(st.hTcp, EM_SETSEL, 0, -1);

    MSG m;
    while (!st.done && GetMessageW(&m, NULL, 0, 0) > 0) {
        if (m.message == WM_KEYDOWN && (m.hwnd == st.hTcp || m.hwnd == st.hUdp || m.hwnd == hwnd)) {
            if (m.wParam == VK_RETURN) { pd_try_save(&st, hwnd); continue; }
            if (m.wParam == VK_ESCAPE) { st.done = TRUE; continue; }
            if (m.wParam == VK_TAB) {
                HWND nx = (m.hwnd == st.hTcp) ? st.hUdp : st.hTcp;
                SetFocus(nx); SendMessageW(nx, EM_SETSEL, 0, -1); continue;
            }
        }
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    EnableWindow(owner, TRUE);
    DestroyWindow(hwnd);
    SetForegroundWindow(owner);
    return st.saved;
}
