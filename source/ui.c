#include "ui.h"
#include "cam.h"
#include "applog.h"
#include "json.h"
#include "update.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ------------------------------------------------------------------
 * 界面依据 home-assistant/android 2026.11.1 官方 APK 反编译结果重建，
 * 不复用 mihome 项目的任何布局：
 *   - common/compose/theme/HAColors.kt  → 品牌色 / Primary / Neutral 色板
 *   - HARadius / HADimens / HAFontSize → 16~24 圆角、4 的倍数间距
 *   - HATopBar / HAInputChip / HATextField / HAFilledButton /
 *     HASettingsCard / HAModalBottomSheet → 组件形态
 *   - HA frontend → 左侧 sidebar 导航 + 概览按地区分组的 Tile 卡片
 *   - 官方 onboarding「连接到 Home Assistant」+ 内部/外部网址
 * 所有 token 见 gfx.h。坐标按 1280x720 电视屏放大。
 * ------------------------------------------------------------------ */

/* 主题色：改成运行时变量，设置页可切浅色 / 深色（HA App 的 dark theme
 * 用 Neutral 深色阶，卡片比背景亮一档，文字反相）。所有绘制仍用 C_*，
 * 只是这些值在 ui_theme_apply() 里被整体替换。 */
static SDL_Color C_BG, C_CARD, C_TEXT, C_TEXT2, C_TEXT3, C_LINE, C_BORDER;
static SDL_Color C_BLUE, C_BLUE_D, C_BLUE_S, C_PALE, C_DEEP, C_WHITE;
static SDL_Color C_OK, C_ERR, C_OFFBG, C_OFFFG, C_SHADOW;
static SDL_Color C_OK_SOFT, C_ERR_SOFT, C_TOAST, C_TOAST_TX, C_RING;
static int g_dark;

static void ui_theme_apply(int dark)
{
    g_dark = dark ? 1 : 0;
    if (!g_dark) {
        /* 浅色：HA 品牌背景 #F2F4F9 + 白卡片 */
        C_BG      = (SDL_Color){ HA_BG };
        C_CARD    = (SDL_Color){ HA_CARD };
        C_TEXT    = (SDL_Color){ HA_TEXT };
        C_TEXT2   = (SDL_Color){ HA_TEXT_2 };
        C_TEXT3   = (SDL_Color){ HA_TEXT_3 };
        C_LINE    = (SDL_Color){ HA_LINE };
        C_BORDER  = (SDL_Color){ HA_BORDER };
        C_BLUE    = (SDL_Color){ HA_BLUE };
        C_BLUE_D  = (SDL_Color){ HA_BLUE_DARK };
        C_BLUE_S  = (SDL_Color){ HA_BLUE_SOFT };
        C_PALE    = (SDL_Color){ HA_BLUE_PALE };
        C_DEEP    = (SDL_Color){ HA_BLUE_DEEP };
        C_WHITE   = (SDL_Color){ 255, 255, 255, 255 };
        C_OK      = (SDL_Color){ HA_OK };
        C_ERR     = (SDL_Color){ HA_ERR };
        C_OFFBG   = (SDL_Color){ HA_OFF };
        C_OFFFG   = (SDL_Color){ 160, 166, 175, 255 };
        C_SHADOW  = (SDL_Color){ 232, 235, 240, 255 };
        C_OK_SOFT  = (SDL_Color){ 227, 247, 232, 255 };
        C_ERR_SOFT = (SDL_Color){ 255, 240, 239, 255 };
        C_TOAST    = (SDL_Color){ 32, 36, 40, 255 };
        C_TOAST_TX = (SDL_Color){ 255, 255, 255, 255 };
        C_RING     = (SDL_Color){ 255, 255, 255, 255 };
    } else {
        /* 深色：卡片与背景要拉开一档（以前只差 12 级灰度，整屏糊成一片），
         * 文字全部提亮 —— 电视 / 掌机屏幕在暗环境里对低对比度特别不友好，
         * 之前的 C_TEXT2/C_TEXT3 偏灰，副标题和状态几乎看不清。
         * 品牌蓝在深底上换 HA 的 dark primary（#29B6F6），前景观再提亮一档。 */
        C_BG      = (SDL_Color){ 15, 17, 21, 255 };
        C_CARD    = (SDL_Color){ 33, 37, 45, 255 };
        C_TEXT    = (SDL_Color){ 245, 247, 250, 255 };
        C_TEXT2   = (SDL_Color){ 201, 207, 217, 255 };
        C_TEXT3   = (SDL_Color){ 161, 169, 182, 255 };
        C_LINE    = (SDL_Color){ 52, 58, 68, 255 };
        C_BORDER  = (SDL_Color){ 74, 81, 94, 255 };
        C_BLUE    = (SDL_Color){ 41, 182, 246, 255 };
        C_BLUE_D  = (SDL_Color){ 108, 213, 247, 255 };
        C_BLUE_S  = (SDL_Color){ 22, 62, 76, 255 };
        C_PALE    = (SDL_Color){ 20, 52, 64, 255 };
        C_DEEP    = (SDL_Color){ 0, 122, 158, 255 };
        C_WHITE   = (SDL_Color){ 255, 255, 255, 255 };
        C_OK      = (SDL_Color){ 82, 212, 134, 255 };
        C_ERR     = (SDL_Color){ 255, 124, 136, 255 };
        C_OFFBG   = (SDL_Color){ 48, 54, 64, 255 };
        C_OFFFG   = (SDL_Color){ 178, 185, 196, 255 };
        C_SHADOW  = (SDL_Color){ 4, 5, 7, 255 };
        C_OK_SOFT  = (SDL_Color){ 22, 60, 40, 255 };
        C_ERR_SOFT = (SDL_Color){ 66, 30, 34, 255 };
        C_TOAST    = (SDL_Color){ 46, 52, 62, 255 };
        C_TOAST_TX = (SDL_Color){ 240, 243, 248, 255 };
        C_RING     = (SDL_Color){ 33, 37, 45, 255 };
    }
}

void ui_theme_set(int dark)
{
    ui_theme_apply(dark);
    ha_set_theme(dark);
}

int ui_theme_is_dark(void) { return g_dark; }

#define DEFAULT_ADDR "http://192.168.1.100:8123"   /* 首次启动预填：示例格式 */
#define DEFAULT_ALT  ""                           /* 外网地址不预填，用户自己输 */

/* 筛选相关（定义在文件后部，ui_rebuild 先用） */
static int filt_match(int filt, const HaEnt *e);
static int fav_has(const char *eid);

/* HA frontend sidebar + 内容区 */
#define SIDEBAR_W   300          /* 加宽：地区名要能完整显示，不再依赖横向 chips */
#define BRAND_W     460
#define PAD         32
#define CX          (SIDEBAR_W + PAD)
#define CW          (UI_W - CX - PAD)
#define NAV_Y       96           /* 侧栏三个导航项起点 */
#define NAV_ROW     60
#define NAV_H       52
#define AREA_HDR_Y  288          /* 「地区」标题 */
#define AREA_TOP    312          /* 侧栏地区列表起点 */
#define AREA_ROW    37
#define AREA_VIS    8            /* 一屏可见地区数（含「全部地区」），超出滚动 */
#define SECTION_Y   100
#define CHIPS_Y     156          /* 类型筛选条 */
#define CHIPS_H     40
#define GRID_Y      206
#define TILE_COLS   5
#define TILE_ROWS   3
#define TILE_GX     16
#define TILE_GY     16
#define TILE_W      ((CW - (TILE_COLS - 1) * TILE_GX) / TILE_COLS)
#define TILE_H      140
#define PER_PAGE    (TILE_COLS * TILE_ROWS)
#define SCENE_TOP   152
#define SCENE_ROW   82
#define SET_CARD1_Y 150
#define SET_CARD2_Y 366
#define SET_ROW_H   56

typedef int (*KbFn)(char *buf, size_t len, const char *guide, int password);
static KbFn g_kb;

/* 地区列表的第 0 项固定是「全部地区」，第 i+1 项是第 i 个地区。
 * 这样侧栏里也能回到「全部」，不用记着按 B 或 Y。 */
static int area_entry_sel(App *a)
{
    int sel = a->area_sel + 1;
    /* 焦点在侧栏里时，滚动跟着高亮行走，而不是跟着已生效的地区走 */
    if (a->zone == ZONE_SIDE && a->side_sel >= 3) sel = a->side_sel - 3;
    int n = ha_narea() + 1;
    if (sel < 0) sel = 0;
    if (sel >= n) sel = n - 1;
    return sel;
}

/* 地区多于 AREA_VIS 时，列表跟着当前行滚动，保证选中项始终可见 */
static int area_list_top(App *a)
{
    int n = ha_narea() + 1;
    if (n <= AREA_VIS) return 0;
    int sel = area_entry_sel(a);
    int top = (sel >= AREA_VIS) ? sel - AREA_VIS + 1 : 0;
    if (top > n - AREA_VIS) top = n - AREA_VIS;
    if (top < 0) top = 0;
    return top;
}

void ui_set_keyboard(KbFn fn) { g_kb = fn; }

void ui_toast(App *a, int ok, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(a->toast, sizeof a->toast, fmt, ap);
    va_end(ap);
    a->toast_ok = ok;
    a->toast_frames = 150;
}

static int kind_icon(int dom)
{
    switch (dom) {
    case DOM_LIGHT:      return IC_BULB;
    case DOM_FAN:        return IC_FAN;
    case DOM_COVER:      return IC_CURTAIN;
    case DOM_CLIMATE:
    case DOM_WATER_HEATER: return IC_AC;
    case DOM_CAMERA:     return IC_CAMERA;
    case DOM_LOCK:       return IC_LOCK;
    case DOM_VACUUM:     return IC_VACUUM;
    case DOM_SENSOR:
    case DOM_BINARY:     return IC_SENSOR;
    case DOM_SCENE:
    case DOM_SCRIPT:     return IC_SCRIPT;
    default:             return IC_PLUG;
    }
}

/* 实体的一句话状态描述 */
static void ent_status(const HaEnt *e, char *out, int sz)
{
    if (!e->online) { snprintf(out, (size_t)sz, "%s", "离线"); return; }

    if (e->dom == DOM_SENSOR || e->dom == DOM_BINARY) {
        if (e->unit[0])
            snprintf(out, (size_t)sz, "%s %s", e->state, e->unit);
        else if (!strcmp(e->state, "on"))  snprintf(out, (size_t)sz, "%s", "检测到");
        else if (!strcmp(e->state, "off")) snprintf(out, (size_t)sz, "%s", "正常");
        else snprintf(out, (size_t)sz, "%.28s", e->state);
        return;
    }

    if (e->dom == DOM_CLIMATE) {
        if (e->on == 1) snprintf(out, (size_t)sz, "%s", "运行中");
        else snprintf(out, (size_t)sz, "%s", "已关闭");
        return;
    }

    if (e->dom == DOM_CAMERA) {
        if (!strcmp(e->state, "idle"))           snprintf(out, (size_t)sz, "%s", "待机");
        else if (!strcmp(e->state, "recording")) snprintf(out, (size_t)sz, "%s", "录制中");
        else if (!strcmp(e->state, "streaming")) snprintf(out, (size_t)sz, "%s", "直播中");
        else snprintf(out, (size_t)sz, "%.28s", e->state);
        return;
    }

    if (e->dom == DOM_COVER) {
        if (e->level >= 0) snprintf(out, (size_t)sz, "开合 %d%%", e->level);
        else snprintf(out, (size_t)sz, "%s", e->on == 1 ? "已打开" : "已关闭");
        return;
    }

    if (e->on == 1) {
        if (e->level >= 0 && e->dom == DOM_LIGHT)
            snprintf(out, (size_t)sz, "已开启 · %d%%", e->level);
        else
            snprintf(out, (size_t)sz, "%s", "已开启");
    } else if (e->on == 0) {
        snprintf(out, (size_t)sz, "%s", "已关闭");
    } else {
        snprintf(out, (size_t)sz, "%.28s", e->state[0] ? e->state : "—");
    }
}

/* ---------------- 视图过滤 ---------------- */

/* 大小写不敏感的包含判断（中文按字节直接比，ASCII 折成小写） */
static int icontains(const char *hay, const char *needle)
{
    if (!needle[0]) return 1;
    size_t nl = strlen(needle);
    for (const char *p = hay; *p; p++) {
        size_t k = 0;
        while (k < nl && p[k] &&
               tolower((unsigned char)p[k]) == tolower((unsigned char)needle[k]))
            k++;
        if (k == nl) return 1;
    }
    return 0;
}

/* 网格滚动：保证焦点所在行落在可视的三行里。
 * 焦点掉出下边界就整屏下滚一行（而不是跳一整页）——跳页会让中间那几行
 * 「看起来不存在」，用户以为翻过头了。 */
static void grid_follow(App *a)
{
    int rows = (a->nview + TILE_COLS - 1) / TILE_COLS;
    int maxrow = rows - TILE_ROWS;
    if (maxrow < 0) maxrow = 0;

    int row = a->card_sel / TILE_COLS;
    if (a->grid_row < 0) a->grid_row = 0;
    if (row < a->grid_row) a->grid_row = row;
    if (row >= a->grid_row + TILE_ROWS) a->grid_row = row - TILE_ROWS + 1;
    if (a->grid_row > maxrow) a->grid_row = maxrow;
    if (a->grid_row > row) a->grid_row = row;
    if (a->grid_row < 0) a->grid_row = 0;
}

void ui_rebuild(App *a)
{
    a->nview = 0;
    const char *area = NULL;
    if (a->area_sel >= 0) {
        const HaArea *ar = ha_area(a->area_sel);
        area = ar ? ar->name : NULL;
    }

    for (int i = 0; i < ha_nent(); i++) {
        const HaEnt *e = ha_ent(i);
        if (area) {
            const char *nm = e->area[0] ? e->area : "未分配";
            if (strcmp(nm, area) != 0) continue;
        }
        if (!filt_match(a->filter_type, e)) continue;
        if (a->query[0] &&
            !icontains(e->name, a->query) && !icontains(e->eid, a->query))
            continue;
        a->view[a->nview++] = i;
    }

    if (a->card_sel >= a->nview) a->card_sel = a->nview > 0 ? a->nview - 1 : 0;
    if (a->card_sel < 0) a->card_sel = 0;
    grid_follow(a);     /* 结果集变了，滚动位置跟着收敛 */
}

static void draw_section(App *a);
static void draw_grid(App *a);

/* ---------------- 绘制：共用件 ---------------- */

/* HA frontend 的左侧导航（sidebar）：品牌 + 导航项 + 地区 + 连接状态 */
static void draw_sidebar(App *a)
{
    gfx_fill((SDL_Rect){ 0, 0, SIDEBAR_W, UI_H }, C_CARD);
    gfx_fill((SDL_Rect){ SIDEBAR_W, 0, 1, UI_H }, C_LINE);

    /* 品牌 */
    gfx_ha_logo((SDL_Rect){ 24, 20, 54, 54 }, 0);
    gfx_text(F_BODY, "Home Assistant", 88, 24, C_TEXT);
    gfx_text(F_TINY, "Switch Companion", 88, 58, C_TEXT3);

    static const char *names[3] = { "概览", "自动化", "设置" };
    static const int icons[3] = { IC_HOME, IC_SMART, IC_GEAR };
    for (int i = 0; i < 3; i++) {
        SDL_Rect r = { 16, NAV_Y + i * NAV_ROW, SIDEBAR_W - 32, NAV_H };
        int on = (a->tab == i);
        int foc = (a->zone == ZONE_SIDE && a->side_sel == i);
        gfx_round(r, NAV_H / 2, on ? C_BLUE_S : C_CARD);
        if (foc) gfx_round_border(r, NAV_H / 2, C_BLUE, 2, on ? C_BLUE_S : C_CARD);
        /* 左侧竖条：当前页签的定位感，比单纯换底色更明确 */
        if (on) gfx_round((SDL_Rect){ r.x + 4, r.y + 15, 3, NAV_H - 30 }, 2, C_BLUE);
        gfx_icon(icons[i], (SDL_Rect){ r.x + 22, r.y + 13, 26, 26 },
                 on ? C_BLUE_D : C_TEXT2);
        gfx_text(F_BODY, names[i], r.x + 60, r.y + 11, on ? C_BLUE_D : C_TEXT2);
    }

    /* 地区列表：竖排显示，第 0 行固定「全部地区」，超出 AREA_VIS 自动滚动 */
    int na = ha_narea();
    int total = na + 1;
    gfx_text(F_TINY, "地区", 28, AREA_HDR_Y, C_TEXT2);

    int atop = area_list_top(a);
    char cnt[40];
    if (total > AREA_VIS)
        snprintf(cnt, sizeof cnt, "%d–%d / %d", atop + 1,
                 atop + AREA_VIS, total);
    else
        snprintf(cnt, sizeof cnt, "%d 个", na);
    gfx_text_right(F_TINY, cnt, SIDEBAR_W - 20, AREA_HDR_Y, C_TEXT3);

    int vis = total - atop;
    if (vis > AREA_VIS) vis = AREA_VIS;
    for (int k = 0; k < vis; k++) {
        int entry = atop + k;
        int ai = entry - 1;                 /* -1 = 全部地区 */
        const HaArea *ar = (ai >= 0) ? ha_area(ai) : NULL;
        if (ai >= 0 && !ar) continue;

        SDL_Rect r = { 16, AREA_TOP + k * AREA_ROW, SIDEBAR_W - 32, AREA_ROW - 3 };
        int on = (a->area_sel == ai && a->tab == 0);
        int foc = (a->zone == ZONE_SIDE && a->side_sel == 3 + entry);
        gfx_round(r, 15, on ? C_BLUE_S : C_CARD);
        if (foc) gfx_round_border(r, 15, C_BLUE, 2, on ? C_BLUE_S : C_CARD);

        /* 小圆点代替大图标：地区是列表，不是功能入口，视觉上要轻 */
        gfx_circle(r.x + 22, r.y + r.h / 2, on ? 5 : 4, on ? C_BLUE : C_BORDER);

        char nm[48];
        gfx_ellipsis(F_SMALL, (ai < 0) ? "全部地区" : ar->name,
                     SIDEBAR_W - 118, nm, sizeof nm);
        gfx_text(F_SMALL, nm, r.x + 38, r.y + 5, on ? C_BLUE_D : C_TEXT2);

        if (ai >= 0) {
            char ne[20];
            snprintf(ne, sizeof ne, "%d", ar->nent);
            gfx_text_right(F_TINY, ne, r.x + r.w - 12, r.y + 9, C_TEXT3);
        }
    }
    if (na == 0) {
        gfx_text(F_TINY, ha_ready() ? "尚未同步，按 X 刷新" : "未连接",
                 28, AREA_TOP + AREA_ROW + 6, C_TEXT3);
    }

    /* 列表滚动条：一眼看出上面/下面还有房间 */
    if (total > AREA_VIS) {
        int track_y = AREA_TOP + 4;
        int track_h = AREA_VIS * AREA_ROW - 12;
        gfx_round((SDL_Rect){ SIDEBAR_W - 11, track_y, 4, track_h }, 2, C_LINE);
        int thumb_h = track_h * AREA_VIS / total;
        if (thumb_h < 18) thumb_h = 18;
        int max_top = total - AREA_VIS;
        int thumb_y = track_y + (track_h - thumb_h) * atop / (max_top > 0 ? max_top : 1);
        gfx_round((SDL_Rect){ SIDEBAR_W - 11, thumb_y, 4, thumb_h }, 2, C_BORDER);
    }

    /* 底部：当前线路 + 地址。
     * 连接状态（已连接 / 未连接）只保留在顶栏那一个胶囊里 —— 两处显示同一件事
     * 反而让人以为在看两个不同的状态，这里只留地址信息。 */
    SDL_Rect box = { 16, UI_H - 86, SIDEBAR_W - 32, 72 };
    gfx_round(box, 16, C_BG);
    gfx_text(F_TINY, ha_route(), box.x + 20, box.y + 12, C_TEXT2);
    char b[64];
    gfx_ellipsis(F_TINY, ha_base()[0] ? ha_base() : "—", SIDEBAR_W - 72, b, sizeof b);
    gfx_text(F_TINY, b, box.x + 20, box.y + 42, C_TEXT3);
}

/* 内容区顶栏（HATopBar）：标题 + 副标题 + 刷新 + 连接状态 */
static void draw_topbar(App *a, const char *title, const char *sub)
{
    gfx_text(F_TITLE, title, CX, 18, C_TEXT);
    if (sub && sub[0]) gfx_text(F_SMALL, sub, CX, 62, C_TEXT2);

    SDL_Rect rf = { UI_W - PAD - 44, 26, 44, 44 };
    gfx_round(rf, 22, C_CARD);
    gfx_round_border(rf, 22, C_LINE, 1, C_CARD);
    gfx_icon(IC_REFRESH, (SDL_Rect){ rf.x + 10, rf.y + 10, 24, 24 }, C_BLUE_D);

    int st = ha_ready();
    const char *label = st ? "已连接" : "未连接";
    SDL_Color dot = st ? C_OK : C_ERR;
    int tw = gfx_text_w(F_TINY, label);
    SDL_Rect pill = { rf.x - tw - 64, 30, tw + 48, 36 };
    gfx_round(pill, 18, st ? C_OK_SOFT : C_ERR_SOFT);
    gfx_circle(pill.x + 18, pill.y + 18, 5, dot);
    gfx_text(F_TINY, label, pill.x + 30, pill.y + 10, dot);
}

/* 概览页的底图，供 More-info / 诊断页叠加使用 */
static void draw_main_back(App *a)
{
    gfx_fill((SDL_Rect){ 0, 0, UI_W, UI_H }, C_BG);
    draw_sidebar(a);
    char sub[96];
    snprintf(sub, sizeof sub, "%d 个实体 · %d 个地区", ha_nent(), ha_narea());
    draw_topbar(a, "概览", sub);
    draw_section(a);
    draw_grid(a);
}

/* 分区标题：当前地区 + 实体数 / 页码（HA frontend 的 section header） */
static void draw_section(App *a)
{
    const HaArea *ar = (a->area_sel >= 0) ? ha_area(a->area_sel) : NULL;
    const char *nm = (ar && ar->name[0]) ? ar->name : "全部实体";
    gfx_text(F_HEAD, nm, CX, SECTION_Y, C_TEXT);

    char right[96];
    /* 滚动式网格没有「页」的概念了，改显示当前可见区间；
     * 下面还有内容时补一个 ↓，提示可以继续往下走 */
    int first = a->grid_row * TILE_COLS;
    int last = first + PER_PAGE;
    if (last > a->nview) last = a->nview;
    if (a->nview > PER_PAGE && first + 1 <= last)
        snprintf(right, sizeof right, "%d 个实体 · %d-%d%s",
                 a->nview, first + 1, last, last < a->nview ? "  ↓" : "");
    else
        snprintf(right, sizeof right, "%d 个实体", a->nview);
    gfx_text_right(F_TINY, right, UI_W - PAD, SECTION_Y + 12, C_TEXT3);
    /* 标题与分割线之间留出一段空气：字号变大后原先 40px 的行距贴得太紧 */
    gfx_fill((SDL_Rect){ CX, SECTION_Y + 48, CW, 1 }, C_LINE);
}

/* ---------------- 绘制：类型筛选条 ---------------- */

static const char *FILT_NAME[] = { "全部", "灯", "开关", "传感器", "摄像头", "收藏" };
#define FILT_N 6

static SDL_Rect chip_rect(int i)
{
    int x = CX;
    for (int k = 0; k < i; k++) {
        int w = (k == FILT_FAV) ? 64 : gfx_text_w(F_SMALL, FILT_NAME[k]) + 44;
        x += w + 10;
    }
    int w = (i == FILT_FAV) ? 64 : gfx_text_w(F_SMALL, FILT_NAME[i]) + 44;
    return (SDL_Rect){ x, CHIPS_Y, w, CHIPS_H };
}

static void draw_chips_area(App *a)
{
    for (int i = 0; i < FILT_N; i++) {
        SDL_Rect r = chip_rect(i);
        int on  = (a->filter_type == i);
        int foc = (a->zone == ZONE_CHIPS && a->chips_sel == i);

        gfx_round(r, CHIPS_H / 2, on ? C_BLUE : C_CARD);
        if (on) {
            if (foc) gfx_round_border(r, CHIPS_H / 2, C_WHITE, 2, C_BLUE);
        } else {
            gfx_round_border(r, CHIPS_H / 2, foc ? C_BLUE : C_LINE,
                             foc ? 2 : 1, C_CARD);
        }

        if (i == FILT_FAV) {
            gfx_icon(IC_STAR, (SDL_Rect){ r.x + (r.w - 22) / 2, r.y + 9, 22, 22 },
                     on ? C_WHITE : C_BLUE);
        } else {
            gfx_text(F_SMALL, FILT_NAME[i], r.x + 22, r.y + 7,
                     on ? C_WHITE : C_TEXT2);
        }
    }

    /* 右侧：搜索词 + 当前结果数（只在有搜索时出现） */
    if (a->query[0]) {
        char info[128], show[64];
        snprintf(info, sizeof info, "搜索「%s」· %d 个结果", a->query, a->nview);
        gfx_ellipsis(F_TINY, info, 240, show, sizeof show);
        int w = gfx_text_w(F_TINY, show) + 28;
        SDL_Rect box = { UI_W - PAD - w, CHIPS_Y + 4, w, CHIPS_H - 8 };
        gfx_round(box, (CHIPS_H - 8) / 2, C_BLUE_S);
        gfx_text_box(F_TINY, show, box, C_BLUE_D);
    }
}

/* ---------------- 收藏（存 SD 卡，注销登录不丢） ---------------- */

#ifdef __SWITCH__
#define FAV_PATH "sdmc:/switch/switch-ha/fav.txt"
#else
#define FAV_PATH "data/fav.txt"
#endif
#define FAV_MAX 40

static char g_fav[FAV_MAX][64];
static int  g_nfav;
static int  g_fav_loaded;

static void fav_load(void)
{
    if (g_fav_loaded) return;
    g_fav_loaded = 1;
    g_nfav = 0;
    FILE *f = fopen(FAV_PATH, "rb");
    if (!f) return;
    char line[96];
    while (fgets(line, sizeof line, f) && g_nfav < FAV_MAX) {
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
        if (!line[0]) continue;
        snprintf(g_fav[g_nfav++], sizeof g_fav[0], "%s", line);
    }
    fclose(f);
}

static void fav_save(void)
{
    FILE *f = fopen(FAV_PATH, "wb");
    if (!f) return;
    for (int i = 0; i < g_nfav; i++) fprintf(f, "%s\n", g_fav[i]);
    fclose(f);
}

static int fav_index(const char *eid)
{
    fav_load();
    for (int i = 0; i < g_nfav; i++)
        if (strcmp(g_fav[i], eid) == 0) return i;
    return -1;
}

static int fav_has(const char *eid) { return fav_index(eid) >= 0; }

/* 返回 1 = 现在已收藏，0 = 已取消，-1 = 收藏夹满了 */
static int fav_toggle(const char *eid)
{
    int at = fav_index(eid);
    if (at >= 0) {
        for (int i = at; i + 1 < g_nfav; i++)
            snprintf(g_fav[i], sizeof g_fav[0], "%s", g_fav[i + 1]);
        g_nfav--;
        fav_save();
        return 0;
    }
    if (g_nfav >= FAV_MAX) return -1;
    snprintf(g_fav[g_nfav++], sizeof g_fav[0], "%s", eid);
    fav_save();
    return 1;
}

/* 类型筛选：把 HA 的 domain 归到 5 个可切换的类别里 */
static int filt_match(int filt, const HaEnt *e)
{
    switch (filt) {
    case FILT_LIGHT:  return e->dom == DOM_LIGHT;
    case FILT_SWITCH: return e->dom == DOM_SWITCH || e->dom == DOM_FAN ||
                             e->dom == DOM_COVER || e->dom == DOM_CLIMATE ||
                             e->dom == DOM_LOCK  || e->dom == DOM_VACUUM ||
                             e->dom == DOM_HUMIDIFIER || e->dom == DOM_WATER_HEATER;
    case FILT_SENSOR: return e->dom == DOM_SENSOR || e->dom == DOM_BINARY;
    case FILT_CAMERA: return e->dom == DOM_CAMERA;
    case FILT_FAV:    return fav_has(e->eid);
    default:          return 1;
    }
}

/* ---------------- 绘制：HA Tile 卡片 ---------------- */

static SDL_Rect tile_rect(int slot)
{
    int col = slot % TILE_COLS;
    int row = slot / TILE_COLS;
    SDL_Rect r = { CX + col * (TILE_W + TILE_GX),
                   GRID_Y + row * (TILE_H + TILE_GY), TILE_W, TILE_H };
    return r;
}

static void draw_tile(App *a, int vi, int slot)
{
    const HaEnt *e = ha_ent(a->view[vi]);
    if (!e) return;

    SDL_Rect r = tile_rect(slot);
    int on = (e->on == 1 && e->online);
    int sel = (vi == a->card_sel && a->screen == SC_HOME &&
               a->zone == ZONE_DEVICE);
    SDL_Color bg = on ? C_PALE : C_CARD;

    /* 焦点环：卡片外再套一圈 3px 品牌蓝，中间留背景色当间隙。
     * 比原来贴边的单层描边更像 HA 的 focus ring，电视上更醒目。 */
    if (sel) {
        SDL_Rect ring = { r.x - 5, r.y - 5, r.w + 10, r.h + 10 };
        gfx_round_border(ring, 21, C_BLUE, 3, C_BG);
    }

    /* 极轻的投影（HA card elevation 1） */
    gfx_round((SDL_Rect){ r.x, r.y + 2, r.w, r.h }, 18, C_SHADOW);
    gfx_round(r, 18, bg);
    gfx_round_border(r, 18, on ? C_BLUE_S : C_LINE, 1, bg);

    /* 状态图标：开启=品牌蓝，关闭=中性灰（HA Tile card 的规则） */
    SDL_Rect cir = { r.x + 18, r.y + 14, 44, 44 };
    gfx_circle(cir.x + 22, cir.y + 22, 22, on ? C_BLUE : C_OFFBG);
    gfx_icon(kind_icon(e->dom), (SDL_Rect){ cir.x + 10, cir.y + 10, 24, 24 },
             on ? C_WHITE : C_OFFFG);

    /* 收藏标记：右上角小星，和侧栏「收藏」分组呼应 */
    if (fav_has(e->eid))
        gfx_icon(IC_STAR, (SDL_Rect){ r.x + r.w - 38, r.y + 16, 20, 20 }, C_BLUE);

    if (e->dom == DOM_CAMERA && !fav_has(e->eid)) {
        SDL_Rect tag = { r.x + r.w - 66, r.y + 18, 48, 26 };
        gfx_round(tag, 13, C_BLUE_S);
        gfx_text_box(F_TINY, "查看", tag, C_BLUE_D);
    }

    char name[64], st[64];
    gfx_ellipsis(F_SMALL, e->name, r.w - 36, name, sizeof name);
    ent_status(e, st, sizeof st);

    gfx_text(F_SMALL, name, r.x + 18, r.y + 66, C_TEXT);
    char sts[64];
    gfx_ellipsis(F_TINY, st, r.w - 36, sts, sizeof sts);
    gfx_text(F_TINY, sts, r.x + 18, r.y + 97, on ? C_BLUE_D : C_TEXT3);

    if (e->level >= 0 && e->online) {
        SDL_Rect track = { r.x + 18, r.y + TILE_H - 16, r.w - 36, 4 };
        gfx_round(track, 2, C_LINE);
        SDL_Rect fill = track;
        fill.w = track.w * e->level / 100;
        if (fill.w < 4) fill.w = 4;
        gfx_round(fill, 2, on ? C_BLUE : C_BORDER);
    }
}

static void draw_grid(App *a)
{
    if (a->nview == 0) {
        int filtered = (a->filter_type != FILT_ALL) || a->query[0];
        SDL_Rect card = { CX, GRID_Y + 12, CW, 180 };
        gfx_round(card, 20, C_CARD);
        gfx_round_border(card, 20, C_LINE, 1, C_CARD);
        gfx_icon(filtered ? IC_SEARCH : IC_INFO,
                 (SDL_Rect){ CX + CW / 2 - 24, card.y + 30, 48, 48 }, C_BORDER);
        gfx_text_center(F_HEAD, filtered ? "没有匹配的实体" : "这里还没有实体",
                        CX + CW / 2, card.y + 92, C_TEXT2);
        const char *hint;
        if (filtered)
            hint = "换个筛选，或按 Y 清除搜索条件";
        else
            hint = ha_ready() ? "换个地区看看，或按 X 重新同步"
                              : "请先在设置里连接 Home Assistant";
        gfx_text_center(F_SMALL, hint, CX + CW / 2, card.y + 128, C_TEXT3);
        return;
    }

    int first = a->grid_row * TILE_COLS;
    int n = a->nview - first;
    if (n > PER_PAGE) n = PER_PAGE;
    /* 底部再画一条渐隐提示：还有内容没显示完时给个「能继续往下」的线索 */
    for (int i = 0; i < n; i++) draw_tile(a, first + i, i);
}

/* ---------------- 绘制：表单 / onboarding ---------------- */

/* 按像素宽度折行绘制（UTF-8 安全），返回结束时的 y */
static int draw_wrapped(int font, const char *s, int x, int y, int maxw, int lh,
                        SDL_Color c, int maxy)
{
    char line[256];
    int n = (int)strlen(s);
    int i = 0;

    while (i < n && y + lh <= maxy) {
        int take = 0;
        while (i + take < n && s[i + take] != '\n') {
            unsigned char ch = (unsigned char)s[i + take];
            int step = 1;
            if (ch >= 0xF0) step = 4;
            else if (ch >= 0xE0) step = 3;
            else if (ch >= 0xC0) step = 2;

            int len = take + step;
            if (i + len > n) len = n - i;
            if (len >= (int)sizeof line) break;

            memcpy(line, s + i, (size_t)len);
            line[len] = '\0';

            if (gfx_text_w(font, line) > maxw && take > 0) break;
            take = len;
        }

        if (take == 0) {
            unsigned char ch = (unsigned char)s[i];
            if (ch == '\n') { i++; continue; }
            take = (ch >= 0xF0) ? 4 : (ch >= 0xE0) ? 3 : (ch >= 0xC0) ? 2 : 1;
            if (i + take > n) take = n - i;
        }

        memcpy(line, s + i, (size_t)take);
        line[take] = '\0';
        gfx_text(font, line, x, y, c);
        y += lh;

        i += take;
        if (i < n && s[i] == '\n') i++;
    }
    return y;
}

/* HATextField：浅底 + 聚焦时 2px 品牌色边框 */
static void draw_input_box(SDL_Rect r, const char *value, const char *placeholder,
                           int focused, int password)
{
    gfx_round(r, 12, C_BG);
    gfx_round_border(r, 12, focused ? C_BLUE : C_LINE, focused ? 2 : 1, C_BG);

    char shown[192];
    if (value && value[0]) {
        if (password) {
            size_t n = strlen(value);
            if (n > 40) n = 40;
            memset(shown, '*', n);
            shown[n] = '\0';
        } else {
            gfx_ellipsis(F_BODY, value, r.w - 36, shown, sizeof shown);
        }
        gfx_text(F_BODY, shown, r.x + 18, r.y + (r.h - 24) / 2, C_TEXT);
    } else {
        gfx_text(F_BODY, placeholder, r.x + 18, r.y + (r.h - 24) / 2, C_TEXT3);
    }
}

/* HAAccentButton */
static void draw_primary_button(SDL_Rect b, const char *label, int focused)
{
    if (focused) {
        SDL_Rect ring = { b.x - 4, b.y - 4, b.w + 8, b.h + 8 };
        gfx_round(ring, 20, C_BLUE_S);
    }
    gfx_round(b, 16, C_BLUE);
    gfx_text_box(F_HEAD, label, b, C_WHITE);
}

/* onboarding 左侧品牌区（HA 品牌蓝），step 1=地址 2=登录 */
static void draw_brand_panel(int step)
{
    /* 品牌蓝 → 深蓝 的纵向渐变（HA 品牌色 #18BCF2 / Primary30 #006787） */
    SDL_Color top = { HA_BLUE }, bot = C_DEEP;
    for (int y = 0; y < UI_H; y++) {
        int t = y * 100 / UI_H;
        SDL_Color c = {
            (Uint8)(top.r + (bot.r - top.r) * t / 100),
            (Uint8)(top.g + (bot.g - top.g) * t / 100),
            (Uint8)(top.b + (bot.b - top.b) * t / 100), 255
        };
        gfx_fill((SDL_Rect){ 0, y, BRAND_W, 1 }, c);
    }
    gfx_ha_logo((SDL_Rect){ 56, 76, 84, 84 }, 1);

    gfx_text(F_TITLE, "Home Assistant", 56, 228, C_WHITE);
    gfx_text(F_SMALL, "Switch Companion", 56, 274, (SDL_Color){ 200, 232, 247, 255 });
    gfx_text(F_BODY, "把整个家搬到电视上", 56, 336, C_WHITE);
    gfx_text(F_TINY, "概览 · 地区 · 自动化 · 设置", 56, 376,
             (SDL_Color){ 168, 214, 234, 255 });

    /* 步骤指示 */
    for (int i = 0; i < 2; i++) {
        int y = 452 + i * 52;
        int on = (step == i + 1);
        gfx_circle(72, y + 12, 12, on ? C_WHITE : (SDL_Color){ 90, 150, 178, 255 });
        if (on) gfx_circle(72, y + 12, 6, (SDL_Color){ HA_BLUE });
        gfx_text(F_BODY, i == 0 ? "连接服务器" : "登录账号", 100, y,
                 on ? C_WHITE : (SDL_Color){ 154, 196, 216, 255 });
    }

    gfx_fill((SDL_Rect){ 56, UI_H - 92, BRAND_W - 112, 1 },
             (SDL_Color){ 90, 150, 178, 255 });
    gfx_text(F_TINY, "基于官方 HATheme 设计系统", 56, UI_H - 72,
             (SDL_Color){ 168, 214, 234, 255 });
    gfx_text(F_TINY, "home-assistant/android · 2026.11.1", 56, UI_H - 46,
             (SDL_Color){ 168, 214, 234, 255 });
}

/* 表单区几何：绘制与触控共用 */
#define FORM_X  (BRAND_W + 72)
#define FORM_W  (UI_W - BRAND_W - 72 - 64)
#define IN_H    56

static SDL_Rect form_input(int n)
{
    int y[3] = { 228, 334, 440 };
    int h[3] = { IN_H, IN_H, 52 };
    return (SDL_Rect){ FORM_X, y[n], FORM_W, h[n] };
}

/* ---------------- 地址页取消 ---------------- */

static SDL_Rect setup_cancel_rect(void)
{
    return (SDL_Rect){ UI_W - PAD - 128, UI_H - 60, 128, 40 };
}

/* 退出「服务器地址」页：丢弃未提交的输入，已保存的配置一点不动。
 * 首次启动（没有可回的地方）不放行，只提示先把地址填完。 */
static void setup_cancel(App *a)
{
    if (!a->setup_from) {
        snprintf(a->status, sizeof a->status,
                 "%s", "!首次使用，请先填写服务器地址");
        return;
    }
    snprintf(a->addr, sizeof a->addr, "%s", ha_base1());
    snprintf(a->alt, sizeof a->alt, "%s", ha_alt());
    snprintf(a->ip, sizeof a->ip, "%s", ha_ip());
    a->status[0] = '\0';
    a->screen = SC_HOME;
    a->tab = 2;
    a->set_sel = 2;         /* 焦点回到「更换服务器地址」这一行 */
    a->zone = ZONE_DEVICE;
    ui_toast(a, 1, "已取消，服务器设置保持不变");
}

static void draw_setup(App *a)
{
    gfx_fill((SDL_Rect){ 0, 0, UI_W, UI_H }, C_BG);
    draw_brand_panel(1);

    gfx_text(F_TITLE, "连接到 Home Assistant", FORM_X, 82, C_TEXT);
    gfx_text(F_SMALL, "填写内部网址即可开始；外部网址用于出门时自动切换", FORM_X, 132, C_TEXT2);

    gfx_text(F_HEAD, "内部网址", FORM_X, 184, C_TEXT);
    gfx_text_right(F_TINY, "家里优先", FORM_X + FORM_W, 192, C_TEXT3);
    draw_input_box(form_input(0), a->addr, "http://192.168.1.100:8123",
                   a->login_focus == 0, 0);

    gfx_text(F_HEAD, "外部网址", FORM_X, 290, C_TEXT);
    gfx_text_right(F_TINY, "可留空", FORM_X + FORM_W, 298, C_TEXT3);
    draw_input_box(form_input(1), a->alt, "https://ha.example.com",
                   a->login_focus == 1, 0);

    gfx_text(F_HEAD, "服务器 IP", FORM_X, 396, C_TEXT);
    gfx_text_right(F_TINY, "可选 · 解析不到域名时填", FORM_X + FORM_W, 404, C_TEXT3);
    draw_input_box(form_input(2), a->ip, "地址填域名而 DNS 解析失败时，在这里直接填 IP",
                   a->login_focus == 2, 0);

    SDL_Rect btn = { FORM_X, 512, FORM_W, 60 };
    draw_primary_button(btn, a->busy ? "正在连接…" : "连接", a->login_focus == 3);

    if (a->status[0]) {
        SDL_Color c = (a->status[0] == '!') ? C_ERR : C_OK;
        gfx_text(F_SMALL, a->status[0] == '!' ? a->status + 1 : a->status,
                 FORM_X, 590, c);
    } else {
        gfx_text(F_SMALL, "两条网址都填上，内网不通会自动改走外网", FORM_X, 590, C_TEXT3);
    }

    /* 从设置页误碰进来的，右下角给一个明确的「取消」，触屏也能退 */
    if (a->setup_from) {
        SDL_Rect c = setup_cancel_rect();
        gfx_round(c, 20, C_CARD);
        gfx_round_border(c, 20, C_LINE, 1, C_CARD);
        gfx_icon(IC_CHEVRON_L, (SDL_Rect){ c.x + 16, c.y + 9, 22, 22 }, C_TEXT2);
        gfx_text(F_SMALL, "取消", c.x + 44, c.y + 8, C_TEXT2);
    }
    gfx_text(F_TINY,
             a->setup_from ? "↑↓ 切换 · A 输入/连接 · X 连接性检查 · B 取消"
                           : "↑↓ 切换 · A 输入/连接 · X 连接性检查",
             FORM_X, UI_H - 42, C_TEXT3);
}

static SDL_Rect login_input(int n)
{
    int y[2] = { 228, 334 };
    return (SDL_Rect){ FORM_X, y[n], FORM_W, IN_H };
}

static void draw_login(App *a)
{
    gfx_fill((SDL_Rect){ 0, 0, UI_W, UI_H }, C_BG);
    draw_brand_panel(2);

    gfx_text(F_TITLE, "登录 Home Assistant", FORM_X, 82, C_TEXT);
    char sub[220];
    snprintf(sub, sizeof sub, "%.180s", ha_base()[0] ? ha_base() : "未设置服务器");
    gfx_text(F_SMALL, sub, FORM_X, 132, C_TEXT2);

    if (!a->use_token) {
        gfx_text(F_HEAD, "用户名", FORM_X, 184, C_TEXT);
        draw_input_box(login_input(0), a->user, "HA 登录名",
                       a->login_focus == 1, 0);
        gfx_text(F_HEAD, "密码", FORM_X, 290, C_TEXT);
        draw_input_box(login_input(1), a->pass, "登录密码",
                       a->login_focus == 2, 1);
    } else {
        gfx_text(F_HEAD, "长期访问令牌", FORM_X, 184, C_TEXT);
        draw_input_box(login_input(0), a->token, "粘贴令牌",
                       a->login_focus == 3, 1);
        gfx_text(F_SMALL, "HA 网页 → 头像 → 安全 → 长期访问令牌 → 创建",
                 FORM_X, 300, C_TEXT3);
        gfx_text(F_SMALL, "启用了两步验证的账号必须用这种方式",
                 FORM_X, 332, C_TEXT3);
    }

    SDL_Rect btn = { FORM_X, 424, FORM_W, 60 };
    draw_primary_button(btn, a->busy ? "正在登录…" : "登录", a->login_focus == 4);

    SDL_Rect sw = { FORM_X, 504, FORM_W, 44 };
    gfx_round(sw, 12, C_BLUE_S);
    gfx_text_box(F_SMALL, a->use_token ? "改用账号密码登录" : "改用长期访问令牌登录",
                 sw, C_BLUE_D);

    if (a->status[0])
        gfx_text(F_SMALL, a->status[0] == '!' ? a->status + 1 : a->status,
                 FORM_X, 574, a->status[0] == '!' ? C_ERR : C_TEXT2);
    gfx_text(F_TINY, "B 返回改地址 · A 确认", FORM_X, UI_H - 42, C_TEXT3);
}

/* ---------------- 绘制：More-info 底部弹层 ---------------- */

static void draw_dev(App *a)
{
    const HaEnt *e = ha_ent(a->dev_idx);
    if (!e) { a->screen = SC_HOME; return; }

    draw_main_back(a);
    gfx_dim((SDL_Rect){ 0, 0, UI_W, UI_H }, 105);

    SDL_Rect s = { UI_W / 2 - 360, 104, 720, 512 };
    gfx_round(s, 24, C_CARD);

    /* drag handle（HAModalBottomSheet） */
    gfx_round((SDL_Rect){ UI_W / 2 - 24, s.y + 14, 48, 4 }, 2, C_BORDER);

    SDL_Rect close = { s.x + s.w - 56, s.y + 24, 36, 36 };
    gfx_round(close, 18, C_BG);
    gfx_icon(IC_CLOSE, (SDL_Rect){ close.x + 8, close.y + 8, 20, 20 }, C_TEXT2);

    int on = (e->on == 1 && e->online);
    gfx_circle(s.x + 92, s.y + 108, 44, on ? C_BLUE : C_OFFBG);
    gfx_icon(kind_icon(e->dom), (SDL_Rect){ s.x + 68, s.y + 84, 48, 48 },
             on ? C_WHITE : C_TEXT3);

    char name[64];
    gfx_ellipsis(F_HEAD, e->name, 420, name, sizeof name);
    gfx_text(F_HEAD, name, s.x + 156, s.y + 74, C_TEXT);

    char st[64];
    ent_status(e, st, sizeof st);
    gfx_text(F_BODY, st, s.x + 156, s.y + 112, on ? C_BLUE_D : C_TEXT2);
    gfx_text(F_TINY, e->eid, s.x + 156, s.y + 144, C_TEXT3);

    if (e->area[0]) {
        int aw = gfx_text_w(F_TINY, e->area) + 28;
        SDL_Rect tag = { s.x + s.w - 44 - aw, s.y + 74, aw, 32 };
        gfx_round(tag, 16, C_BLUE_S);
        gfx_text_box(F_TINY, e->area, tag, C_BLUE_D);
    }

    /* 大开关按钮 */
    gfx_circle(UI_W / 2, s.y + 234, 44, on ? C_BLUE : C_OFFBG);
    gfx_icon(kind_icon(e->dom), (SDL_Rect){ UI_W / 2 - 22, s.y + 212, 44, 44 },
             on ? C_WHITE : C_TEXT3);
    gfx_text_center(F_TINY, e->dom == DOM_CAMERA ? "查看画面" : "点击开关",
                    UI_W / 2, s.y + 290, C_TEXT3);

    /* 亮度 / 开合滑条（HASlider 风格） */
    if (e->level >= 0 && e->online && e->dom != DOM_CAMERA) {
        const char *label = (e->dom == DOM_COVER) ? "开合" : "亮度";
        gfx_text(F_SMALL, label, s.x + 48, s.y + 326, C_TEXT2);
        char vv[16];
        snprintf(vv, sizeof vv, "%d%%", e->level);
        gfx_text_right(F_SMALL, vv, s.x + s.w - 48, s.y + 326, C_BLUE_D);

        SDL_Rect track = { s.x + 48, s.y + 366, s.w - 96, 12 };
        gfx_round(track, 6, C_LINE);
        SDL_Rect fill = track;
        fill.w = track.w * e->level / 100;
        if (fill.w < 12) fill.w = 12;
        gfx_round(fill, 6, C_BLUE);
        int kx = track.x + track.w * e->level / 100;
        if (kx < track.x + 12) kx = track.x + 12;
        gfx_circle(kx, track.y + 6, 12, C_WHITE);
        gfx_circle(kx, track.y + 6, 9, C_BLUE);
        gfx_text_center(F_TINY, "← → 调节，A 开关", UI_W / 2, s.y + 396, C_TEXT3);
    } else {
        /* 没有可调参数时补一段实体属性（HA more-info 的属性区） */
        SDL_Rect info = { s.x + 48, s.y + 326, s.w - 96, 92 };
        gfx_round(info, 12, C_BG);
        gfx_text(F_TINY, "地区", info.x + 20, info.y + 14, C_TEXT3);
        gfx_text(F_BODY, e->area[0] ? e->area : "未分配",
                 info.x + 20, info.y + 38, C_TEXT);
        gfx_text(F_TINY, "实体 ID", info.x + 320, info.y + 14, C_TEXT3);
        char eid[64];
        gfx_ellipsis(F_BODY, e->eid, 240, eid, sizeof eid);
        gfx_text(F_BODY, eid, info.x + 320, info.y + 38, C_TEXT);
        gfx_text_center(F_TINY, "A 开关 · B 返回 · X 重新同步",
                        UI_W / 2, info.y + 68, C_TEXT3);
    }

    SDL_Rect back = { s.x + 48, s.y + s.h - 76, s.w - 96, 52 };
    gfx_round(back, 26, C_BG);
    gfx_text_box(F_BODY, "返回概览", back, C_TEXT2);

}

static void draw_cam(App *a)
{
    const HaEnt *e = NULL;
    for (int i = 0; i < ha_nent(); i++) {
        const HaEnt *c = ha_ent(i);
        if (strcmp(c->eid, a->cam_eid) == 0) { e = c; break; }
    }

    gfx_fill((SDL_Rect){ 0, 0, UI_W, UI_H }, (SDL_Color){ 20, 20, 20, 255 });

    SDL_Rect box = { 0, 0, UI_W, UI_H - 76 };
    if (!cam_draw(box)) {
        gfx_icon(IC_CAMERA, (SDL_Rect){ UI_W / 2 - 40, UI_H / 2 - 90, 80, 80 },
                 (SDL_Color){ 90, 96, 104, 255 });
        gfx_text_center(F_HEAD, cam_status(), UI_W / 2, UI_H / 2 + 10,
                        (SDL_Color){ 170, 176, 184, 255 });
    }

    /* HA 风格底部信息条 */
    gfx_fill((SDL_Rect){ 0, UI_H - 76, UI_W, 76 }, (SDL_Color){ 32, 36, 40, 255 });
    SDL_Rect bk = { 24, UI_H - 62, 48, 48 };
    gfx_round(bk, 24, (SDL_Color){ 56, 60, 64, 255 });
    gfx_icon(IC_CHEVRON_L, (SDL_Rect){ bk.x + 12, bk.y + 12, 24, 24 }, C_WHITE);
    gfx_text(F_SMALL, e ? e->name : a->cam_eid, 88, UI_H - 64,
             (SDL_Color){ 235, 238, 242, 255 });
    gfx_text(F_TINY, cam_status(), 88, UI_H - 34, (SDL_Color){ 150, 156, 164, 255 });
    gfx_text_right(F_TINY, "B 返回 · X 重连", UI_W - PAD, UI_H - 54,
                   (SDL_Color){ 150, 156, 164, 255 });
}

/* 连接性检查：对齐官方 connection_check_* 的分步思路 */
static void draw_diag(App *a)
{
    gfx_fill((SDL_Rect){ 0, 0, UI_W, UI_H }, C_BG);
    draw_sidebar(a);
    draw_topbar(a, "连接性检查", ha_base());

    SDL_Rect card = { CX, 116, CW, UI_H - 116 - 72 };
    gfx_round(card, 20, C_CARD);
    gfx_round_border(card, 20, C_LINE, 1, C_CARD);

    const char *txt = a->diag[0] ? a->diag : "还没有诊断结果";
    draw_wrapped(F_SMALL, txt, card.x + 28, card.y + 22, card.w - 56, 28, C_TEXT,
                 card.y + card.h - 20);

    gfx_text_center(F_TINY, "B 返回 · A 重新检查", UI_W / 2, UI_H - 44, C_TEXT3);
}

/* ---------------- 绘制：自动化 / 设置 ---------------- */

static void draw_scenes(App *a)
{
    if (ha_nscene() == 0) {
        SDL_Rect card = { CX, 168, CW, 180 };
        gfx_round(card, 20, C_CARD);
        gfx_round_border(card, 20, C_LINE, 1, C_CARD);
        gfx_icon(IC_SMART, (SDL_Rect){ CX + CW / 2 - 24, card.y + 30, 48, 48 }, C_BORDER);
        gfx_text_center(F_HEAD, "还没有自动化", CX + CW / 2, card.y + 92, C_TEXT2);
        gfx_text_center(F_SMALL, "在 HA 里创建的场景与脚本会出现在这里",
                        CX + CW / 2, card.y + 128, C_TEXT3);
        return;
    }

    int page = a->scene_sel / 5;
    int first = page * 5;
    int shown = ha_nscene() - first;
    if (shown > 5) shown = 5;
    SDL_Rect card = { CX, SCENE_TOP, CW, shown * SCENE_ROW + 16 };
    gfx_round(card, 20, C_CARD);
    gfx_round_border(card, 20, C_LINE, 1, C_CARD);

    for (int i = 0; i < shown && first + i < ha_nscene(); i++) {
        const HaEnt *s = ha_scene(first + i);
        SDL_Rect r = { card.x + 8, card.y + 8 + i * SCENE_ROW, card.w - 16, SCENE_ROW - 8 };
        int sel = (first + i) == a->scene_sel;

        gfx_round(r, 12, sel ? C_PALE : C_CARD);
        gfx_circle(r.x + 44, r.y + r.h / 2, 22, sel ? C_BLUE : C_OFFBG);
        gfx_icon(s->dom == DOM_SCRIPT ? IC_SCRIPT : IC_SMART,
                 (SDL_Rect){ r.x + 32, r.y + r.h / 2 - 12, 24, 24 },
                 sel ? C_WHITE : C_TEXT3);

        char nm[64];
        gfx_ellipsis(F_BODY, s->name, r.w - 260, nm, sizeof nm);
        gfx_text(F_BODY, nm, r.x + 82, r.y + 14, C_TEXT);
        if (s->area[0])
            gfx_text(F_TINY, s->area, r.x + 82, r.y + 45, C_TEXT3);
        else
            gfx_text(F_TINY, s->eid, r.x + 82, r.y + 45, C_TEXT3);

        SDL_Rect btn = { r.x + r.w - 122, r.y + 18, 100, 38 };
        gfx_round(btn, 19, C_BLUE);
        gfx_text_box(F_TINY, "执行", btn, C_WHITE);

        if (i < 4 && first + i + 1 < ha_nscene())
            gfx_fill((SDL_Rect){ r.x + 82, r.y + r.h - 1, r.w - 100, 1 }, C_LINE);
    }

    if (ha_nscene() > 5) {
        char pg[48];
        snprintf(pg, sizeof pg, "共 %d 项 · 第 %d 页", ha_nscene(), page + 1);
        gfx_text_right(F_TINY, pg, UI_W - PAD, SCENE_TOP - 28, C_TEXT3);
    }
}

/* 外观分段控件（设置页右上角）：浅色 / 深色 */
static SDL_Rect theme_seg(int which)
{
    SDL_Rect seg = { UI_W - PAD - 220, 108, 220, 40 };
    return which ? (SDL_Rect){ seg.x + 110, seg.y + 2, 108, 36 }
                 : (SDL_Rect){ seg.x + 2, seg.y + 2, 108, 36 };
}

static void draw_settings(App *a)
{
    /* 外观切换：分段控件比下拉更直接，Y 键也能切；
     * 焦点用 ↑ 从「重新同步实体」再往上一步进入（set_sel = -1），
     * ←/→ 直接选浅色/深色，A 键切换当前值。 */
    {
        SDL_Rect seg = { UI_W - PAD - 220, 108, 220, 40 };
        SDL_Rect l = theme_seg(0), r = theme_seg(1);
        int foc = (a->set_sel < 0);
        SDL_Rect sel = g_dark ? r : l;
        /* 注意顺序：gfx_round_border 会把内部整体填充，必须先画框再画字 */
        gfx_round(seg, 20, C_BG);
        gfx_round_border(seg, 20, foc ? C_BLUE : C_LINE, foc ? 2 : 1, C_BG);
        if (foc)
            gfx_round_border((SDL_Rect){ sel.x - 3, sel.y - 3, sel.w + 6, sel.h + 6 },
                             20, C_BLUE, 2, C_BG);
        gfx_round(sel, 18, C_CARD);
        gfx_text_box(F_TINY, "浅色", l, g_dark ? C_TEXT3 : C_TEXT);
        gfx_text_box(F_TINY, "深色", r, g_dark ? C_TEXT : C_TEXT3);
        gfx_text_box(F_TINY, g_dark ? "深色" : "浅色", sel, g_dark ? C_TEXT : C_BLUE_D);
    }

    /* 分组：连接 */
    gfx_text(F_SMALL, "连接", CX, 118, C_TEXT3);
    SDL_Rect c1 = { CX, SET_CARD1_Y, CW, 2 * 74 + 16 };
    gfx_round(c1, 20, C_CARD);
    gfx_round_border(c1, 20, C_LINE, 1, C_CARD);

    for (int i = 0; i < 2; i++) {
        SDL_Rect r = { c1.x + 8, c1.y + 8 + i * 74, c1.w - 16, 66 };
        gfx_circle(r.x + 42, r.y + 33, 22, i == 0 ? C_BLUE_S : C_BG);
        gfx_icon(i == 0 ? IC_WIFI : IC_LOCK,
                 (SDL_Rect){ r.x + 30, r.y + 21, 24, 24 },
                 i == 0 ? C_BLUE_D : (ha_ready() ? C_OK : C_TEXT3));
        if (i == 0) {
            gfx_text(F_SMALL, "服务器", r.x + 80, r.y + 8, C_TEXT2);
            char b[96];
            gfx_ellipsis(F_BODY, ha_base()[0] ? ha_base() : "未设置", c1.w - 300, b, sizeof b);
            gfx_text(F_BODY, b, r.x + 80, r.y + 33, C_TEXT);
            gfx_text_right(F_TINY, ha_route(), r.x + r.w - 24, r.y + 22, C_BLUE_D);
        } else {
            gfx_text(F_SMALL, "账号", r.x + 80, r.y + 8, C_TEXT2);
            gfx_text(F_BODY, ha_user()[0] ? ha_user() : "未登录", r.x + 80, r.y + 33, C_TEXT);
        }
        if (i == 0) gfx_fill((SDL_Rect){ r.x + 80, r.y + 64, r.w - 96, 1 }, C_LINE);
    }

    /* 分组：操作 */
    gfx_text(F_SMALL, "操作", CX, SET_CARD2_Y - 34, C_TEXT3);
    SDL_Rect c2 = { CX, SET_CARD2_Y, CW, 5 * SET_ROW_H + 16 };
    gfx_round(c2, 20, C_CARD);
    gfx_round_border(c2, 20, C_LINE, 1, C_CARD);

    static const char *acts[5] = {
        "重新同步实体", "连接性检查", "更换服务器地址", "检查更新", "注销"
    };
    static const int aicons[5] = { IC_REFRESH, IC_WIFI, IC_GEAR, IC_INFO, IC_LOCK };
    for (int i = 0; i < 5; i++) {
        SDL_Rect r = { c2.x + 8, c2.y + 8 + i * SET_ROW_H, c2.w - 16, SET_ROW_H - 6 };
        int sel = (a->set_sel == i);
        gfx_round(r, 12, sel ? C_PALE : C_CARD);
        gfx_icon(aicons[i], (SDL_Rect){ r.x + 26, r.y + 13, 24, 24 },
                 i == 4 ? C_ERR : C_BLUE_D);
        gfx_text(F_BODY, acts[i], r.x + 70, r.y + 10, i == 4 ? C_ERR : C_TEXT);
        if (i == 3) {
            /* 当前行右侧显示当前版本，方便和发布页对版本 */
            char vs[32];
            snprintf(vs, sizeof vs, "v%s", upd_cur_version());
            gfx_text_right(F_TINY, vs, r.x + r.w - 54, r.y + 17, C_TEXT3);
        }
        gfx_icon(IC_CHEVRON_R, (SDL_Rect){ r.x + r.w - 44, r.y + 13, 24, 24 }, C_TEXT3);
        if (i < 4) gfx_fill((SDL_Rect){ r.x + 70, r.y + SET_ROW_H - 8, r.w - 86, 1 }, C_LINE);
    }
}

/* ---------------- 主绘制 ---------------- */

static void ui_draw_screen(App *a)
{
    if (a->screen == SC_SETUP) { draw_setup(a); return; }
    if (a->screen == SC_LOGIN) { draw_login(a); return; }
    if (a->screen == SC_DEV)   { draw_dev(a); return; }
    if (a->screen == SC_CAM)   { draw_cam(a); return; }
    if (a->screen == SC_DIAG)  { draw_diag(a); return; }

    gfx_fill((SDL_Rect){ 0, 0, UI_W, UI_H }, C_BG);
    draw_sidebar(a);

    const char *title = "概览";
    char sub[96];
    if (a->tab == 1) {
        title = "自动化";
        snprintf(sub, sizeof sub, "%d 个场景与脚本", ha_nscene());
    } else if (a->tab == 2) {
        title = "设置";
        snprintf(sub, sizeof sub, "Home Assistant Companion · v%s",
                 upd_cur_version());
    } else {
        /* 概览副标题带数据新鲜度：一眼能看出是不是自动刷新过 */
        char fresh[32] = "";
        if (a->sync_ms) {
            Uint32 age = (SDL_GetTicks() - a->sync_ms) / 1000;
            if (age < 10)      snprintf(fresh, sizeof fresh, " · 刚刚更新");
            else if (age < 60) snprintf(fresh, sizeof fresh, " · %u 秒前", age);
            else               snprintf(fresh, sizeof fresh, " · %u 分钟前", age / 60);
        }
        snprintf(sub, sizeof sub, "%d 个实体 · %d 个地区%s",
                 ha_nent(), ha_narea(), fresh);
    }
    draw_topbar(a, title, sub);

    if (a->tab == 0) {
        draw_section(a);
        draw_chips_area(a);
        draw_grid(a);
    } else if (a->tab == 1) {
        draw_scenes(a);
    } else {
        draw_settings(a);
    }

    /* 底部常驻按键提示：做成胶囊条，不抢内容区的视觉重量 */
    {
        const char *hint =
            "L/R 筛选 · ZL/ZR 房间 · Y 搜索 · A 打开 · 连按两次 B 退出 · X 同步";
        if (a->tab == 2)
            hint = "↑↓ 选择 · ←→ 切外观 · A 确认 · Y 切主题 · B 返回";
        else if (a->tab == 1)
            hint = "↑↓ 选择 · A 执行 · ZL/ZR 切页 · B 返回";
        int w = gfx_text_w(F_TINY, hint) + 48;
        SDL_Rect pill = { SIDEBAR_W + (UI_W - SIDEBAR_W - w) / 2, UI_H - 48, w, 34 };
        gfx_round(pill, 17, C_CARD);
        gfx_round_border(pill, 17, C_LINE, 1, C_CARD);
        gfx_text_box(F_TINY, hint, pill, C_TEXT3);
    }

    /* HA 规范要求用 snackbar，不用 toast：深色圆角条 + 白字 */
    if (a->toast[0] && a->toast_frames > 0) {
        int tw = gfx_text_w(F_SMALL, a->toast) + 64;
        SDL_Rect t = { CX + (CW - tw) / 2, UI_H - 96, tw, 48 };
        gfx_round(t, 24, C_TOAST);
        gfx_text_box(F_SMALL, a->toast, t, C_TOAST_TX);
    }
}
/* ---------------- 升级确认弹层 ---------------- */

static void dlg_rects(SDL_Rect *card, SDL_Rect *cancel, SDL_Rect *ok)
{
    SDL_Rect c = { UI_W / 2 - 320, UI_H / 2 - 140, 640, 280 };
    if (card)   *card   = c;
    if (cancel) *cancel = (SDL_Rect){ c.x + 64,  c.y + 172, 240, 60 };
    if (ok)     *ok     = (SDL_Rect){ c.x + 336, c.y + 172, 240, 60 };
}

static void draw_dlg(App *a)
{
    gfx_dim((SDL_Rect){ 0, 0, UI_W, UI_H }, 150);

    SDL_Rect card, b0, b1;
    dlg_rects(&card, &b0, &b1);
    gfx_round(card, 24, C_CARD);

    char t[96];
    snprintf(t, sizeof t, "发现新版本 %s", a->dlg_ver);
    gfx_text_center(F_HEAD, t, UI_W / 2, card.y + 42, C_TEXT);
    gfx_text_center(F_SMALL, "当前版本 v" APP_VERSION_STR "，下载完成后重启应用生效",
                    UI_W / 2, card.y + 94, C_TEXT2);

    if (a->dlg_sel == 0) gfx_round(b0, 30, C_BLUE_D);
    else                 gfx_round_border(b0, 30, C_BORDER, 2, C_CARD);
    gfx_text_box(F_BODY, "取消", b0, a->dlg_sel == 0 ? C_WHITE : C_TEXT);

    if (a->dlg_sel == 1) gfx_round(b1, 30, C_BLUE);
    else                 gfx_round_border(b1, 30, C_BORDER, 2, C_CARD);
    gfx_text_box(F_BODY, "升级", b1, a->dlg_sel == 1 ? C_WHITE : C_TEXT);

    gfx_text_center(F_TINY, "← → 选择 · A 确认 · B 取消",
                    UI_W / 2, card.y + 244, C_TEXT3);
}

/* 更新完成的弹层：圆 + 对勾 + 一个主按钮（重启 / 关闭） */
static void upd_done_rects(SDL_Rect *card, SDL_Rect *btn)
{
    SDL_Rect c = { UI_W / 2 - 320, UI_H / 2 - 172, 640, 344 };
    if (card) *card = c;
    if (btn)  *btn  = (SDL_Rect){ UI_W / 2 - 130, c.y + 206, 260, 60 };
}

/* 粗线：像素风一格格填，斜线也不糊 */
static void thick_line(int x0, int y0, int x1, int y1, int w, SDL_Color c)
{
    int dx = x1 - x0, dy = y1 - y0;
    int n = (dx < 0 ? -dx : dx);
    if ((dy < 0 ? -dy : dy) > n) n = (dy < 0 ? -dy : dy);
    if (n == 0) n = 1;
    for (int i = 0; i <= n; i++) {
        int x = x0 + dx * i / n;
        int y = y0 + dy * i / n;
        gfx_fill((SDL_Rect){ x - w / 2, y - w / 2, w, w }, c);
    }
}

static void draw_upd_done(App *a)
{
    gfx_dim((SDL_Rect){ 0, 0, UI_W, UI_H }, 165);

    SDL_Rect card, btn;
    upd_done_rects(&card, &btn);
    gfx_round(card, 24, C_CARD);

    int cx = UI_W / 2;
    int cy = card.y + 72;
    gfx_round((SDL_Rect){ cx - 34, cy - 34, 68, 68 }, 34, C_BLUE);
    thick_line(cx - 15, cy + 1, cx - 3, cy + 13, 5, C_WHITE);
    thick_line(cx - 3, cy + 13, cx + 16, cy - 12, 5, C_WHITE);

    gfx_text_center(F_HEAD, "更新完成", cx, card.y + 126, C_TEXT);

    char sub[128];
    snprintf(sub, sizeof sub, "已下载 v%s，关闭应用后重新打开生效", a->upd_done_ver);
    gfx_text_center(F_SMALL, sub, cx, card.y + 172, C_TEXT2);

    /* 不做 chainload 自动重启：实测重启拉起的还是旧文件，
     * 不如老老实实关掉，用户从 hbmenu 重开一次就是新版 */
    gfx_round(btn, 30, C_BLUE);
    gfx_text_box(F_BODY, "关闭应用", btn, C_WHITE);

    gfx_text_center(F_TINY, "A 立即关闭 · B 稍后再关",
                    cx, card.y + 292, C_TEXT3);
}

void ui_draw(App *a)
{
    ui_draw_screen(a);
    if (a->dlg) draw_dlg(a);
    if (a->upd_done) draw_upd_done(a);
}

/* ---------------- 动作执行 ---------------- */

static void do_logout(App *a)
{
    ha_forget();
    a->tab = 0;
    a->screen = SC_SETUP;
    a->setup_from = 0;      /* 注销后没有可用配置，必须重新填 */
    a->login_focus = 0;
    a->status[0] = '\0';
    a->nview = 0;
    snprintf(a->addr, sizeof a->addr, "%s", "");
    ui_toast(a, 1, "已退出登录");
}

int ui_run_act(App *a)
{
    int act = a->act;
    if (!act) return 0;

    int idx = a->act_idx;
    int val = a->act_val;
    a->act = ACT_NONE;

    char msg[192] = "";

    switch (act) {
    case ACT_PROBE: {
        ha_set_base(a->addr);
        ha_set_alt(a->alt);
        ha_set_ip(a->ip);
        ha_save_prefs();          /* 两条地址与 IP 立刻落盘，下次启动预填 */
        snprintf(a->status, sizeof a->status, "%s", "");
        if (ha_probe(msg, sizeof msg) == 0) {
            /* 只有「内网不通自动改走外网」这种提示才带到登录页，普通成功不打扰 */
            if (strstr(msg, "已自动改走")) snprintf(a->status, sizeof a->status, "%s", msg);
            else a->status[0] = '\0';
            a->screen = SC_LOGIN;
            a->login_focus = 1;
            a->act = ACT_NONE;
            applog("probe ok: %s (%s)", ha_base(), ha_route());
        } else {
            snprintf(a->status, sizeof a->status, "!%s", msg);
            a->login_focus = 0;
        }
        break;
    }

    case ACT_LOGIN: {
        snprintf(a->status, sizeof a->status, "%s", "");
        int rc = a->use_token ? ha_login_with_token(a->token, msg, sizeof msg)
                              : ha_login(a->user, a->pass, msg, sizeof msg);
        if (rc == 0) {
            a->screen = SC_HOME;
            a->tab = 0;
            a->area_sel = -1;
            a->card_sel = 0;
            a->status[0] = '\0';
            a->act = ACT_SYNC;
        } else {
            snprintf(a->status, sizeof a->status, "!%s", msg);
        }
        break;
    }

    case ACT_SYNC: {
        int n = ha_sync(msg, sizeof msg);
        if (n >= 0) {
            a->sync_ms = SDL_GetTicks();     /* 数据新鲜度起点 */
            /* 内容一致就只刷新时间，不重排列表（滚动位置、选中项都不会被撞掉） */
            if (ha_last_changed()) ui_rebuild(a);
            ui_toast(a, 1, "%s", msg);
        } else {
            ui_toast(a, 0, "%s", msg[0] ? msg : "同步失败");
        }
        break;
    }

    case ACT_TOGGLE: {
        if (ha_toggle(idx, msg, sizeof msg) == 0) {
            ui_toast(a, 1, "%s", "已下发");
        } else {
            ui_toast(a, 0, "%s", msg[0] ? msg : "控制失败");
        }
        break;
    }

    case ACT_LEVEL:
    case ACT_POSITION: {
        int rc = (act == ACT_LEVEL) ? ha_set_level(idx, val, msg, sizeof msg)
                                    : ha_set_position(idx, val, msg, sizeof msg);
        if (rc != 0) ui_toast(a, 0, "%s", msg[0] ? msg : "调节失败");
        break;
    }

    case ACT_RUN: {
        const HaEnt *s = ha_scene(idx);
        if (s && ha_run_entity(s->eid, msg, sizeof msg) == 0)
            ui_toast(a, 1, "「%.24s」已执行", s->name);
        else
            ui_toast(a, 0, "%s", msg[0] ? msg : "执行失败");
        break;
    }

    case ACT_DIAG: {
        if (a->screen == SC_SETUP) {
            if (a->addr[0]) ha_set_base(a->addr);
            ha_set_alt(a->alt);
            ha_set_ip(a->ip);
            ha_save_prefs();
        } else if (!ha_base()[0]) {
            ha_set_base(a->addr);
        }
        a->diag_from = a->screen;
        ha_diag(a->diag, (int)sizeof a->diag);
        a->screen = SC_DIAG;
        break;
    }

    case ACT_UPD: {
#ifdef __SWITCH__
        /* 第一步只查版本：发现新版先弹确认框，用户选「升级」才下载 */
        upd_set_paint(gfx_busy_overlay);
        int rc = upd_check(msg, (int)sizeof msg);
        upd_set_paint(NULL);
        if (rc < 0) {
            ui_toast(a, 0, "%s", msg);
        } else if (rc == 0) {
            ui_toast(a, 1, "%s", msg);
        } else {
            snprintf(a->dlg_ver, sizeof a->dlg_ver, "%s", upd_new_version());
            a->dlg = 1;
            a->dlg_sel = 1;          /* 默认落在「升级」上 */
        }
#else
        ui_toast(a, 1, "v%s（桌面预览不支持更新）", upd_cur_version());
#endif
        break;
    }

    case ACT_UPD_INSTALL: {
#ifdef __SWITCH__
        /* 下载期间主循环出不了帧，由 upd_install 通过这个钩子就地画进度条 */
        upd_set_paint(gfx_busy_overlay);
        int rc = upd_install(msg, (int)sizeof msg);
        upd_set_paint(NULL);
        if (rc < 0) {
            ui_toast(a, 0, "%s", msg);
        } else {
            snprintf(a->upd_done_ver, sizeof a->upd_done_ver, "%s",
                     upd_done_version());
            a->upd_done = 1;
        }
#endif
        break;
    }

    case ACT_LOGOUT:
        do_logout(a);
        break;

    default:
        break;
    }

    return 1;
}

/* ---------------- 导航 ---------------- */

void ui_open_dev(App *a, int idx)
{
    const HaEnt *e = ha_ent(idx);
    if (!e) return;

    if (e->dom == DOM_CAMERA) { ui_open_cam(a, idx); return; }

    a->dev_idx = idx;
    a->screen = SC_DEV;
}

void ui_open_cam(App *a, int idx)
{
    const HaEnt *e = ha_ent(idx);
    if (!e) return;
    snprintf(a->cam_eid, sizeof a->cam_eid, "%s", e->eid);
    a->screen = SC_CAM;
}


void ui_back(App *a)
{
    if (a->screen == SC_SETUP) { setup_cancel(a); return; }
    if (a->screen == SC_DEV || a->screen == SC_CAM) {
        a->screen = SC_HOME;
        return;
    }
    if (a->screen == SC_LOGIN) {
        a->screen = SC_SETUP;
        a->login_focus = 0;
        a->status[0] = '\0';
        return;
    }
    if (a->screen == SC_HOME) {
        if (a->tab != 0) a->tab = 0;
    }
}

/* ---------------- 事件 ---------------- */

static void move_card(App *a, int dx, int dy)
{
    int n = a->nview;
    if (n <= 0) return;

    int i = a->card_sel;
    int col = i % TILE_COLS, row = i / TILE_COLS;

    if (dx) {
        int nc = col + dx;
        int ni = row * TILE_COLS + nc;
        if (nc >= 0 && nc < TILE_COLS && ni < n) i = ni;
        else {
            /* 跨行继续走 */
            int flat = i + dx;
            if (flat >= 0 && flat < n) i = flat;
        }
    }
    if (dy) {
        int flat = i + dy * TILE_COLS;
        if (flat >= 0 && flat < n) i = flat;
        else if (flat >= n && i < n) {
            /* 落到最后一行的同列 */
            int last = n - 1;
            int lc = last % TILE_COLS, lr = last / TILE_COLS;
            int nc = col > lc ? lc : col;
            i = lr * TILE_COLS + nc;
        }
    }
    a->card_sel = i;
    grid_follow(a);     /* 焦点出屏就顺势滚一行 */
}

/* 搜索：唤软键盘输入关键字，留空即清除。取消（返回 -1）不改动 */
static void open_search(App *a)
{
    if (!g_kb) return;
    char buf[48];
    snprintf(buf, sizeof buf, "%s", a->query);
    if (g_kb(buf, sizeof buf, "搜索实体名称（留空 = 清除搜索）", 0) < 0) return;

    /* 掐掉首尾空格：手柄输入法容易带出来 */
    char *p = buf;
    while (*p == ' ') p++;
    int n = (int)strlen(p);
    while (n > 0 && p[n - 1] == ' ') p[--n] = '\0';

    snprintf(a->query, sizeof a->query, "%s", p);
    a->card_sel = 0;
    a->zone = ZONE_DEVICE;
    ui_rebuild(a);
    if (a->query[0]) ui_toast(a, 1, "搜索：%s", a->query);
    else             ui_toast(a, 1, "已清除搜索");
}

/* 切换地区：sidebar、L/R、方向键边界都走这里，保证行为一致 */
static void switch_area(App *a, int d)
{
    int n = ha_narea();
    if (n <= 0) return;
    int s = a->area_sel + d;
    if (s < -1) s = n - 1;
    if (s >= n) s = -1;
    a->area_sel = s;
    a->card_sel = 0;
    ui_rebuild(a);
}

/* L/R 循环切类型筛选：和点筛选条等效，直接生效并重建网格。
 * 弹一条 toast 报名称，手上不用挪视线去确认筛到哪一档了。 */
static void step_filter(App *a, int d)
{
    a->filter_type = (a->filter_type + d + FILT_N) % FILT_N;
    a->chips_sel = a->filter_type;
    a->card_sel = 0;
    ui_rebuild(a);
    ui_toast(a, 1, "筛选：%s", FILT_NAME[a->filter_type]);
}

/* 更新完成弹层上的确认：能重启就登记重启，否则直接退出 */
static void upd_done_confirm(App *a)
{
    a->upd_done = 0;
    a->want_quit = 1;
}

/* 切换外观：设置页的 ←/→、A、Y 都走这里，保证提示文案一致 */
static void theme_choose(App *a, int dark)
{
    dark = dark ? 1 : 0;
    if (g_dark == dark) return;
    ui_theme_set(dark);
    ui_toast(a, 1, dark ? "已切换到深色" : "已切换到浅色");
}

void ui_event(App *a, int ev)
{
    if (ev == EV_NONE) return;

    if (a->upd_done) {
        if (ev == EV_OK)        upd_done_confirm(a);
        else if (ev == EV_BACK) { a->upd_done = 0; ui_toast(a, 1, "关闭应用后生效"); }
        return;
    }

    /* 弹层优先，吃掉所有按键：← → 选按钮，A 确认，B 取消 */
    if (a->dlg) {
        if (ev == EV_LEFT || ev == EV_RIGHT) {
            a->dlg_sel = a->dlg_sel ? 0 : 1;
        } else if (ev == EV_OK) {
            int go = a->dlg_sel == 1;
            a->dlg = 0;
            if (go) a->act = ACT_UPD_INSTALL;
        } else if (ev == EV_BACK) {
            a->dlg = 0;
            ui_toast(a, 1, "已取消升级");
        }
        return;
    }

#ifdef __SWITCH__
    /* 真机排障：确认按键确实送到了 UI 层，并记录当时的页面状态 */
    static int nkey;
    if (nkey++ < 60)
        applog("key ev=%d scr=%d tab=%d view=%d sel=%d area=%d/%d",
               ev, a->screen, a->tab, a->nview, a->card_sel,
               a->area_sel, ha_narea());
#endif

    /* 只要按了别的键，就清掉「双击 B 退出」的待命状态：
     * 避免「B（待命）→ A 进详情 → B 返回 → B」这种路径误杀应用 */
    if (ev != EV_BACK) a->quit_armed = 0;

    /* ZL / ZR：概览页切地区（房间）；自动化 / 设置页仍用于切页。
     * L / R：概览页切类型筛选（全部 / 灯 / 开关…）；其他页切页。 */
    if (a->screen == SC_HOME && (ev == EV_NAV_PREV || ev == EV_NAV_NEXT)) {
        if (a->tab == 0) {
            switch_area(a, (ev == EV_NAV_PREV) ? -1 : 1);
            return;
        }
        a->tab = (ev == EV_NAV_PREV) ? (a->tab + 2) % 3 : (a->tab + 1) % 3;
        a->zone = ZONE_DEVICE;
        return;
    }

    /* ---- 侧栏焦点：三个导航项 + 地区列表 ---- */
    if (a->screen == SC_HOME && a->zone == ZONE_SIDE) {
        int maxs = 3 + ha_narea();          /* 0..2 导航，3.. 地区条目 */
        switch (ev) {
        case EV_UP:    if (a->side_sel > 0) a->side_sel--; break;
        case EV_DOWN:  if (a->side_sel < maxs) a->side_sel++; break;
        case EV_RIGHT: a->zone = ZONE_DEVICE; break;
        case EV_OK:
            if (a->side_sel < 3) {
                a->tab = a->side_sel;
            } else {
                a->area_sel = a->side_sel - 3 - 1;   /* 3 = 全部地区 */
                a->tab = 0;
                a->card_sel = 0;
                ui_rebuild(a);
            }
            a->zone = ZONE_DEVICE;
            break;
        case EV_BACK:  a->zone = ZONE_DEVICE; break;
        case EV_REFRESH: a->act = ACT_SYNC; break;
        case EV_TAB_PREV:
        case EV_TAB_NEXT:
            a->tab = (ev == EV_TAB_PREV) ? (a->tab + 2) % 3 : (a->tab + 1) % 3;
            a->zone = ZONE_DEVICE;
            break;
        default: break;
        }
        return;
    }

    /* ---- 筛选条焦点：全部 / 灯 / 开关 / 传感器 / 摄像头 / 收藏 ---- */
    if (a->screen == SC_HOME && a->tab == 0 && a->zone == ZONE_CHIPS) {
        switch (ev) {
        case EV_LEFT:  if (a->chips_sel > 0) a->chips_sel--; break;
        case EV_RIGHT: if (a->chips_sel < FILT_N - 1) a->chips_sel++; break;
        case EV_DOWN:
        case EV_BACK:  a->zone = ZONE_DEVICE; break;
        case EV_OK:
            a->filter_type = a->chips_sel;
            a->card_sel = 0;
            a->zone = ZONE_DEVICE;
            ui_rebuild(a);
            break;
        case EV_MENU:  open_search(a); break;
        case EV_REFRESH: a->act = ACT_SYNC; break;
        default: break;
        }
        return;
    }

    /* ---- 设置页 ---- */
    if (a->screen == SC_HOME && a->tab == 2) {
        switch (ev) {
        case EV_UP:
            /* 第一行再往上 → 顶部的外观分段控件（set_sel = -1） */
            if (a->set_sel > 0) a->set_sel--;
            else                a->set_sel = -1;
            break;
        case EV_DOWN:
            if (a->set_sel < 0)      a->set_sel = 0;
            else if (a->set_sel < 4) a->set_sel++;
            break;
        case EV_LEFT:
            if (a->set_sel < 0) theme_choose(a, 0);
            else { a->zone = ZONE_SIDE; a->side_sel = 2; }
            break;
        case EV_RIGHT:
            if (a->set_sel < 0) theme_choose(a, 1);
            break;
        case EV_OK:
            if (a->set_sel < 0)           theme_choose(a, !g_dark);
            else if (a->set_sel == 0)     a->act = ACT_SYNC;
            else if (a->set_sel == 1)     a->act = ACT_DIAG;
            else if (a->set_sel == 2) { a->screen = SC_SETUP; a->setup_from = 1; a->login_focus = 0; }
            else if (a->set_sel == 3)     a->act = ACT_UPD;
            else                          a->act = ACT_LOGOUT;
            break;
        case EV_REFRESH: a->act = ACT_SYNC; break;
        case EV_MENU:    /* Y：切换浅色 / 深色 */
            theme_choose(a, !g_dark);
            break;
        case EV_BACK:    a->tab = 0; break;
        case EV_TAB_PREV: a->tab = (a->tab + 2) % 3; break;
        case EV_TAB_NEXT: a->tab = (a->tab + 1) % 3; break;
        default: break;
        }
        return;
    }

    /* ---- 自动化页 ---- */
    if (a->screen == SC_HOME && a->tab == 1) {
        switch (ev) {
        case EV_UP:   if (a->scene_sel > 0) a->scene_sel--; break;
        case EV_DOWN: if (a->scene_sel + 1 < ha_nscene()) a->scene_sel++; break;
        case EV_LEFT: a->zone = ZONE_SIDE; a->side_sel = 1; break;
        case EV_OK:
            if (ha_nscene() > 0) { a->act_idx = a->scene_sel; a->act = ACT_RUN; }
            break;
        case EV_REFRESH: a->act = ACT_SYNC; break;
        case EV_BACK:    a->tab = 0; break;
        case EV_TAB_PREV: a->tab = (a->tab + 2) % 3; break;
        case EV_TAB_NEXT: a->tab = (a->tab + 1) % 3; break;
        default: break;
        }
        return;
    }

    /* ---- 设备列表 ---- */
    if (a->screen == SC_HOME && a->tab == 0) {
        switch (ev) {
        case EV_LEFT:
            /* 最左一列再往左：焦点进侧栏（对应 HA 的 sidebar 收回手势） */
            if (a->nview == 0 || a->card_sel % TILE_COLS == 0) {
                a->zone = ZONE_SIDE;
                a->side_sel = 3 + a->area_sel + 1;
                if (a->side_sel > 3 + ha_narea()) a->side_sel = 3 + ha_narea();
            } else {
                move_card(a, -1, 0);
            }
            break;
        case EV_RIGHT:
            /* 只在网格内右移：到最右边就停住，不再顺手切地区。
             * 切地区交给 ZL/ZR 或左侧栏，避免「想多挪一格结果整屏换了」。 */
            move_card(a, 1, 0);
            break;
        case EV_UP:
            /* 第一行再往上：进筛选条（全部 / 灯 / 开关 / 传感器 / 摄像头 / 收藏） */
            if (a->nview == 0 || a->card_sel < TILE_COLS) {
                a->zone = ZONE_CHIPS;
                a->chips_sel = a->filter_type;
            } else {
                move_card(a, 0, -1);
            }
            break;
        case EV_DOWN:  move_card(a, 0, 1); break;

        case EV_TAB_PREV: step_filter(a, -1); break;
        case EV_TAB_NEXT: step_filter(a, 1); break;

        case EV_OK:
            if (a->nview > 0) ui_open_dev(a, a->view[a->card_sel]);
            else ui_toast(a, 0, ha_ready() ? "没有实体，按 X 重新同步"
                                           : "请先在设置里连接 Home Assistant");
            break;
        case EV_BACK:
            if (a->area_sel != -1) {
                a->area_sel = -1;
                a->card_sel = 0;
                ui_rebuild(a);
            } else {
                /* 双击 B 退出：第一次只提示，2.5 秒内再按一次才真退。
                 * 单次 B 直接退太容易误杀（想返回结果整个应用没了），
                 * 而 + 键在部分手柄/固件组合下读不到，不能只靠它。 */
                Uint32 now = SDL_GetTicks();
                if (a->quit_armed && (Uint32)(now - a->quit_armed) < 2500) {
                    a->quit_armed = 0;
                    a->want_quit = 1;
                } else {
                    a->quit_armed = now ? now : 1;
                    ui_toast(a, 1, "再次按 B 退出应用");
                }
            }
            break;
        case EV_REFRESH: a->act = ACT_SYNC; break;
        case EV_MENU:
            /* Y：搜索框（原来这里是「回到全部地区」，已由 B 承担） */
            open_search(a);
            break;
        default: break;
        }
        return;
    }

    /* ---- 设备详情 ---- */
    if (a->screen == SC_DEV) {
        const HaEnt *e = ha_ent(a->dev_idx);
        int idx = a->dev_idx;

        switch (ev) {
        case EV_BACK:
            a->screen = SC_HOME;
            break;

        case EV_MENU: {
            /* Y：收藏 / 取消收藏（收藏夹在概览的筛选条里可直接筛） */
            if (!e) break;
            int r = fav_toggle(e->eid);
            if (r < 0)      ui_toast(a, 0, "收藏已满（最多 %d 个）", FAV_MAX);
            else if (r)     ui_toast(a, 1, "已收藏：%s", e->name);
            else            ui_toast(a, 1, "已取消收藏");
            ui_rebuild(a);
            break;
        }

        case EV_OK:
            if (!e || !e->online) { ui_toast(a, 0, "设备当前不可用"); break; }
            if (e->dom == DOM_CAMERA) { ui_open_cam(a, idx); break; }
            if (e->on < 0) { ui_toast(a, 0, "该实体不支持开关"); break; }
            a->act_idx = idx;
            a->act = ACT_TOGGLE;
            break;

        case EV_LEFT:
            if (e && e->level >= 0 && e->online) {
                a->act_idx = idx;
                a->act_val = e->level - 5 < 0 ? 0 : e->level - 5;
                a->act = (e->dom == DOM_COVER) ? ACT_POSITION : ACT_LEVEL;
            }
            break;

        case EV_RIGHT:
            if (e && e->level >= 0 && e->online) {
                a->act_idx = idx;
                a->act_val = e->level + 5 > 100 ? 100 : e->level + 5;
                a->act = (e->dom == DOM_COVER) ? ACT_POSITION : ACT_LEVEL;
            }
            break;

        case EV_REFRESH:
            if (e) { a->act = ACT_SYNC; }
            break;

        default: break;
        }
        return;
    }

    /* ---- 摄像头 ---- */
    if (a->screen == SC_CAM) {
        if (ev == EV_BACK || ev == EV_OK) { a->screen = SC_HOME; return; }
        if (ev == EV_REFRESH) cam_reload();
        return;
    }

    /* ---- 设置 / 登录页 ---- */
    if (a->screen == SC_SETUP) {
        switch (ev) {
        case EV_BACK:
            /* 误碰进来要能退：丢弃未提交的输入，已保存的服务器配置不动 */
            setup_cancel(a);
            break;
        case EV_UP:
        case EV_DOWN:
            if (ev == EV_UP) a->login_focus = (a->login_focus + 3) % 4;
            else             a->login_focus = (a->login_focus + 1) % 4;
            break;
        case EV_OK:
            if (a->login_focus == 0) {
                if (g_kb) g_kb(a->addr, sizeof a->addr, "内网地址（如 http://192.168.1.100:8123）", 0);
                break;
            }
            if (a->login_focus == 1) {
                if (g_kb) g_kb(a->alt, sizeof a->alt, "外网地址（如 https://ha.你的域名，可留空）", 0);
                break;
            }
            if (a->login_focus == 2) {
                if (g_kb) g_kb(a->ip, sizeof a->ip, "服务器 IP（可留空）", 0);
                break;
            }
            if (!a->addr[0] && !a->alt[0]) {
                snprintf(a->status, sizeof a->status, "%s", "!请先输入服务器地址");
                a->login_focus = 0;
                break;
            }
            a->act = ACT_PROBE;
            break;
        case EV_REFRESH:
            /* X：先按当前输入做诊断，不改变已保存的服务器 */
            if (a->addr[0] || a->alt[0]) a->act = ACT_DIAG;
            break;
        default: break;
        }
        return;
    }

    /* ---- 连接诊断页 ---- */
    if (a->screen == SC_DIAG) {
        if (ev == EV_BACK) { a->screen = a->diag_from ? a->diag_from : SC_SETUP; return; }
        if (ev == EV_OK)   { a->act = ACT_DIAG; return; }
        return;
    }

    if (a->screen == SC_LOGIN) {
        switch (ev) {
        case EV_UP:
            a->login_focus = a->login_focus > 0 ? a->login_focus - 1 : 0;
            break;
        case EV_DOWN:
            a->login_focus = a->login_focus < 4 ? a->login_focus + 1 : 4;
            break;
        case EV_OK:
            if (a->login_focus <= 3 && g_kb) {
                if (a->login_focus == 1)      g_kb(a->user, sizeof a->user, "HA 用户名", 0);
                else if (a->login_focus == 2) g_kb(a->pass, sizeof a->pass, "登录密码", 1);
                else if (a->login_focus == 3) g_kb(a->token, sizeof a->token, "长期访问令牌", 1);
                break;
            }
            /* 焦点在按钮 */
            if (a->use_token && !a->token[0]) {
                snprintf(a->status, sizeof a->status, "%s", "!请填写长期访问令牌");
                break;
            }
            if (!a->use_token && !a->user[0]) {
                snprintf(a->status, sizeof a->status, "%s", "!请填写用户名");
                break;
            }
            a->act = ACT_LOGIN;
            break;
        case EV_BACK:
            a->screen = SC_SETUP;
            a->status[0] = '\0';
            break;
        default: break;
        }
        return;
    }
}

/* ---------------- 触控 ---------------- */

static int hit(SDL_Rect r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

void ui_tap(App *a, int x, int y)
{
    /* 更新完成弹层：点按钮等于按 A，点空白等于关掉 */
    if (a->upd_done) {
        SDL_Rect card, btn;
        upd_done_rects(&card, &btn);
        if (hit(btn, x, y)) upd_done_confirm(a);
        else                { a->upd_done = 0; ui_toast(a, 1, "关闭应用后生效"); }
        return;
    }

    /* 弹层：点「升级」直接开始下载，点其它任何地方等于取消 */
    if (a->dlg) {
        SDL_Rect card, b0, b1;
        dlg_rects(&card, &b0, &b1);
        a->dlg = 0;
        if (hit(b1, x, y)) a->act = ACT_UPD_INSTALL;
        return;
    }
    if (a->screen == SC_DIAG) {
        a->screen = a->diag_from ? a->diag_from : SC_HOME;
        return;
    }

    if (a->screen == SC_SETUP) {
        if (a->setup_from && hit(setup_cancel_rect(), x, y)) {
            setup_cancel(a);
            return;
        }
        if (hit(form_input(0), x, y)) {
            a->login_focus = 0;
            if (g_kb) g_kb(a->addr, sizeof a->addr, "内部网址（如 http://192.168.1.100:8123）", 0);
            return;
        }
        if (hit(form_input(1), x, y)) {
            a->login_focus = 1;
            if (g_kb) g_kb(a->alt, sizeof a->alt, "外部网址（如 https://ha.你的域名，可留空）", 0);
            return;
        }
        if (hit(form_input(2), x, y)) {
            a->login_focus = 2;
            if (g_kb) g_kb(a->ip, sizeof a->ip, "服务器 IP（可留空）", 0);
            return;
        }
        SDL_Rect btn = { FORM_X, 512, FORM_W, 60 };
        if (hit(btn, x, y)) {
            if (!a->addr[0] && !a->alt[0]) {
                snprintf(a->status, sizeof a->status, "%s", "!请先输入服务器地址");
                return;
            }
            a->act = ACT_PROBE;
            return;
        }
        if (y > UI_H - 90 && (a->addr[0] || a->alt[0])) a->act = ACT_DIAG;
        return;
    }

    if (a->screen == SC_LOGIN) {
        if (hit(login_input(0), x, y)) {
            a->login_focus = a->use_token ? 3 : 1;
            if (g_kb) {
                if (a->use_token) g_kb(a->token, sizeof a->token, "长期访问令牌", 1);
                else              g_kb(a->user, sizeof a->user, "HA 用户名", 0);
            }
            return;
        }
        if (!a->use_token && hit(login_input(1), x, y)) {
            a->login_focus = 2;
            if (g_kb) g_kb(a->pass, sizeof a->pass, "登录密码", 1);
            return;
        }
        SDL_Rect btn = { FORM_X, 424, FORM_W, 60 };
        if (hit(btn, x, y)) {
            if (a->use_token && !a->token[0]) {
                snprintf(a->status, sizeof a->status, "%s", "!请填写长期访问令牌");
                return;
            }
            if (!a->use_token && !a->user[0]) {
                snprintf(a->status, sizeof a->status, "%s", "!请填写用户名");
                return;
            }
            a->act = ACT_LOGIN;
            return;
        }
        SDL_Rect sw = { FORM_X, 504, FORM_W, 44 };
        if (hit(sw, x, y)) {
            a->use_token = !a->use_token;
            a->login_focus = a->use_token ? 3 : 1;
            a->status[0] = '\0';
        }
        return;
    }

    if (a->screen == SC_DEV) {
        const HaEnt *e = ha_ent(a->dev_idx);
        if (!e) return;

        SDL_Rect s = { UI_W / 2 - 360, 104, 720, 512 };
        SDL_Rect close = { s.x + s.w - 56, s.y + 24, 36, 36 };
        SDL_Rect back = { s.x + 48, s.y + s.h - 76, s.w - 96, 52 };
        if (hit(close, x, y) || hit(back, x, y)) { a->screen = SC_HOME; return; }

        /* 大开关圆 */
        int cx = UI_W / 2, cy = s.y + 234;
        if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= 44 * 44) {
            if (!e->online) { ui_toast(a, 0, "设备当前不可用"); return; }
            if (e->dom == DOM_CAMERA) { ui_open_cam(a, a->dev_idx); return; }
            if (e->on < 0) { ui_toast(a, 0, "该实体不支持开关"); return; }
            a->act_idx = a->dev_idx;
            a->act = ACT_TOGGLE;
            return;
        }

        if (e->level >= 0 && e->online && e->dom != DOM_CAMERA) {
            SDL_Rect track = { s.x + 48, s.y + 366, s.w - 96, 12 };
            SDL_Rect hitbox = { track.x, track.y - 20, track.w, 52 };
            if (hit(hitbox, x, y)) {
                int v = (x - track.x) * 100 / track.w;
                if (v < 0) v = 0;
                if (v > 100) v = 100;
                a->act_idx = a->dev_idx;
                a->act_val = v;
                a->act = (e->dom == DOM_COVER) ? ACT_POSITION : ACT_LEVEL;
            }
        }
        return;
    }

    if (a->screen == SC_CAM) { a->screen = SC_HOME; return; }

    /* ---- 主界面：sidebar ---- */
    if (x < SIDEBAR_W) {
        for (int i = 0; i < 3; i++) {
            SDL_Rect r = { 16, NAV_Y + i * NAV_ROW, SIDEBAR_W - 32, NAV_H };
            if (hit(r, x, y)) {
                a->tab = i;
                a->zone = ZONE_DEVICE;
                a->side_sel = i;
                return;
            }
        }
        int atop = area_list_top(a);
        int total = ha_narea() + 1;
        int n = total - atop;
        if (n > AREA_VIS) n = AREA_VIS;
        for (int k = 0; k < n; k++) {
            SDL_Rect r = { 16, AREA_TOP + k * AREA_ROW, SIDEBAR_W - 32, AREA_ROW - 3 };
            if (hit(r, x, y)) {
                int entry = atop + k;
                a->tab = 0;
                a->area_sel = entry - 1;
                a->card_sel = 0;
                a->zone = ZONE_DEVICE;
                a->side_sel = 3 + entry;
                ui_rebuild(a);
                return;
            }
        }
        return;
    }

    /* 顶栏刷新按钮 */
    SDL_Rect rf = { UI_W - PAD - 44, 26, 44, 44 };
    if (hit(rf, x, y)) { a->act = ACT_SYNC; return; }

    if (a->tab == 0) {
        for (int i = 0; i < FILT_N; i++) {
            if (hit(chip_rect(i), x, y)) {
                a->filter_type = i;
                a->chips_sel = i;
                a->card_sel = 0;
                a->zone = ZONE_DEVICE;
                ui_rebuild(a);
                return;
            }
        }

        int first = a->grid_row * TILE_COLS;
        int n = a->nview - first;
        if (n > PER_PAGE) n = PER_PAGE;
        for (int i = 0; i < n; i++) {
            if (hit(tile_rect(i), x, y)) {
                a->zone = ZONE_DEVICE;
                a->card_sel = first + i;
                grid_follow(a);
                ui_open_dev(a, a->view[first + i]);
                return;
            }
        }
    } else if (a->tab == 1) {
        int page = a->scene_sel / 5;
        int first = page * 5;
        for (int i = 0; i < 5 && first + i < ha_nscene(); i++) {
            SDL_Rect r = { CX + 8, SCENE_TOP + 8 + i * SCENE_ROW, CW - 16, 72 };
            if (hit(r, x, y)) {
                a->scene_sel = first + i;
                a->act_idx = a->scene_sel;
                a->act = ACT_RUN;
                return;
            }
        }
    } else {
        if (hit(theme_seg(0), x, y)) {
            a->set_sel = -1;
            theme_choose(a, 0);
            return;
        }
        if (hit(theme_seg(1), x, y)) {
            a->set_sel = -1;
            theme_choose(a, 1);
            return;
        }
        SDL_Rect c1 = { CX, SET_CARD1_Y, CW, 2 * 72 + 16 };
        for (int i = 0; i < 2; i++) {
            SDL_Rect r = { c1.x + 8, c1.y + 8 + i * 72, c1.w - 16, 64 };
            if (hit(r, x, y)) {
                if (i == 0) { a->screen = SC_SETUP; a->setup_from = 1; a->login_focus = 0; }
                return;
            }
        }
        SDL_Rect c2 = { CX, SET_CARD2_Y, CW, 5 * SET_ROW_H + 16 };
        for (int i = 0; i < 5; i++) {
            SDL_Rect r = { c2.x + 8, c2.y + 8 + i * SET_ROW_H, c2.w - 16, SET_ROW_H - 6 };
            if (hit(r, x, y)) {
                a->set_sel = i;
                if (i == 0)      a->act = ACT_SYNC;
                else if (i == 1) a->act = ACT_DIAG;
                else if (i == 2) { a->screen = SC_SETUP; a->setup_from = 1; a->login_focus = 0; }
                else if (i == 3) a->act = ACT_UPD;
                else             a->act = ACT_LOGOUT;
                return;
            }
        }
    }
}

/* ---------------- 初始化 ---------------- */

void ui_init(App *a)
{
    memset(a, 0, sizeof *a);
    ui_theme_apply(ha_theme());     /* 先应用主题，后面所有绘制都依赖 C_* */
    a->screen = SC_SETUP;
    a->tab = 0;
    a->area_sel = -1;
    a->login_focus = 0;
    a->dev_idx = -1;
    a->set_sel = 0;
    a->use_token = 0;
    a->zone = ZONE_DEVICE;
    a->side_sel = 3;        /* 侧栏第 0 个地区条目 = 全部地区 */
    snprintf(a->addr, sizeof a->addr, "%s", DEFAULT_ADDR);
    snprintf(a->alt, sizeof a->alt, "%s", DEFAULT_ALT);
}
