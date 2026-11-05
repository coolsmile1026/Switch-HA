/* 摄像头取图：HA 的 /api/camera_proxy/<entity_id> 直接返回一张 JPEG 快照。
 *
 * 分工刻意分开：
 *   worker 线程只负责「下载字节」（网络慢也不影响界面帧率）
 *   主线程 tick 里才解码成纹理（SDL 的纹理操作必须留在渲染线程）
 *
 * 拉不到就用预置文案提示原因，不阻塞、不崩溃。
 */

#include "cam.h"
#include "net.h"
#include "applog.h"

#ifdef __SWITCH__
#include <switch.h>
#endif

#include <SDL_image.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CAM_INTERVAL_MS 700
#define CAM_MAX_BYTES   (6 * 1024 * 1024)

static SDL_Renderer *g_ren;
static SDL_Texture  *g_tex;
static int  g_tex_w, g_tex_h;

static char g_url[512];
static char g_tok[768];
static char g_stat[120] = "未开始";

static pthread_t g_th;
static int  g_run;
static int  g_active;

static pthread_mutex_t g_mtx = PTHREAD_MUTEX_INITIALIZER;
static unsigned char *g_jpeg;      /* 最新一帧，等主线程解码 */
static int  g_jpeg_len;
static int  g_frames;
static int  g_fails;

static void set_stat(const char *s)
{
    pthread_mutex_lock(&g_mtx);
    snprintf(g_stat, sizeof g_stat, "%s", s);
    pthread_mutex_unlock(&g_mtx);
}

static void msleep(int ms)
{
#ifdef __SWITCH__
    svcSleepThread((int64_t)ms * 1000000LL);
#else
    SDL_Delay(ms);
#endif
}

static void *worker(void *arg)
{
    (void)arg;

    while (g_run) {
        char *url = NULL, *tok = NULL;
        pthread_mutex_lock(&g_mtx);
        if (g_url[0]) {
            url = strdup(g_url);
            tok = strdup(g_tok);
        }
        pthread_mutex_unlock(&g_mtx);

        if (!url) { msleep(200); continue; }

        unsigned char *buf = NULL;
        int len = 0;
        char err[96] = "";

        int got = net_get_bin(url, tok, &buf, &len, CAM_MAX_BYTES, err, sizeof err);
        free(url);
        free(tok);

        if (got > 0 && buf) {
            /* 只保留 JPEG 本体（有的代理会在前后带噪声） */
            int s = 0;
            while (s + 1 < len && !(buf[s] == 0xFF && buf[s + 1] == 0xD8)) s++;
            int e = len - 2;
            while (e > s && !(buf[e] == 0xFF && buf[e + 1] == 0xD9)) e--;

            if (e > s) {
                int n = e + 2 - s;
                unsigned char *copy = (unsigned char *)malloc((size_t)n);
                if (copy) {
                    memcpy(copy, buf + s, (size_t)n);

                    pthread_mutex_lock(&g_mtx);
                    free(g_jpeg);
                    g_jpeg = copy;
                    g_jpeg_len = n;
                    snprintf(g_stat, sizeof g_stat, "画面已连接");
                    pthread_mutex_unlock(&g_mtx);
                    g_fails = 0;
                }
            } else {
                set_stat("返回的不是图像");
                g_fails++;
            }
            free(buf);
        } else {
            g_fails++;
            pthread_mutex_lock(&g_mtx);
            snprintf(g_stat, sizeof g_stat, "取图失败：%s", err[0] ? err : "无响应");
            pthread_mutex_unlock(&g_mtx);
        }

        /* 连续失败就放慢，别把 HA 打满 */
        int wait = CAM_INTERVAL_MS;
        if (g_fails > 3) wait = 2500;
        if (g_fails > 10) wait = 6000;

        for (int slept = 0; slept < wait && g_run; slept += 50) msleep(50);
    }
    return NULL;
}

void cam_init(SDL_Renderer *ren)
{
    g_ren = ren;
    IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG);
}

int cam_open(const char *url, const char *token)
{
    cam_close();

    if (!url || !url[0]) { set_stat("没有可用的摄像头地址"); return -1; }

    pthread_mutex_lock(&g_mtx);
    snprintf(g_url, sizeof g_url, "%s", url);
    snprintf(g_tok, sizeof g_tok, "%s", token ? token : "");
    free(g_jpeg);
    g_jpeg = NULL;
    g_jpeg_len = 0;
    snprintf(g_stat, sizeof g_stat, "%s", "正在连接摄像头…");
    pthread_mutex_unlock(&g_mtx);

    g_frames = 0;
    g_fails = 0;
    g_run = 1;
    g_active = 1;

    if (pthread_create(&g_th, NULL, worker, NULL) != 0) {
        g_run = 0;
        g_active = 0;
        set_stat("无法创建取图线程");
        return -1;
    }

    applog("cam: open %s", url);
    return 0;
}

void cam_close(void)
{
    if (g_active) {
        g_run = 0;
        pthread_join(g_th, NULL);
        g_active = 0;
    }
    if (g_tex) { SDL_DestroyTexture(g_tex); g_tex = NULL; }
    g_tex_w = g_tex_h = 0;

    pthread_mutex_lock(&g_mtx);
    free(g_jpeg);
    g_jpeg = NULL;
    g_jpeg_len = 0;
    g_url[0] = '\0';
    pthread_mutex_unlock(&g_mtx);
}

void cam_reload(void)
{
    char url[512], tok[768];
    pthread_mutex_lock(&g_mtx);
    snprintf(url, sizeof url, "%s", g_url);
    snprintf(tok, sizeof tok, "%s", g_tok);
    pthread_mutex_unlock(&g_mtx);

    if (url[0]) {
        applog("cam: reload");
        cam_open(url, tok);
    }
}

void cam_tick(void)
{
    unsigned char *jpg = NULL;
    int len = 0;

    pthread_mutex_lock(&g_mtx);
    if (g_jpeg) {
        jpg = g_jpeg;
        len = g_jpeg_len;
        g_jpeg = NULL;
        g_jpeg_len = 0;
    }
    pthread_mutex_unlock(&g_mtx);

    if (!jpg) return;

    SDL_RWops *rw = SDL_RWFromMem(jpg, len);
    if (!rw) { free(jpg); return; }

    SDL_Surface *sf = IMG_Load_RW(rw, 1);   /* 1 = 顺带释放 rw */
    free(jpg);
    if (!sf) {
        applog("cam: decode fail %s", IMG_GetError());
        set_stat("图像解码失败");
        return;
    }

    SDL_Texture *t = SDL_CreateTextureFromSurface(g_ren, sf);
    g_tex_w = sf->w;
    g_tex_h = sf->h;
    SDL_FreeSurface(sf);

    if (!t) { set_stat("纹理创建失败"); return; }

    if (g_tex) SDL_DestroyTexture(g_tex);
    g_tex = t;
    g_frames++;
    if (g_frames == 1) applog("cam: first frame %dx%d", g_tex_w, g_tex_h);
}

int cam_ready(void) { return g_tex != NULL; }

const char *cam_status(void)
{
    return g_stat;
}

int cam_draw(SDL_Rect box)
{
    if (!g_tex) return 0;

    /* 等比缩放居中，留黑边 */
    float sx = (float)box.w / (float)g_tex_w;
    float sy = (float)box.h / (float)g_tex_h;
    float s = sx < sy ? sx : sy;

    int w = (int)(g_tex_w * s);
    int h = (int)(g_tex_h * s);
    SDL_Rect dst = { box.x + (box.w - w) / 2, box.y + (box.h - h) / 2, w, h };

    SDL_RenderCopy(g_ren, g_tex, NULL, &dst);
    return 1;
}
