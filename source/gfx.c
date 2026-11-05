#include "gfx.h"
#include "applog.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static SDL_Renderer *g_ren;
static TTF_Font *g_font[F_COUNT];
/* 整体放大一档：电视 / 掌机距离比手机远，14/17px 的小字看久了很累 */
static const int g_size[F_COUNT] = { 37, 29, 23, 19, 16 };

/* ---------- 圆角矩形：四角遮罩缓存 ---------- */
#define MAX_R 40
static SDL_Texture *g_corner[MAX_R + 1];

static SDL_Texture *corner_tex(int radius)
{
    if (radius < 1) radius = 1;
    if (radius > MAX_R) radius = MAX_R;
    if (g_corner[radius]) return g_corner[radius];

    SDL_Texture *t = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_RGBA8888,
                                       SDL_TEXTUREACCESS_STATIC, radius, radius);
    if (!t) return NULL;
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);

    SDL_PixelFormat *fmt = SDL_AllocFormat(SDL_PIXELFORMAT_RGBA8888);
    Uint32 *px = (Uint32 *)malloc(sizeof(Uint32) * (size_t)radius * (size_t)radius);
    if (!px || !fmt) { free(px); if (fmt) SDL_FreeFormat(fmt); SDL_DestroyTexture(t); return NULL; }

    for (int y = 0; y < radius; y++) {
        for (int x = 0; x < radius; x++) {
            float dx = (float)radius - (x + 0.5f);
            float dy = (float)radius - (y + 0.5f);
            float d = sqrtf(dx * dx + dy * dy);
            float a = (float)radius - d;
            if (a > 1.0f) a = 1.0f;
            if (a < 0.0f) a = 0.0f;
            Uint8 al = (Uint8)(a * 255.0f);
            px[y * radius + x] = SDL_MapRGBA(fmt, 255, 255, 255, al);
        }
    }

    SDL_UpdateTexture(t, NULL, px, radius * (int)sizeof(Uint32));
    SDL_FreeFormat(fmt);
    free(px);
    g_corner[radius] = t;
    return t;
}

static void text_cache_clear(void);

int gfx_init(SDL_Renderer *ren, const char *font_path, int font_index)
{
    g_ren = ren;
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    if (TTF_Init() != 0) {
        applog("TTF_Init FAIL: %s", TTF_GetError());
        return -1;
    }
    for (int i = 0; i < F_COUNT; i++) {
        g_font[i] = TTF_OpenFontIndex(font_path, g_size[i], font_index);
        if (!g_font[i]) {
            applog("OpenFont size=%d FAIL: %s", g_size[i], TTF_GetError());
            fprintf(stderr, "font fail: %s (%s)\n", font_path, TTF_GetError());
            return -1;
        }
        TTF_SetFontHinting(g_font[i], TTF_HINTING_LIGHT);
    }
    applog("font loaded: %s", font_path);
    return 0;
}

void gfx_shutdown(void)
{
    text_cache_clear();
    for (int i = 0; i < F_COUNT; i++) {
        if (g_font[i]) TTF_CloseFont(g_font[i]);
        g_font[i] = NULL;
    }
    for (int i = 0; i <= MAX_R; i++) {
        if (g_corner[i]) SDL_DestroyTexture(g_corner[i]);
        g_corner[i] = NULL;
    }
    TTF_Quit();
}

void gfx_fill(SDL_Rect r, SDL_Color c)
{
    SDL_SetRenderDrawColor(g_ren, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(g_ren, &r);
}

/* HAModalBottomSheet 的 scrim：临时打开混合，画完关掉，不影响其它实心绘制 */
void gfx_dim(SDL_Rect r, int alpha)
{
    SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(g_ren, 0, 0, 0, (Uint8)alpha);
    SDL_RenderFillRect(g_ren, &r);
    SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_NONE);
}

/* HA 品牌图标：蓝色圆角方 + 白色「屋顶 / 圆点 / 地面」简笔。
 * dark=1 时反色（用于深色品牌底上：白底蓝图形） */
void gfx_ha_logo(SDL_Rect box, int dark)
{
    SDL_Color bg = dark ? (SDL_Color){ 255, 255, 255, 255 } : (SDL_Color){ HA_BLUE };
    SDL_Color fg = dark ? (SDL_Color){ HA_BLUE } : (SDL_Color){ 255, 255, 255, 255 };

    gfx_round(box, box.w / 5, bg);

    int cx = box.x + box.w / 2;
    int cy = box.y + box.h / 2;
    int u  = box.w / 10;
    if (u < 2) u = 2;

    SDL_SetRenderDrawColor(g_ren, fg.r, fg.g, fg.b, fg.a);
    for (int t = -1; t <= 1; t++) {
        SDL_RenderDrawLine(g_ren, cx - 3 * u, cy + u + t, cx, cy - 2 * u + t);
        SDL_RenderDrawLine(g_ren, cx, cy - 2 * u + t, cx + 3 * u, cy + u + t);
        SDL_RenderDrawLine(g_ren, cx - 2 * u, cy + 3 * u + t, cx + 2 * u, cy + 3 * u + t);
    }
    gfx_circle(cx, cy + u / 2, u, fg);
}

void gfx_circle(int cx, int cy, int radius, SDL_Color c)
{
    SDL_SetRenderDrawColor(g_ren, c.r, c.g, c.b, c.a);
    for (int dy = -radius; dy <= radius; dy++) {
        int w = (int)sqrtf((float)(radius * radius - dy * dy));
        SDL_RenderDrawLine(g_ren, cx - w, cy + dy, cx + w, cy + dy);
    }
}

void gfx_round(SDL_Rect r, int radius, SDL_Color c)
{
    if (radius * 2 > r.w) radius = r.w / 2;
    if (radius * 2 > r.h) radius = r.h / 2;
    if (radius < 1) { gfx_fill(r, c); return; }

    SDL_Texture *t = corner_tex(radius);
    SDL_SetTextureColorMod(t, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(t, c.a);

    SDL_Rect band;

    band.x = r.x + radius; band.y = r.y;
    band.w = r.w - radius * 2; band.h = r.h;
    gfx_fill(band, c);

    band.x = r.x; band.y = r.y + radius;
    band.w = radius; band.h = r.h - radius * 2;
    gfx_fill(band, c);

    band.x = r.x + r.w - radius; band.y = r.y + radius;
    band.w = radius; band.h = r.h - radius * 2;
    gfx_fill(band, c);

    SDL_Rect dst;
    dst.w = radius; dst.h = radius;

    dst.x = r.x; dst.y = r.y;
    SDL_RenderCopyEx(g_ren, t, NULL, &dst, 0, NULL, SDL_FLIP_NONE);

    dst.x = r.x + r.w - radius; dst.y = r.y;
    SDL_RenderCopyEx(g_ren, t, NULL, &dst, 0, NULL, SDL_FLIP_HORIZONTAL);

    dst.x = r.x; dst.y = r.y + r.h - radius;
    SDL_RenderCopyEx(g_ren, t, NULL, &dst, 0, NULL, SDL_FLIP_VERTICAL);

    dst.x = r.x + r.w - radius; dst.y = r.y + r.h - radius;
    SDL_RenderCopyEx(g_ren, t, NULL, &dst, 0, NULL,
                     (SDL_RendererFlip)(SDL_FLIP_HORIZONTAL | SDL_FLIP_VERTICAL));
}

void gfx_round_border(SDL_Rect r, int radius, SDL_Color c, int thickness, SDL_Color bg)
{
    gfx_round(r, radius, c);

    SDL_Rect in;
    in.x = r.x + thickness;
    in.y = r.y + thickness;
    in.w = r.w - thickness * 2;
    in.h = r.h - thickness * 2;
    if (in.w <= 0 || in.h <= 0) return;

    int ir = radius - thickness;
    if (ir < 1) ir = 1;
    gfx_round(in, ir, bg);
}

/* ---------- 文本：小缓存，避免每帧重建纹理 ---------- */
typedef struct {
    char key[96];
    SDL_Texture *tex;
    int w, h;
    unsigned tick;
} TextEnt;

#define TEXT_CACHE 512
static TextEnt g_tc[TEXT_CACHE];
static unsigned g_tick;

/* 诊断用：本帧新建了多少张文字纹理。稳态下应该是 0，
 * 一直大于 0 说明缓存在抖动（每帧都要重新光栅化，就是卡顿的来源）。 */
static unsigned g_tex_new;
unsigned gfx_tex_new(void) { return g_tex_new; }
void gfx_tex_new_reset(void) { g_tex_new = 0; }

/* 淘汰的纹理不能立刻销毁：本帧的渲染命令里还引用着它，SDL_RenderPresent 前
 * DestroyTexture 会变成 use-after-free —— 真机表现为系统崩溃。
 * 统一入队，present 之后由 gfx_frame_end() 释放。 */
#define MAX_GRAVE 64
static SDL_Texture *g_grave[MAX_GRAVE];
static int g_grave_n;

static void grave_push(SDL_Texture *t)
{
    if (!t) return;
    if (g_grave_n < MAX_GRAVE) { g_grave[g_grave_n++] = t; return; }

    /* 队列满：销毁**最早**入队的那个（它在之前的帧就已提交，肯定安全）。
     * 不能销毁队尾——那是本帧刚淘汰的，渲染命令还引用着 */
    SDL_DestroyTexture(g_grave[0]);
    memmove(&g_grave[0], &g_grave[1], sizeof(g_grave[0]) * (MAX_GRAVE - 1));
    g_grave[MAX_GRAVE - 1] = t;
}

void gfx_frame_end(void)
{
    for (int i = 0; i < g_grave_n; i++) SDL_DestroyTexture(g_grave[i]);
    g_grave_n = 0;
}

/* 退出时回收文字纹理缓存（否则 SDL_Quit 前会残留上百张纹理） */
static void text_cache_clear(void)
{
    for (int i = 0; i < TEXT_CACHE; i++) {
        if (g_tc[i].tex) SDL_DestroyTexture(g_tc[i].tex);
        g_tc[i].tex = NULL;
        g_tc[i].key[0] = '\0';
        g_tc[i].w = g_tc[i].h = 0;
        g_tc[i].tick = 0;
    }
    g_tick = 0;
}

/* FNV-1a：长文本也要参与缓存 key（截断前缀会让超长行宽度算错） */
static unsigned str_hash(const char *s)
{
    unsigned h = 2166136261u;
    while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; }
    return h;
}

static SDL_Texture *text_tex(int size, const char *s, SDL_Color c, int *w, int *h)
{
    char key[96];
    snprintf(key, sizeof key, "%d|%02x%02x%02x|%08x",
             size, c.r, c.g, c.b, str_hash(s));

    for (int i = 0; i < TEXT_CACHE; i++) {
        if (g_tc[i].tex && strcmp(g_tc[i].key, key) == 0) {
            g_tc[i].tick = ++g_tick;
            *w = g_tc[i].w; *h = g_tc[i].h;
            return g_tc[i].tex;
        }
    }

    SDL_Color fg = c;
    SDL_Surface *sf = TTF_RenderUTF8_Blended(g_font[size], s, fg);
    if (!sf) return NULL;
    SDL_Texture *t = SDL_CreateTextureFromSurface(g_ren, sf);
    g_tex_new++;

    int slot = 0;
    unsigned oldest = 0xffffffffu;
    for (int i = 0; i < TEXT_CACHE; i++) {
        if (!g_tc[i].tex) { slot = i; oldest = 0; break; }
        if (g_tc[i].tick < oldest) { oldest = g_tc[i].tick; slot = i; }
    }
    if (g_tc[slot].tex) grave_push(g_tc[slot].tex);
    g_tc[slot].tex = t;
    snprintf(g_tc[slot].key, sizeof g_tc[slot].key, "%s", key);
    g_tc[slot].w = sf->w;
    g_tc[slot].h = sf->h;
    g_tc[slot].tick = ++g_tick;

    *w = sf->w; *h = sf->h;
    SDL_FreeSurface(sf);
    return t;
}

int gfx_text(int size, const char *utf8, int x, int y, SDL_Color c)
{
    if (!utf8 || !*utf8) return 0;
    int w = 0, h = 0;
    SDL_Texture *t = text_tex(size, utf8, c, &w, &h);
    if (!t) return 0;
    SDL_Rect d = { x, y, w, h };
    SDL_RenderCopy(g_ren, t, NULL, &d);
    return w;
}

int gfx_text_w(int size, const char *utf8)
{
    if (!utf8 || !*utf8) return 0;
    int w = 0, h = 0;
    SDL_Texture *t = text_tex(size, utf8, (SDL_Color){255, 255, 255, 255}, &w, &h);
    (void)t;
    return w;
}

void gfx_text_size(int size, const char *utf8, int *w, int *h)
{
    *w = 0; *h = 0;
    if (!utf8 || !*utf8) return;
    int ww = 0, hh = 0;
    SDL_Texture *t = text_tex(size, utf8, (SDL_Color){255, 255, 255, 255}, &ww, &hh);
    if (t) { *w = ww; *h = hh; }
}

int gfx_text_box(int size, const char *utf8, SDL_Rect box, SDL_Color c)
{
    int w = 0, h = 0;
    gfx_text_size(size, utf8, &w, &h);
    return gfx_text(size, utf8, box.x + (box.w - w) / 2, box.y + (box.h - h) / 2, c);
}

int gfx_text_center(int size, const char *utf8, int cx, int y, SDL_Color c)
{
    int w = gfx_text_w(size, utf8);
    return gfx_text(size, utf8, cx - w / 2, y, c);
}

int gfx_text_right(int size, const char *utf8, int rx, int y, SDL_Color c)
{
    int w = gfx_text_w(size, utf8);
    return gfx_text(size, utf8, rx - w, y, c);
}

/* ---------- 阻塞期间的进度遮罩 ---------- */

/* 不进缓存：进度文字每次都变，走缓存会瞬间塞满纹理池 */
static void text_now(int size, const char *s, SDL_Color c, int cx, int y)
{
    if (!s || !*s) return;
    SDL_Surface *sf = TTF_RenderUTF8_Blended(g_font[size], s, c);
    if (!sf) return;
    SDL_Texture *t = SDL_CreateTextureFromSurface(g_ren, sf);
    if (t) {
        SDL_Rect d = { cx - sf->w / 2, y, sf->w, sf->h };
        SDL_RenderCopy(g_ren, t, NULL, &d);
        SDL_DestroyTexture(t);
    }
    SDL_FreeSurface(sf);
}

void gfx_busy_overlay(const char *title, const char *sub, int pct)
{
    if (!g_ren) return;

    gfx_fill((SDL_Rect){ 0, 0, UI_W, UI_H }, (SDL_Color){ 255, 255, 255, 235 });
    text_now(F_HEAD, title, (SDL_Color){ 33, 33, 33, 255 }, UI_W / 2, UI_H / 2 - 56);
    if (sub && sub[0])
        text_now(F_SMALL, sub, (SDL_Color){ 120, 124, 130, 255 }, UI_W / 2, UI_H / 2 + 4);

    if (pct >= 0) {
        if (pct > 100) pct = 100;
        SDL_Rect track = { UI_W / 2 - 300, UI_H / 2 + 52, 600, 12 };
        gfx_round(track, 6, (SDL_Color){ 232, 236, 240, 255 });
        int w = 600 * pct / 100;
        if (w > 0)
            gfx_round((SDL_Rect){ track.x, track.y, w, 12 }, 6,
                      (SDL_Color){ HA_BLUE });
    }

    SDL_RenderPresent(g_ren);
}


/* ---------- 图标：纯几何绘制 ---------- */

static void icon_arc(int cx, int cy, int rad, int a0, int a1, SDL_Color c)
{
    SDL_SetRenderDrawColor(g_ren, c.r, c.g, c.b, c.a);
    for (int a = a0; a <= a1; a += 2) {
        float th = (float)a * 3.14159f / 180.0f;
        SDL_RenderDrawPoint(g_ren, cx + (int)(cosf(th) * rad),
                            cy + (int)(sinf(th) * rad));
    }
}

void gfx_icon(int kind, SDL_Rect box, SDL_Color c)
{
    int cx = box.x + box.w / 2;
    int cy = box.y + box.h / 2;
    int s  = box.w < box.h ? box.w : box.h;
    int r  = s / 2;

    SDL_SetRenderDrawColor(g_ren, c.r, c.g, c.b, c.a);

    switch (kind) {
    case IC_BULB: {
        int rr = r * 55 / 100;
        for (int dy = -rr; dy <= rr - rr / 3; dy++) {
            int w = (int)sqrtf((float)(rr * rr - dy * dy));
            SDL_RenderDrawLine(g_ren, cx - w, cy - r / 3 + dy, cx + w, cy - r / 3 + dy);
        }
        SDL_Rect base = { cx - r / 4, cy + r * 2 / 3, r / 2, r / 4 };
        SDL_RenderFillRect(g_ren, &base);
        break;
    }
    case IC_PLUG: {
        SDL_Rect b = { cx - r * 55 / 100, cy - r * 45 / 100, r * 11 / 10, r * 9 / 10 };
        SDL_RenderDrawRect(g_ren, &b);
        SDL_Rect p1 = { cx - r / 5, cy - r * 20 / 100, r / 12, r / 3 };
        SDL_Rect p2 = { cx + r / 8, cy - r * 20 / 100, r / 12, r / 3 };
        SDL_RenderFillRect(g_ren, &p1);
        SDL_RenderFillRect(g_ren, &p2);
        SDL_Rect stem = { cx - r / 20, cy + r * 45 / 100, r / 10, r / 4 };
        SDL_RenderFillRect(g_ren, &stem);
        break;
    }
    case IC_SENSOR: {
        for (int i = 0; i < 3; i++)
            icon_arc(cx, cy + r / 4, r * (30 + i * 22) / 100, 200, 340, c);
        gfx_circle(cx, cy + r / 4, r / 10, c);
        break;
    }
    case IC_FAN: {
        gfx_circle(cx, cy, r / 8, c);
        for (int i = 0; i < 3; i++) {
            float th = (float)i * 2.0944f - 1.5708f;
            int px = cx + (int)(cosf(th) * r * 55 / 100);
            int py = cy + (int)(sinf(th) * r * 55 / 100);
            gfx_circle(px, py, r / 5, c);
        }
        break;
    }
    case IC_CAMERA: {
        SDL_Rect body = { cx - r * 70 / 100, cy - r * 40 / 100, r * 14 / 10, r * 80 / 100 };
        SDL_RenderDrawRect(g_ren, &body);
        gfx_circle(cx, cy, r * 26 / 100, c);
        SDL_Rect top = { cx - r * 25 / 100, cy - r * 58 / 100, r * 40 / 100, r * 18 / 100 };
        SDL_RenderFillRect(g_ren, &top);
        break;
    }
    case IC_AC: {
        SDL_Rect body = { cx - r * 70 / 100, cy - r * 42 / 100, r * 14 / 10, r * 50 / 100 };
        SDL_RenderDrawRect(g_ren, &body);
        for (int i = 0; i < 3; i++) {
            int wy = cy + r * 20 / 100 + i * (r / 6);
            SDL_Rect line = { cx - r * 45 / 100, wy, r * 9 / 10, r / 20 };
            SDL_RenderFillRect(g_ren, &line);
        }
        break;
    }
    case IC_CURTAIN: {
        SDL_Rect rail = { cx - r * 70 / 100, cy - r * 50 / 100, r * 14 / 10, r / 14 };
        SDL_RenderFillRect(g_ren, &rail);
        SDL_Rect l = { cx - r * 55 / 100, cy - r * 42 / 100, r * 4 / 10, r * 90 / 100 };
        SDL_RenderDrawRect(g_ren, &l);
        SDL_Rect rr2 = { cx + r * 15 / 100, cy - r * 42 / 100, r * 4 / 10, r * 90 / 100 };
        SDL_RenderDrawRect(g_ren, &rr2);
        break;
    }
    case IC_LOCK: {
        SDL_Rect body = { cx - r * 50 / 100, cy - r * 5 / 100, r, r * 60 / 100 };
        gfx_round(body, r / 6, c);
        icon_arc(cx, cy + r * 5 / 100, r * 34 / 100, 180, 360, c);
        break;
    }
    case IC_VACUUM: {
        gfx_circle(cx, cy, r * 62 / 100, c);
        gfx_circle(cx, cy, r * 34 / 100, (SDL_Color){ c.r, c.g, c.b, 0 });
        SDL_SetRenderDrawColor(g_ren, 255, 255, 255, 255);
        gfx_circle(cx, cy, r * 26 / 100, (SDL_Color){ 255, 255, 255, 255 });
        break;
    }
    case IC_SCRIPT: {
        /* 闪电 = 自动化 */
        SDL_Point pts[7] = {
            { cx + r * 10 / 100, cy - r * 70 / 100 },
            { cx - r * 45 / 100, cy + r * 10 / 100 },
            { cx - r * 5 / 100,  cy + r * 10 / 100 },
            { cx - r * 15 / 100, cy + r * 70 / 100 },
            { cx + r * 45 / 100, cy - r * 10 / 100 },
            { cx + r * 5 / 100,  cy - r * 10 / 100 },
            { cx + r * 10 / 100, cy - r * 70 / 100 }
        };
        SDL_RenderDrawLines(g_ren, pts, 7);
        break;
    }
    case IC_HOME: {
        SDL_Point roof[4] = { {cx, cy - r * 60 / 100}, {cx + r * 62 / 100, cy - r * 5 / 100},
                              {cx - r * 62 / 100, cy - r * 5 / 100}, {cx, cy - r * 60 / 100} };
        SDL_RenderDrawLines(g_ren, roof, 4);
        SDL_Rect body = { cx - r * 42 / 100, cy - r * 2 / 100, r * 84 / 100, r * 55 / 100 };
        SDL_RenderDrawRect(g_ren, &body);
        break;
    }
    case IC_SMART: {
        for (int gy = 0; gy < 2; gy++)
            for (int gx = 0; gx < 2; gx++) {
                SDL_Rect q = { cx - r * 55 / 100 + gx * r * 62 / 100,
                               cy - r * 55 / 100 + gy * r * 62 / 100,
                               r * 42 / 100, r * 42 / 100 };
                SDL_RenderDrawRect(g_ren, &q);
            }
        break;
    }
    case IC_GEAR: {
        gfx_circle(cx, cy, r * 30 / 100, (SDL_Color){ c.r, c.g, c.b, 0 });
        for (int i = 0; i < 8; i++) {
            float th = (float)i * 0.7854f;
            int x0 = cx + (int)(cosf(th) * r * 45 / 100);
            int y0 = cy + (int)(sinf(th) * r * 45 / 100);
            int x1 = cx + (int)(cosf(th) * r * 70 / 100);
            int y1 = cy + (int)(sinf(th) * r * 70 / 100);
            SDL_RenderDrawLine(g_ren, x0, y0, x1, y1);
        }
        for (int i = 0; i < 3; i++)
            icon_arc(cx, cy, r * (45 + i * 12) / 100, 0, 360, c);
        break;
    }
    case IC_CHEVRON_L: {
        for (int t = 0; t < 3; t++) {           /* 加粗：三条平行折线 */
            SDL_Point pts[3] = {
                { cx + r * 20 / 100 - t, cy - r * 45 / 100 },
                { cx - r * 20 / 100 - t, cy },
                { cx + r * 20 / 100 - t, cy + r * 45 / 100 }
            };
            SDL_RenderDrawLines(g_ren, pts, 3);
        }
        break;
    }
    case IC_CHEVRON_R: {
        for (int t = 0; t < 3; t++) {
            SDL_Point pts[3] = {
                { cx - r * 20 / 100 + t, cy - r * 45 / 100 },
                { cx + r * 20 / 100 + t, cy },
                { cx - r * 20 / 100 + t, cy + r * 45 / 100 }
            };
            SDL_RenderDrawLines(g_ren, pts, 3);
        }
        break;
    }
    case IC_REFRESH:
        icon_arc(cx, cy, r * 55 / 100, -60, 240, c);
        SDL_Point tip[3] = { { cx + r * 55 / 100, cy - r * 55 / 100 },
                             { cx + r * 20 / 100, cy - r * 50 / 100 },
                             { cx + r * 55 / 100, cy - r * 20 / 100 } };
        SDL_SetRenderDrawColor(g_ren, c.r, c.g, c.b, c.a);
        SDL_RenderDrawLines(g_ren, tip, 3);
        break;
    case IC_MINUS: {
        SDL_Rect h = { cx - r * 50 / 100, cy - r / 16, r, r / 8 };
        SDL_RenderFillRect(g_ren, &h);
        break;
    }
    case IC_WIFI: {
        for (int i = 0; i < 3; i++)
            icon_arc(cx, cy + r * 35 / 100, r * (25 + i * 22) / 100, 200, 340, c);
        gfx_circle(cx, cy + r * 32 / 100, r / 10, c);
        break;
    }
    case IC_AREA: {   /* 田字格：HA frontend 的「地区」 */
        int a = r * 34 / 100, g = r * 16 / 100;
        SDL_Rect q[4] = {
            { cx - a - g / 2, cy - a - g / 2, a, a },
            { cx + g / 2,     cy - a - g / 2, a, a },
            { cx - a - g / 2, cy + g / 2,     a, a },
            { cx + g / 2,     cy + g / 2,     a, a },
        };
        for (int i = 0; i < 4; i++) SDL_RenderDrawRect(g_ren, &q[i]);
        break;
    }
    case IC_CLOSE: {  /* X */
        int a = r * 55 / 100;
        for (int t = -1; t <= 1; t++) {
            SDL_RenderDrawLine(g_ren, cx - a + t, cy - a, cx + a + t, cy + a);
            SDL_RenderDrawLine(g_ren, cx + a + t, cy - a, cx - a + t, cy + a);
        }
        break;
    }
    case IC_INFO: {
        icon_arc(cx, cy, r * 70 / 100, 0, 360, c);
        gfx_circle(cx, cy - r * 35 / 100, r / 10, c);
        SDL_Rect stem = { cx - r / 12, cy - r * 10 / 100, r / 6, r * 65 / 100 };
        SDL_RenderFillRect(g_ren, &stem);
        break;
    }
    case IC_STAR: {
        /* 五角星：5 个外顶点用圆点表示 + 中心实心，
         * 小尺寸下比细线轮廓更饱满，也省掉多边形填充 */
        for (int i = 0; i < 5; i++) {
            float th = -1.5708f + (float)i * 1.25664f;   /* 72° */
            int px = cx + (int)(cosf(th) * (float)r * 74 / 100);
            int py = cy + (int)(sinf(th) * (float)r * 74 / 100);
            gfx_circle(px, py, r * 26 / 100, c);
        }
        gfx_circle(cx, cy, r * 34 / 100, c);
        break;
    }
    case IC_SEARCH: {
        icon_arc(cx - r / 6, cy - r / 6, r * 52 / 100, 0, 360, c);
        for (int i = 0; i < r / 4; i++)
            SDL_RenderDrawLine(g_ren, cx + r / 5 + i, cy + r / 5 + i,
                               cx + r * 55 / 100 + i, cy + r * 55 / 100 + i);
        break;
    }
    case IC_PLUS:
    default: {
        SDL_Rect h = { cx - r * 50 / 100, cy - r / 16, r, r / 8 };
        SDL_Rect v = { cx - r / 16, cy - r * 50 / 100, r / 8, r };
        SDL_RenderFillRect(g_ren, &h);
        SDL_RenderFillRect(g_ren, &v);
        break;
    }
    }
}

/* 按像素宽度截断文本，末尾补省略号（CJK 文本按字节小心切分，避免半个字符） */
void gfx_ellipsis(int size, const char *utf8, int max_w, char *out, int outsz)
{
    if (!out || outsz <= 1) return;
    out[0] = '\0';
    if (!utf8 || !utf8[0]) return;

    if (gfx_text_w(size, utf8) <= max_w) {
        snprintf(out, (size_t)outsz, "%s", utf8);
        return;
    }

    const char *ell = "...";
    int ell_w = gfx_text_w(size, ell);
    if (max_w <= ell_w) {
        snprintf(out, (size_t)outsz, "%s", ell);
        return;
    }

    /* 逐步缩短，遇到 UTF-8 续字节时再退一个字节 */
    size_t n = strlen(utf8);
    char tmp[256];
    if (n >= sizeof tmp) n = sizeof tmp - 1;
    memcpy(tmp, utf8, n);
    tmp[n] = '\0';

    while (n > 0) {
        n--;
        while (n > 0 && ((unsigned char)tmp[n] & 0xC0) == 0x80) n--;
        tmp[n] = '\0';
        if (gfx_text_w(size, tmp) + ell_w <= max_w) break;
    }
    snprintf(out, (size_t)outsz, "%s%s", tmp, ell);
}
