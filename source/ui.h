#pragma once

#include <SDL.h>
#include <stddef.h>
#include "gfx.h"
#include "ha.h"

enum { SC_SETUP = 0, SC_LOGIN, SC_HOME, SC_DEV, SC_CAM, SC_DIAG };

enum {
    EV_NONE = 0, EV_UP, EV_DOWN, EV_LEFT, EV_RIGHT, EV_OK, EV_BACK,
    EV_TAB_PREV, EV_TAB_NEXT, EV_REFRESH, EV_MENU,
    EV_NAV_PREV, EV_NAV_NEXT
};

enum {
    ACT_NONE = 0, ACT_PROBE, ACT_LOGIN, ACT_TOKEN_LOGIN, ACT_SYNC,
    ACT_TOGGLE, ACT_LEVEL, ACT_POSITION, ACT_RUN, ACT_LOGOUT, ACT_DIAG,
    ACT_UPD, ACT_UPD_INSTALL
};

/* 主界面的三段焦点：设备网格 / 左侧 sidebar / 顶部筛选条 */
enum { ZONE_DEVICE = 0, ZONE_SIDE = 1, ZONE_CHIPS = 2 };

/* 概览的类型筛选（与地区、搜索叠加生效） */
enum { FILT_ALL = 0, FILT_LIGHT, FILT_SWITCH, FILT_SENSOR, FILT_CAMERA, FILT_FAV };

typedef struct {
    int screen;
    int tab;            /* 0=设备 1=自动化 2=设置 */
    int login_focus;    /* 登录页焦点：0 地址 1 用户 2 密码 3 长期令牌 4 按钮 */

    int zone;           /* ZONE_DEVICE / ZONE_SIDE / ZONE_CHIPS */
    int side_sel;       /* 侧栏焦点：0..2 导航项，3+ 地区条目（3=全部地区） */
    int chips_sel;      /* 筛选条焦点 0..5 */
    int filter_type;    /* FILT_* */
    char query[48];     /* 搜索关键字（空 = 不过滤） */

    int area_sel;       /* -1 = 全部 */
    int card_sel;
    int grid_row;       /* 网格首行（滚动位置）：焦点走到第 4 行就整屏下滚一行 */
    int nview;
    int view[HA_MAX_ENT];

    int scene_sel;
    int dev_idx;        /* 详情页对应实体下标 */
    int slide_drag;     /* 正在拖动滑条 */
    int set_sel;        /* 设置页条目焦点 */

    char addr[192];
    char alt[192];      /* 地址 2：外网域名（可选）。内网不通时自动切过去 */
    char ip[64];        /* 可选：直接指定服务器 IP（绕开 DNS） */
    char user[64];
    char pass[64];
    char token[768];
    int  use_token;

    char status[192];
    char hint[96];
    char toast[192];
    int  toast_ok;
    int  toast_frames;

    int busy;
    int act;
    int act_idx;
    int act_val;
    char busy_msg[96];  /* busy 遮罩上的提示（空 = 默认文案） */

    char cam_eid[80];

    char diag[1400];   /* 连接诊断结果（多行文本） */
    int  diag_from;    /* 从哪个页面进来的，B 键回哪 */

    int  dlg;          /* 弹层：0=无 1=发现新版本（升级确认） */
    int  dlg_sel;      /* 弹层按钮焦点：0=取消 1=升级 */
    char dlg_ver[32];  /* 弹层里显示的新版本号 */
    int  want_quit;    /* 主循环看到这个标志就退出应用 */
    int  exiting;      /* 按了 + / -：主循环画一帧「正在退出」再收尾 */
    int  setup_from;   /* 进入「服务器地址」页的来源：0=首次启动 1=从设置页 */
    Uint32 quit_armed; /* B 双击退出的第一次按键时刻（0=未待命） */
    Uint32 sync_ms;    /* 上次成功同步的时刻（SDL_GetTicks），显示数据新鲜度 */

    int  upd_done;          /* 更新完成弹层：1 = 显示 */
    char upd_done_ver[32];  /* 刚装好的版本号 */
} App;

/* 软键盘回调（main.c 注册；桌面预览可留空） */
typedef int (*UiKbFn)(char *buf, size_t len, const char *guide, int password);
void ui_set_keyboard(UiKbFn fn);

void ui_init(App *a);
void ui_event(App *a, int ev);
void ui_tap(App *a, int x, int y);
void ui_draw(App *a);
void ui_rebuild(App *a);
void ui_toast(App *a, int ok, const char *fmt, ...);

/* 外观：0 = 浅色，1 = 深色。会同时写进配置 */
void ui_theme_set(int dark);
int  ui_theme_is_dark(void);

/* 在主循环里执行排队中的网络动作（返回 1 表示执行过） */
int ui_run_act(App *a);

/* 需要主循环配合的动作 */
void ui_open_dev(App *a, int idx);
void ui_open_cam(App *a, int idx);
void ui_back(App *a);
