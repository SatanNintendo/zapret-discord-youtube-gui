/*
 * Zapret GUI — uidraw.cpp
 * Chrome drawing primitives (see uidraw.h).
 *
 * Rendering notes:
 *  - LinearGradientBrush must use WrapModeClamp for overlay fades,
 *    otherwise the gradient TILES past its end points.
 *  - Shadows are layered rounded strokes; the body painted afterwards
 *    covers the centre, leaving a soft fringe.
 *  - Every function receives the Gdiplus::Graphics of a scoped phase-A
 *    block — no GDI calls may happen inside.
 */
#include <windows.h>
#include <gdiplus.h>
#include "theme.h"
#include "controls.h"
#include "uidraw.h"

using namespace Gdiplus;

Gdiplus::GraphicsPath* zg_round_path(int x, int y, int w, int h, int r)
{
    Gdiplus::GraphicsPath* p = new Gdiplus::GraphicsPath();
    if (w <= 0 || h <= 0) { p->AddRectangle(Gdiplus::Rect(x, y, 1, 1)); return p; }
    if (r <= 0) { p->AddRectangle(Gdiplus::Rect(x, y, w, h)); return p; }
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    int d = r * 2;
    p->AddArc(x,     y,     d, d, 180, 90);
    p->AddArc(x+w-d, y,     d, d, 270, 90);
    p->AddArc(x+w-d, y+h-d, d, d,   0, 90);
    p->AddArc(x,     y+h-d, d, d,  90, 90);
    p->CloseFigure();
    return p;
}

void zg_draw_shadow(Gdiplus::Graphics& g, int x, int y, int w, int h,
                    int r, int depth, int alpha)
{
    if (depth < 1 || alpha <= 0 || w < 4 || h < 4) return;
    int per = alpha / depth;
    if (per <= 0) per = 1;
    for (int i = depth; i >= 1; i--) {
        Gdiplus::GraphicsPath* p = zg_round_path(x - i, y - 1,
                                                w + 2*i, h + i + 1, r + i);
        Gdiplus::SolidBrush b(Gdiplus::Color(zg_argb(per, g_th.shadow)));
        g.FillPath(&b, p);
        delete p;
    }
}

void zg_draw_chrome_panel(Gdiplus::Graphics& g, int x, int y, int w, int h,
                          int r, COLORREF top, COLORREF bot,
                          COLORREF border, int gloss_alpha)
{
    if (w < 2 || h < 2) return;
    Gdiplus::GraphicsPath* p = zg_round_path(x, y, w, h, r);

    /* vertical gradient body */
    Gdiplus::LinearGradientBrush br(Gdiplus::Point(x, y), Gdiplus::Point(x, y + h),
                                    Gdiplus::Color(zg_argb(255, top)),
                                    Gdiplus::Color(zg_argb(255, bot)));
    g.FillPath(&br, p);

    /* specular gloss over the upper half */
    if (gloss_alpha > 0) {
        int gh = h * 55 / 100;
        Gdiplus::LinearGradientBrush gl(Gdiplus::Point(x, y), Gdiplus::Point(x, y + gh),
                                        Gdiplus::Color(gloss_alpha, 255, 255, 255),
                                        Gdiplus::Color(0, 255, 255, 255));
        gl.SetWrapMode(Gdiplus::WrapModeClamp);
        g.FillPath(&gl, p);
    }

    /* light inner bevel (top), then outer border */
    Gdiplus::GraphicsPath* inset = zg_round_path(x + 1, y + 1, w - 2, h - 2,
                                                r > 1 ? r - 1 : 1);
    Gdiplus::Pen light(Gdiplus::Color(g_th.is_dark ? 55 : 130, 255, 255, 255), 1.0f);
    g.DrawPath(&light, inset);
    delete inset;

    Gdiplus::Pen pen(Gdiplus::Color(zg_argb(255, border)), 1.0f);
    g.DrawPath(&pen, p);
    delete p;
}

void zg_ctrl_body(int x, int y, int w, int h, RECT* out)
{
    int pad = SC(DU_SHADOW);
    if (w < 2 * pad + 12 || h < 2 * pad + 8) pad = 1;
    out->left   = x + pad;
    out->top    = y + pad;
    out->right  = x + w - pad;
    out->bottom = y + h - pad;
}

void zg_draw_button(Gdiplus::Graphics& g, int x, int y, int w, int h,
                    UINT style, int scheme, int state)
{
    if (style == ZGBTN_LINK) return;   /* link = text only, no chrome */

    RECT b;
    zg_ctrl_body(x, y, w, h, &b);
    int bx = b.left, by = b.top, bw = b.right - b.left, bh = b.bottom - b.top;
    if (bw < 4 || bh < 4) return;

    BOOL pressed  = (state == ZGST_PRESSED);
    BOOL hover    = (state == ZGST_HOVER);
    BOOL disabled = (state == ZGST_DISABLED);

    int radius = (style == ZGBTN_PRIMARY) ? bh / 2 : SC(9);
    if (radius * 2 > bw) radius = bw / 2;

    /* --- soft drop shadow (tight & faint when pressed) --- */
    if (!disabled)
        zg_draw_shadow(g, bx, by, bw, bh, radius,
                       pressed ? 2 : SC(DU_SHADOW),
                       pressed ? 45 : (style == ZGBTN_PRIMARY ? 95 : 60));

    Gdiplus::GraphicsPath* body = zg_round_path(bx, by, bw, bh, radius);

    if (style == ZGBTN_PRIMARY) {

        /* base: horizontal scheme gradient */
        Gdiplus::Color c1, c2;
        if (scheme == ZGBP_GREEN) {
            if (pressed)       { c1 = Gdiplus::Color(255, 0x1E, 0x7A, 0x2E); c2 = Gdiplus::Color(255, 0x24, 0x8F, 0x37); }
            else if (hover)     { c1 = Gdiplus::Color(255, 0x2F, 0xB0, 0x44); c2 = Gdiplus::Color(255, 0x39, 0xC4, 0x4F); }
            else                { c1 = Gdiplus::Color(255, 0x2A, 0xA0, 0x3A); c2 = Gdiplus::Color(255, 0x32, 0xC0, 0x50); }
        } else {
            if (pressed)       { c1 = Gdiplus::Color(255, 0x5E, 0x4E, 0xC0); c2 = Gdiplus::Color(255, 0xD8, 0x50, 0x38); }
            else if (hover)     { c1 = Gdiplus::Color(255, 0x8F, 0x7C, 0xFF); c2 = Gdiplus::Color(255, 0xFF, 0x7E, 0x5E); }
            else                { c1 = Gdiplus::Color(255, 0x7B, 0x68, 0xEE); c2 = Gdiplus::Color(255, 0xFF, 0x63, 0x47); }
        }
        Gdiplus::LinearGradientBrush base(Gdiplus::Point(bx, by),
                                          Gdiplus::Point(bx + bw, by), c1, c2);
        g.FillPath(&base, body);

        /* bottom shade — spherical volume */
        {
            Gdiplus::LinearGradientBrush sh(Gdiplus::Point(bx, by + bh / 2),
                                            Gdiplus::Point(bx, by + bh),
                                            Gdiplus::Color(0, 0, 0, 0),
                                            Gdiplus::Color(70, 0, 0, 0));
            sh.SetWrapMode(Gdiplus::WrapModeClamp);
            g.FillPath(&sh, body);
        }

        /* top gloss — specular highlight */
        {
            Gdiplus::LinearGradientBrush gl(Gdiplus::Point(bx, by),
                                            Gdiplus::Point(bx, by + bh * 55 / 100),
                                            Gdiplus::Color(pressed ? 35 : 115, 255, 255, 255),
                                            Gdiplus::Color(0, 255, 255, 255));
            gl.SetWrapMode(Gdiplus::WrapModeClamp);
            g.FillPath(&gl, body);
        }

        /* pressed inner shadow at the top edge */
        if (pressed) {
            Gdiplus::LinearGradientBrush ish(Gdiplus::Point(bx, by),
                                             Gdiplus::Point(bx, by + bh * 40 / 100),
                                             Gdiplus::Color(85, 0, 0, 0),
                                             Gdiplus::Color(0, 0, 0, 0));
            ish.SetWrapMode(Gdiplus::WrapModeClamp);
            g.FillPath(&ish, body);
        }

        /* chrome rim: bright inner stroke + dark outer line */
        {
            Gdiplus::GraphicsPath* inset = zg_round_path(bx + 1, by + 1,
                                                         bw - 2, bh - 2,
                                                         radius > 1 ? radius - 1 : 1);
            Gdiplus::Pen light(Gdiplus::Color(145, 255, 255, 255), 1.0f);
            g.DrawPath(&light, inset);
            delete inset;
            Gdiplus::Pen dark(Gdiplus::Color(165, 0, 0, 0), 1.0f);
            g.DrawPath(&dark, body);
        }

        if (disabled) {
            Gdiplus::SolidBrush dim(Gdiplus::Color(150, 0x20, 0x20, 0x20));
            g.FillPath(&dim, body);
        }
    }
    else {
        /* FLAT — gunmetal / silver chrome bar */
        COLORREF top, bot;
        if (pressed)      { top = g_th.btn_top_p;  bot = g_th.btn_bot_p;  }
        else if (hover)   { top = g_th.btn_top_h;  bot = g_th.btn_bot_h;  }
        else              { top = g_th.btn_top;    bot = g_th.btn_bot;    }

        Gdiplus::LinearGradientBrush base(Gdiplus::Point(bx, by),
                                          Gdiplus::Point(bx, by + bh),
                                          Gdiplus::Color(zg_argb(255, top)),
                                          Gdiplus::Color(zg_argb(255, bot)));
        g.FillPath(&base, body);

        /* top gloss */
        {
            Gdiplus::LinearGradientBrush gl(Gdiplus::Point(bx, by),
                                            Gdiplus::Point(bx, by + bh * 50 / 100),
                                            Gdiplus::Color(pressed ? 0 : 55, 255, 255, 255),
                                            Gdiplus::Color(0, 255, 255, 255));
            gl.SetWrapMode(Gdiplus::WrapModeClamp);
            g.FillPath(&gl, body);
        }

        /* pressed inner shadow */
        if (pressed) {
            Gdiplus::LinearGradientBrush ish(Gdiplus::Point(bx, by),
                                             Gdiplus::Point(bx, by + bh * 45 / 100),
                                             Gdiplus::Color(75, 0, 0, 0),
                                             Gdiplus::Color(0, 0, 0, 0));
            ish.SetWrapMode(Gdiplus::WrapModeClamp);
            g.FillPath(&ish, body);
        }

        /* bevels */
        {
            Gdiplus::GraphicsPath* inset = zg_round_path(bx + 1, by + 1,
                                                         bw - 2, bh - 2,
                                                         radius > 1 ? radius - 1 : 1);
            Gdiplus::Pen light(Gdiplus::Color(g_th.is_dark ? 80 : 160, 255, 255, 255), 1.0f);
            g.DrawPath(&light, inset);
            delete inset;
            Gdiplus::Pen pen(Gdiplus::Color(zg_argb(disabled ? 120 : 230, g_th.border)), 1.0f);
            g.DrawPath(&pen, body);
        }

        if (disabled) {
            Gdiplus::SolidBrush dim(Gdiplus::Color(zg_argb(120, g_th.bg)));
            g.FillPath(&dim, body);
        }
    }

    delete body;
}
