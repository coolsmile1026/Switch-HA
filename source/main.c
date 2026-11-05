/* Switch HA 客户端 —— 入口
 *
 * 只做三件事：把 HA 的数据拿回来、画出来、把操作发回去。
 * 所有网络动作都排在 ui_run_act() 里执行（主循环消费），
 * 事件回调里绝不阻塞，避免按键后界面卡死。
 */

#include <switch.h>

#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

#include "applog.h"
#include "cam.h"
#include "gfx.h"
#include "ha.h"
#include "net.h"
#include "ui.h"
#include "update.h"

static const char *FONT_PATH = "romfs:/fonts/main.otf";

#ifdef __SWITCH__
#define HA_DIR "sdmc:/switch/switch-ha"
#else
#define HA_DIR "data"
#endif

static void show_error(const char *title, const char *detail)
{
    consoleInit(NULL);
    printf("\x1b[2J\x1b[1;1H");
    printf("\n  %s\n\n", title);
    if (detail && detail[0]) printf("  %s\n", detail);
    printf("\n  日志: sdmc:/switch/switch-ha/log.txt\n");
    printf("  按 HOME 返回\n");
    consoleUpdate(NULL);

    while (appletMainLoop()) svcSleepThread(100000000LL);
    consoleExit(NULL);
}

static int kb_input(char *buf, size_t len, const char *guide, int password)
{
    SwkbdConfig kbd;
    if (R_FAILED(swkbdCreate(&kbd, 0))) return -1;

    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetGuideText(&kbd, guide);
    if (buf[0]) swkbdConfigSetInitialText(&kbd, buf);
    if (password) swkbdConfigSetPasswordFlag(&kbd, 1);

    Result rc = swkbdShow(&kbd, buf, (u32)len);
    swkbdClose(&kbd);

    /* 系统软键盘会接管并重配 npad；不恢复的话回到应用后手柄按键全部读不到，
     * 表现就是「登录完之后按什么都没反应」。 */
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    applog("kb: done, npad reconfigured");
    return R_SUCCEEDED(rc) ? 0 : -1;
}

/* 每种网络动作在遮罩上显示的文案。返回 NULL 用默认（普通 HA 操作）。
 * 检查更新只访问 GitHub，绝不能显示「正在与 HA 通信」 */
static const char *busy_text(int act)
{
    switch (act) {
    case ACT_UPD:         return "正在检查更新…";
    case ACT_UPD_INSTALL: return "正在下载新版本…";
    case ACT_SYNC:        return "正在与 Home Assistant 同步…";
    case ACT_DIAG:        return "正在检查连接…";
    case ACT_PROBE:       return "正在连接服务器…";
    case ACT_LOGIN:
    case ACT_TOKEN_LOGIN: return "正在登录…";
    default:              return NULL;
    }
}

/* 每帧耗时（每 300 帧打一条，用来判断卡顿是渲染还是逻辑） */
static double g_avg_ms;
static double frame_ms(void) { return g_avg_ms; }

static void frame(SDL_Renderer *ren, App *a)
{
    SDL_SetRenderDrawColor(ren, 245, 247, 249, 255);
    SDL_RenderClear(ren);
    ui_draw(a);

    if (a->busy) {
        gfx_fill((SDL_Rect){ 0, 0, UI_W, UI_H }, (SDL_Color){ 255, 255, 255, 200 });
        gfx_text_center(F_HEAD,
                        a->busy_msg[0] ? a->busy_msg : "正在与 Home Assistant 通信…",
                        UI_W / 2, UI_H / 2 - 20, (SDL_Color){ 33, 33, 33, 255 });
        /* 「首次同步」副标题只属于没写明文案的普通 HA 操作 */
        if (!a->busy_msg[0])
            gfx_text_center(F_SMALL, "首次同步设备可能需要几秒", UI_W / 2, UI_H / 2 + 24,
                            (SDL_Color){ 120, 124, 130, 255 });
    }

    SDL_RenderPresent(ren);
    gfx_frame_end();
}

static void map_pad(App *a, u64 down, u64 held, int *quit)
{
    /* + 退出：held 和 down 都认。
     * 只认 down 会有漏：动作阻塞（同步 / 检查更新）期间主循环不转，
     * 松手后才恢复的话，边沿就永远拿不到。
     * 同时认 - 键：部分手柄/固件组合下 Plus 位可能拿不到，
     * Minus 是同一排的兄弟键且无其他用途，作为兜底退出键。 */
    u64 quitkey = HidNpadButton_Plus | HidNpadButton_Minus;
    if ((down | held) & quitkey) {
        applog("pad: quit key down=%08llX held=%08llX -> quit",
               (unsigned long long)down, (unsigned long long)held);
        applog_flush();
        *quit = 1;
        a->exiting = 1;
        return;
    }

    /* 真机排障：前 40 次按键留痕，能区分「按键没进来」和「界面没响应」。
     * 不能每次按键都写：SD 卡一次同步写 10~50ms，方向键连发会被拖死。 */
    static int npad_log;
    if (down && npad_log < 40) {
        npad_log++;
        applog("pad down=%08llX scr=%d tab=%d ent=%d area=%d",
               (unsigned long long)down, a->screen, a->tab,
               ha_nent(), ha_narea());
    }

    if (down & HidNpadButton_AnyUp)    ui_event(a, EV_UP);
    if (down & HidNpadButton_AnyDown)  ui_event(a, EV_DOWN);
    if (down & HidNpadButton_AnyLeft)  ui_event(a, EV_LEFT);
    if (down & HidNpadButton_AnyRight) ui_event(a, EV_RIGHT);

    if (down & HidNpadButton_L)  ui_event(a, EV_TAB_PREV);
    if (down & HidNpadButton_R)  ui_event(a, EV_TAB_NEXT);
    if (down & HidNpadButton_ZL) ui_event(a, EV_NAV_PREV);
    if (down & HidNpadButton_ZR) ui_event(a, EV_NAV_NEXT);
    /* 概览页语义：L/R = 类型筛选（全部/灯/开关…），ZL/ZR = 切地区；
     * 自动化/设置页两者都沿用切页。具体见 ui.c 的 ui_event。 */

    if (down & HidNpadButton_Y) ui_event(a, EV_MENU);
    if (down & HidNpadButton_X) ui_event(a, EV_REFRESH);
    if (down & HidNpadButton_A) ui_event(a, EV_OK);
    if (down & HidNpadButton_B) ui_event(a, EV_BACK);
}

/* 方向键长按重复：按下 20 帧后开始连发，之后每 6 帧一次。
 * 只处理方向，A / B / L / R 不重复，避免误触开关设备。 */
#define DIR_MASK (HidNpadButton_AnyUp | HidNpadButton_AnyDown | \
                  HidNpadButton_AnyLeft | HidNpadButton_AnyRight)

static int dir_to_ev(u64 b)
{
    if (b & HidNpadButton_AnyUp)    return EV_UP;
    if (b & HidNpadButton_AnyDown)  return EV_DOWN;
    if (b & HidNpadButton_AnyLeft)  return EV_LEFT;
    if (b & HidNpadButton_AnyRight) return EV_RIGHT;
    return EV_NONE;
}

static void pad_repeat(App *a, u64 held)
{
    static u64 prev;
    static u32 next_ms;
    u64 d = held & DIR_MASK;
    u32 now = SDL_GetTicks();

    /* 按帧计时会被帧率带偏：软件渲染掉到 25fps 时，原来「20 帧」
     * 等于 800ms 才走第二格，手感就是又黏又慢。改成毫秒：
     * 长按 220ms 后开始连发，每 70ms 一格（约 14 格/秒）。 */
    if (d != prev) {
        prev = d;
        if (d) next_ms = now + 220;
        return;
    }
    if (!d) return;
    if ((s32)(now - next_ms) < 0) return;

    int ev = dir_to_ev(d);
    if (ev) ui_event(a, ev);
    next_ms = now + 70;
}

/* 自身的 NRO 路径：hbmenu 会把启动路径作为 argv[0] 传进来，
 * 「更新完成后重启」要靠它登记 chainload 目标 */
static char g_self_path[256];

int main(int argc, char **argv)
{
    if (argc > 0 && argv && argv[0] && argv[0][0])
        snprintf(g_self_path, sizeof g_self_path, "%s", argv[0]);

    socketInitializeDefault();

#ifdef __SWITCH__
    mkdir("sdmc:/switch", 0777);
    mkdir(HA_DIR, 0777);
    /* 上次「检查更新」如果没换成功，这里补上（.new → 正式文件） */
    upd_apply_pending();
#endif

    applog("switch-ha launch v%s", upd_cur_version());

    {
        NifmInternetConnectionType ntype;
        u32 strength = 0;
        NifmInternetConnectionStatus nst;
        if (R_SUCCEEDED(nifmGetInternetConnectionStatus(&ntype, &strength, &nst)))
            applog("nifm: type=%d status=%d strength=%u", (int)ntype, (int)nst, strength);
    }

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);

    Result rci = romfsInit();
    if (R_FAILED(rci)) {
        show_error("romfs 初始化失败", "程序文件不完整，请重新拷贝 .nro");
        socketExit();
        return 1;
    }

    PadState pad;
    padInitializeDefault(&pad);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        applog("SDL_Init FAIL: %s", SDL_GetError());
        show_error("SDL 初始化失败", SDL_GetError());
        romfsExit();
        socketExit();
        return 1;
    }

    SDL_Window *win = SDL_CreateWindow("Switch HA", SDL_WINDOWPOS_CENTERED,
                                       SDL_WINDOWPOS_CENTERED, UI_W, UI_H,
                                       SDL_WINDOW_SHOWN);
    if (!win) {
        show_error("窗口创建失败", SDL_GetError());
        SDL_Quit();
        romfsExit();
        socketExit();
        return 1;
    }

    /* 渲染器：优先进硬件加速。
     * 软件渲染每帧要把 1280x720 全屏在 CPU 上合成再拷贝，实测会掉到
     * 20~30fps，方向键因此发飘（按一下要等一帧才动）。加速 + vsync
     * 稳定 60fps，输入延迟从 ~40ms 降到 ~16ms。
     * 万一某个环境上加速渲染黑屏，在 SD 卡放
     *   sdmc:/switch/switch-ha/render.txt   内容写 soft
     * 就强制回软件渲染，不用重装。 */
    int force_soft = 0;
#ifdef __SWITCH__
    {
        FILE *rf = fopen("sdmc:/switch/switch-ha/render.txt", "rb");
        if (rf) {
            char b[16] = "";
            if (fgets(b, sizeof b, rf) && strncmp(b, "soft", 4) == 0) force_soft = 1;
            fclose(rf);
            applog("render: force_soft=%d", force_soft);
        }
    }
#endif

    SDL_Renderer *ren = NULL;
    if (!force_soft) {
        ren = SDL_CreateRenderer(win, -1,
                                 SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    }
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
    if (ren) {
        SDL_RendererInfo ri;
        if (SDL_GetRendererInfo(ren, &ri) == 0)
            applog("render: %s flags=0x%x", ri.name ? ri.name : "?", ri.flags);
    }
    if (!ren) {
        show_error("渲染器创建失败", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        romfsExit();
        socketExit();
        return 1;
    }

    if (gfx_init(ren, FONT_PATH, 0) != 0) {
        show_error("字体加载失败", "详见 log.txt");
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
        romfsExit();
        socketExit();
        return 1;
    }

    net_init();
    cam_init(ren);
    ui_set_keyboard(kb_input);

    static App app;
    ui_init(&app);

    /* 有缓存就直接进：启动不联网、不同步——设备增减是低频事件，
     * 每次开应用都拉一遍只会让开场卡一下。要看最新就按 X，
     * 换服务器地址时也会自动同步一次。 */
    if (ha_load()) {
        snprintf(app.addr, sizeof app.addr, "%s", ha_base1());
        if (ha_alt()[0]) snprintf(app.alt, sizeof app.alt, "%s", ha_alt());
        if (ha_ip()[0]) snprintf(app.ip, sizeof app.ip, "%s", ha_ip());
        app.screen = SC_HOME;

        int cached = ha_load_cache();
        if (cached > 0) {
            long age = ha_cache_age();
            if (age < 0) age = 0;
            if (age > 3600) age = 3600;
            /* 副标题显示「N 分钟前」，明确这是上次的数据，不是刚拉的 */
            app.sync_ms = (Uint32)(SDL_GetTicks() - (Uint32)age * 1000u);
            ui_rebuild(&app);
            applog("resume: cache %d ents (age=%lds), no boot sync", cached, age);
        } else {
            app.act = ACT_SYNC;     /* 没缓存只能先同步，遮罩提示照旧 */
            applog("resume: no cache, sync first");
        }
        applog("resume: %s (%s) route=%s alt=%s", ha_base(), ha_user(), ha_route(), ha_alt());
    } else {
        app.screen = SC_SETUP;
        app.login_focus = 0;
        if (ha_base1()[0]) snprintf(app.addr, sizeof app.addr, "%s", ha_base1());
        if (ha_alt()[0]) snprintf(app.alt, sizeof app.alt, "%s", ha_alt());
        if (ha_ip()[0]) snprintf(app.ip, sizeof app.ip, "%s", ha_ip());
        applog("no config, showing setup");
    }

    int running = 1;
    int frames = 0;
    Uint64 frame_t0 = SDL_GetTicks64();
    int last_screen = -1;
    Uint64 last_input = SDL_GetTicks64();

    while (running && appletMainLoop()) {
        if (app.screen != last_screen) {
            applog("screen -> %d (tab=%d ent=%d)", app.screen, app.tab, ha_nent());
            last_screen = app.screen;
        }

        padUpdate(&pad);
        u64 held = padGetButtons(&pad);

        /* 双保险：除了 libnx 的 down 边沿，自己也用 held 变化算一次边沿。
         * 软键盘 / hbmenu 切回后若 npad 状态没同步，前者可能一直为 0。 */
        static u64 prev_held;
        u64 edge = held & ~prev_held;
        prev_held = held;
        u64 down = padGetButtonsDown(&pad) | edge;

        map_pad(&app, down, held, &running);
        pad_repeat(&app, held);
        if (down) last_input = SDL_GetTicks64();
        if (app.want_quit) { applog("quit requested from UI"); running = 0; }

        /* 退出前给一屏反馈：
         * 这一屏出现了但应用迟迟不退 → 卡在收尾，看 log.txt 的 exit: 步骤；
         * 这一屏根本没出现 → 按键没进来。 */
        if (!running) {
            snprintf(app.busy_msg, sizeof app.busy_msg, "正在退出…");
            app.busy = 1;
            frame(ren, &app);
        }

        SDL_Event ev;
        while (running && SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
            else if (ev.type == SDL_FINGERDOWN) {
                last_input = SDL_GetTicks64();
                ui_tap(&app, (int)(ev.tfinger.x * (float)UI_W),
                              (int)(ev.tfinger.y * (float)UI_H));
            } else if (ev.type == SDL_MOUSEBUTTONDOWN &&
                       ev.button.which != SDL_TOUCH_MOUSEID) {
                last_input = SDL_GetTicks64();
                ui_tap(&app, ev.button.x, ev.button.y);
            }
        }

        /* 摄像头页面才会拉图：进页面开，退出关 */
        static int cam_on;
        if (app.screen == SC_CAM && !cam_on) {
            char url[512];
            if (ha_cam_url(app.cam_eid, url, sizeof url) == 0) {
                cam_open(url, ha_token());
            } else {
                applog("cam: url build failed");
            }
            cam_on = 1;
        } else if (app.screen != SC_CAM && cam_on) {
            cam_close();
            cam_on = 0;
        }
        if (app.screen == SC_CAM) cam_tick();

        /* 网络动作：先画一帧"正在做什么"，再执行。
         * 注意 busy_msg 必须在这里提前设置——遮罩是在 ui_run_act 之前
         * 渲染的，而动作内部（如 upd_run）会阻塞几十秒不再刷新，
         * 晚设置的画面永远出不来（曾导致检查更新全程显示
         * "正在与 Home Assistant 通信…"，其实查的是 GitHub）。 */
        if (app.act && !app.busy) {
            app.busy_msg[0] = '\0';
            const char *bt = busy_text(app.act);
            if (bt) snprintf(app.busy_msg, sizeof app.busy_msg, "%s", bt);
            app.busy = 1;
            frame(ren, &app);
            ui_run_act(&app);
            app.busy = 0;
            app.busy_msg[0] = '\0';
        }

        if (app.toast_frames > 0 && --app.toast_frames == 0) app.toast[0] = '\0';

        frame(ren, &app);

        /* 帧耗时滑动平均：真机上 avg 超过 20ms 就说明渲染跟不上，
         * 输入延迟必然被拉长（这正是「按键黏」的量化指标） */
        {
            Uint64 tn = SDL_GetTicks64();
            double dt = (double)(tn - frame_t0);
            g_avg_ms = g_avg_ms > 0 ? g_avg_ms * 0.9 + dt * 0.1 : dt;
            frame_t0 = tn;
        }

        frames++;
        if (frames <= 3) applog("frame %d ok", frames);
        else if (frames % 300 == 0)
            applog("alive frame=%d screen=%d ent=%d avg=%.1fms tex_new=%u",
                   frames, app.screen, ha_nent(), frame_ms(), gfx_tex_new());
    }

    applog("exit");
    applog_flush();      /* 退出前把缓冲区刷到 SD 卡，否则最后 1 秒日志会丢 */
    ha_save_cache();     /* 退出时把当前实体状态存下来，下次启动直接画（节流外的强制写） */
    cam_close();
    applog("exit: cam closed");
    net_done();
    applog("exit: net done");
    gfx_shutdown();
    applog("exit: gfx done");
    romfsExit();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    applog("exit: sdl done");
    socketExit();
    applog("exit: all done");
    applog_flush();
    return 0;
}
