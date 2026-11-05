#include "applog.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

#ifdef __SWITCH__
#define LOG_PATH "sdmc:/switch/mihome/log.txt"
#else
#define LOG_PATH "applog.txt"
#endif

/* 日志缓冲：真机上每次 fflush 都是一次 SD 卡同步写（10~50ms）。
 * 原来每条日志都 fflush，而按键路径上有两处日志（main.c 的 pad down、
 * ui.c 的 key ev），于是「按一下方向键 = 两次 SD 写」——方向键连发时
 * 手感就是又黏又顿。现在攒在内存里，最多每秒落一次盘。 */
#define LOGBUF 8192
static char   g_buf[LOGBUF];
static size_t g_len;
static time_t g_last_flush;

static FILE *g_f;

static FILE *ensure(void)
{
    if (!g_f) {
        /* 日志超过 512KB 就轮转：挪到 log.1.txt 留着上一次的现场
         * （排查「上次为什么崩」往往要用），最多保留一份不占太多卡 */
        FILE *chk = fopen(LOG_PATH, "rb");
        if (chk) {
            fseek(chk, 0, SEEK_END);
            long sz = ftell(chk);
            fclose(chk);
            if (sz > 512 * 1024) {
                remove(LOG_PATH ".1");
                if (rename(LOG_PATH, LOG_PATH ".1") != 0) {
                    /* 个别文件系统不让改名：退回直接清空 */
                    FILE *t = fopen(LOG_PATH, "wb");
                    if (t) fclose(t);
                }
            }
        }
        g_f = fopen(LOG_PATH, "a");
        if (g_f) {
            static const char banner[] = "\n---- new launch ----\n";
            fwrite(banner, 1, sizeof banner - 1, g_f);
            fflush(g_f);
        }
    }
    return g_f;
}

static void write_out(int force)
{
    if (!g_len) return;
    FILE *f = ensure();
    if (!f) { g_len = 0; return; }
    fwrite(g_buf, 1, g_len, f);
    g_len = 0;
    if (force) { fflush(f); g_last_flush = time(NULL); }
}

void applog_flush(void)
{
    write_out(1);
}

void applog(const char *fmt, ...)
{
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n >= sizeof line) n = sizeof line - 1;

    if (g_len + (size_t)n + 1 > LOGBUF) write_out(1);

    memcpy(g_buf + g_len, line, (size_t)n);
    g_len += (size_t)n;
    g_buf[g_len++] = '\n';

    /* 每秒最多真正写一次 SD 卡；程序崩溃最多丢 1 秒日志 */
    time_t now = time(NULL);
    if (now != g_last_flush) write_out(1);
}

void applog_close(void)
{
    write_out(1);
    if (g_f) {
        fclose(g_f);
        g_f = NULL;
    }
}
