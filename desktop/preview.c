/* 桌面预览：不需要 Switch，直接开个窗口把界面画出来，方便截图检查排版。
 *
 *   ./desktop/preview --screen home --shot out.bmp
 *   ./desktop/preview --screen dev --sel 2
 */

#include <SDL.h>
#include <stdio.h>
#include <string.h>

static Uint64 now_ms(void) { return SDL_GetTicks64(); }

#include "gfx.h"
#include "ha.h"
#include "ui.h"

int main(int argc, char **argv)
{
    const char *screen = "home";
    const char *shot = NULL;
    int sel = 0;
    int tab = -1;
    int zone = -1, side = -1;
    int age = -1;
    int setrow = -999;
    int dark = 0;
    const char *dlg = NULL;
    int bench = 0;
    int busy = 0;
    const char *done = NULL;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--screen") && i + 1 < argc) screen = argv[++i];
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--sel") && i + 1 < argc) sel = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--tab") && i + 1 < argc) tab = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--zone") && i + 1 < argc) zone = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--side") && i + 1 < argc) side = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--age") && i + 1 < argc) age = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--set") && i + 1 < argc) setrow = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dark")) dark = 1;
        else if (!strcmp(argv[i], "--dlg") && i + 1 < argc) dlg = argv[++i];
        else if (!strcmp(argv[i], "--bench")) bench = 1;
        else if (!strcmp(argv[i], "--busy")) busy = 1;
        else if (!strcmp(argv[i], "--done") && i + 1 < argc) done = argv[++i];
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

#ifdef __APPLE__
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
#endif

    SDL_Window *win = SDL_CreateWindow("Switch HA Preview", SDL_WINDOWPOS_CENTERED,
                                       SDL_WINDOWPOS_CENTERED, UI_W, UI_H, SDL_WINDOW_SHOWN);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);

    if (gfx_init(ren, "romfs/fonts/main.otf", 0) != 0) {
        fprintf(stderr, "font load failed\n");
        return 1;
    }

    static App app;
    ui_init(&app);
    if (dark) ui_theme_set(1);

    if (!strcmp(screen, "setup")) {
        /* 保持 ui_init 的默认地址，验证「打开就有预填」 */
        app.screen = SC_SETUP;
        app.login_focus = 1;
    } else if (!strcmp(screen, "setup-change")) {
        /* 从设置页「更换服务器地址」进来：应显示「取消」按钮 */
        app.screen = SC_SETUP;
        app.setup_from = 1;
        app.login_focus = 3;
        snprintf(app.status, sizeof app.status, "%s", "已取消，服务器设置保持不变");
    } else if (!strcmp(screen, "setup-empty")) {
        app.screen = SC_SETUP;
        app.login_focus = 0;
    } else if (!strcmp(screen, "login")) {
        ha_set_base("192.168.1.100:8123");
        app.screen = SC_LOGIN;
        snprintf(app.user, sizeof app.user, "%s", "coolsmile");
        snprintf(app.pass, sizeof app.pass, "%s", "secret123");
        app.login_focus = 4;
    } else if (!strcmp(screen, "login-token")) {
        ha_set_base("192.168.1.100:8123");
        app.screen = SC_LOGIN;
        app.use_token = 1;
        snprintf(app.token, sizeof app.token, "%s",
                 "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJ4In0.abcdefg");
        app.login_focus = 3;
    } else if (!strcmp(screen, "dev")) {
        ha_load();
        ha_sync(NULL, 0);
        ui_rebuild(&app);
        app.screen = SC_DEV;
        app.dev_idx = sel;
    } else if (!strcmp(screen, "diag")) {
        app.screen = SC_SETUP;
        snprintf(app.addr, sizeof app.addr, "%s", "https://ha.example.com");
        app.diag_from = SC_SETUP;
        ha_diag(app.diag, (int)sizeof app.diag);
        app.screen = SC_DIAG;
    } else if (!strcmp(screen, "cam")) {
        ha_load();
        ha_sync(NULL, 0);
        ui_rebuild(&app);
        snprintf(app.cam_eid, sizeof app.cam_eid, "%s", "camera.doorbell");
        app.screen = SC_CAM;
    } else {
        ha_load();
        ha_sync(NULL, 0);
        ui_rebuild(&app);
        app.screen = SC_HOME;
        app.card_sel = sel;
        app.area_sel = -1;
        if (tab >= 0) app.tab = tab;
        if (sel > 0) app.card_sel = sel;
        if (zone >= 0) app.zone = zone;
        if (side >= 0) app.side_sel = side;
        if (setrow != -999) app.set_sel = setrow;
        /* 让 grid_row 跟着焦点收敛，截图才能看到滚动后的那一屏 */
        ui_rebuild(&app);
        /* --age N：模拟「N 秒前同步过」，验证副标题的数据新鲜度 */
        if (age >= 0) app.sync_ms = SDL_GetTicks() - (Uint32)age * 1000;
    }

    /* --dlg 9.9.9：模拟「发现新版本」确认弹层（截图校验用） */
    if (dlg) {
        app.dlg = 1;
        app.dlg_sel = 1;
        snprintf(app.dlg_ver, sizeof app.dlg_ver, "%s", dlg);
    }

    /* --bench：连画 30 帧，报告每帧新建纹理数与耗时。
     * 稳态应为「0 张新建」，否则说明文字缓存在抖动。 */
    if (bench) {
        for (int f = 0; f < 30; f++) {
            Uint64 t0 = now_ms();
            gfx_tex_new_reset();
            SDL_SetRenderDrawColor(ren, 245, 247, 249, 255);
            SDL_RenderClear(ren);
            ui_draw(&app);
            SDL_RenderPresent(ren);
            gfx_frame_end();
            Uint64 t1 = now_ms();
            printf("frame %2d: 新建纹理 %3u 张, 耗时 %llu ms\n",
                   f, gfx_tex_new(), (unsigned long long)(t1 - t0));
        }
        gfx_shutdown();
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 0;
    }

    SDL_SetRenderDrawColor(ren, 245, 247, 249, 255);
    SDL_RenderClear(ren);
    ui_draw(&app);
    SDL_RenderPresent(ren);
    gfx_frame_end();

    /* --done 9.9.9：叠加「更新完成」弹层 */
    if (done) {
        app.upd_done = 1;
        snprintf(app.upd_done_ver, sizeof app.upd_done_ver, "%s", done);
        ui_draw(&app);
        SDL_RenderPresent(ren);
        gfx_frame_end();
    }

    /* --busy：叠加下载进度遮罩（校验速度/剩余时间的排版） */
    if (busy) {
        gfx_busy_overlay("正在下载新版本 9.9.9",
                         "6.2 / 13.9 MB   45%   1.82 MB/s   剩余 0:04", 45);
    }

    if (shot) {
        /* 手写 24bpp BMP：SDL_SaveBMP 存的是带 V4 头的 32bpp，很多查看器不认 */
        SDL_Surface *ss = SDL_CreateRGBSurfaceWithFormat(0, UI_W, UI_H, 32,
                                                         SDL_PIXELFORMAT_ARGB8888);
        SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, ss->pixels, ss->pitch);

        int row = (UI_W * 3 + 3) / 4 * 4;
        unsigned char *px = (unsigned char *)malloc((size_t)row * UI_H);
        const unsigned char *src = (const unsigned char *)ss->pixels;
        int spitch = ss->pitch;
        for (int y = 0; y < UI_H; y++) {
            const unsigned char *sl = src + (size_t)y * spitch;   /* ARGB */
            unsigned char *dl = px + (size_t)(UI_H - 1 - y) * row;
            for (int x = 0; x < UI_W; x++) {
                dl[x * 3 + 0] = sl[x * 4 + 0];   /* B（ARGB 小端内存序） */
                dl[x * 3 + 1] = sl[x * 4 + 1];   /* G */
                dl[x * 3 + 2] = sl[x * 4 + 2];   /* R */
            }
        }

        FILE *fp = fopen(shot, "wb");
        if (fp) {
            unsigned char hdr[54] = { 0 };
            unsigned int size = 54 + (unsigned int)row * UI_H;
            hdr[0] = 'B'; hdr[1] = 'M';
            memcpy(hdr + 2, &size, 4);
            unsigned int off = 54;
            memcpy(hdr + 10, &off, 4);
            unsigned int isz = 40;
            memcpy(hdr + 14, &isz, 4);
            int w = UI_W, hh = UI_H;
            memcpy(hdr + 18, &w, 4);
            memcpy(hdr + 22, &hh, 4);
            unsigned short planes = 1, bpp = 24;
            memcpy(hdr + 26, &planes, 2);
            memcpy(hdr + 28, &bpp, 2);
            unsigned int comp = 0, imgsz = (unsigned int)row * UI_H;
            memcpy(hdr + 34, &imgsz, 4);
            int ppm = 2835;
            memcpy(hdr + 38, &ppm, 4);
            memcpy(hdr + 42, &ppm, 4);
            fwrite(hdr, 1, 54, fp);
            fwrite(px, 1, (size_t)row * UI_H, fp);
            fclose(fp);
            printf("saved %s\n", shot);
        } else {
            fprintf(stderr, "cannot write %s\n", shot);
        }
        free(px);
        SDL_FreeSurface(ss);
        gfx_shutdown();
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 0;
    }

    int running = 1;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
            else if (ev.type == SDL_KEYDOWN) {
                switch (ev.key.keysym.sym) {
                case SDLK_ESCAPE: running = 0; break;
                case SDLK_UP:    ui_event(&app, EV_UP); break;
                case SDLK_DOWN:  ui_event(&app, EV_DOWN); break;
                case SDLK_LEFT:  ui_event(&app, EV_LEFT); break;
                case SDLK_RIGHT: ui_event(&app, EV_RIGHT); break;
                case SDLK_RETURN: ui_event(&app, EV_OK); break;
                case SDLK_BACKSPACE: ui_event(&app, EV_BACK); break;
                case SDLK_r:     ui_event(&app, EV_REFRESH); break;
                default: break;
                }
            } else if (ev.type == SDL_MOUSEBUTTONDOWN) {
                ui_tap(&app, ev.button.x, ev.button.y);
            }
        }
        ui_run_act(&app);
        SDL_SetRenderDrawColor(ren, 245, 247, 249, 255);
        SDL_RenderClear(ren);
        ui_draw(&app);
        SDL_RenderPresent(ren);
        gfx_frame_end();
        SDL_Delay(16);
    }

    gfx_shutdown();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
