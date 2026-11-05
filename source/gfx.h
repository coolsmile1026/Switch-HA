#pragma once

#include <SDL.h>
#include <SDL_ttf.h>

/* ------------------------------------------------------------------
 * 设计 token：来自 home-assistant/android 2026.11.1 app-minimal-release.apk
 * 反编译出的 common/compose/theme/ 设计系统（HAColors / HARadius /
 * HADimens / HAFontSize），以及 contributors.home-assistant.io 的
 * HATheme / HA* 组件规范。这里是 Switch 1280x720 的等比放大值。
 *
 *   Blue           #18BCF2   品牌主色（HABrandColors.Blue）
 *   Background     #F2F4F9   品牌背景（HABrandColors.Background）
 *   Primary40      #009AC7   primary normal
 *   Primary90/95   #DFF3FC / #EFF9FE  primary quiet / 选中底色
 *   Neutral05..95  #141414 ... #F3F3F3 中性灰阶
 *   Radius XL=16 / X2L=20 / X3L=24 / Pill
 *   Space 4 / 8 / 12 / 16 / 24 / 32
 *   FontSize 10 / 12 / 14 / 16 / 20 / 24 / 28 / 32
 * ------------------------------------------------------------------ */

#define HA_BLUE        24, 188, 242, 255      /* #18BCF2  brand */
#define HA_BLUE_DARK   0, 154, 199, 255      /* #009AC7  Primary40 */
#define HA_BLUE_DEEP   0, 103, 135, 255      /* #006787  Primary30 */
#define HA_BLUE_SOFT   223, 243, 252, 255    /* #DFF3FC  Primary90 */
#define HA_BLUE_PALE   239, 249, 254, 255    /* #EFF9FE  Primary95 */
#define HA_BG          242, 244, 249, 255    /* #F2F4F9  brand background */
#define HA_CARD        255, 255, 255, 255    /* surface default */
#define HA_TEXT        20, 20, 20, 255       /* #141414  Neutral05 */
#define HA_TEXT_2      94, 94, 94, 255       /* #5E5E5E  Neutral40 */
#define HA_TEXT_3      122, 122, 122, 255    /* #7A7A7A  Neutral50 */
#define HA_LINE        230, 230, 230, 255    /* #E6E6E6  Neutral90 */
#define HA_OK          0, 136, 60, 255       /* #00883C  Green50 */
#define HA_WARN        201, 78, 0, 255       /* #C94E00  Orange50 */
#define HA_ERR         220, 49, 70, 255      /* #DC3146  Red50 */
#define HA_OFF         243, 243, 243, 255    /* #F3F3F3  Neutral95 */
#define HA_BORDER      204, 204, 204, 255    /* #CCCCCC  Neutral80 */

#define UI_W 1280
#define UI_H 720

/* 字体档位对齐 HATextStyle：Headline / HeadlineMedium / Body / BodyMedium / caption */
enum { F_TITLE = 0, F_HEAD, F_BODY, F_SMALL, F_TINY, F_COUNT };

int  gfx_init(SDL_Renderer *ren, const char *font_path, int font_index);
void gfx_shutdown(void);

/* 每帧 SDL_RenderPresent 之后调用：真正释放本帧被淘汰的纹理。
 * 纹理在本帧仍被渲染命令引用，present 前销毁会导致 use-after-free（真机直接崩溃） */
void gfx_frame_end(void);

void gfx_fill(SDL_Rect r, SDL_Color c);
void gfx_round(SDL_Rect r, int radius, SDL_Color c);
void gfx_round_border(SDL_Rect r, int radius, SDL_Color c, int thickness, SDL_Color bg);
void gfx_circle(int cx, int cy, int radius, SDL_Color c);

/* 半透明遮罩（HAModalBottomSheet 的 scrim），alpha 0-255 */
void gfx_dim(SDL_Rect r, int alpha);

/* 阻塞式动作（OTA 下载等）期间就地重绘的遮罩：主循环此时不会出帧，
 * 只能由网络回调直接铺一层白底 + 进度条再 present。
 * pct < 0 表示不确定进度（只显示文字）。
 * 文字走「用完即毁」的纹理路径，不吃文字缓存。 */
void gfx_busy_overlay(const char *title, const char *sub, int pct);

/* HA 品牌图标：蓝色圆角方 + 白色「房子 + 圆点」简笔 */
void gfx_ha_logo(SDL_Rect box, int dark);

int  gfx_text(int size, const char *utf8, int x, int y, SDL_Color c);
int  gfx_text_w(int size, const char *utf8);
void gfx_text_size(int size, const char *utf8, int *w, int *h);
int  gfx_text_box(int size, const char *utf8, SDL_Rect box, SDL_Color c);
int  gfx_text_center(int size, const char *utf8, int cx, int y, SDL_Color c);
int  gfx_text_right(int size, const char *utf8, int rx, int y, SDL_Color c);

/* 诊断：本帧新建的文字纹理数 / 清零（稳态应为 0） */
unsigned gfx_tex_new(void);
void     gfx_tex_new_reset(void);

/* 超长文本按宽度截断（加省略号），写到 out */
void gfx_ellipsis(int size, const char *utf8, int max_w, char *out, int outsz);

/* 图标 id */
enum {
    IC_BULB = 0, IC_PLUG, IC_SENSOR, IC_FAN, IC_CAMERA, IC_AC, IC_CURTAIN,
    IC_LOCK, IC_VACUUM, IC_SCRIPT, IC_HOME, IC_SMART, IC_GEAR,
    IC_CHEVRON_L, IC_CHEVRON_R, IC_REFRESH, IC_PLUS, IC_MINUS, IC_WIFI,
    IC_AREA, IC_CLOSE, IC_INFO, IC_STAR, IC_SEARCH,
    IC_COUNT
};

void gfx_icon(int kind, SDL_Rect box, SDL_Color c);
