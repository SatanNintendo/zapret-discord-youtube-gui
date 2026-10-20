/*
 * Zapret GUI — gfx.cpp
 * Software glass / carbon renderer (see gfx.h for the rationale).
 *
 * Shapes are rounded rectangles described by a signed distance field
 * (circles are the special case r == w/2 == h/2). From the SDF we get
 *   - anti-aliased coverage,
 *   - the distance to the edge  -> bevel profile,
 *   - the outward gradient      -> a surface normal for lighting.
 * A "convex button" is therefore a real lit dome: the normal tilts
 * outwards near the rim, a headlight from the upper left produces the
 * diffuse falloff, a tiny sky/ground environment map produces the clear
 * coat reflection, and a lens-shaped gloss cap fades out with C1
 * continuity (no visible edge). The carbon weave only modulates the
 * reflections, exactly like a clear-coated carbon-fibre panel.
 */
#include <windows.h>
#include <math.h>
#include <string.h>
#include "theme.h"
#include "gfx.h"

/* ------------------------------------------------------------------ */
/* tiny math kit                                                       */
/* ------------------------------------------------------------------ */
namespace {

struct V3 { float r, g, b; };

inline float clamp01(float x) { return x < 0.f ? 0.f : (x > 1.f ? 1.f : x); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float fmaxf_(float a, float b) { return a > b ? a : b; }
inline float fminf_(float a, float b) { return a < b ? a : b; }

/* C1 smoothstep and C2 smootherstep over [e0, e1] (e0 may be > e1) */
inline float sstep(float e0, float e1, float x)
{
    float t = clamp01((x - e0) / (e1 - e0));
    return t * t * (3.f - 2.f * t);
}
inline float sstep5(float e0, float e1, float x)
{
    float t = clamp01((x - e0) / (e1 - e0));
    return t * t * t * (t * (t * 6.f - 15.f) + 10.f);
}

inline V3 cv(COLORREF c) { V3 v = { (float)GetRValue(c), (float)GetGValue(c), (float)GetBValue(c) }; return v; }
inline V3 vmix(V3 a, V3 b, float t) { V3 v = { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t }; return v; }
inline V3 vmul(V3 a, float k) { V3 v = { a.r * k, a.g * k, a.b * k }; return v; }
inline V3 vadd(V3 a, V3 b) { V3 v = { a.r + b.r, a.g + b.g, a.b + b.b }; return v; }
inline V3 vgrey(float g) { V3 v = { g, g, g }; return v; }
const V3 WHITE = { 255.f, 255.f, 255.f };
const V3 BLACK = { 0.f, 0.f, 0.f };

/* interleaved gradient noise (Jimenez) — cheap, blue-ish, no pattern */
inline float ign(float x, float y)
{
    float f = 0.06711056f * x + 0.00583715f * y;
    f -= floorf(f);
    float g = 52.9829189f * f;
    return g - floorf(g);
}

inline V3 getpx(const ZgCanvas* c, int x, int y)
{
    unsigned int p = c->px[(size_t)y * c->w + x];
    V3 v = { (float)((p >> 16) & 255), (float)((p >> 8) & 255), (float)(p & 255) };
    return v;
}

/* store with triangular dithering (+-0.85 LSB) */
inline void putpx(ZgCanvas* c, int x, int y, V3 v)
{
    float n = (ign((float)x, (float)y) + ign((float)x + 5.588238f, (float)y + 11.37f)) - 1.f;
    float k = 0.5f + n * 0.85f;
    int r = (int)floorf(v.r + k), g = (int)floorf(v.g + k), b = (int)floorf(v.b + k);
    if (r < 0) r = 0; else if (r > 255) r = 255;
    if (g < 0) g = 0; else if (g > 255) g = 255;
    if (b < 0) b = 0; else if (b > 255) b = 255;
    c->px[(size_t)y * c->w + x] = ((unsigned)r << 16) | ((unsigned)g << 8) | (unsigned)b;
}

/* ---- rounded-rect signed distance field -------------------------- */
struct RR { float cx, cy, hw, hh, r; };

inline RR make_rr(float x, float y, float w, float h, float r)
{
    RR s;
    s.hw = w * 0.5f; s.hh = h * 0.5f;
    s.cx = x + s.hw; s.cy = y + s.hh;
    float mr = fminf_(s.hw, s.hh);
    s.r = r > mr ? mr : (r < 0.f ? 0.f : r);
    return s;
}

/* negative inside; gx/gy = unit gradient pointing OUTWARD */
inline float rr_sdf(const RR& s, float px, float py, float* gx, float* gy)
{
    float dx = px - s.cx, dy = py - s.cy;
    float sx = dx < 0.f ? -1.f : 1.f, sy = dy < 0.f ? -1.f : 1.f;
    float qx = fabsf(dx) - (s.hw - s.r), qy = fabsf(dy) - (s.hh - s.r);
    float mx = qx > 0.f ? qx : 0.f, my = qy > 0.f ? qy : 0.f;
    float len = sqrtf(mx * mx + my * my);
    float d = len + fminf_(fmaxf_(qx, qy), 0.f) - s.r;
    if (gx) {
        if (len > 1e-4f)      { *gx = sx * mx / len; *gy = sy * my / len; }
        else if (qx > qy)     { *gx = sx;  *gy = 0.f; }
        else                  { *gx = 0.f; *gy = sy; }
    }
    return d;
}

/* clip a float bbox to the canvas */
inline void clip_box(const ZgCanvas* c, float x0, float y0, float x1, float y1,
                     int* ox0, int* oy0, int* ox1, int* oy1)
{
    int a = (int)floorf(x0), b = (int)floorf(y0), cc = (int)ceilf(x1), d = (int)ceilf(y1);
    if (a < 0) a = 0;
    if (b < 0) b = 0;
    if (cc > c->w) cc = c->w;
    if (d > c->h)  d = c->h;
    *ox0 = a; *oy0 = b; *ox1 = cc; *oy1 = d;
}

/* headlight: upper-left, slightly in front */
struct Light { float lx, ly, lz, hx, hy, hz; };
Light make_light()
{
    Light l;
    float lx = -0.30f, ly = -0.62f, lz = 0.74f;
    float n = 1.f / sqrtf(lx * lx + ly * ly + lz * lz);
    l.lx = lx * n; l.ly = ly * n; l.lz = lz * n;
    float hx = l.lx, hy = l.ly, hz = l.lz + 1.f;          /* half vector, V = (0,0,1) */
    float hn = 1.f / sqrtf(hx * hx + hy * hy + hz * hz);
    l.hx = hx * hn; l.hy = hy * hn; l.hz = hz * hn;
    return l;
}
const Light LT = make_light();

/* carbon weave: alternating 45deg twill cells, returns -1..1 */
inline float weave(int x, int y, int P)
{
    int cx = x / P, cy = y / P;
    int lx = x - cx * P, ly = y - cy * P;
    int diag = ((cx + cy) & 1) ? (lx + ly) : (lx - ly + P - 1);
    float t = (float)(diag % P) / (float)(P - 1 > 0 ? P - 1 : 1);   /* 0..1 sawtooth */
    float tri = 1.f - fabsf(t * 2.f - 1.f);                          /* 0..1..0 */
    return tri * 2.f - 1.f;
}
inline int weave_period() { int p = SC(4); return p < 3 ? 3 : p; }

/* soft drop shadow of a rounded rect, drawn OUTSIDE the body only */
void drop_shadow(ZgCanvas* c, const RR& s, float dy, float sigma, float alpha, V3 col)
{
    if (alpha <= 0.f) return;
    float reach = sigma * 3.4f + dy + 2.f;
    int x0, y0, x1, y1;
    clip_box(c, s.cx - s.hw - reach, s.cy - s.hh - reach,
                s.cx + s.hw + reach, s.cy + s.hh + reach + dy, &x0, &y0, &x1, &y1);
    float k = 1.7f / sigma;       /* logistic ~ gaussian CDF */
    for (int py = y0; py < y1; py++) {
        for (int px = x0; px < x1; px++) {
            float fx = px + 0.5f, fy = py + 0.5f;
            float d  = rr_sdf(s, fx, fy, 0, 0);
            float cov = clamp01(0.5f - d);
            if (cov >= 1.f) continue;
            float ds = rr_sdf(s, fx, fy - dy, 0, 0);
            float a  = alpha / (1.f + expf(ds * k)) * (1.f - cov);
            if (a < 0.003f) continue;
            putpx(c, px, py, vmix(getpx(c, px, py), col, a));
        }
    }
}

/* ------------------------------------------------------------------ */
/* the glossy dome shader                                               */
/* ------------------------------------------------------------------ */
struct GP {
    V3    top, bot;        /* base colour, vertical                       */
    V3    left, right;     /* base colour, horizontal (when horiz)        */
    int   horiz;
    float bulge;           /* normal tilt at the rim (0.. ~1.1)           */
    float kd;              /* diffuse strength                            */
    float env;             /* sky reflection strength                     */
    float bounce;          /* ground bounce strength                      */
    float spec;            /* specular strength                           */
    float cap;             /* gloss cap alpha                             */
    float sheen;           /* diagonal sweep alpha                        */
    float rim;             /* rim light alpha                             */
    float outline;         /* dark outline alpha                          */
    float outline_col;     /* 0..255 outline colour                       */
    float wb;              /* bevel width in px                           */
    float shade;           /* 1 = convex, -1 = concave                    */
    float inner_shadow;    /* pressed top inner shadow alpha              */
    float carbon;          /* 0..1 weave visibility inside reflections    */
    float dim;             /* 0..1 disabled wash                          */
    V3    dim_col;
};

void glossy(ZgCanvas* c, const RR& s, const GP& p)
{
    int x0, y0, x1, y1;
    clip_box(c, s.cx - s.hw - 2, s.cy - s.hh - 2, s.cx + s.hw + 2, s.cy + s.hh + 2, &x0, &y0, &x1, &y1);
    float w = s.hw * 2.f, h = s.hh * 2.f;
    float left = s.cx - s.hw, top = s.cy - s.hh;
    int   P = weave_period();
    float inv_wb = 1.f / (p.wb > 1.f ? p.wb : 1.f);

    for (int py = y0; py < y1; py++) {
        for (int px = x0; px < x1; px++) {
            float fx = px + 0.5f, fy = py + 0.5f;
            float gx, gy;
            float d = rr_sdf(s, fx, fy, &gx, &gy);
            float cov = clamp01(0.5f - d);
            if (cov <= 0.f) continue;

            float depth = -d;                             /* >= -0.5 */
            float t  = clamp01(depth * inv_wb);           /* 0 rim .. 1 plateau */
            float vy = clamp01((fy - top) / h);
            float ux = clamp01((fx - left) / w);

            /* ---- base colour ---- */
            V3 base;
            if (p.horiz) {
                base = vmix(p.left, p.right, sstep(0.f, 1.f, ux));
                float vs = lerpf(1.10f, 0.84f, sstep(0.f, 1.f, vy));
                base = vmul(base, vs);
            } else {
                base = vmix(p.top, p.bot, sstep(0.f, 1.f, vy));
            }

            /* ---- dome normal ---- */
            float u1 = 1.f - t;
            float m  = p.bulge * u1 * sqrtf(u1) * p.shade;     /* (1-t)^1.5 */
            float nx = gx * m, ny = gy * m, nz = 1.f;
            float inv = 1.f / sqrtf(nx * nx + ny * ny + 1.f);
            nx *= inv; ny *= inv; nz *= inv;

            /* diffuse: plateau == 1, lit rim brighter, shadow rim darker */
            float lf = (nx * LT.lx + ny * LT.ly + nz * LT.lz) / LT.lz;
            V3 col = vmul(base, 1.f + p.kd * (lf - 1.f));

            /* sky reflection (clear coat): R.y = 2 nz ny, up == negative y */
            float ry = 2.f * nz * ny;
            float sky = sstep5(0.12f, 0.92f, -ry);
            float gnd = sstep5(0.30f, 0.95f,  ry);
            float wv  = 1.f;
            float wvv = weave(px, py, P);
            wv = 1.f + p.carbon * 0.16f * wvv;                /* texture only in reflections */
            col = vadd(col, vgrey(sky * p.env * wv));
            col = vadd(col, vmul(vmix(base, WHITE, 0.45f), gnd * p.bounce / 255.f));

            /* specular glint */
            float nh = nx * LT.hx + ny * LT.hy + nz * LT.hz;
            if (nh > 0.f) {
                float s2 = nh * nh, s4 = s2 * s2, s8 = s4 * s4, s16 = s8 * s8;
                col = vadd(col, vgrey(s16 * s8 * p.spec * wv));    /* ^24 */
            }

            /* ---- lens-shaped gloss cap: smooth in both directions ---- */
            if (p.cap > 0.f) {
                float bell = 1.f - (2.f * ux - 1.f) * (2.f * ux - 1.f);   /* 0 at ends, 1 mid */
                float ty = vy - 0.13f * bell;                             /* cap reaches lower in the middle */
                float cap = 1.f - sstep5(0.0f, 0.66f, ty);
                cap *= cap;                                               /* softer tail */
                cap *= sstep5(0.f, 3.5f, depth);                          /* tucked inside the rim */
                float a = p.cap * cap * (1.f + p.carbon * 0.10f * wvv);
                col = vmix(col, WHITE, clamp01(a));
            }

            /* ---- diagonal sweep sheen ---- */
            if (p.sheen > 0.f) {
                float ud = (ux + 0.55f * vy) / 1.55f;
                float b1 = (ud - 0.22f) / 0.17f; b1 = 1.f / (1.f + b1 * b1); b1 *= b1;
                float b2 = (ud - 0.52f) / 0.045f; b2 = 1.f / (1.f + b2 * b2); b2 *= b2;
                float a = p.sheen * (b1 + 0.30f * b2) * (1.f - 0.45f * vy)
                        * sstep5(0.f, 3.f, depth) * (1.f + p.carbon * 0.14f * wvv);
                col = vmix(col, WHITE, clamp01(a));
            }

            /* ---- pressed: inner shadow falling from the top edge ---- */
            if (p.inner_shadow > 0.f) {
                float a = p.inner_shadow * (1.f - sstep5(0.f, 0.55f, vy)) ;
                col = vmix(col, BLACK, a);
            }

            /* ---- rim light (direction dependent) + dark outline ---- */
            float lit = clamp01(gx * (-0.62f) + gy * (-0.78f));
            float dirw = 0.20f + 0.80f * lit;
            float ra = p.rim * dirw * (1.f - sstep(1.0f, 2.6f, depth)) * sstep(0.2f, 1.0f, depth);
            col = vmix(col, WHITE, ra);
            float oa = p.outline * (1.f - sstep(0.55f, 1.25f, depth));
            col = vmix(col, vgrey(p.outline_col), oa);

            if (p.dim > 0.f) col = vmix(col, p.dim_col, p.dim);

            V3 D = getpx(c, px, py);
            putpx(c, px, py, vmix(D, col, cov));
        }
    }
}

inline float S(float v) { return v * (float)g_scale_pct / 100.f; }

} /* namespace */

/* ================================================================== */
/* canvas                                                              */
/* ================================================================== */

BOOL zg_canvas_create(ZgCanvas* c, int w, int h)
{
    memset(c, 0, sizeof(*c));
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;              /* top-down */
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    c->bmp = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!c->bmp || !bits) { if (c->bmp) DeleteObject(c->bmp); memset(c, 0, sizeof(*c)); return FALSE; }
    c->dc = CreateCompatibleDC(NULL);
    if (!c->dc) { DeleteObject(c->bmp); memset(c, 0, sizeof(*c)); return FALSE; }
    c->old = (HBITMAP)SelectObject(c->dc, c->bmp);
    c->px = (unsigned int*)bits;
    c->w = w; c->h = h;
    return TRUE;
}

void zg_canvas_destroy(ZgCanvas* c)
{
    if (!c) return;
    if (c->dc) { SelectObject(c->dc, c->old); DeleteDC(c->dc); }
    if (c->bmp) DeleteObject(c->bmp);
    memset(c, 0, sizeof(*c));
}

void zg_canvas_copy_rect(ZgCanvas* dst, int dx, int dy, const ZgCanvas* src,
                         int sx, int sy, int w, int h)
{
    if (!dst || !src || !dst->px || !src->px) return;
    if (sx < 0) { dx -= sx; w += sx; sx = 0; }
    if (sy < 0) { dy -= sy; h += sy; sy = 0; }
    if (dx < 0) { sx -= dx; w += dx; dx = 0; }
    if (dy < 0) { sy -= dy; h += dy; dy = 0; }
    if (sx + w > src->w) w = src->w - sx;
    if (sy + h > src->h) h = src->h - sy;
    if (dx + w > dst->w) w = dst->w - dx;
    if (dy + h > dst->h) h = dst->h - dy;
    if (w <= 0 || h <= 0) return;
    for (int y = 0; y < h; y++)
        memcpy(dst->px + (size_t)(dy + y) * dst->w + dx,
               src->px + (size_t)(sy + y) * src->w + sx,
               (size_t)w * sizeof(unsigned int));
}

void zg_canvas_grab_dc(ZgCanvas* dst, int dx, int dy, HDC src, int sx, int sy, int w, int h)
{
    if (!dst || !dst->dc || !src) return;
    BitBlt(dst->dc, dx, dy, w, h, src, sx, sy, SRCCOPY);
    GdiFlush();
}

void zg_canvas_fill(ZgCanvas* c, COLORREF col)
{
    if (!c || !c->px) return;
    unsigned int v = ((unsigned)GetRValue(col) << 16) | ((unsigned)GetGValue(col) << 8) | GetBValue(col);
    for (size_t i = 0, n = (size_t)c->w * c->h; i < n; i++) c->px[i] = v;
}

/* ================================================================== */
/* backdrop registry                                                   */
/* ================================================================== */

static const wchar_t* PROP_BACKDROP = L"ZgBackdrop";

void zg_backdrop_attach(HWND wnd, ZgCanvas* c)
{
    if (c) SetPropW(wnd, PROP_BACKDROP, (HANDLE)c);
    else   RemovePropW(wnd, PROP_BACKDROP);
}

BOOL zg_backdrop_fill(HWND ctrl, ZgCanvas* dst)
{
    HWND parent = GetParent(ctrl);
    if (!parent || !dst) return FALSE;
    ZgCanvas* bd = (ZgCanvas*)GetPropW(parent, PROP_BACKDROP);
    if (!bd || !bd->px) return FALSE;
    RECT r; GetWindowRect(ctrl, &r);
    POINT p = { r.left, r.top };
    ScreenToClient(parent, &p);
    zg_canvas_copy_rect(dst, 0, 0, bd, p.x, p.y, dst->w, dst->h);
    return TRUE;
}

/* ================================================================== */
/* window background                                                   */
/* ================================================================== */

void zg_gfx_background(ZgCanvas* c)
{
    if (!c || !c->px) return;
    int W = c->w, H = c->h;
    V3 g0 = cv(g_th.bg_g0), g1 = cv(g_th.bg_g1);
    V3 ga = cv(g_th.glow_a), gb = cv(g_th.glow_b);
    float sa = g_th.glow_a_pct / 100.f, sb = g_th.glow_b_pct / 100.f;

    /* ambient light: a large cool/violet blob top-left, a smaller one low right */
    float ax = 0.12f * W, ay = -0.02f * H, ar = 0.92f * W;
    float bx = 1.02f * W, by = 0.60f * H, br = 0.78f * W;
    float ar2 = ar * ar, br2 = br * br;

    float vig = g_th.is_dark ? 0.30f : 0.07f;
    float wamp = g_th.carbon * 0.024f;                 /* levels */
    int   P = weave_period();
    float invH = 1.f / (float)(H > 1 ? H - 1 : 1);

    for (int y = 0; y < H; y++) {
        float t = y * invH;
        V3 rowbase = vmix(g0, g1, sstep(0.f, 1.f, t));
        for (int x = 0; x < W; x++) {
            V3 col = rowbase;

            float dxa = x - ax, dya = y - ay;
            float qa = (dxa * dxa + dya * dya) / ar2;
            if (qa < 1.f) { float k = 1.f - qa; col = vmix(col, ga, sa * k * k); }

            float dxb = x - bx, dyb = y - by;
            float qb = (dxb * dxb + dyb * dyb) / br2;
            if (qb < 1.f) { float k = 1.f - qb; col = vmix(col, gb, sb * k * k); }

            /* vignette */
            float vx = (x - W * 0.5f) / (W * 0.5f), vy = (y - H * 0.5f) / (H * 0.5f);
            float vq = (vx * vx + vy * vy) * 0.5f;
            col = vmul(col, 1.f - vig * vq * vq);

            if (wamp > 0.f) {
                float wv = weave(x, y, P) * wamp;
                col.r += wv; col.g += wv; col.b += wv;
            }
            putpx(c, x, y, col);
        }
    }
}

/* ================================================================== */
/* glass panels                                                        */
/* ================================================================== */

void zg_glass_style_card(ZgGlass* st)
{
    memset(st, 0, sizeof(*st));
    st->tint = g_th.glass;
    st->a_hi = g_th.glass_hi;  st->a_lo = g_th.glass_lo;
    st->rim = g_th.glass_rim;  st->sheen = g_th.glass_sheen;
    st->shadow_a = g_th.is_dark ? 120 : 52;
    st->shadow_sigma = (int)(S(5.f) + 0.5f);
    st->shadow_dy = (int)(S(3.f) + 0.5f);
}

void zg_glass_style_panel(ZgGlass* st)
{
    zg_glass_style_card(st);
    st->a_hi = g_th.glass_hi * 85 / 100;
    st->a_lo = g_th.glass_lo * 85 / 100;
    st->sheen = g_th.glass_sheen * 90 / 100;
    st->shadow_sigma = (int)(S(7.f) + 0.5f);
    st->shadow_dy = (int)(S(4.f) + 0.5f);
}

void zg_glass_style_field(ZgGlass* st)
{
    memset(st, 0, sizeof(*st));
    st->tint = g_th.bg_dark;
    st->a_hi = st->a_lo = 255;
    st->rim = g_th.glass_rim / 2;
    st->inset = 1;
}

void zg_glass_style_popup(ZgGlass* st)
{
    memset(st, 0, sizeof(*st));
    st->tint = g_th.card_top;
    st->a_hi = st->a_lo = 255;
    st->opaque = 1;
    st->rim = g_th.glass_rim;
    st->sheen = g_th.glass_sheen;
    st->shadow_a = g_th.is_dark ? 170 : 90;
    st->shadow_sigma = (int)(S(6.f) + 0.5f);
    st->shadow_dy = (int)(S(4.f) + 0.5f);
}

void zg_gfx_glass(ZgCanvas* c, int x, int y, int w, int h, int r, const ZgGlass* st)
{
    if (!c || w < 2 || h < 2) return;
    RR s = make_rr((float)x, (float)y, (float)w, (float)h, (float)r);

    if (st->shadow_a > 0)
        drop_shadow(c, s, (float)st->shadow_dy, (float)(st->shadow_sigma > 0 ? st->shadow_sigma : 1),
                    st->shadow_a / 255.f, cv(g_th.shadow));

    int x0, y0, x1, y1;
    clip_box(c, (float)x - 2, (float)y - 2, (float)(x + w + 2), (float)(y + h + 2), &x0, &y0, &x1, &y1);

    V3 tint  = cv(st->tint);
    V3 tint2 = st->opaque ? cv(g_th.card_bot) : tint;
    float ahi = st->a_hi / 255.f, alo = st->a_lo / 255.f;
    float rim = st->rim / 255.f, sheen = st->sheen / 255.f;
    V3 outl = cv(g_th.border_dim);
    float carbon = g_th.carbon / 100.f;
    int P = weave_period();
    float fw = (float)w, fh = (float)h;

    for (int py = y0; py < y1; py++) {
        for (int px = x0; px < x1; px++) {
            float fx = px + 0.5f, fy = py + 0.5f;
            float gx, gy;
            float d = rr_sdf(s, fx, fy, &gx, &gy);
            float cov = clamp01(0.5f - d);
            if (cov <= 0.f) continue;

            float depth = -d;
            float t  = clamp01((fy - y) / fh);
            float u  = clamp01((fx - x) / fw);
            float tv = sstep5(0.f, 1.f, t);
            V3 D = getpx(c, px, py);
            V3 col;

            if (!st->inset) {
                /* --- frosted tint, strongest at the top --- */
                float a = lerpf(ahi, alo, tv);
                if (st->opaque) {
                    col = vmix(tint, tint2, tv);
                } else {
                    a += 0.045f * (1.f - sstep(0.f, 7.f, depth));     /* edge thickness glow */
                    col = vmix(D, tint, clamp01(a));
                }

                /* --- bottom thickness shade --- */
                float bshade = 0.14f * (1.f - sstep5(0.f, S(10.f), (float)(y + h) - fy)) ;
                col = vmix(col, BLACK, bshade);

                /* --- soft corner light --- */
                float cdx = fx - (x + 0.12f * fw), cdy = fy - y;
                float cq = (cdx * cdx + cdy * cdy * 2.4f) / (0.62f * fw * 0.62f * fw);
                float cl = cq < 1.f ? (1.f - cq) * (1.f - cq) : 0.f;
                col = vmix(col, WHITE, clamp01(0.55f * sheen * cl));

                /* --- diagonal reflection with weave texture --- */
                if (sheen > 0.f) {
                    float ud = (u + 0.9f * (fy - y) / fw) / (1.f + 0.9f * fh / fw);
                    float b1 = (ud - 0.20f) / 0.14f; b1 = 1.f / (1.f + b1 * b1); b1 *= b1;
                    float b2 = (ud - 0.47f) / 0.035f; b2 = 1.f / (1.f + b2 * b2); b2 *= b2;
                    float a2 = sheen * (b1 + 0.30f * b2) * (1.f - 0.60f * t) * sstep5(0.f, 4.f, depth)
                             * (1.f + carbon * 0.16f * weave(px, py, P));
                    col = vmix(col, WHITE, clamp01(a2));
                }

                /* --- outline, then directional rim light --- */
                float oa = 0.75f * (1.f - sstep(0.55f, 1.2f, depth));
                col = vmix(col, outl, oa * (st->opaque ? 1.f : 0.9f));
                float lit = clamp01(gx * (-0.62f) + gy * (-0.78f));
                float ra = rim * (0.16f + 0.84f * lit) * sstep(0.7f, 1.2f, depth)
                         * (1.f - sstep(1.4f, 2.8f, depth));
                col = vmix(col, WHITE, ra);
            } else {
                /* --- sunken field: dark fill, inner shadow on top/left, light lip bottom --- */
                col = tint;
                float topw = clamp01(0.35f - 0.9f * gy - 0.25f * gx);
                float is = 0.55f * topw * (1.f - sstep5(0.f, S(7.f), depth));
                col = vmix(col, BLACK, is);
                float botw = clamp01(0.3f + 0.9f * gy + 0.2f * gx);
                float la = rim * 0.55f * botw * (1.f - sstep(0.7f, 2.0f, depth)) * sstep(0.2f, 0.9f, depth);
                col = vmix(col, WHITE, la);
                float oa = 0.55f * (1.f - sstep(0.55f, 1.25f, depth));
                col = vmix(col, outl, oa);
            }
            putpx(c, px, py, vmix(D, col, cov));
        }
    }
}

void zg_gfx_separator(ZgCanvas* c, int x0, int x1, int y)
{
    if (!c || x1 <= x0 || y < 0 || y + 1 >= c->h) return;
    float L = (float)(x1 - x0);
    for (int x = x0; x < x1 && x < c->w; x++) {
        if (x < 0) continue;
        float u = (x - x0) / L;
        float fade = sstep5(0.f, 0.14f, u) * sstep5(1.f, 0.86f, u);     /* soft ends */
        float dk = (g_th.is_dark ? 0.34f : 0.10f) * fade;
        float lt = (g_th.is_dark ? 0.07f : 0.65f) * fade;
        putpx(c, x, y,     vmix(getpx(c, x, y),     BLACK, dk));
        putpx(c, x, y + 1, vmix(getpx(c, x, y + 1), WHITE, lt));
    }
}

/* ================================================================== */
/* buttons / toggles                                                   */
/* ================================================================== */

void zg_gfx_button(ZgCanvas* c, int x, int y, int w, int h, int primary, int scheme, int state)
{
    if (!c || w < 4 || h < 4) return;
    int pressed = (state == ZGG_PRESSED), hover = (state == ZGG_HOVER), disabled = (state == ZGG_DISABLED);

    float rad = primary ? h * 0.5f : S(9.f);
    RR s = make_rr((float)x, (float)y, (float)w, (float)h, rad);

    if (!disabled)
        drop_shadow(c, s, pressed ? S(1.f) : S(2.f), S(2.4f),
                    pressed ? 0.30f : (primary ? 0.58f : 0.42f), cv(g_th.shadow));

    GP p;
    memset(&p, 0, sizeof(p));
    p.carbon = g_th.carbon / 100.f;
    p.outline_col = 0.f;
    p.shade = 1.f;
    p.dim_col = cv(g_th.bg_g1);

    if (primary) {
        if (scheme == 0) {                     /* green */
            if (pressed)    { p.top = vmul(cv(RGB(0x34,0xC4,0x58)), 0.80f); p.bot = vmul(cv(RGB(0x12,0x8A,0x2C)), 0.84f); }
            else if (hover) { p.top = cv(RGB(0x5A,0xE8,0x78)); p.bot = cv(RGB(0x1E,0xA6,0x3E)); }
            else            { p.top = cv(RGB(0x40,0xD6,0x64)); p.bot = cv(RGB(0x17,0x92,0x34)); }
        } else {                               /* violet -> coral */
            p.horiz = 1;
            if (pressed)    { p.left = cv(RGB(0x62,0x52,0xC8)); p.right = cv(RGB(0xD8,0x52,0x3C)); }
            else if (hover) { p.left = cv(RGB(0x92,0x80,0xFF)); p.right = cv(RGB(0xFF,0x7C,0x5C)); }
            else            { p.left = cv(RGB(0x7B,0x68,0xEE)); p.right = cv(RGB(0xFF,0x63,0x47)); }
        }
        p.bulge = pressed ? 0.80f : 1.02f;
        p.kd    = 0.46f;
        p.env   = pressed ? 22.f : 40.f;
        p.bounce = 20.f;
        p.spec  = pressed ? 40.f : 95.f;
        p.cap   = pressed ? 0.06f : 0.24f;
        p.sheen = pressed ? 0.03f : (hover ? 0.20f : 0.14f);
        p.rim   = pressed ? 0.30f : 0.62f;
        p.outline = 0.62f;
        p.wb    = h * 0.5f;
        p.inner_shadow = pressed ? 0.34f : 0.f;
    } else {
        COLORREF ct, cb;
        if (pressed)    { ct = g_th.btn_top_p; cb = g_th.btn_bot_p; }
        else if (hover) { ct = g_th.btn_top_h; cb = g_th.btn_bot_h; }
        else            { ct = g_th.btn_top;   cb = g_th.btn_bot;   }
        p.top = cv(ct); p.bot = cv(cb);
        p.bulge = pressed ? 0.55f : 0.78f;
        p.kd    = g_th.is_dark ? 0.40f : 0.30f;
        p.env   = pressed ? 8.f : (g_th.is_dark ? 26.f : 40.f);
        p.bounce = g_th.is_dark ? 14.f : 10.f;
        p.spec  = pressed ? 10.f : (g_th.is_dark ? 38.f : 50.f);
        p.cap   = pressed ? 0.f : (g_th.is_dark ? 0.12f : 0.34f);
        p.sheen = pressed ? 0.f : (hover ? 0.11f : 0.075f);
        p.rim   = pressed ? 0.25f : (g_th.is_dark ? 0.46f : 0.85f);
        p.outline = g_th.is_dark ? 0.78f : 0.42f;
        p.outline_col = g_th.is_dark ? 0.f : 90.f;
        p.wb    = fminf_(h * 0.5f, S(13.f));
        p.inner_shadow = pressed ? 0.30f : 0.f;
    }
    if (disabled) { p.dim = 0.52f; p.cap *= 0.4f; p.sheen *= 0.3f; p.spec *= 0.3f; }

    glossy(c, s, p);
}

void zg_gfx_toggle(ZgCanvas* c, int x, int y, int w, int h, int on, int hover, int disabled)
{
    if (!c || w < 8 || h < 8) return;
    RR tr = make_rr((float)x, (float)y, (float)w, (float)h, h * 0.5f);

    /* ---- concave glass track ---- */
    GP p;
    memset(&p, 0, sizeof(p));
    p.carbon = g_th.carbon / 100.f * 0.5f;
    p.shade = -1.f;
    p.bulge = 0.85f; p.kd = 0.50f;
    p.wb = h * 0.5f;
    p.rim = on ? 0.30f : 0.26f;
    p.outline = 0.70f; p.outline_col = g_th.is_dark ? 0.f : 80.f;
    p.env = 0.f; p.cap = 0.f; p.sheen = 0.f; p.spec = 0.f;
    p.bounce = 10.f;
    p.dim_col = cv(g_th.bg_g1);
    if (on) {
        V3 gtop = cv(g_th.green_hover), gbot = cv(g_th.green);
        if (hover && !disabled) { gtop = vadd(gtop, vgrey(12.f)); gbot = vadd(gbot, vgrey(6.f)); }
        p.top = gtop; p.bot = gbot;
    } else {
        V3 t0 = cv(g_th.toggle_off);
        p.top = vadd(t0, vgrey(hover && !disabled ? 18.f : 8.f));
        p.bot = vadd(t0, vgrey(-26.f));
    }
    if (disabled) p.dim = 0.50f;
    glossy(c, tr, p);

    /* ---- glass ball thumb ---- */
    float pad = S(3.f);
    float td  = (float)h - 2.f * pad;
    if (td < 4.f) td = 4.f;
    float tx = on ? (float)(x + w) - pad - td : (float)x + pad;
    float ty = (float)y + ((float)h - td) * 0.5f;
    RR th = make_rr(tx, ty, td, td, td * 0.5f);

    drop_shadow(c, th, S(1.6f), S(1.9f), g_th.is_dark ? 0.55f : 0.34f, cv(g_th.shadow));

    GP b;
    memset(&b, 0, sizeof(b));
    b.carbon = 0.f;
    b.shade = 1.f;
    b.top = cv(RGB(0xFF,0xFF,0xFF)); b.bot = cv(RGB(0xC4,0xCB,0xD6));
    b.bulge = 0.95f; b.kd = 0.30f;
    b.env = 40.f; b.bounce = 16.f; b.spec = 90.f;
    b.cap = 0.30f; b.sheen = 0.f;
    b.rim = 0.70f; b.outline = 0.40f; b.outline_col = 70.f;
    b.wb = td * 0.5f;
    b.dim_col = cv(g_th.bg_g1);
    if (disabled) b.dim = 0.45f;
    glossy(c, th, b);
}

/* ================================================================== */
/* small icons                                                         */
/* ================================================================== */

static float seg_dist(float px, float py, float ax, float ay, float bx, float by)
{
    float vx = bx - ax, vy = by - ay, wx = px - ax, wy = py - ay;
    float t = clamp01((wx * vx + wy * vy) / (vx * vx + vy * vy));
    float dx = wx - vx * t, dy = wy - vy * t;
    return sqrtf(dx * dx + dy * dy);
}

void zg_gfx_chevron(ZgCanvas* c, float cx, float cy, float size, COLORREF col, int alpha)
{
    if (!c) return;
    float th = fmaxf_(1.5f, size * 0.42f);
    float ax = cx - size, ay = cy - size * 0.45f;
    float mx = cx,        my = cy + size * 0.45f;
    float bx = cx + size, by = cy - size * 0.45f;
    int x0, y0, x1, y1;
    clip_box(c, cx - size - th, cy - size - th, cx + size + th, cy + size + th, &x0, &y0, &x1, &y1);
    V3 cc = cv(col);
    float a0 = alpha / 255.f;
    for (int py = y0; py < y1; py++)
        for (int px = x0; px < x1; px++) {
            float fx = px + 0.5f, fy = py + 0.5f;
            float d = fminf_(seg_dist(fx, fy, ax, ay, mx, my), seg_dist(fx, fy, mx, my, bx, by));
            float cov = clamp01(th * 0.5f + 0.5f - d) * a0;
            if (cov > 0.f) putpx(c, px, py, vmix(getpx(c, px, py), cc, cov));
        }
}

void zg_gfx_gear(ZgCanvas* c, float cx, float cy, float size, COLORREF col, int alpha)
{
    if (!c) return;
    float R = size * 0.5f;
    int x0, y0, x1, y1;
    clip_box(c, cx - R - 2, cy - R - 2, cx + R + 2, cy + R + 2, &x0, &y0, &x1, &y1);
    V3 cc = cv(col);
    float a0 = alpha / 255.f;
    for (int py = y0; py < y1; py++)
        for (int px = x0; px < x1; px++) {
            float dx = px + 0.5f - cx, dy = py + 0.5f - cy;
            float len = sqrtf(dx * dx + dy * dy);
            float ang = atan2f(dy, dx);
            /* 8 rounded teeth: smooth square wave */
            float tooth = 0.5f + 0.5f * tanhf(3.0f * cosf(ang * 8.f));
            float Ro = R * (0.76f + 0.24f * tooth);
            float cov = clamp01(0.5f - (len - Ro));
            cov *= clamp01(len - R * 0.30f + 0.5f);            /* centre hole */
            cov *= a0;
            if (cov > 0.f) putpx(c, px, py, vmix(getpx(c, px, py), cc, cov));
        }
}

void zg_gfx_dot(ZgCanvas* c, float cx, float cy, float r, COLORREF col, int glow)
{
    if (!c) return;
    if (glow > 0) {
        float R = r * 3.2f;
        int x0, y0, x1, y1;
        clip_box(c, cx - R, cy - R, cx + R, cy + R, &x0, &y0, &x1, &y1);
        V3 gc = cv(col);
        for (int py = y0; py < y1; py++)
            for (int px = x0; px < x1; px++) {
                float dx = px + 0.5f - cx, dy = py + 0.5f - cy;
                float q = sqrtf(dx * dx + dy * dy) / R;
                if (q >= 1.f) continue;
                float k = 1.f - q; k = k * k * k;
                float a = glow / 255.f * k;
                putpx(c, px, py, vmix(getpx(c, px, py), gc, a));
            }
    }
    RR s = make_rr(cx - r, cy - r, r * 2.f, r * 2.f, r);
    V3 base = cv(col);
    GP p;
    memset(&p, 0, sizeof(p));
    p.top = vmix(base, WHITE, 0.18f); p.bot = vmul(base, 0.78f);
    p.bulge = 1.0f; p.kd = 0.40f; p.env = 55.f; p.bounce = 12.f; p.spec = 120.f;
    p.cap = 0.30f; p.rim = 0.45f; p.outline = 0.30f; p.outline_col = 0.f;
    p.wb = r; p.shade = 1.f; p.dim_col = base;
    glossy(c, s, p);
}

void zg_gfx_icon_tile(ZgCanvas* c, int x, int y, int size)
{
    if (!c) return;
    RR s = make_rr((float)x, (float)y, (float)size, (float)size, size * 0.26f);
    drop_shadow(c, s, S(2.f), S(3.f), 0.50f, cv(g_th.shadow));
    GP p;
    memset(&p, 0, sizeof(p));
    p.top = cv(RGB(0x93,0x76,0xFA)); p.bot = cv(RGB(0x60,0x48,0xD0));
    p.bulge = 0.80f; p.kd = 0.40f; p.env = 34.f; p.bounce = 14.f; p.spec = 70.f;
    p.cap = 0.28f; p.sheen = 0.14f; p.rim = 0.55f; p.outline = 0.45f;
    p.carbon = g_th.carbon / 100.f;
    p.wb = size * 0.30f; p.shade = 1.f; p.dim_col = cv(g_th.bg_g1);
    glossy(c, s, p);
}

/* ================================================================== */
/* hero                                                                 */
/* ================================================================== */

void zg_gfx_hero(ZgCanvas* c, float cx, float cy, float r, int active)
{
    if (!c) return;
    float pw = fmaxf_(2.5f, S(3.f));                  /* ring thickness */

    /* ---- soft halo (active) — a smooth radial falloff, no stepped rings ---- */
    if (active) {
        float R = r + S(46.f);
        int x0, y0, x1, y1;
        clip_box(c, cx - R, cy - R, cx + R, cy + R, &x0, &y0, &x1, &y1);
        V3 gcol = cv(RGB(0x32, 0xE0, 0x4C));
        for (int py = y0; py < y1; py++)
            for (int px = x0; px < x1; px++) {
                float dx = px + 0.5f - cx, dy = py + 0.5f - cy;
                float dist = sqrtf(dx * dx + dy * dy);
                float q = (dist - r * 0.55f) / (R - r * 0.55f);
                if (q >= 1.f) continue;
                if (q < 0.f) q = 0.f;
                float k = 1.f - q; k = k * k * k;           /* long soft tail */
                putpx(c, px, py, vmix(getpx(c, px, py), gcol, 0.46f * k));
            }
    }

    /* ---- glass disc (dark sphere) ---- */
    RR disc = make_rr(cx - r, cy - r, r * 2.f, r * 2.f, r);
    drop_shadow(c, disc, S(3.f), S(5.f), g_th.is_dark ? 0.60f : 0.30f, cv(g_th.shadow));
    GP p;
    memset(&p, 0, sizeof(p));
    V3 dcol = cv(g_th.hero_disc);
    p.top = vmix(dcol, WHITE, g_th.is_dark ? 0.10f : 0.0f);
    p.bot = vmul(dcol, g_th.is_dark ? 0.85f : 0.90f);
    p.bulge = 0.70f; p.kd = 0.35f; p.env = g_th.is_dark ? 22.f : 14.f;
    p.bounce = 8.f; p.spec = 40.f; p.cap = g_th.is_dark ? 0.10f : 0.25f;
    p.rim = 0.f; p.outline = 0.f;
    p.wb = r; p.shade = 1.f; p.dim_col = dcol;
    p.carbon = g_th.carbon / 100.f;
    glossy(c, disc, p);

    /* ---- chrome ring ---- */
    float rin = r - pw * 0.15f, rout = r + pw * 0.85f;
    float mid = (rin + rout) * 0.5f, half = (rout - rin) * 0.5f;
    int x0, y0, x1, y1;
    clip_box(c, cx - rout - 2, cy - rout - 2, cx + rout + 2, cy + rout + 2, &x0, &y0, &x1, &y1);
    V3 ctop, cbot;
    if (active) { ctop = cv(RGB(0x8E, 0xF4, 0x8E)); cbot = cv(RGB(0x1B, 0x86, 0x2A)); }
    else        { ctop = cv(RGB(0xD6, 0xDB, 0xE2)); cbot = cv(RGB(0x3E, 0x44, 0x4C)); }
    for (int py = y0; py < y1; py++)
        for (int px = x0; px < x1; px++) {
            float dx = px + 0.5f - cx, dy = py + 0.5f - cy;
            float dist = sqrtf(dx * dx + dy * dy);
            float cov = clamp01(half + 0.5f - fabsf(dist - mid));
            if (cov <= 0.f) continue;
            float vy = clamp01((dy / rout) * 0.5f + 0.5f);
            V3 col = vmix(ctop, cbot, sstep5(0.12f, 0.92f, vy));
            /* specular band on the upper-left arc */
            float nxl = dx / (dist + 1e-4f), nyl = dy / (dist + 1e-4f);
            float lit = clamp01(nxl * -0.55f + nyl * -0.83f);
            lit = lit * lit * lit;
            col = vmix(col, WHITE, 0.55f * lit);
            /* darker inner edge for depth */
            float inner = 1.f - sstep(0.f, 1.2f, dist - rin);
            col = vmix(col, BLACK, 0.28f * inner);
            putpx(c, px, py, vmix(getpx(c, px, py), col, cov));
        }
}
