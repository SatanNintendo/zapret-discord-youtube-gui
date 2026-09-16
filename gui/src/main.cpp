/*
 * Zapret GUI — main.cpp
 * Application entry point, main window, layout and painting.
 *
 * Design recreated from the reference screenshot (dark theme,
 * green accent, purple->coral gradient primary button).
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
static HWND g_hTglSvc = NULL, g_hTglUpd = NULL;
static HWND g_hLog = NULL;

static wchar_t** g_strats = NULL;
static int       g_stratCount = 0;
static ZgStatus  g_st;
static BOOL      g_busy = FALSE;      /* worker op in flight */

static const wchar_t* GAME_LABELS[4] = {
    L"Отключён", L"Включён (UDP)", L"Включён (TCP)", L"Включён (TCP и UDP)"
};
static const int GAME_MODES[4] = { ZG_GAME_OFF, ZG_GAME_UDP, ZG_GAME_TCP, ZG_GAME_ALL };

static const wchar_t* IPSET_LABELS[3] = {
    L"Загружен (список)", L"Отключён (none)", L"Любой IP (any)"
};
static const int IPSET_MODES[3] = { ZG_IPSET_LOADED, ZG_IPSET_NONE, ZG_IPSET_ANY };

/* ================================================================== */
/* fonts                                                               */
/* ================================================================== */

static HFONT make_font(int pt10, const wchar_t* face, int weight)
{
    /*
     * pt10       — font size in points x10 (e.g. 100 = 10.0pt)
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
    g_font_status = make_font(200, L"Segoe UI", FW_BOLD);
    g_font_body   = make_font(100, L"Segoe UI", FW_NORMAL);
    g_font_body_b = make_font(100, L"Segoe UI Semibold", FW_SEMIBOLD);
    g_font_small  = make_font(90,  L"Segoe UI", FW_NORMAL);
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
/* settings persistence (%APPDATA%\ZapretGUI\settings.ini)             */
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

static void settings_load(wchar_t* strat, size_t cap)
{
    wchar_t p[MAX_PATH]; settings_path(p, MAX_PATH);
    strat[0] = 0;
    GetPrivateProfileStringW(L"gui", L"strategy", L"", strat, (DWORD)cap, p);
}

static void settings_save(const wchar_t* strat)
{
    wchar_t p[MAX_PATH]; settings_path(p, MAX_PATH);
    wchar_t dir[MAX_PATH];
    wcscpy(dir, p);
    wchar_t* slash = wcsrchr(dir, L'\\');
    if (slash) { *slash = 0; CreateDirectoryW(dir, NULL); }
    WritePrivateProfileStringW(L"gui", L"strategy", strat, p);
}

/* ================================================================== */
/* "zapret folder missing": folder picker + question dialog            */
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
    fd->SetTitle(L"Укажите папку zapret — где лежат bin\\winws.exe и general*.bat");

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
            _snwprintf(content, 799,
                L"ZapretGUI.exe запущен из папки:\n%s\n\n"
                L"Там нет bin\\winws.exe.\n\n"
                L"Папка zapret — это распакованный архив zapret-discord-youtube:\n"
                L"внутри неё лежат bin\\, lists\\ и файлы general*.bat.\n"
                L"Копировать ZapretGUI.exe внутрь не обязательно —\n"
                L"можно просто указать эту папку.",
                exe_dir);
            content[799] = 0;

            TASKDIALOG_BUTTON btns[3] = {
                { 1001, L"Указать папку zapret…"    },
                { 1002, L"Найти папку автоматически" },
                { 1003, L"Продолжить без zapret"    },
            };

            TASKDIALOGCONFIG cfg;
            ZeroMemory(&cfg, sizeof(cfg));
            cfg.cbSize             = sizeof(cfg);
            cfg.hwndParent         = owner;
            cfg.dwFlags            = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
            cfg.pszWindowTitle     = L"Zapret GUI — файлы zapret не найдены";
            cfg.pszMainIcon        = TD_WARNING_ICON;
            cfg.pszMainInstruction = L"Файлы zapret не найдены";
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
     * Да = pick manually, Нет = retry autosearch, Отмена = continue   */
    wchar_t text[900];
    _snwprintf(text, 899,
        L"Файлы zapret не найдены: рядом с ZapretGUI.exe нет bin\\winws.exe.\n\n"
        L"ZapretGUI.exe запущен из папки:\n%s\n\n"
        L"«Да» — указать папку zapret вручную\n"
        L"«Нет» — повторить автоматический поиск\n"
        L"«Отмена» — продолжить без zapret",
        exe_dir);
    text[899] = 0;
    int mb = MessageBoxW(owner, text, L"Zapret GUI — файлы zapret не найдены",
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
            zg_log_from_worker(ZLOG_OK, L"Служба zapret установлена и запущена (%s)", wa->strat);
        else
            zg_log_from_worker(ZLOG_ERR, L"Ошибка установки службы: %s", err);
        break;
    }
    case OP_SVC_REMOVE: {
        wchar_t err[256];
        if (zg_service_remove(err, 256))
            zg_log_from_worker(ZLOG_OK, L"Службы удалены (zapret, WinDivert)");
        else
            zg_log_from_worker(ZLOG_ERR, L"Ошибка удаления службы: %s", err);
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

static Gdiplus::GraphicsPath* make_round_path(int x, int y, int w, int h, int r)
{
    Gdiplus::GraphicsPath* p = new Gdiplus::GraphicsPath();
    int d = r * 2;
    p->AddArc(x,     y,     d, d, 180, 90);
    p->AddArc(x+w-d, y,     d, d, 270, 90);
    p->AddArc(x+w-d, y+h-d, d, d,   0, 90);
    p->AddArc(x,     y+h-d, d, d,  90, 90);
    p->CloseFigure();
    return p;
}

/* lightning bolt polygon (unit coords) */
static const float BOLT_X[6] = { 0.585f, 0.245f, 0.460f, 0.415f, 0.760f, 0.535f };
static const float BOLT_Y[6] = { 0.080f, 0.545f, 0.545f, 0.920f, 0.395f, 0.395f };

static void draw_app_icon(Graphics& g, int x, int y, int size)
{
    /* purple rounded square with white bolt — same shape as app.ico */
    Gdiplus::GraphicsPath* p = make_round_path(x, y, size, size, size / 4);
    Gdiplus::LinearGradientBrush br(
        Gdiplus::Point(x, y), Gdiplus::Point(x, y + size),
        Gdiplus::Color(255, 0x8A, 0x6C, 0xF5), Gdiplus::Color(255, 0x6A, 0x50, 0xD8));
    g.FillPath(&br, p);
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
    SetTextColor(hdc, RGB(0x99, 0x99, 0x99));
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
    BOOL files_ok = g_paths.files_ok;

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
    Gdiplus::SolidBrush disc(files_ok ? Gdiplus::Color(255, 0x12, 0x12, 0x12)
                                      : Gdiplus::Color(255, 0x14, 0x10, 0x10));
    g.FillEllipse(&disc, cx - r, cy - r, r * 2, r * 2);

    /* ring */
    Gdiplus::Pen ring(active ? Gdiplus::Color(255, 0x32, 0xCD, 0x32)
                             : Gdiplus::Color(255, 0x60, 0x60, 0x60),
                      (Gdiplus::REAL)SC(3));
    g.DrawEllipse(&ring, cx - r, cy - r, r * 2, r * 2);

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
                                  : Gdiplus::Color(255, 0x8A, 0x8A, 0x8A));
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
    DrawTextW(hdc, active ? L"ОБХОД ВКЛЮЧЁН"
                  : (files_ok ? L"ОБХОД ВЫКЛЮЧЕН" : L"ФАЙЛЫ ZAPRET НЕ НАЙДЕНЫ"),
              -1, &tr, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    SelectObject(hdc, of);

    /* subtext */
    wchar_t sub[160];
    if (!files_ok)
        _snwprintf(sub, 159, L"укажите папку zapret — нажмите «Выбрать папку zapret» ниже");
    else if (g_st.svc_running && g_st.winws_running)
        _snwprintf(sub, 159, L"работает как служба — стратегия: %s",
                   g_st.svc_strategy[0] ? g_st.svc_strategy : L"?");
    else if (g_st.own_child_alive && g_st.winws_running) {
        int idx = zg_combo_get_sel(g_hComboStrat);
        _snwprintf(sub, 159, L"запущен из Zapret GUI — стратегия: %s",
                   (idx >= 0 && idx < g_stratCount) ? g_strats[idx] : L"?");
    }
    else if (g_st.winws_running)
        _snwprintf(sub, 159, L"winws.exe запущен вручную (вне Zapret GUI)");
    else if (g_st.svc_installed)
        _snwprintf(sub, 159, L"служба zapret установлена, но не запущена");
    else
        _snwprintf(sub, 159, L"нажмите кнопку ниже, чтобы включить обход");
    sub[159] = 0;

    RECT sr2 = { 0, SC(DU_SUBTXT_Y), SC(DU_WIN_W), SC(DU_SUBTXT_Y + 18) };
    of = (HFONT)SelectObject(hdc, g_font_body);
    SetTextColor(hdc, files_ok ? COL_MUTED : COL_YELLOW);
    DrawTextW(hdc, sub, -1, &sr2, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS);
    SelectObject(hdc, of);
}

struct CardInfo { const wchar_t* title; const wchar_t* text; BOOL ok; COLORREF dot; };

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
        Gdiplus::GraphicsPath* p = make_round_path(x, y, w - 1, h - 1, SC(10));
        Gdiplus::SolidBrush br(Gdiplus::Color(zg_argb(255, COL_PANEL)));
        g.FillPath(&br, p);
        Gdiplus::Pen pen(Gdiplus::Color(zg_argb(255, COL_BORDER_DIM)));
        g.DrawPath(&pen, p);
        delete p;

        /* dot */
        int dy = y + SC(40);
        Gdiplus::SolidBrush dotb(dots[i]);
        g.FillEllipse(&dotb, x + SC(16), dy, SC(7), SC(7));
    }
}

static void paint_cards_text(HDC hdc)
{
    struct { const wchar_t* title; const wchar_t* text; BOOL ok; } cards[3];

    cards[0].title = L"ПРОЦЕСС WINWS";
    cards[0].text  = g_st.winws_running ? L"работает" : L"не запущен";
    cards[0].ok    = g_st.winws_running;

    cards[1].title = L"СЛУЖБА ZAPRET";
    if (g_st.svc_running)         { cards[1].text = L"запущена";          cards[1].ok = TRUE; }
    else if (g_st.svc_installed)  { cards[1].text = L"установлена";       cards[1].ok = FALSE; }
    else                          { cards[1].text = L"не установлена";   cards[1].ok = FALSE; }

    cards[2].title = L"ДРАЙВЕР WINDIVERT";
    cards[2].text  = g_st.windivert_running ? L"активен" : L"не активен";
    cards[2].ok    = g_st.windivert_running;

    int w = SC(DU_CARD_W), y = SC(DU_CARDS_Y);
    SetBkMode(hdc, TRANSPARENT);
    for (int i = 0; i < 3; i++) {
        int x = SC(DU_PAD) + i * (w + SC(DU_CARD_GAP));

        /* title */
        HFONT of = (HFONT)SelectObject(hdc, g_font_h2);
        SetTextColor(hdc, RGB(0x99, 0x99, 0x99));
        SetTextCharacterExtra(hdc, SC(1));
        TextOutW(hdc, x + SC(16), y + SC(12), cards[i].title, (int)wcslen(cards[i].title));
        SetTextCharacterExtra(hdc, 0);
        SelectObject(hdc, of);

        /* status text */
        of = (HFONT)SelectObject(hdc, g_font_body_b);
        SetTextColor(hdc, cards[i].ok ? COL_TEXT : COL_MUTED);
        TextOutW(hdc, x + SC(30), y + SC(33), cards[i].text, (int)wcslen(cards[i].text));
        SelectObject(hdc, of);
    }
}

static void paint_settings_panel_shapes(Graphics& g)
{
    int x = SC(DU_PAD), y = SC(DU_SET_Y), w = SC(DU_CONTENT_W), h = SC(DU_SET_H);
    Gdiplus::GraphicsPath* p = make_round_path(x, y, w - 1, h - 1, SC(10));
    Gdiplus::SolidBrush br(Gdiplus::Color(zg_argb(255, RGB(0x26, 0x26, 0x26))));
    g.FillPath(&br, p);
    Gdiplus::Pen pen(Gdiplus::Color(zg_argb(255, COL_BORDER_DIM)));
    g.DrawPath(&pen, p);
    delete p;

    /* row separators (subtle) */
    for (int i = 1; i < 5; i++) {
        int sy = y + SC(6) + i * SC(DU_ROW_H);
        Gdiplus::Pen sp(Gdiplus::Color(40, 0x44, 0x44, 0x44));
        g.DrawLine(&sp, x + SC(16), sy, x + w - SC(16), sy);
    }
}

static void paint_settings_panel_text(HDC hdc)
{
    int x = SC(DU_PAD), y = SC(DU_SET_Y), w = SC(DU_CONTENT_W);

    /* labels */
    static const wchar_t* labels[5] = {
        L"Стратегия обхода",
        L"Игровой фильтр",
        L"Автозапуск с Windows (служба)",
        L"Проверять обновления zapret",
        L"Фильтр IP-списков (IPSet)",
    };
    SetBkMode(hdc, TRANSPARENT);
    HFONT of = (HFONT)SelectObject(hdc, g_font_body);
    SetTextColor(hdc, COL_TEXT);
    for (int i = 0; i < 5; i++) {
        int ry = y + SC(6) + i * SC(DU_ROW_H);
        RECT tr = { x + SC(16), ry, x + w - SC(240), ry + SC(DU_ROW_H) };
        DrawTextW(hdc, labels[i], -1, &tr,
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
    TextOutW(hdc, SC(DU_TITLE_X), SC(DU_SUB_Y),
             L"Обход блокировок Discord и YouTube", 34);
    SelectObject(hdc, of);

    /* version (top-right) */
    wchar_t ver[64];
    _snwprintf(ver, 63, L"zapret %s  ·  GUI %s",
               g_st.local_version[0] ? g_st.local_version : L"?",
               ZG_GUI_VERSION);
    ver[63] = 0;

    of = (HFONT)SelectObject(hdc, g_font_small);
    SetTextColor(hdc, RGB(0x90, 0x90, 0x90));
    SIZE sz; GetTextExtentPoint32W(hdc, ver, (int)wcslen(ver), &sz);
    TextOutW(hdc, SC(DU_WIN_W - DU_PAD) - sz.cx, SC(DU_VER_Y), ver, (int)wcslen(ver));
    SelectObject(hdc, of);

    /* "Проверить обновления" link is a child control (ZGBTN_LINK) */
}

static void paint_log_frame(Graphics& g)
{
    /* thin rounded border around the log area (control sits on top) */
    int x = SC(DU_PAD) - 1, y = SC(DU_LOG_Y) - 1;
    int w = SC(DU_CONTENT_W) + 2, h = SC(DU_LOG_H) + 2;
    Gdiplus::GraphicsPath* p = make_round_path(x, y, w - 1, h - 1, SC(8));
    Gdiplus::Pen pen(Gdiplus::Color(zg_argb(255, COL_BORDER_DIM)));
    g.DrawPath(&pen, p);
    delete p;
}

/* ================================================================== */
/* layout                                                              */
/* ================================================================== */

static void apply_layout(void)
{
    if (!g_hMain) return;

    /* footer buttons */
    int fw = (SC(DU_CONTENT_W) - 3 * SC(DU_FOOT_GAP)) / 4;
    int fy = SC(DU_FOOT_Y), fh = SC(DU_FOOT_H);
    HWND foot[4] = { g_hBtnDiag, g_hBtnHosts, g_hBtnIpset, g_hBtnTests };
    for (int i = 0; i < 4; i++) {
        int fx = SC(DU_PAD) + i * (fw + SC(DU_FOOT_GAP));
        SetWindowPos(foot[i], NULL, fx, fy, fw, fh, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    /* primary button */
    SetWindowPos(g_hPrimary, NULL, SC(DU_PRIMARY_X), SC(DU_PRIMARY_Y),
                 SC(DU_PRIMARY_W), SC(DU_PRIMARY_H), SWP_NOZORDER | SWP_NOACTIVATE);

    /* settings rows */
    int px = SC(DU_PAD), pw = SC(DU_CONTENT_W);
#define ROW_Y(i)   (SC(DU_SET_Y + 6) + (i) * SC(DU_ROW_H))
#define ROW_CY(i)  (ROW_Y(i) + (SC(DU_ROW_H) - SC(DU_CTRL_H)) / 2)
#define ROW_TY(i)  (ROW_Y(i) + (SC(DU_ROW_H) - SC(24)) / 2)

    /* strategy combo */
    SetWindowPos(g_hComboStrat, NULL,
                 px + pw - SC(16) - SC(DU_COMBO_W), ROW_CY(0),
                 SC(DU_COMBO_W), SC(DU_CTRL_H), SWP_NOZORDER | SWP_NOACTIVATE);

    /* game combo */
    SetWindowPos(g_hComboGame, NULL,
                 px + pw - SC(16) - SC(200), ROW_CY(1),
                 SC(200), SC(DU_CTRL_H), SWP_NOZORDER | SWP_NOACTIVATE);

    /* autostart toggle */
    SetWindowPos(g_hTglSvc, NULL,
                 px + pw - SC(16) - SC(46), ROW_TY(2),
                 SC(46), SC(24), SWP_NOZORDER | SWP_NOACTIVATE);

    /* update check toggle */
    SetWindowPos(g_hTglUpd, NULL,
                 px + pw - SC(16) - SC(46), ROW_TY(3),
                 SC(46), SC(24), SWP_NOZORDER | SWP_NOACTIVATE);

    /* ipset combo */
    SetWindowPos(g_hComboIpset, NULL,
                 px + pw - SC(16) - SC(200), ROW_CY(4),
                 SC(200), SC(DU_CTRL_H), SWP_NOZORDER | SWP_NOACTIVATE);
#undef ROW_Y
#undef ROW_CY
#undef ROW_TY

    /* header link */
    SetWindowPos(g_hBtnCheckUpd, NULL,
                 SC(DU_WIN_W - DU_PAD) - SC(150), SC(DU_UPDLINK_Y),
                 SC(150), SC(20), SWP_NOZORDER | SWP_NOACTIVATE);

    /* log */
    SetWindowPos(g_hLog, NULL, SC(DU_PAD), SC(DU_LOG_Y),
                 SC(DU_CONTENT_W), SC(DU_LOG_H), SWP_NOZORDER | SWP_NOACTIVATE);

    /* "Очистить" link above log, right-aligned */
    SetWindowPos(g_hBtnLogClear, NULL,
                 SC(DU_WIN_W - DU_PAD) - SC(70), SC(DU_LOG_HDR_Y) - SC(3),
                 SC(70), SC(20), SWP_NOZORDER | SWP_NOACTIVATE);

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

static void refresh_primary_button(void)
{
    if (!g_paths.files_ok) {
        zg_button_set_text(g_hPrimary, L"ВЫБРАТЬ ПАПКУ ZAPRET");
        zg_button_set_scheme(g_hPrimary, ZGBP_PURPLE);
    } else if (g_st.winws_running) {
        zg_button_set_text(g_hPrimary, L"ВЫКЛЮЧИТЬ ОБХОД");
        zg_button_set_scheme(g_hPrimary, ZGBP_PURPLE);
    } else {
        zg_button_set_text(g_hPrimary, L"ВКЛЮЧИТЬ ОБХОД");
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
/* switching the zapret base folder at runtime                        */
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
    ui_log(ZLOG_OK, L"Папка zapret: %s", g_paths.exe_dir);

    if (g_strats) { zg_free_strategies(g_strats, g_stratCount); g_strats = NULL; }
    g_stratCount = zg_find_strategies(&g_strats);

    wchar_t saved[MAX_PATH];
    settings_load(saved, MAX_PATH);
    int def_idx = 0;
    if (saved[0]) {
        for (int i = 0; i < g_stratCount; i++)
            if (_wcsicmp(g_strats[i], saved) == 0) { def_idx = i; break; }
    }
    rebuild_strategy_combo(def_idx);

    zg_ensure_user_lists();
    zg_status_refresh(&g_st);
    refresh_status(TRUE);

    ui_log(ZLOG_INFO, L"Версия zapret: %s · найдено стратегий: %d",
           g_st.local_version, g_stratCount);
    if (g_stratCount > 0)
        ui_log(ZLOG_INFO, L"Текущая стратегия: %s",
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
                ui_log(ZLOG_ERR, L"В выбранной папке нет bin\\winws.exe: %s", dir);
                MessageBoxW(hwnd,
                    L"В выбранной папке нет bin\\winws.exe.\n\n"
                    L"Выберите папку, в которую распакован zapret —\n"
                    L"внутри неё должны быть папки bin и lists\n"
                    L"и файлы general*.bat.",
                    L"Zapret GUI", MB_OK | MB_ICONWARNING);
            }
        }
    } else if (rc == 1002) {
        wchar_t dir[MAX_PATH];
        ui_log(ZLOG_INFO, L"Повторный автопоиск папки zapret…");
        if (zg_autosearch_dir(dir, MAX_PATH)) {
            apply_zapret_dir(hwnd, dir);
        } else {
            ui_log(ZLOG_ERR, L"Папка zapret не найдена. Проверены: %s",
                   zg_search_report());
            MessageBoxW(hwnd,
                L"Автоматический поиск не нашёл папку zapret.\n\n"
                L"Нажмите «ВЫБРАТЬ ПАПКУ ZAPRET» и укажите папку,\n"
                L"в которую распакован zapret (внутри — bin\\, lists\\,\n"
                L"файлы general*.bat).",
                L"Zapret GUI", MB_OK | MB_ICONWARNING);
        }
    }
    /* rc == 1003 or closed: keep running without zapret files */
}

/* ================================================================== */
/* window procedure                                                    */
/* ================================================================== */

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {

    case WM_CREATE: {
        g_hMain = hwnd;

        /* header link */
        g_hBtnCheckUpd = zg_button_create(hwnd, IDC_BTN_CHECKUPD,
                                          L"Проверить обновления", ZGBTN_LINK, 0);
        /* log clear link */
        g_hBtnLogClear = zg_button_create(hwnd, IDC_BTN_LOGCLEAR,
                                          L"Очистить", ZGBTN_LINK, 0);
        /* primary */
        g_hPrimary = zg_button_create(hwnd, IDC_BTN_PRIMARY,
                                      L"ВКЛЮЧИТЬ ОБХОД", ZGBTN_PRIMARY, ZGBP_GREEN);
        /* footer */
        g_hBtnDiag  = zg_button_create(hwnd, IDC_BTN_DIAG,  L"Диагностика",   ZGBTN_FLAT, 0);
        g_hBtnHosts = zg_button_create(hwnd, IDC_BTN_HOSTS, L"Обновить hosts", ZGBTN_FLAT, 0);
        g_hBtnIpset = zg_button_create(hwnd, IDC_BTN_IPSETUPD, L"Обновить IPSet", ZGBTN_FLAT, 0);
        g_hBtnTests = zg_button_create(hwnd, IDC_BTN_TESTS, L"Тесты",         ZGBTN_FLAT, 0);

        /* combos */
        g_hComboStrat = zg_combo_create(hwnd, IDC_COMBO_STRAT,
                                        (const wchar_t* const*)g_strats, g_stratCount,
                                        g_stratCount > 0 ? 0 : -1);
        g_hComboGame = zg_combo_create(hwnd, IDC_COMBO_GAME,
                                        GAME_LABELS, 4, 0);
        g_hComboIpset = zg_combo_create(hwnd, IDC_COMBO_IPSET,
                                        IPSET_LABELS, 3, 0);

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
            draw_section_header(mem, L"НАСТРОЙКИ",
                                SC(DU_PAD), SC(DU_SET_HDR_Y), SC(DU_CONTENT_W));
            paint_settings_panel_text(mem);
            draw_section_header(mem, L"ЖУРНАЛ СОБЫТИЙ",
                                SC(DU_PAD), SC(DU_LOG_HDR_Y), SC(DU_CONTENT_W));
        }

        BitBlt(hdc, 0, 0, cw, ch, mem, 0, 0, SRCCOPY);
        SelectObject(mem, obmp);
        DeleteObject(bmp);
        DeleteDC(mem);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_TIMER:
        if (wp == ZG_TIMER_STATUS) {
            zg_status_refresh(&g_st);
            refresh_primary_button();
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
        break;

    case WM_ACTIVATE:
        if (wp == WA_INACTIVE) {
            zg_combo_close_popup(g_hComboStrat);
            zg_combo_close_popup(g_hComboGame);
            zg_combo_close_popup(g_hComboIpset);
        } else {
            refresh_status(TRUE);
        }
        return 0;

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
                ui_log(ZLOG_INFO, L"Останавливаю обход…");
                zg_stop_bypass(&stopped_svc);
                ui_log(ZLOG_OK, stopped_svc
                       ? L"Служба zapret остановлена, обход выключен"
                       : L"Обход выключен (winws.exe остановлен)");
            } else {
                int idx = zg_combo_get_sel(g_hComboStrat);
                if (idx < 0 || idx >= g_stratCount) {
                    ui_log(ZLOG_ERR, L"Не выбрана стратегия обхода");
                    break;
                }
                wchar_t err[256];
                ui_log(ZLOG_INFO, L"Запускаю стратегию: %s", g_strats[idx]);
                if (zg_launch_bypass(g_strats[idx], err, 256)) {
                    Sleep(400);
                    zg_status_refresh(&g_st);
                    if (g_st.winws_running)
                        ui_log(ZLOG_OK, L"Обход запущен (winws.exe работает)");
                    else
                        ui_log(ZLOG_ERR, L"winws.exe не запустился — проверьте стратегию");
                } else {
                    ui_log(ZLOG_ERR, L"%s", err);
                }
            }
            refresh_status(TRUE);
            break;
        }
        case IDC_BTN_DIAG:
            ui_log(ZLOG_INFO, L"Открываю диагностику (консоль)…");
            zg_open_console_bat(L"diag");
            break;
        case IDC_BTN_TESTS:
            ui_log(ZLOG_INFO, L"Запускаю тесты стратегий (PowerShell)…");
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
                    ui_log(ZLOG_ERR, L"Не выбрана стратегия для службы");
                    zg_toggle_set(g_hTglSvc, FALSE, FALSE);
                    break;
                }
                ui_log(ZLOG_INFO, L"Устанавливаю службу zapret (%s)…", g_strats[idx]);
                run_op(OP_SVC_INSTALL, g_strats[idx]);
            } else {
                ui_log(ZLOG_INFO, L"Удаляю службы zapret / WinDivert…");
                run_op(OP_SVC_REMOVE, NULL);
            }
        } else if (id == IDC_TOGGLE_UPD) {
            if (zg_checkupdates_set(on))
                ui_log(ZLOG_OK, on ? L"Автопроверка обновлений включена"
                                   : L"Автопроверка обновлений выключена");
            else
                ui_log(ZLOG_ERR, L"Не удалось изменить настройку автообновлений");
        }
        return 0;
    }

    case WM_ZGCOMBO: {
        int id = (int)wp;
        int idx = (int)lp;
        if (id == IDC_COMBO_STRAT) {
            if (idx >= 0 && idx < g_stratCount) {
                settings_save(g_strats[idx]);
                ui_log(ZLOG_INFO, L"Стратегия выбрана: %s", g_strats[idx]);
                if (g_st.winws_running)
                    ui_log(ZLOG_WARN, L"Для применения перезапустите обход");
            }
        } else if (id == IDC_COMBO_GAME) {
            if (idx >= 0 && idx < 4) {
                if (zg_gamefilter_set(GAME_MODES[idx]))
                    ui_log(ZLOG_OK, L"Игровой фильтр: %s", GAME_LABELS[idx]);
                else
                    ui_log(ZLOG_ERR, L"Не удалось изменить игровой фильтр");
                ui_log(ZLOG_WARN, L"Перезапустите обход, чтобы применить изменения");
            }
        } else if (id == IDC_COMBO_IPSET) {
            if (idx >= 0 && idx < 3) {
                if (zg_ipset_set(IPSET_MODES[idx])) {
                    ui_log(ZLOG_OK, L"Фильтр IPSet: %s", IPSET_LABELS[idx]);
                } else {
                    ui_log(ZLOG_ERR,
                        L"Не удалось переключить IPSet: нет резервной копии списка. "
                        L"Нажмите «Обновить IPSet»");
                    /* revert combo to actual state */
                    int im = zg_ipset_get(), ri = 0;
                    for (int i = 0; i < 3; i++) if (IPSET_MODES[i] == im) ri = i;
                    zg_combo_set_sel(g_hComboIpset, ri, FALSE);
                }
                ui_log(ZLOG_WARN, L"Перезапустите обход, чтобы применить изменения");
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
        return 0;
    }

    case WM_DESTROY: {
        KillTimer(hwnd, ZG_TIMER_STATUS);
        if (g_st.winws_running && g_st.own_child_alive)
            ui_log(ZLOG_INFO, L"GUI закрыт — обход продолжает работать");
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ================================================================== */
/* WinMain                                                             */
/* ================================================================== */

static void enable_dark_titlebar(HWND hwnd)
{
    typedef HRESULT (WINAPI *FN_SetAttr)(HWND, DWORD, LPCVOID, DWORD);
    HMODULE hDwm = LoadLibraryW(L"dwmapi.dll");
    if (!hDwm) return;
    FN_SetAttr f = (FN_SetAttr)(void*)GetProcAddress(hDwm, "DwmSetWindowAttribute");
    if (!f) { FreeLibrary(hDwm); return; }

    BOOL dark = TRUE;
    /* DWMWA_USE_IMMERSIVE_DARK_MODE: 20 on Win10 2004+, 19 on older builds */
    if (FAILED(f(hwnd, 20, &dark, sizeof(dark))))
        f(hwnd, 19, &dark, sizeof(dark));

    /* rounded corners on Win11 */
    int pref = 2;  /* DWMWCP_ROUND */
    f(hwnd, 33, &pref, sizeof(pref));   /* DWMWA_WINDOW_CORNER_PREFERENCE */

    /* dark window border on Win11 (DWMWA_BORDER_COLOR = 34) */
    COLORREF border = RGB(0x3E, 0x3E, 0x3E);
    f(hwnd, 34, &border, sizeof(border));

    FreeLibrary(hDwm);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, PWSTR cmd, int show)
{
    (void)hPrev; (void)cmd;

    /* COM for the IFileDialog folder picker */
    HRESULT com_hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

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

    /* pick remembered strategy */
    wchar_t saved[MAX_PATH];
    settings_load(saved, MAX_PATH);
    int def_idx = 0;
    if (saved[0]) {
        for (int i = 0; i < g_stratCount; i++)
            if (_wcsicmp(g_strats[i], saved) == 0) { def_idx = i; break; }
    }

    /* window slightly larger than the 561x745 reference */
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT wr = { 0, 0, SC(DU_WIN_W), SC(DU_WIN_H) };
    AdjustWindowRect(&wr, style, FALSE);

    HWND hwnd = CreateWindowExW(0, ZG_WND_CLASS,
                                L"Zapret GUI — Discord и YouTube",
                                style,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                wr.right - wr.left, wr.bottom - wr.top,
                                NULL, NULL, hInst, NULL);
    if (!hwnd) return 1;

    /* apply remembered strategy selection */
    zg_combo_set_sel(g_hComboStrat, g_stratCount > 0 ? def_idx : -1, FALSE);

    enable_dark_titlebar(hwnd);

    zg_status_refresh(&g_st);
    refresh_status(TRUE);
    set_controls_busy(FALSE);

    /* show the window BEFORE the "files not found" dialog so the user
     * sees the GUI (and its log) behind it                          */
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    ui_log(ZLOG_INFO, L"Zapret GUI %s запущен (exe: %s)",
           ZG_GUI_VERSION, g_paths.real_exe_dir);
    if (!g_paths.files_ok) {
        ui_log(ZLOG_ERR, L"Рядом с ZapretGUI.exe нет bin\\winws.exe — папка zapret не найдена");
        ui_log(ZLOG_INFO, L"Автопоиск проверил: %s", zg_search_report());
        fix_zapret_folder_flow(hwnd);
    }
    else {
        if (g_paths.detect == ZG_DET_CONFIG)
            ui_log(ZLOG_INFO, L"Используется сохранённая папка zapret: %s",
                   g_paths.exe_dir);
        else if (g_paths.detect == ZG_DET_AUTO_PARENT)
            ui_log(ZLOG_OK, L"Папка zapret найдена автоматически: %s",
                   g_paths.exe_dir);
        ui_log(ZLOG_INFO, L"Версия zapret: %s · найдено стратегий: %d",
               g_st.local_version, g_stratCount);
        if (g_stratCount > 0)
            ui_log(ZLOG_INFO, L"Текущая стратегия: %s",
                   g_strats[zg_combo_get_sel(g_hComboStrat)]);
    }

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_strats) zg_free_strategies(g_strats, g_stratCount);
    theme_destroy_fonts();
    if (gdiplusToken) Gdiplus::GdiplusShutdown(gdiplusToken);
    if (SUCCEEDED(com_hr)) CoUninitialize();
    return (int)msg.wParam;
}
