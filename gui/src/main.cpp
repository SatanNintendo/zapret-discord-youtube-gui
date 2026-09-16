/*
 * Zapret GUI — main.cpp
 * Application entry point, main window, layout and painting.
 *
 * Chrome restyle: glossy 3D buttons, gradient panels, soft shadows
 * (see uidraw.cpp). Tray mode: closing hides to the notification area;
 * the full exit lives in the tray menu. UI language (RU/EN) and theme
 * (dark / light / midnight) switch at runtime.
 */
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <objbase.h>
#include <commctrl.h>
#include <gdiplus.h>
#include "common.h"
#include "controls.h"
#include "zapret.h"
#include "netops.h"
#include "applog.h"
#include "uidraw.h"

using namespace Gdiplus;

/* ================================================================== */
/* globals                                                             */
/* ================================================================== */

int  g_scale_pct = 100;
HFONT g_font_title = NULL, g_font_status = NULL, g_font_body = NULL,
      g_font_body_b = NULL, g_font_small = NULL, g_font_h2 = NULL,
      g_font_btn = NULL, g_font_mono = NULL;

static HWND g_hMain = NULL;
static HWND g_hPrimary = NULL;
static HWND g_hBtnDiag = NULL, g_hBtnHosts = NULL, g_hBtnIpset = NULL,
            g_hBtnTests = NULL, g_hBtnCheckUpd = NULL, g_hBtnLogClear = NULL;
static HWND g_hComboStrat = NULL, g_hComboGame = NULL, g_hComboIpset = NULL;
static HWND g_hComboLang = NULL, g_hComboTheme = NULL;
static HWND g_hTglSvc = NULL, g_hTglUpd = NULL;
static HWND g_hLog = NULL;

static wchar_t** g_strats = NULL;
static int       g_stratCount = 0;
static ZgStatus  g_st;
static BOOL      g_busy = FALSE;      /* worker op in flight */
static BOOL      g_hidden = FALSE;   /* window hidden to tray      */
static BOOL      g_tray_added = FALSE;
static BOOL      g_balloon_shown = FALSE;   /* once per session     */
static UINT      g_msgTaskbarCreated = 0;

/* tray state icons (32bpp ARGB, generated at runtime) */
static HICON g_icoTrayOn = NULL, g_icoTrayOff = NULL;

static const int GAME_MODES[4] = { ZG_GAME_OFF, ZG_GAME_UDP, ZG_GAME_TCP, ZG_GAME_ALL };
static const int IPSET_MODES[3] = { ZG_IPSET_LOADED, ZG_IPSET_NONE, ZG_IPSET_ANY };

/* ================================================================== */
/* fonts                                                               */
/* ================================================================== */

static HFONT make_font(int pt10, const wchar_t* face, int weight)
{
    /*
     * pt10        — font size in points x10 (e.g. 100 = 10.0pt)
     * g_scale_pct — scale in PERCENT (100 = 96 dpi, 150 = 144 dpi).
     *
     * hpx = -(pt10/10) pt * dpi / 72,  dpi = 96 * scale_pct / 100.
     * For 10pt at 100%: -10 * 96 / 72 = -13 px  (char height).
     */
    int dpi = MulDiv(96, g_scale_pct, 100);
    int hpx = -MulDiv(pt10, dpi, 10 * 72);
    return CreateFontW(hpx, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
}

void theme_create_fonts(void)
{
    theme_destroy_fonts();
    g_font_title  = make_font(200, L"Segoe UI Semibold", FW_SEMIBOLD);
    g_font_status = make_font(180, L"Segoe UI", FW_BOLD);
    g_font_body   = make_font(100, L"Segoe UI", FW_NORMAL);
    g_font_body_b = make_font(100, L"Segoe UI Semibold", FW_SEMIBOLD);
    g_font_small  = make_font(85,  L"Segoe UI", FW_NORMAL);
    g_font_h2     = make_font(95,  L"Segoe UI Semibold", FW_SEMIBOLD);
    g_font_btn    = make_font(105, L"Segoe UI Semibold", FW_SEMIBOLD);
    g_font_mono   = make_font(95,  L"Consolas", FW_NORMAL);
}

void theme_destroy_fonts(void)
{
    HFONT* fonts[] = { &g_font_title, &g_font_status, &g_font_body,
                       &g_font_body_b, &g_font_small, &g_font_h2,
                       &g_font_btn, &g_font_mono };
    for (int i = 0; i < 8; i++) {
        if (*fonts[i]) { DeleteObject(*fonts[i]); *fonts[i] = NULL; }
    }
}

/* ================================================================== */
/* settings persistence (%APPDATA%\ZapretGUI\settings.ini)              */
/* ================================================================== */

static void settings_path(wchar_t* out, size_t cap)
{
    wchar_t ad[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, ad)))
        _snwprintf(out, cap, L"%s\\ZapretGUI\\settings.ini", ad);
    else
        _snwprintf(out, cap, L"%s\\gui-settings.ini", g_paths.exe_dir);
    out[cap - 1] = 0;
}

typedef struct {
    wchar_t strategy[80];
    int     language;    /* ZG_LANG_*   */
    int     theme;       /* ZG_THEME_*  */
} ZgGuiSettings;

static void settings_load(ZgGuiSettings* s)
{
    wchar_t p[MAX_PATH]; settings_path(p, MAX_PATH);
    s->strategy[0] = 0;
    s->language = ZG_LANG_RU;
    s->theme = ZG_THEME_DARK;

    wchar_t lang[16], theme[16];
    GetPrivateProfileStringW(L"gui", L"strategy", L"", s->strategy, 80, p);
    GetPrivateProfileStringW(L"gui", L"language", L"", lang, 16, p);
    GetPrivateProfileStringW(L"gui", L"theme", L"", theme, 16, p);

    if (_wcsicmp(lang, L"en") == 0) s->language = ZG_LANG_EN;
    if (_wcsicmp(theme, L"light") == 0)        s->theme = ZG_THEME_LIGHT;
    else if (_wcsicmp(theme, L"midnight") == 0) s->theme = ZG_THEME_MIDNIGHT;
}

static void settings_save(void)
{
    wchar_t p[MAX_PATH]; settings_path(p, MAX_PATH);
    wchar_t dir[MAX_PATH];
    wcscpy(dir, p);
    wchar_t* slash = wcsrchr(dir, L'\\');
    if (slash) { *slash = 0; CreateDirectoryW(dir, NULL); }

    int si = zg_combo_get_sel(g_hComboStrat);
    if (si >= 0 && si < g_stratCount)
        WritePrivateProfileStringW(L"gui", L"strategy", g_strats[si], p);
    WritePrivateProfileStringW(L"gui", L"language",
                                (g_lang == ZG_LANG_EN) ? L"en" : L"ru", p);
    const wchar_t* th = L"dark";
    if (g_theme == ZG_THEME_LIGHT)    th = L"light";
    if (g_theme == ZG_THEME_MIDNIGHT) th = L"midnight";
    WritePrivateProfileStringW(L"gui", L"theme", th, p);
}

/* ================================================================== */
/* "zapret folder missing": folder picker + question dialog             */
/* ================================================================== */

/* {DC1C5A9C-E88A-4DDE-A5A1-60F82A20AEF7} FileOpenDialog */
static const CLSID zg_CLSID_FileOpenDialog =
    { 0xDC1C5A9C, 0xE88A, 0x4DDE, { 0xA5, 0xA1, 0x60, 0xF8, 0x2A, 0x20, 0xAE, 0xF7 } };
/* {D57C7288-D4AD-4768-BE02-9D969532D960} IFileDialog */
static const IID zg_IID_IFileDialog =
    { 0xD57C7288, 0xD4AD, 0x4768, { 0xBE, 0x02, 0x9D, 0x96, 0x95, 0x32, 0xD9, 0x60 } };

/* modern Vista+ folder picker; returns TRUE and fills `out` on success */
static BOOL pick_folder_dialog(HWND owner, wchar_t* out, DWORD cap)
{
    out[0] = 0;
    IFileDialog* fd = NULL;
    HRESULT hr = CoCreateInstance(zg_CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                  zg_IID_IFileDialog, (void**)&fd);
    if (FAILED(hr) || !fd) return FALSE;

    DWORD opts = 0;
    fd->GetOptions(&opts);
    fd->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    fd->SetTitle(zg_str(S_DLG_PICK_TITLE));

    BOOL ok = FALSE;
    hr = fd->Show(owner);
    if (SUCCEEDED(hr)) {
        IShellItem* si = NULL;
        if (SUCCEEDED(fd->GetResult(&si)) && si) {
            PWSTR p = NULL;
            if (SUCCEEDED(si->GetDisplayName(SIGDN_FILESYSPATH, &p)) && p) {
                wcsncpy(out, p, cap - 1);
                out[cap - 1] = 0;
                CoTaskMemFree(p);
                ok = (out[0] != 0);
            }
            si->Release();
        }
    }
    fd->Release();
    return ok;
}

typedef HRESULT (WINAPI *FN_TaskDialogIndirect)(const TASKDIALOGCONFIG*, int*, int*, BOOL*);

/*
 * Asks the user what to do about the missing zapret folder.
 * Returns 1001 = pick folder manually, 1002 = retry autosearch,
 *        1003 = continue without zapret.
 */
static int ask_missing_folder(HWND owner, const wchar_t* exe_dir)
{
    /* TaskDialog (comctl32 v6 via manifest, loaded at runtime) */
    HMODULE cc = LoadLibraryW(L"comctl32.dll");
    if (cc) {
        FN_TaskDialogIndirect pTDI =
            (FN_TaskDialogIndirect)(void*)GetProcAddress(cc, "TaskDialogIndirect");
        if (pTDI) {
            wchar_t content[800];
            _snwprintf(content, 799, zg_str(S_DLG_CONTENT), exe_dir);
            content[799] = 0;

            TASKDIALOG_BUTTON btns[3] = {
                { 1001, (PCWSTR)zg_str(S_DLG_BTN_PICK)   },
                { 1002, (PCWSTR)zg_str(S_DLG_BTN_SEARCH) },
                { 1003, (PCWSTR)zg_str(S_DLG_BTN_CONTINUE) },
            };

            TASKDIALOGCONFIG cfg;
            ZeroMemory(&cfg, sizeof(cfg));
            cfg.cbSize             = sizeof(cfg);
            cfg.hwndParent         = owner;
            cfg.dwFlags            = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
            cfg.pszWindowTitle     = zg_str(S_DLG_TITLE);
            cfg.pszMainIcon        = TD_WARNING_ICON;
            cfg.pszMainInstruction  = zg_str(S_DLG_MAIN);
            cfg.pszContent         = content;
            cfg.pButtons           = btns;
            cfg.cButtons           = 3;
            cfg.nDefaultButton     = 1001;

            int pressed = 0;
            HRESULT hr = pTDI(&cfg, &pressed, NULL, NULL);
            FreeLibrary(cc);
            if (SUCCEEDED(hr))
                return (pressed == 1001 || pressed == 1002) ? pressed : 1003;
        } else {
            FreeLibrary(cc);
        }
    }

    /* fallback for environments without comctl32 v6: plain MessageBox
     * Yes = pick manually, No = retry autosearch, Cancel = continue   */
    wchar_t text[900];
    _snwprintf(text, 899, zg_str(S_DLG_FB_TEXT), exe_dir);
    text[899] = 0;
    int mb = MessageBoxW(owner, text, zg_str(S_DLG_TITLE),
                         MB_ICONWARNING | MB_YESNOCANCEL | MB_DEFBUTTON1);
    if (mb == IDYES) return 1001;
    if (mb == IDNO)  return 1002;
    return 1003;
}

/* ================================================================== */
/* UI log helper (UI thread)                                           */
/* ================================================================== */

static void ui_log(int level, const wchar_t* fmt, ...)
{
    if (!g_hLog) return;
    wchar_t buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(buf, 1023, fmt, ap);
    va_end(ap);
    buf[1023] = 0;
    zg_log_append(g_hLog, level, buf);
}

/* ================================================================== */
/* forward declarations                                                */
/* ================================================================== */

static void set_controls_busy(BOOL busy);
static void refresh_primary_button(void);
static void update_ui_language(void);
static void apply_ui_theme(int id);
static void log_state_summary(void);

/* ================================================================== */
/* worker thread                                                       */
/* ================================================================== */

struct WorkerArg { int op; wchar_t strat[80]; };

static DWORD WINAPI worker_thread(LPVOID param)
{
    WorkerArg* wa = (WorkerArg*)param;
    int op = wa->op;

    switch (op) {
    case OP_VERSION_CHECK: zg_net_version_check(); break;
    case OP_IPSET_UPDATE:  zg_net_ipset_update();  break;
    case OP_HOSTS_CHECK:   zg_net_hosts_check();   break;
    case OP_SVC_INSTALL: {
        wchar_t err[256];
        if (wa->strat[0] && zg_service_install(wa->strat, err, 256))
            zg_log_from_worker(ZLOG_OK, zg_str(S_LOG_SVC_INSTALLED), wa->strat);
        else
            zg_log_from_worker(ZLOG_ERR, zg_str(S_LOG_SVC_INSTALL_ERR), err);
        break;
    }
    case OP_SVC_REMOVE: {
        wchar_t err[256];
        if (zg_service_remove(err, 256))
            zg_log_from_worker(ZLOG_OK, L"%s", zg_str(S_LOG_SVC_REMOVED));
        else
            zg_log_from_worker(ZLOG_ERR, zg_str(S_LOG_SVC_REMOVE_ERR), err);
        break;
    }
    default: break;
    }

    HeapFree(GetProcessHeap(), 0, wa);
    PostMessageW(g_hMain, WM_APP_OPDONE, (WPARAM)op, 0);
    return 0;
}

static void run_op(int op, const wchar_t* strat_for_install)
{
    if (g_busy) return;
    g_busy = TRUE;
    set_controls_busy(TRUE);
    WorkerArg* wa = (WorkerArg*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(WorkerArg));
    if (!wa) { g_busy = FALSE; set_controls_busy(FALSE); return; }
    wa->op = op;
    if (strat_for_install)
        wcsncpy(wa->strat, strat_for_install, 79);
    HANDLE h = CreateThread(NULL, 0, worker_thread, wa, 0, NULL);
    if (h) CloseHandle(h);
    else { HeapFree(GetProcessHeap(), 0, wa); g_busy = FALSE; set_controls_busy(FALSE); }
}

/* ================================================================== */
/* painting                                                            */
/* ================================================================== */

/* lightning bolt polygon (unit coords) */
static const float BOLT_X[6] = { 0.585f, 0.245f, 0.460f, 0.415f, 0.760f, 0.535f };
static const float BOLT_Y[6] = { 0.080f, 0.545f, 0.545f, 0.920f, 0.395f, 0.395f };

static void draw_app_icon(Graphics& g, int x, int y, int size)
{
    /* purple rounded square with white bolt + chrome gloss — matches app.ico */
    Gdiplus::GraphicsPath* p = zg_round_path(x, y, size, size, size / 4);
    Gdiplus::LinearGradientBrush br(
        Gdiplus::Point(x, y), Gdiplus::Point(x, y + size),
        Gdiplus::Color(255, 0x8A, 0x6C, 0xF5), Gdiplus::Color(255, 0x6A, 0x50, 0xD8));
    g.FillPath(&br, p);

    /* specular gloss on the upper half */
    {
        Gdiplus::LinearGradientBrush gl(Gdiplus::Point(x, y),
                                         Gdiplus::Point(x, y + size * 55 / 100),
                                         Gdiplus::Color(105, 255, 255, 255),
                                         Gdiplus::Color(0, 255, 255, 255));
        gl.SetWrapMode(Gdiplus::WrapModeClamp);
        g.FillPath(&gl, p);
    }

    /* chrome rim */
    {
        Gdiplus::GraphicsPath* inset = zg_round_path(x + 1, y + 1, size - 2, size - 2, size / 4 - 1);
        Gdiplus::Pen rim(Gdiplus::Color(120, 255, 255, 255), 1.0f);
        g.DrawPath(&rim, inset);
        delete inset;
    }
    delete p;

    Gdiplus::PointF pts[6];
    for (int i = 0; i < 6; i++) {
        pts[i].X = x + size * 0.18f + size * 0.64f * BOLT_X[i];
        pts[i].Y = y + size * 0.05f + size * 0.90f * BOLT_Y[i];
    }
    Gdiplus::GraphicsPath bolt;
    bolt.AddPolygon(pts, 6);
    Gdiplus::SolidBrush wb(Gdiplus::Color(255, 255, 255, 255));
    g.FillPath(&wb, &bolt);
}

static void draw_section_header(HDC hdc, const wchar_t* text, int x, int y, int w)
{
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, COL_HDR);
    HFONT of = (HFONT)SelectObject(hdc, g_font_h2);
    SetTextCharacterExtra(hdc, SC(2));
    TextOutW(hdc, x, y, text, (int)wcslen(text));
    SetTextCharacterExtra(hdc, 0);
    (void)w;
    SelectObject(hdc, of);
}

static void paint_hero_shapes(Graphics& g)
{
    int cx = SC(DU_CIRCLE_CX), cy = SC(DU_CIRCLE_CY), r = SC(DU_CIRCLE_R);
    BOOL active = g_st.winws_running;

    /* glow rings */
    if (active) {
        Gdiplus::Color alphas[] = {
            Gdiplus::Color(26,  0x32, 0xCD, 0x32),
            Gdiplus::Color(40,  0x32, 0xCD, 0x32),
            Gdiplus::Color(60,  0x32, 0xCD, 0x32),
            Gdiplus::Color(95,  0x32, 0xCD, 0x32),
        };
        int dr[] = { 11, 8, 5, 2 };
        for (int i = 0; i < 4; i++) {
            Gdiplus::SolidBrush gb(alphas[i]);
            g.FillEllipse(&gb, cx - r - SC(dr[i]), cy - r - SC(dr[i]),
                             (r + SC(dr[i])) * 2, (r + SC(dr[i])) * 2);
        }
    }

    /* disc */
    Gdiplus::SolidBrush disc(Gdiplus::Color(zg_argb(255, g_th.hero_disc)));
    g.FillEllipse(&disc, cx - r, cy - r, r * 2, r * 2);

    /* chrome ring: vertical gradient stroke (metallic / glowing green) */
    {
        int pw = SC(3);
        Gdiplus::Rect ringrc(cx - r - pw, cy - r - pw, (r + pw) * 2, (r + pw) * 2);
        Gdiplus::Color c_top, c_bot;
        if (active) {
            c_top = Gdiplus::Color(255, 0x7C, 0xEC, 0x7C);
            c_bot = Gdiplus::Color(255, 0x1F, 0x8F, 0x1F);
        } else {
            c_top = Gdiplus::Color(255, 0xC8, 0xCD, 0xD4);
            c_bot = Gdiplus::Color(255, 0x4A, 0x50, 0x58);
        }
        Gdiplus::LinearGradientBrush rb(ringrc, c_top, c_bot,
                                        Gdiplus::LinearGradientModeVertical);
        Gdiplus::Pen ring(&rb, (Gdiplus::REAL)pw);
        g.DrawEllipse(&ring, cx - r, cy - r, r * 2, r * 2);
    }

    /* bolt */
    Gdiplus::PointF pts[6];
    float m = 0.62f;
    for (int i = 0; i < 6; i++) {
        pts[i].X = cx + r * m * (BOLT_X[i] * 2.0f - 1.0f) * 0.95f;
        pts[i].Y = cy + r * m * (BOLT_Y[i] * 2.0f - 1.0f) * 0.95f;
    }
    Gdiplus::GraphicsPath bolt;
    bolt.AddPolygon(pts, 6);
    Gdiplus::SolidBrush wb(active ? Gdiplus::Color(255, 255, 255, 255)
                                  : Gdiplus::Color(zg_argb(255, g_th.hero_bolt_off)));
    g.FillPath(&wb, &bolt);
}

static void paint_hero_text(HDC hdc)
{
    BOOL active = g_st.winws_running;
    BOOL files_ok = g_paths.files_ok;

    SetBkMode(hdc, TRANSPARENT);

    /* status text */
    RECT tr = { 0, SC(DU_STAT_Y), SC(DU_WIN_W), SC(DU_STAT_Y + DU_STAT_H) };
    HFONT of = (HFONT)SelectObject(hdc, g_font_status);
    SetTextColor(hdc, active ? COL_GREEN : (files_ok ? COL_MUTED : COL_RED));
    DrawTextW(hdc, active ? zg_str(S_ST_ON)
                  : (files_ok ? zg_str(S_ST_OFF) : zg_str(S_ST_NOFILES)),
              -1, &tr, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    SelectObject(hdc, of);

    /* subtext */
    wchar_t sub[200];
    if (!files_ok)
        wcsncpy(sub, zg_str(S_SUB_NOFILES), 199);
    else if (g_st.svc_running && g_st.winws_running)
        _snwprintf(sub, 199, zg_str(S_SUB_SVC),
                   g_st.svc_strategy[0] ? g_st.svc_strategy : L"?");
    else if (g_st.own_child_alive && g_st.winws_running) {
        int idx = zg_combo_get_sel(g_hComboStrat);
        _snwprintf(sub, 199, zg_str(S_SUB_OWN),
                   (idx >= 0 && idx < g_stratCount) ? g_strats[idx] : L"?");
    }
    else if (g_st.winws_running)
        wcsncpy(sub, zg_str(S_SUB_MANUAL), 199);
    else if (g_st.svc_installed)
        wcsncpy(sub, zg_str(S_SUB_SVCINST), 199);
    else
        wcsncpy(sub, zg_str(S_SUB_IDLE), 199);
    sub[199] = 0;

    RECT sr2 = { 0, SC(DU_SUBTXT_Y), SC(DU_WIN_W), SC(DU_SUBTXT_Y + 16) };
    of = (HFONT)SelectObject(hdc, g_font_body);
    SetTextColor(hdc, files_ok ? COL_MUTED : COL_YELLOW);
    DrawTextW(hdc, sub, -1, &sr2, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS);
    SelectObject(hdc, of);
}

static void paint_cards_shapes(Graphics& g)
{
    COLORREF dots[3];
    dots[0] = g_st.winws_running ? COL_GREEN : COL_GRAY_DIM;

    if (g_st.svc_running)         { dots[1] = COL_GREEN; }
    else if (g_st.svc_installed)  { dots[1] = COL_GRAY; }
    else                          { dots[1] = COL_GRAY_DIM; }

    dots[2] = g_st.windivert_running ? COL_GREEN : COL_GRAY_DIM;

    int w = SC(DU_CARD_W), h = SC(DU_CARD_H), y = SC(DU_CARDS_Y);
    for (int i = 0; i < 3; i++) {
        int x = SC(DU_PAD) + i * (w + SC(DU_CARD_GAP));

        /* soft lift + chrome body */
        zg_draw_shadow(g, x, y, w, h, SC(12), SC(3), 45);
        zg_draw_chrome_panel(g, x, y, w, h, SC(12),
                            g_th.card_top, g_th.card_bot, g_th.border_dim, 35);

        /* status dot with glow */
        int dy = y + SC(28);
        {
            Gdiplus::Color dc(zg_argb(255, dots[i]));
            Gdiplus::SolidBrush glow(Gdiplus::Color(60, dc.GetR(), dc.GetG(), dc.GetB()));
            g.FillEllipse(&glow, x + SC(13), dy - SC(3), SC(13), SC(13));
        }
        Gdiplus::SolidBrush dotb(zg_argb(255, dots[i]));
        g.FillEllipse(&dotb, x + SC(16), dy, SC(7), SC(7));
    }
}

static void paint_cards_text(HDC hdc)
{
    struct { const wchar_t* title; const wchar_t* text; BOOL ok; } cards[3];

    cards[0].title = zg_str(S_CARD_WINWS);
    cards[0].text  = g_st.winws_running ? zg_str(S_CARD_WINWS_ON) : zg_str(S_CARD_WINWS_OFF);
    cards[0].ok    = g_st.winws_running;

    cards[1].title = zg_str(S_CARD_SVC);
    if (g_st.svc_running)         { cards[1].text = zg_str(S_CARD_SVC_RUN);  cards[1].ok = TRUE; }
    else if (g_st.svc_installed)  { cards[1].text = zg_str(S_CARD_SVC_INST); cards[1].ok = FALSE; }
    else                          { cards[1].text = zg_str(S_CARD_SVC_NONE); cards[1].ok = FALSE; }

    cards[2].title = zg_str(S_CARD_WD);
    cards[2].text  = g_st.windivert_running ? zg_str(S_CARD_WD_ON) : zg_str(S_CARD_WD_OFF);
    cards[2].ok    = g_st.windivert_running;

    int w = SC(DU_CARD_W), y = SC(DU_CARDS_Y);
    SetBkMode(hdc, TRANSPARENT);
    for (int i = 0; i < 3; i++) {
        int x = SC(DU_PAD) + i * (w + SC(DU_CARD_GAP));

        /* title */
        HFONT of = (HFONT)SelectObject(hdc, g_font_h2);
        SetTextColor(hdc, COL_HDR);
        SetTextCharacterExtra(hdc, SC(1));
        TextOutW(hdc, x + SC(16), y + SC(10), cards[i].title, (int)wcslen(cards[i].title));
        SetTextCharacterExtra(hdc, 0);
        SelectObject(hdc, of);

        /* status text */
        of = (HFONT)SelectObject(hdc, g_font_body_b);
        SetTextColor(hdc, cards[i].ok ? COL_TEXT : COL_MUTED);
        TextOutW(hdc, x + SC(30), y + SC(26), cards[i].text, (int)wcslen(cards[i].text));
        SelectObject(hdc, of);
    }
}

static void paint_settings_panel_shapes(Graphics& g)
{
    int x = SC(DU_PAD), y = SC(DU_SET_Y), w = SC(DU_CONTENT_W), h = SC(DU_SET_H);

    zg_draw_shadow(g, x, y, w, h, SC(12), SC(3), 45);
    zg_draw_chrome_panel(g, x, y, w, h, SC(12),
                         g_th.setp_top, g_th.setp_bot, g_th.border_dim, 25);

    /* row separators (subtle) */
    for (int i = 1; i < DU_SET_ROWS; i++) {
        int sy = y + SC(6) + i * SC(DU_ROW_H);
        Gdiplus::Pen sp(Gdiplus::Color(45, GetRValue(g_th.border), GetGValue(g_th.border),
                                       GetBValue(g_th.border)));
        g.DrawLine(&sp, x + SC(16), sy, x + w - SC(16), sy);
    }
}

static void paint_settings_panel_text(HDC hdc)
{
    int x = SC(DU_PAD), y = SC(DU_SET_Y), w = SC(DU_CONTENT_W);
    int mid = x + w / 2;    /* the right half starts here */

    /*
     * 7 labels in 4 rows: strategy is full-width; the other three
     * rows carry two labels each. A label ends ~10px before its
     * control, ellipsized if the text does not fit.
     */
    struct { int id; int row; int x0, x1; } L[7];
    int n = 0;

    /* row 0 — strategy (full width) */
    L[n].id = S_LBL_STRAT; L[n].row = 0;
    L[n].x0 = x + SC(16);
    L[n].x1 = x + w - SC(16) - SC(DU_COMBO_W) - SC(10);
    n++;

    /* row 1 — game filter (left) + ipset (right) */
    L[n].id = S_LBL_GAME; L[n].row = 1;
    L[n].x0 = x + SC(16);
    L[n].x1 = mid - SC(16) - SC(DU_COMBO_W2) - SC(10);
    n++;
    L[n].id = S_LBL_IPSET; L[n].row = 1;
    L[n].x0 = mid + SC(16);
    L[n].x1 = x + w - SC(16) - SC(DU_COMBO_W2B) - SC(10);
    n++;

    /* row 2 — service autostart (left) + update checks (right) */
    L[n].id = S_LBL_SVC; L[n].row = 2;
    L[n].x0 = x + SC(16);
    L[n].x1 = mid - SC(16) - SC(46) - SC(10);
    n++;
    L[n].id = S_LBL_UPD; L[n].row = 2;
    L[n].x0 = mid + SC(16);
    L[n].x1 = x + w - SC(16) - SC(46) - SC(10);
    n++;

    /* row 3 — language (left) + theme (right) */
    L[n].id = S_LBL_LANG; L[n].row = 3;
    L[n].x0 = x + SC(16);
    L[n].x1 = mid - SC(16) - SC(DU_COMBO_W3) - SC(10);
    n++;
    L[n].id = S_LBL_THEME; L[n].row = 3;
    L[n].x0 = mid + SC(16);
    L[n].x1 = x + w - SC(16) - SC(DU_COMBO_W3B) - SC(10);
    n++;

    SetBkMode(hdc, TRANSPARENT);
    HFONT of = (HFONT)SelectObject(hdc, g_font_body);
    SetTextColor(hdc, COL_TEXT);
    for (int i = 0; i < n; i++) {
        int ry = y + SC(6) + L[i].row * SC(DU_ROW_H);
        RECT tr = { L[i].x0, ry, L[i].x1, ry + SC(DU_ROW_H) };
        DrawTextW(hdc, zg_str(L[i].id), -1, &tr,
                  DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
    }
    SelectObject(hdc, of);
}

static void paint_header_shapes(Graphics& g)
{
    /* icon */
    draw_app_icon(g, SC(DU_ICON_X), SC(DU_ICON_Y), SC(DU_ICON_SIZE));
}

static void paint_header_text(HDC hdc)
{
    SetBkMode(hdc, TRANSPARENT);

    /* title + subtitle */
    HFONT of = (HFONT)SelectObject(hdc, g_font_title);
    SetTextColor(hdc, COL_TEXT);
    TextOutW(hdc, SC(DU_TITLE_X), SC(DU_TITLE_Y), L"Zapret GUI", 10);
    SelectObject(hdc, of);

    of = (HFONT)SelectObject(hdc, g_font_body);
    SetTextColor(hdc, COL_MUTED);
    {
        const wchar_t* s = zg_str(S_SUBTITLE);
        TextOutW(hdc, SC(DU_TITLE_X), SC(DU_SUB_Y), s, (int)wcslen(s));
    }
    SelectObject(hdc, of);

    /* version (top-right) */
    wchar_t ver[64];
    _snwprintf(ver, 63, L"zapret %s  \u00b7  GUI %s",
               g_st.local_version[0] ? g_st.local_version : L"?",
               ZG_GUI_VERSION);
    ver[63] = 0;

    of = (HFONT)SelectObject(hdc, g_font_small);
    SetTextColor(hdc, g_th.ver);
    SIZE sz; GetTextExtentPoint32W(hdc, ver, (int)wcslen(ver), &sz);
    TextOutW(hdc, SC(DU_WIN_W - DU_PAD) - sz.cx, SC(DU_VER_Y), ver, (int)wcslen(ver));
    SelectObject(hdc, of);

    /* update-check link is a child control (ZGBTN_LINK) */
}

static void paint_log_frame(Graphics& g)
{
    /* chrome plate behind the log area (the RichEdit sits on top) */
    int x = SC(DU_PAD) - 1, y = SC(DU_LOG_Y) - 1;
    int w = SC(DU_CONTENT_W) + 2, h = SC(DU_LOG_H) + 2;
    zg_draw_shadow(g, x, y, w, h, SC(8), SC(2), 35);
    zg_draw_chrome_panel(g, x, y, w, h, SC(8),
                         g_th.bg_dark, g_th.bg_dark, g_th.border_dim, 0);
}

/* ================================================================== */
/* layout                                                              */
/* ================================================================== */

/* ---- local background replicas for the child controls -------------- */

/*
 * Mirrors zg_draw_chrome_panel(): a vertical gradient plus a white
 * gloss fading out over the upper 55%. Integer math stays within
 * ~1/255 per channel of what GDI+ renders — invisible under the 4px
 * shadow fringe of a control.
 */
static COLORREF panel_color_at(int y, int gloss_alpha)
{
    int py = SC(DU_SET_Y), ph = SC(DU_SET_H);
    int t = y - py;
    if (t < 0) t = 0;
    if (t > ph) t = ph;

    int r = GetRValue(g_th.setp_top) + (GetRValue(g_th.setp_bot) - GetRValue(g_th.setp_top)) * t / ph;
    int g = GetGValue(g_th.setp_top) + (GetGValue(g_th.setp_bot) - GetGValue(g_th.setp_top)) * t / ph;
    int b = GetBValue(g_th.setp_top) + (GetBValue(g_th.setp_bot) - GetBValue(g_th.setp_top)) * t / ph;

    int gh = ph * 55 / 100;
    if (t < gh && gloss_alpha > 0) {
        int a = gloss_alpha * (gh - t) / gh;   /* 0..gloss_alpha */
        r += (255 - r) * a / 255;
        g += (255 - g) * a / 255;
        b += (255 - b) * a / 255;
    }
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return RGB(r, g, b);
}

/* the settings-panel gradient colors under a control's top/bottom edge */
static void set_panel_bg(HWND h)
{
    RECT r;
    GetWindowRect(h, &r);
    MapWindowPoints(HWND_DESKTOP, g_hMain, (POINT*)&r, 2);
    zg_ctrl_set_bg(h, panel_color_at(r.top, 25), panel_color_at(r.bottom, 25));
}

/* push a local background replica into every child control */
static void update_ctrl_bgs(void)
{
    if (!g_hMain) return;

    /* controls sitting on the plain window background */
    HWND plain[] = { g_hPrimary, g_hBtnDiag, g_hBtnHosts, g_hBtnIpset,
                     g_hBtnTests, g_hBtnCheckUpd, g_hBtnLogClear };
    for (int i = 0; i < (int)(sizeof(plain) / sizeof(plain[0])); i++)
        if (plain[i]) zg_ctrl_set_bg(plain[i], g_th.bg, g_th.bg);

    /* combos / toggles sitting on the chrome settings panel */
    HWND panel[] = { g_hComboStrat, g_hComboGame, g_hComboIpset,
                     g_hComboLang, g_hComboTheme, g_hTglSvc, g_hTglUpd };
    for (int i = 0; i < (int)(sizeof(panel) / sizeof(panel[0])); i++)
        if (panel[i]) set_panel_bg(panel[i]);
}

static void apply_layout(void)
{
    if (!g_hMain) return;

    int pad = SC(DU_SHADOW);   /* chrome shadow fringe around controls */

    /* footer buttons */
    int fw = (SC(DU_CONTENT_W) - 3 * SC(DU_FOOT_GAP)) / 4;
    int fy = SC(DU_FOOT_Y), fh = SC(DU_FOOT_H);
    HWND foot[4] = { g_hBtnDiag, g_hBtnHosts, g_hBtnIpset, g_hBtnTests };
    for (int i = 0; i < 4; i++) {
        int fx = SC(DU_PAD) + i * (fw + SC(DU_FOOT_GAP));
        SetWindowPos(foot[i], NULL, fx - pad, fy - pad,
                     fw + 2 * pad, fh + 2 * pad, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    /* primary button */
    SetWindowPos(g_hPrimary, NULL,
                 SC(DU_PRIMARY_X) - pad, SC(DU_PRIMARY_Y) - pad,
                 SC(DU_PRIMARY_W) + 2 * pad, SC(DU_PRIMARY_H) + 2 * pad,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    /*
     * Settings rows (4). Row 0 = strategy, full width. Rows 1..3 carry
     * two controls each: the left one is right-aligned to the mid-gap,
     * the right one to the panel edge — same geometry the label
     * painting in paint_settings_panel_text() uses.
     */
    int px = SC(DU_PAD), pw = SC(DU_CONTENT_W);
    int mid = px + pw / 2;
#define ROW_Y(i)   (SC(DU_SET_Y + 6) + (i) * SC(DU_ROW_H))
#define ROW_CY(i)  (ROW_Y(i) + (SC(DU_ROW_H) - SC(DU_CTRL_H)) / 2)
#define ROW_TY(i)  (ROW_Y(i) + (SC(DU_ROW_H) - SC(24)) / 2)

    /* row 0: strategy combo (full width) */
    SetWindowPos(g_hComboStrat, NULL,
                 px + pw - SC(16) - SC(DU_COMBO_W) - pad, ROW_CY(0) - pad,
                 SC(DU_COMBO_W) + 2 * pad, SC(DU_CTRL_H) + 2 * pad,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    /* row 1: game filter (left) + ipset (right) */
    SetWindowPos(g_hComboGame, NULL,
                 mid - SC(16) - SC(DU_COMBO_W2) - pad, ROW_CY(1) - pad,
                 SC(DU_COMBO_W2) + 2 * pad, SC(DU_CTRL_H) + 2 * pad,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(g_hComboIpset, NULL,
                 px + pw - SC(16) - SC(DU_COMBO_W2B) - pad, ROW_CY(1) - pad,
                 SC(DU_COMBO_W2B) + 2 * pad, SC(DU_CTRL_H) + 2 * pad,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    /* row 2: service autostart (left) + update checks (right) */
    SetWindowPos(g_hTglSvc, NULL,
                 mid - SC(16) - SC(46), ROW_TY(2),
                 SC(46), SC(24), SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(g_hTglUpd, NULL,
                 px + pw - SC(16) - SC(46), ROW_TY(2),
                 SC(46), SC(24), SWP_NOZORDER | SWP_NOACTIVATE);

    /* row 3: language (left) + theme (right) */
    SetWindowPos(g_hComboLang, NULL,
                 mid - SC(16) - SC(DU_COMBO_W3) - pad, ROW_CY(3) - pad,
                 SC(DU_COMBO_W3) + 2 * pad, SC(DU_CTRL_H) + 2 * pad,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(g_hComboTheme, NULL,
                 px + pw - SC(16) - SC(DU_COMBO_W3B) - pad, ROW_CY(3) - pad,
                 SC(DU_COMBO_W3B) + 2 * pad, SC(DU_CTRL_H) + 2 * pad,
                 SWP_NOZORDER | SWP_NOACTIVATE);
#undef ROW_Y
#undef ROW_CY
#undef ROW_TY

    /* header link */
    SetWindowPos(g_hBtnCheckUpd, NULL,
                 SC(DU_WIN_W - DU_PAD) - SC(160), SC(DU_UPDLINK_Y),
                 SC(160), SC(20), SWP_NOZORDER | SWP_NOACTIVATE);

    /* log */
    SetWindowPos(g_hLog, NULL, SC(DU_PAD), SC(DU_LOG_Y),
                 SC(DU_CONTENT_W), SC(DU_LOG_H), SWP_NOZORDER | SWP_NOACTIVATE);

    /* "Clear" link above log, right-aligned */
    SetWindowPos(g_hBtnLogClear, NULL,
                 SC(DU_WIN_W - DU_PAD) - SC(80), SC(DU_LOG_HDR_Y) - SC(3),
                 SC(80), SC(20), SWP_NOZORDER | SWP_NOACTIVATE);

    update_ctrl_bgs();

    InvalidateRect(g_hMain, NULL, TRUE);
}

/* ================================================================== */
/* state refresh / reconciliation                                      */
/* ================================================================== */

static void set_controls_busy(BOOL busy)
{
    /* primary stays clickable even without zapret files — it opens
     * the folder picker in that state                          */
    zg_button_set_disabled(g_hPrimary, busy);
    zg_button_set_disabled(g_hBtnDiag, busy);
    zg_button_set_disabled(g_hBtnHosts, busy);
    zg_button_set_disabled(g_hBtnIpset, busy);
    zg_button_set_disabled(g_hBtnTests, busy);
    zg_button_set_disabled(g_hBtnCheckUpd, busy);
    zg_toggle_set_disabled(g_hTglSvc, busy);
    zg_toggle_set_disabled(g_hTglUpd, busy);
}

static void invalidate_all(void)
{
    HWND ctrls[] = { g_hPrimary, g_hBtnDiag, g_hBtnHosts, g_hBtnIpset,
                     g_hBtnTests, g_hBtnCheckUpd, g_hBtnLogClear,
                     g_hComboStrat, g_hComboGame, g_hComboIpset,
                     g_hComboLang, g_hComboTheme, g_hTglSvc, g_hTglUpd };
    for (int i = 0; i < (int)(sizeof(ctrls) / sizeof(ctrls[0])); i++)
        if (ctrls[i]) InvalidateRect(ctrls[i], NULL, TRUE);
    if (g_hMain) InvalidateRect(g_hMain, NULL, TRUE);
}

static void refresh_primary_button(void)
{
    if (!g_paths.files_ok) {
        zg_button_set_text(g_hPrimary, zg_str(S_BTN_PICK_DIR));
        zg_button_set_scheme(g_hPrimary, ZGBP_PURPLE);
    } else if (g_st.winws_running) {
        zg_button_set_text(g_hPrimary, zg_str(S_BTN_TURN_OFF));
        zg_button_set_scheme(g_hPrimary, ZGBP_PURPLE);
    } else {
        zg_button_set_text(g_hPrimary, zg_str(S_BTN_TURN_ON));
        zg_button_set_scheme(g_hPrimary, ZGBP_GREEN);
    }
    zg_button_set_disabled(g_hPrimary, g_busy);
}

static void refresh_status(BOOL full)
{
    zg_status_refresh(&g_st);

    /* primary button & hero & cards are repainted on WM_PAINT */
    refresh_primary_button();

    if (full) {
        /* reconcile toggles & combos with on-disk / SCM truth */
        zg_toggle_set(g_hTglSvc, g_st.svc_installed, FALSE);
        zg_toggle_set(g_hTglUpd, zg_checkupdates_enabled(), FALSE);

        int gi = 0;
        int gm = zg_gamefilter_get();
        for (int i = 0; i < 4; i++) if (GAME_MODES[i] == gm) gi = i;
        if (zg_combo_get_sel(g_hComboGame) != gi)
            zg_combo_set_sel(g_hComboGame, gi, FALSE);

        int ii = 0;
        int im = zg_ipset_get();
        for (int i = 0; i < 3; i++) if (IPSET_MODES[i] == im) ii = i;
        if (zg_combo_get_sel(g_hComboIpset) != ii)
            zg_combo_set_sel(g_hComboIpset, ii, FALSE);
    }

    InvalidateRect(g_hMain, NULL, FALSE);
}

/* ================================================================== */
/* system tray                                                          */
/* ================================================================== */

static void enable_dark_titlebar(HWND hwnd, BOOL dark);   /* defined below */

/* draws the 32bpp ARGB tray state icon (active / inactive) at runtime */
static HICON make_state_icon(BOOL active)
{
    int size = GetSystemMetrics(SM_CXSMICON);
    if (size < 16) size = 16;
    if (size > 48) size = 48;

    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = size;
    bi.bmiHeader.biHeight      = -(LONG)size;   /* top-down DIB */
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = NULL;
    HBITMAP hbColor = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!hbColor) return NULL;
    HBITMAP hbMask = CreateBitmap(size, size, 1, 1, NULL);
    if (!hbMask) { DeleteObject(hbColor); return NULL; }

    HICON icon = NULL;
    {
        HDC mdc = CreateCompatibleDC(NULL);
        if (mdc) {
            HGDIOBJ old = SelectObject(mdc, hbColor);
            {
                Gdiplus::Graphics g(mdc);
                if (g.GetLastStatus() == Gdiplus::Ok) {
                    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

                    int rad = size * 3 / 10;
                    Gdiplus::GraphicsPath* p = zg_round_path(0, 0, size - 1, size - 1, rad);

                    /* dark plate (visible on any taskbar) */
                    Gdiplus::LinearGradientBrush br(
                        Gdiplus::Point(0, 0), Gdiplus::Point(0, size),
                        Gdiplus::Color(255, 0x33, 0x38, 0x40),
                        Gdiplus::Color(255, 0x14, 0x17, 0x1C));
                    g.FillPath(&br, p);

                    /* gloss */
                    Gdiplus::LinearGradientBrush gl(
                        Gdiplus::Point(0, 0), Gdiplus::Point(0, size * 55 / 100),
                        Gdiplus::Color(85, 255, 255, 255),
                        Gdiplus::Color(0, 255, 255, 255));
                    gl.SetWrapMode(Gdiplus::WrapModeClamp);
                    g.FillPath(&gl, p);

                    Gdiplus::Pen rim(Gdiplus::Color(210, 0x6A, 0x72, 0x80), 1.0f);
                    g.DrawPath(&rim, p);
                    delete p;

                    /* state ring */
                    int cx = size / 2, cy = size / 2, rr = size * 28 / 100;
                    Gdiplus::Pen ring(active ? Gdiplus::Color(255, 0x32, 0xCD, 0x32)
                                             : Gdiplus::Color(255, 0x9A, 0xA2, 0xAC),
                                      (Gdiplus::REAL)(size / 10.0f));
                    g.DrawEllipse(&ring, cx - rr, cy - rr, rr * 2, rr * 2);

                    /* bolt */
                    Gdiplus::PointF pts[6];
                    float m = (float)rr * 0.95f;
                    for (int i = 0; i < 6; i++) {
                        pts[i].X = cx + m * (BOLT_X[i] * 2.0f - 1.0f);
                        pts[i].Y = cy + m * (BOLT_Y[i] * 2.0f - 1.0f);
                    }
                    Gdiplus::GraphicsPath bolt;
                    bolt.AddPolygon(pts, 6);
                    Gdiplus::SolidBrush wb(Gdiplus::Color(255, 255, 255, 255));
                    g.FillPath(&wb, &bolt);
                }
            }
            SelectObject(mdc, old);
            DeleteDC(mdc);

            ICONINFO ii;
            ZeroMemory(&ii, sizeof(ii));
            ii.fIcon    = TRUE;
            ii.hbmColor = hbColor;
            ii.hbmMask  = hbMask;
            icon = CreateIconIndirect(&ii);
        }
    }
    DeleteObject(hbMask);
    DeleteObject(hbColor);
    return icon;
}

static const wchar_t* tray_tip_text(void)
{
    if (!g_paths.files_ok)      return zg_str(S_TRAY_TIP_NOFILES);
    if (g_st.winws_running)     return zg_str(S_TRAY_TIP_ON);
    return zg_str(S_TRAY_TIP_OFF);
}

/* tooltip cache — skip NIM_MODIFY when nothing actually changed, so the
 * 2-second status timer never touches an unchanged tray icon */
static wchar_t g_tray_tip_cache[128];
static int     g_tray_on_cache = -1;   /* -1 = nothing cached yet */

static void tray_tip_cache_store(const wchar_t* tip)
{
    wcsncpy(g_tray_tip_cache, tip, 127);
    g_tray_tip_cache[127] = 0;
    g_tray_on_cache = g_st.winws_running ? 1 : 0;
}

static void tray_add(BOOL show_balloon)
{
    NOTIFYICONDATAW nid;
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize           = sizeof(nid);      /* full struct: OK on Vista+/Win7 */
    nid.hWnd             = g_hMain;
    nid.uID              = 1;
    nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_APP_TRAY;
    nid.hIcon            = g_st.winws_running ? g_icoTrayOn : g_icoTrayOff;
    wcsncpy(nid.szTip, tray_tip_text(), 127);
    nid.szTip[127] = 0;

    g_tray_added = Shell_NotifyIconW(NIM_ADD, &nid) ? TRUE : FALSE;

    if (g_tray_added)
        tray_tip_cache_store(tray_tip_text());

    /* one-time balloon so the user knows where the app went */
    if (g_tray_added && show_balloon && !g_balloon_shown) {
        NOTIFYICONDATAW nb;
        ZeroMemory(&nb, sizeof(nb));
        nb.cbSize   = sizeof(nb);
        nb.hWnd     = g_hMain;
        nb.uID      = 1;
        nb.uFlags   = NIF_INFO;
        nb.dwInfoFlags = NIIF_INFO;
        wcsncpy(nb.szInfoTitle, zg_str(S_TRAY_BALLOON_TITLE), 63);
        nb.szInfoTitle[63] = 0;
        wcsncpy(nb.szInfo, zg_str(S_TRAY_BALLOON), 255);
        nb.szInfo[255] = 0;
        Shell_NotifyIconW(NIM_MODIFY, &nb);
        g_balloon_shown = TRUE;
    }
}

static void tray_remove(void)
{
    if (!g_tray_added) return;
    NOTIFYICONDATAW nid;
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd   = g_hMain;
    nid.uID    = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    g_tray_added = FALSE;
    g_tray_on_cache = -1;   /* force a fresh tooltip on the next add */
}

/* refresh icon + tooltip after a status change */
static void tray_update_tooltip(void)
{
    if (!g_tray_added) return;

    const wchar_t* tip = tray_tip_text();
    int on = g_st.winws_running ? 1 : 0;
    if (g_tray_on_cache == on && wcscmp(g_tray_tip_cache, tip) == 0)
        return;   /* unchanged — don't touch the tray icon */

    NOTIFYICONDATAW nid;
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd   = g_hMain;
    nid.uID    = 1;
    nid.uFlags = NIF_ICON | NIF_TIP;
    nid.hIcon  = g_st.winws_running ? g_icoTrayOn : g_icoTrayOff;
    wcsncpy(nid.szTip, tip, 127);
    nid.szTip[127] = 0;
    Shell_NotifyIconW(NIM_MODIFY, &nid);

    tray_tip_cache_store(tip);
}

static void show_main_window(void)
{
    ShowWindow(g_hMain, IsIconic(g_hMain) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(g_hMain);
    g_hidden = FALSE;
    refresh_status(TRUE);
}

static void hide_to_tray(HWND hwnd)
{
    tray_add(TRUE);
    ShowWindow(hwnd, SW_HIDE);
    g_hidden = TRUE;
    ui_log(ZLOG_INFO, L"%s", zg_str(S_LOG_HIDDEN));
}

static void tray_show_menu(HWND hwnd)
{
    HMENU m = CreatePopupMenu();
    if (!m) return;
    AppendMenuW(m, MF_STRING, ZG_TRAY_CMD_OPEN, zg_str(S_TRAY_OPEN));
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, ZG_TRAY_CMD_EXIT, zg_str(S_TRAY_EXIT));

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);   /* classic TrackPopupMenu quirk */
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
                             pt.x, pt.y, 0, hwnd, NULL);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(m);

    if (cmd == ZG_TRAY_CMD_OPEN) {
        show_main_window();
    } else if (cmd == ZG_TRAY_CMD_EXIT) {
        tray_remove();
        DestroyWindow(hwnd);
    }
}

/* ================================================================== */
/* switching the zapret base folder at runtime                          */
/* ================================================================== */

/* destroy + recreate the strategy combo (items are fixed at creation) */
static void rebuild_strategy_combo(int def_idx)
{
    if (g_hComboStrat) { DestroyWindow(g_hComboStrat); g_hComboStrat = NULL; }
    g_hComboStrat = zg_combo_create(g_hMain, IDC_COMBO_STRAT,
                                    (const wchar_t* const*)g_strats, g_stratCount,
                                    g_stratCount > 0 ? def_idx : -1);
    apply_layout();
}

/* rebase every path to `dir`, persist it, rescan strategies & refresh UI */
static BOOL apply_zapret_dir(HWND hwnd, const wchar_t* dir)
{
    if (!zg_set_base_dir(dir)) return FALSE;
    zg_save_dir_config(dir);
    ui_log(ZLOG_OK, zg_str(S_LOG_DIR_SET), g_paths.exe_dir);

    if (g_strats) { zg_free_strategies(g_strats, g_stratCount); g_strats = NULL; }
    g_stratCount = zg_find_strategies(&g_strats);

    wchar_t saved[MAX_PATH];
    ZgGuiSettings st;
    settings_load(&st);
    int def_idx = 0;
    if (st.strategy[0]) {
        wcsncpy(saved, st.strategy, MAX_PATH - 1);
        saved[MAX_PATH - 1] = 0;
        for (int i = 0; i < g_stratCount; i++)
            if (_wcsicmp(g_strats[i], saved) == 0) { def_idx = i; break; }
    }
    rebuild_strategy_combo(def_idx);

    zg_ensure_user_lists();
    zg_status_refresh(&g_st);
    refresh_status(TRUE);

    ui_log(ZLOG_INFO, zg_str(S_LOG_VER_STRATS), g_st.local_version, g_stratCount);
    if (g_stratCount > 0)
        ui_log(ZLOG_INFO, zg_str(S_LOG_CUR_STRAT),
               g_strats[zg_combo_get_sel(g_hComboStrat)]);
    (void)hwnd;
    return TRUE;
}

/* dialog chain shown when bin\winws.exe is nowhere to be found */
static void fix_zapret_folder_flow(HWND hwnd)
{
    int rc = ask_missing_folder(hwnd, g_paths.real_exe_dir);

    if (rc == 1001) {
        wchar_t dir[MAX_PATH];
        if (pick_folder_dialog(hwnd, dir, MAX_PATH)) {
            if (zg_dir_has_winws(dir)) {
                apply_zapret_dir(hwnd, dir);
            } else {
                ui_log(ZLOG_ERR, zg_str(S_LOG_DIR_BAD), dir);
                MessageBoxW(hwnd, zg_str(S_MB_BAD_DIR),
                            ZG_APP_NAME, MB_OK | MB_ICONWARNING);
            }
        }
    } else if (rc == 1002) {
        wchar_t dir[MAX_PATH];
        ui_log(ZLOG_INFO, L"%s", zg_str(S_LOG_RETRY_SEARCH));
        if (zg_autosearch_dir(dir, MAX_PATH)) {
            apply_zapret_dir(hwnd, dir);
        } else {
            ui_log(ZLOG_ERR, zg_str(S_LOG_NOT_FOUND), zg_search_report());
            MessageBoxW(hwnd, zg_str(S_MB_NOT_FOUND),
                        ZG_APP_NAME, MB_OK | MB_ICONWARNING);
        }
    }
    /* rc == 1003 or closed: keep running without zapret files */
}

/* ================================================================== */
/* runtime language / theme switching                                   */
/* ================================================================== */

static const wchar_t* theme_display_name(int id)
{
    switch (id) {
    case ZG_THEME_LIGHT:    return zg_str(S_TH_LIGHT);
    case ZG_THEME_MIDNIGHT: return zg_str(S_TH_MIDNIGHT);
    default:                return zg_str(S_TH_DARK);
    }
}

static void log_state_summary(void)
{
    ui_log(ZLOG_OK, zg_str(S_LOG_THEME_SET), theme_display_name(g_theme));
    if (g_paths.files_ok) {
        ui_log(ZLOG_INFO, zg_str(S_LOG_DIR_SET), g_paths.exe_dir);
        ui_log(ZLOG_INFO, zg_str(S_LOG_VER_STRATS), g_st.local_version, g_stratCount);
        if (g_stratCount > 0)
            ui_log(ZLOG_INFO, zg_str(S_LOG_CUR_STRAT),
                   g_strats[zg_combo_get_sel(g_hComboStrat)]);
    } else {
        ui_log(ZLOG_ERR, L"%s", zg_str(S_LOG_NOFILES_ERR));
    }
}

static void update_ui_language(void)
{
    SetWindowTextW(g_hMain, zg_str(S_WINDOW_TITLE));

    zg_button_set_text(g_hBtnCheckUpd, zg_str(S_CHECK_UPDATES));
    zg_button_set_text(g_hBtnLogClear, zg_str(S_CLEAR));
    zg_button_set_text(g_hBtnDiag,  zg_str(S_BTN_DIAG));
    zg_button_set_text(g_hBtnHosts, zg_str(S_BTN_HOSTS));
    zg_button_set_text(g_hBtnIpset, zg_str(S_BTN_IPSET));
    zg_button_set_text(g_hBtnTests, zg_str(S_BTN_TESTS));
    refresh_primary_button();

    /* re-label the dynamic combos */
    int gs = zg_combo_get_sel(g_hComboGame);
    zg_combo_set_items(g_hComboGame, zg_game_labels(), 4, gs);

    int is = zg_combo_get_sel(g_hComboIpset);
    zg_combo_set_items(g_hComboIpset, zg_ipset_labels(), 3, is);

    const wchar_t* thn[3] = { zg_str(S_TH_DARK), zg_str(S_TH_LIGHT), zg_str(S_TH_MIDNIGHT) };
    int ts = zg_combo_get_sel(g_hComboTheme);
    zg_combo_set_items(g_hComboTheme, thn, 3, ts);

    tray_update_tooltip();
    settings_save();
    invalidate_all();
    ui_log(ZLOG_OK, L"%s",
           zg_str(g_lang == ZG_LANG_EN ? S_LOG_LANG_EN : S_LOG_LANG_RU));
}

static void apply_ui_theme(int id)
{
    zg_theme_apply(id);
    if (g_hMain) enable_dark_titlebar(g_hMain, g_th.is_dark);

    /* old colored log lines are unreadable on the new background — reset */
    zg_log_clear(g_hLog);
    zg_log_apply_theme(g_hLog);

    /* control fringe replicas depend on the palette */
    update_ctrl_bgs();

    settings_save();
    invalidate_all();
    log_state_summary();
}

/* ================================================================== */
/* window procedure                                                     */
/* ================================================================== */

/* true when the two status snapshots are visually identical — the 2s
 * timer must not touch the screen (or the tray icon) in that case */
static BOOL status_same(const ZgStatus* a, const ZgStatus* b)
{
    return a->winws_running     == b->winws_running
        && a->own_child_alive   == b->own_child_alive
        && a->svc_installed     == b->svc_installed
        && a->svc_running       == b->svc_running
        && a->windivert_running == b->windivert_running
        && wcscmp(a->svc_strategy, b->svc_strategy) == 0
        && wcscmp(a->local_version, b->local_version) == 0;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    /* TaskbarCreated: explorer restarted — re-add the tray icon */
    if (g_msgTaskbarCreated && msg == g_msgTaskbarCreated) {
        if (g_hidden) { g_tray_added = FALSE; tray_add(FALSE); }
        return 0;
    }

    switch (msg) {

    case WM_CREATE: {
        g_hMain = hwnd;

        /* header link */
        g_hBtnCheckUpd = zg_button_create(hwnd, IDC_BTN_CHECKUPD,
                                          zg_str(S_CHECK_UPDATES), ZGBTN_LINK, 0);
        /* log clear link */
        g_hBtnLogClear = zg_button_create(hwnd, IDC_BTN_LOGCLEAR,
                                          zg_str(S_CLEAR), ZGBTN_LINK, 0);
        /* primary */
        g_hPrimary = zg_button_create(hwnd, IDC_BTN_PRIMARY,
                                      zg_str(S_BTN_TURN_ON), ZGBTN_PRIMARY, ZGBP_GREEN);
        /* footer */
        g_hBtnDiag  = zg_button_create(hwnd, IDC_BTN_DIAG,  zg_str(S_BTN_DIAG),  ZGBTN_FLAT, 0);
        g_hBtnHosts = zg_button_create(hwnd, IDC_BTN_HOSTS, zg_str(S_BTN_HOSTS), ZGBTN_FLAT, 0);
        g_hBtnIpset = zg_button_create(hwnd, IDC_BTN_IPSETUPD, zg_str(S_BTN_IPSET), ZGBTN_FLAT, 0);
        g_hBtnTests = zg_button_create(hwnd, IDC_BTN_TESTS, zg_str(S_BTN_TESTS), ZGBTN_FLAT, 0);

        /* combos */
        g_hComboStrat = zg_combo_create(hwnd, IDC_COMBO_STRAT,
                                        (const wchar_t* const*)g_strats, g_stratCount,
                                        g_stratCount > 0 ? 0 : -1);
        g_hComboGame = zg_combo_create(hwnd, IDC_COMBO_GAME,
                                        zg_game_labels(), 4, 0);
        g_hComboIpset = zg_combo_create(hwnd, IDC_COMBO_IPSET,
                                        zg_ipset_labels(), 3, 0);
        g_hComboLang = zg_combo_create(hwnd, IDC_COMBO_LANG,
                                        zg_lang_names(), zg_lang_name_count(), g_lang);
        {
            const wchar_t* thn[3] = { zg_str(S_TH_DARK), zg_str(S_TH_LIGHT),
                                      zg_str(S_TH_MIDNIGHT) };
            g_hComboTheme = zg_combo_create(hwnd, IDC_COMBO_THEME, thn, 3, g_theme);
        }

        /* toggles */
        g_hTglSvc = zg_toggle_create(hwnd, IDC_TOGGLE_SVC, FALSE);
        g_hTglUpd = zg_toggle_create(hwnd, IDC_TOGGLE_UPD, FALSE);

        /* log */
        g_hLog = zg_log_create(hwnd, 0, 0, 10, 10);

        apply_layout();

        SetTimer(hwnd, ZG_TIMER_STATUS, 2000, NULL);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT crc; GetClientRect(hwnd, &crc);
        int cw = crc.right, ch = crc.bottom;

        /* double buffer */
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, cw, ch);
        HBITMAP obmp = (HBITMAP)SelectObject(mem, bmp);

        HBRUSH bg = CreateSolidBrush(COL_BG);
        RECT fr = { 0, 0, cw, ch };
        FillRect(mem, &fr, bg);
        DeleteObject(bg);

        /*
         * Two-phase painting — NEVER interleave GDI and GDI+ calls
         * on the same DC: a live Gdiplus::Graphics object changes the
         * DC state (GM_ADVANCED + world transform), which corrupts
         * GDI text output and displaces later GDI+ shapes on cached
         * common DCs. Phase A = all GDI+ shapes; phase B = all GDI text.
         */
        {
            Graphics g(mem);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

            paint_header_shapes(g);
            paint_hero_shapes(g);
            paint_cards_shapes(g);
            paint_settings_panel_shapes(g);
            paint_log_frame(g);
        }  /* Graphics object destroyed here — DC back to clean GDI state */

        {
            SetBkMode(mem, TRANSPARENT);
            paint_header_text(mem);
            paint_hero_text(mem);
            paint_cards_text(mem);
            draw_section_header(mem, zg_str(S_SEC_SETTINGS),
                                SC(DU_PAD), SC(DU_SET_HDR_Y), SC(DU_CONTENT_W));
            paint_settings_panel_text(mem);
            draw_section_header(mem, zg_str(S_SEC_LOG),
                                SC(DU_PAD), SC(DU_LOG_HDR_Y), SC(DU_CONTENT_W));
        }

        BitBlt(hdc, ps.rcPaint.left, ps.rcPaint.top,
               ps.rcPaint.right - ps.rcPaint.left,
               ps.rcPaint.bottom - ps.rcPaint.top,
               mem, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
        SelectObject(mem, obmp);
        DeleteObject(bmp);
        DeleteDC(mem);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_TIMER:
        if (wp == ZG_TIMER_STATUS) {
            ZgStatus prev = g_st;      /* snapshot for change detection */
            zg_status_refresh(&g_st);
            refresh_primary_button();
            tray_update_tooltip();
            if (!status_same(&prev, &g_st))
                InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        break;

    case WM_GETMINMAXINFO: {
        /* pt*TrackSize are OUTER window sizes — convert the intended
         * client size with AdjustWindowRect before using them. */
        MINMAXINFO* mmi = (MINMAXINFO*)lp;
        DWORD st = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        RECT wr = { 0, 0, SC(DU_WIN_W), SC(DU_WIN_H) };
        AdjustWindowRect(&wr, st, FALSE);
        mmi->ptMinTrackSize.x = wr.right - wr.left;
        mmi->ptMinTrackSize.y = wr.bottom - wr.top;
        mmi->ptMaxTrackSize   = mmi->ptMinTrackSize;
        return 0;
    }

    case WM_DPICHANGED: {
        /* HIWORD(wp) is the new DPI (96/120/144...) — convert to percent. */
        int newpct = MulDiv(HIWORD(wp), 100, 96);
        if (newpct >= 50 && newpct != g_scale_pct) {
            g_scale_pct = newpct;
            theme_create_fonts();
            zg_log_apply_theme(g_hLog);   /* 9pt log font, DPI-scaled */
            apply_layout();
        }
        RECT* r = (RECT*)lp;
        SetWindowPos(hwnd, NULL, r->left, r->top,
                     r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }

    case WM_MOVE:
    case WM_SIZE:
        zg_combo_close_popup(g_hComboStrat);
        zg_combo_close_popup(g_hComboGame);
        zg_combo_close_popup(g_hComboIpset);
        zg_combo_close_popup(g_hComboLang);
        zg_combo_close_popup(g_hComboTheme);
        break;

    case WM_ACTIVATE:
        if (wp == WA_INACTIVE) {
            zg_combo_close_popup(g_hComboStrat);
            zg_combo_close_popup(g_hComboGame);
            zg_combo_close_popup(g_hComboIpset);
            zg_combo_close_popup(g_hComboLang);
            zg_combo_close_popup(g_hComboTheme);
        } else {
            refresh_status(TRUE);
        }
        return 0;

    /* ---- close = hide to tray; the full exit lives in the tray menu ---- */
    case WM_CLOSE:
        hide_to_tray(hwnd);
        return 0;

    case WM_QUERYENDSESSION:
        return TRUE;                      /* allow logoff / shutdown */

    case WM_ENDSESSION:
        if (wp) tray_remove();            /* clean the icon up on logoff */
        return 0;

    /* ---- tray icon events (classic callback: message in lParam) ---- */
    case WM_APP_TRAY: {
        UINT em = (UINT)lp;
        if (em == WM_LBUTTONUP || em == WM_LBUTTONDBLCLK) {
            show_main_window();
        } else if (em == WM_RBUTTONUP || em == WM_CONTEXTMENU) {
            tray_show_menu(hwnd);
        }
        return 0;
    }

    /* ---- custom control notifications ---- */

    case WM_ZGBUTTON: {
        int id = (int)wp;
        switch (id) {
        case IDC_BTN_PRIMARY: {
            if (g_busy) break;
            if (!g_paths.files_ok) {
                fix_zapret_folder_flow(hwnd);
                break;
            }
            if (g_st.winws_running) {
                int stopped_svc = 0;
                ui_log(ZLOG_INFO, L"%s", zg_str(S_LOG_STOPPING));
                zg_stop_bypass(&stopped_svc);
                ui_log(ZLOG_OK, L"%s", zg_str(stopped_svc ? S_LOG_STOPPED_SVC
                                                          : S_LOG_STOPPED));
            } else {
                int idx = zg_combo_get_sel(g_hComboStrat);
                if (idx < 0 || idx >= g_stratCount) {
                    ui_log(ZLOG_ERR, L"%s", zg_str(S_LOG_NO_STRAT));
                    break;
                }
                wchar_t err[256];
                ui_log(ZLOG_INFO, zg_str(S_LOG_LAUNCHING), g_strats[idx]);
                if (zg_launch_bypass(g_strats[idx], err, 256)) {
                    Sleep(400);
                    zg_status_refresh(&g_st);
                    if (g_st.winws_running)
                        ui_log(ZLOG_OK, L"%s", zg_str(S_LOG_STARTED_OK));
                    else
                        ui_log(ZLOG_ERR, L"%s", zg_str(S_LOG_STARTED_FAIL));
                } else {
                    ui_log(ZLOG_ERR, L"%s", err);
                }
            }
            refresh_status(TRUE);
            tray_update_tooltip();
            break;
        }
        case IDC_BTN_DIAG:
            ui_log(ZLOG_INFO, L"%s", zg_str(S_LOG_OPEN_DIAG));
            zg_open_console_bat(L"diag");
            break;
        case IDC_BTN_TESTS:
            ui_log(ZLOG_INFO, L"%s", zg_str(S_LOG_RUN_TESTS));
            zg_open_console_bat(L"tests");
            break;
        case IDC_BTN_HOSTS:
            run_op(OP_HOSTS_CHECK, NULL);
            break;
        case IDC_BTN_IPSETUPD:
            run_op(OP_IPSET_UPDATE, NULL);
            break;
        case IDC_BTN_CHECKUPD:
            run_op(OP_VERSION_CHECK, NULL);
            break;
        case IDC_BTN_LOGCLEAR:
            zg_log_clear(g_hLog);
            break;
        }
        return 0;
    }

    case WM_ZGTOGGLE: {
        int id = (int)wp;
        BOOL on = (lp != 0);
        if (g_busy) break;
        if (id == IDC_TOGGLE_SVC) {
            if (on) {
                int idx = zg_combo_get_sel(g_hComboStrat);
                if (idx < 0 || idx >= g_stratCount) {
                    ui_log(ZLOG_ERR, L"%s", zg_str(S_LOG_NO_STRAT_SVC));
                    zg_toggle_set(g_hTglSvc, FALSE, FALSE);
                    break;
                }
                ui_log(ZLOG_INFO, zg_str(S_LOG_SVC_INSTALLING), g_strats[idx]);
                run_op(OP_SVC_INSTALL, g_strats[idx]);
            } else {
                ui_log(ZLOG_INFO, L"%s", zg_str(S_LOG_SVC_REMOVING));
                run_op(OP_SVC_REMOVE, NULL);
            }
        } else if (id == IDC_TOGGLE_UPD) {
            if (zg_checkupdates_set(on))
                ui_log(ZLOG_OK, L"%s", zg_str(on ? S_LOG_UPD_ON : S_LOG_UPD_OFF));
            else
                ui_log(ZLOG_ERR, L"%s", zg_str(S_LOG_UPD_ERR));
        }
        return 0;
    }

    case WM_ZGCOMBO: {
        int id = (int)wp;
        int idx = (int)lp;
        if (id == IDC_COMBO_STRAT) {
            if (idx >= 0 && idx < g_stratCount) {
                settings_save();
                ui_log(ZLOG_INFO, zg_str(S_LOG_STRAT_SEL), g_strats[idx]);
                if (g_st.winws_running)
                    ui_log(ZLOG_WARN, L"%s", zg_str(S_LOG_RESTART_HINT));
            }
        } else if (id == IDC_COMBO_GAME) {
            if (idx >= 0 && idx < 4) {
                if (zg_gamefilter_set(GAME_MODES[idx]))
                    ui_log(ZLOG_OK, zg_str(S_LOG_GAME_SET), zg_game_labels()[idx]);
                else
                    ui_log(ZLOG_ERR, L"%s", zg_str(S_LOG_GAME_ERR));
                ui_log(ZLOG_WARN, L"%s", zg_str(S_LOG_RESTART_HINT2));
            }
        } else if (id == IDC_COMBO_IPSET) {
            if (idx >= 0 && idx < 3) {
                if (zg_ipset_set(IPSET_MODES[idx])) {
                    ui_log(ZLOG_OK, zg_str(S_LOG_IPSET_SET), zg_ipset_labels()[idx]);
                } else {
                    ui_log(ZLOG_ERR, L"%s", zg_str(S_LOG_IPSET_ERR));
                    /* revert combo to actual state */
                    int im = zg_ipset_get(), ri = 0;
                    for (int i = 0; i < 3; i++) if (IPSET_MODES[i] == im) ri = i;
                    zg_combo_set_sel(g_hComboIpset, ri, FALSE);
                }
                ui_log(ZLOG_WARN, L"%s", zg_str(S_LOG_RESTART_HINT2));
            }
        } else if (id == IDC_COMBO_LANG) {
            if (idx >= 0 && idx < ZG_LANG_COUNT && idx != g_lang) {
                zg_lang_set(idx);
                update_ui_language();
            }
        } else if (id == IDC_COMBO_THEME) {
            if (idx >= 0 && idx < ZG_THEME_COUNT && idx != g_theme) {
                apply_ui_theme(idx);
            }
        }
        return 0;
    }

    case WM_APP_LOG: {
        const wchar_t* text = (const wchar_t*)lp;
        if (text) {
            zg_log_append(g_hLog, (int)wp, text);
            HeapFree(GetProcessHeap(), 0, (void*)text);
        }
        return 0;
    }

    case WM_APP_OPDONE: {
        g_busy = FALSE;
        set_controls_busy(FALSE);
        refresh_status(TRUE);
        tray_update_tooltip();
        return 0;
    }

    case WM_DESTROY: {
        KillTimer(hwnd, ZG_TIMER_STATUS);
        tray_remove();
        if (g_st.winws_running && g_st.own_child_alive)
            ui_log(ZLOG_INFO, L"%s", zg_str(S_LOG_GUI_CLOSED));
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ================================================================== */
/* WinMain                                                              */
/* ================================================================== */

static void enable_dark_titlebar(HWND hwnd, BOOL dark)
{
    typedef HRESULT (WINAPI *FN_SetAttr)(HWND, DWORD, LPCVOID, DWORD);
    HMODULE hDwm = LoadLibraryW(L"dwmapi.dll");
    if (!hDwm) return;
    FN_SetAttr f = (FN_SetAttr)(void*)GetProcAddress(hDwm, "DwmSetWindowAttribute");
    if (!f) { FreeLibrary(hDwm); return; }

    BOOL d = dark;
    /* DWMWA_USE_IMMERSIVE_DARK_MODE: 20 on Win10 2004+, 19 on older builds */
    if (FAILED(f(hwnd, 20, &d, sizeof(d))))
        f(hwnd, 19, &d, sizeof(d));

    /* rounded corners on Win11 */
    int pref = 2;  /* DWMWCP_ROUND */
    f(hwnd, 33, &pref, sizeof(pref));   /* DWMWA_WINDOW_CORNER_PREFERENCE */

    /* dark window border on Win11 (DWMWA_BORDER_COLOR = 34) */
    if (dark) {
        COLORREF border = RGB(0x3E, 0x3E, 0x3E);
        f(hwnd, 34, &border, sizeof(border));
    }

    FreeLibrary(hDwm);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, PWSTR cmd, int show)
{
    (void)hPrev; (void)cmd;

    /* single instance: a second launch restores the (maybe hidden) window
     * instead of stacking a second tray icon */
    HANDLE hSingle = CreateMutexW(NULL, FALSE, L"Local\\ZapretGUI.SingleInstance");
    if (hSingle && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND ex = FindWindowW(ZG_WND_CLASS, NULL);
        if (ex) {
            ShowWindow(ex, IsIconic(ex) ? SW_RESTORE : SW_SHOW);
            SetForegroundWindow(ex);
        }
        CloseHandle(hSingle);
        return 0;
    }

    /* COM for the IFileDialog folder picker */
    HRESULT com_hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    /* settings FIRST: language + theme must be active before any string /
     * color is touched */
    ZgGuiSettings st;
    settings_load(&st);
    zg_lang_set(st.language);
    zg_theme_apply(st.theme);

    /* initial DPI — g_scale_pct is a PERCENT (100 = 96 dpi) */
    HDC dc = GetDC(NULL);
    int dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(NULL, dc);
    g_scale_pct = (dpi > 0) ? MulDiv(dpi, 100, 96) : 100;
    if (g_scale_pct < 50) g_scale_pct = 100;

    Gdiplus::GdiplusStartupInput gsi;
    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gsi, NULL);

    theme_create_fonts();
    zg_paths_init();
    zg_register_controls();

    g_stratCount = zg_find_strategies(&g_strats);

    g_msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hIcon         = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(1),
                                         IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    wc.hIconSm       = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(1),
                                         IMAGE_ICON, 16, 16, 0);
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = ZG_WND_CLASS;
    RegisterClassExW(&wc);

    /* window slightly larger than the 561x745 reference; WS_CLIPCHILDREN
     * keeps the parent's periodic repaints away from the child controls
     * (footer buttons / combos / log) — the #1 flicker source before   */
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX
                | WS_CLIPCHILDREN;
    RECT wr = { 0, 0, SC(DU_WIN_W), SC(DU_WIN_H) };
    AdjustWindowRect(&wr, style, FALSE);

    HWND hwnd = CreateWindowExW(0, ZG_WND_CLASS,
                                zg_str(S_WINDOW_TITLE),
                                style,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                wr.right - wr.left, wr.bottom - wr.top,
                                NULL, NULL, hInst, NULL);
    if (!hwnd) {
        if (g_strats) zg_free_strategies(g_strats, g_stratCount);
        theme_destroy_fonts();
        if (gdiplusToken) Gdiplus::GdiplusShutdown(gdiplusToken);
        if (SUCCEEDED(com_hr)) CoUninitialize();
        if (hSingle) CloseHandle(hSingle);
        return 1;
    }

    /* tray state icons (fallback to the embedded .ico) */
    g_icoTrayOn  = make_state_icon(TRUE);
    g_icoTrayOff = make_state_icon(FALSE);
    if (!g_icoTrayOn)
        g_icoTrayOn = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                        GetSystemMetrics(SM_CXSMICON),
                                        GetSystemMetrics(SM_CYSMICON), 0);
    if (!g_icoTrayOff)
        g_icoTrayOff = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                         GetSystemMetrics(SM_CXSMICON),
                                         GetSystemMetrics(SM_CYSMICON), 0);

    /* pick remembered strategy */
    int def_idx = 0;
    if (st.strategy[0]) {
        for (int i = 0; i < g_stratCount; i++)
            if (_wcsicmp(g_strats[i], st.strategy) == 0) { def_idx = i; break; }
    }
    zg_combo_set_sel(g_hComboStrat, g_stratCount > 0 ? def_idx : -1, FALSE);

    enable_dark_titlebar(hwnd, g_th.is_dark);

    zg_status_refresh(&g_st);
    refresh_status(TRUE);
    set_controls_busy(FALSE);

    /* show the window BEFORE the "files not found" dialog so the user
     * sees the GUI (and its log) behind it                          */
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    ui_log(ZLOG_INFO, zg_str(S_LOG_STARTED), ZG_GUI_VERSION, g_paths.real_exe_dir);
    if (!g_paths.files_ok) {
        ui_log(ZLOG_ERR, L"%s", zg_str(S_LOG_NOFILES_ERR));
        ui_log(ZLOG_INFO, zg_str(S_LOG_SEARCH_REPORT), zg_search_report());
        fix_zapret_folder_flow(hwnd);
    }
    else {
        if (g_paths.detect == ZG_DET_CONFIG)
            ui_log(ZLOG_INFO, zg_str(S_LOG_DIR_CFG), g_paths.exe_dir);
        else if (g_paths.detect == ZG_DET_AUTO_PARENT)
            ui_log(ZLOG_OK, zg_str(S_LOG_DIR_AUTO), g_paths.exe_dir);
        ui_log(ZLOG_INFO, zg_str(S_LOG_VER_STRATS), g_st.local_version, g_stratCount);
        if (g_stratCount > 0)
            ui_log(ZLOG_INFO, zg_str(S_LOG_CUR_STRAT),
                   g_strats[zg_combo_get_sel(g_hComboStrat)]);
    }
    ui_log(ZLOG_OK, zg_str(S_LOG_THEME_SET), theme_display_name(g_theme));

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_strats) zg_free_strategies(g_strats, g_stratCount);
    tray_remove();
    if (g_icoTrayOn)  { DestroyIcon(g_icoTrayOn);  g_icoTrayOn  = NULL; }
    if (g_icoTrayOff) { DestroyIcon(g_icoTrayOff); g_icoTrayOff = NULL; }
    theme_destroy_fonts();
    if (gdiplusToken) Gdiplus::GdiplusShutdown(gdiplusToken);
    if (SUCCEEDED(com_hr)) CoUninitialize();
    if (hSingle) { ReleaseMutex(hSingle); CloseHandle(hSingle); }
    return (int)msg.wParam;
}
