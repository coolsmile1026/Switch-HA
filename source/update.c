/* OTA 自更新实现，见 update.h */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "applog.h"
#include "json.h"
#include "net.h"
#include "update.h"

#ifdef __SWITCH__
#include <switch.h>
#endif

#ifdef __SWITCH__
#define UPD_BASE   "sdmc:/switch"
#define UPD_NRO    "sdmc:/switch/switch-ha.nro"
#else
#define UPD_BASE   "data"
#define UPD_NRO    "data/switch-ha.nro"
#endif

/* 默认更新源：GitHub Releases。换自己的仓库/自托管地址时，
 * 在 SD 卡放 sdmc:/switch/switch-ha/update.txt 第一行写 manifest URL
 * 注意：仓库名大小写要与 GitHub 上完全一致（API 不敏感，但便于排查） */
#define UPD_SRC_DEFAULT \
    "https://api.github.com/repos/coolsmile1026/Switch-HA/releases/latest"

const char *upd_cur_version(void) { return APP_VERSION_STR; }

static UpdPaint g_paint;
void upd_set_paint(UpdPaint fn) { g_paint = fn; }

static void paint(const char *title, const char *sub, int pct)
{
    if (g_paint) g_paint(title, sub, pct);
}

/* 下载失败时依次尝试的镜像前缀（GitHub 直连被墙/极慢时唯一可行路径）。
 * release 附件地址形如 https://github.com/<owner>/<repo>/releases/download/...
 * 镜像用法就是把原地址整段接在前缀后面。
 * 实际用哪条由下载前的线路竞速决定（net_race_probe），这里只是候选池。
 * update.txt 里用 "mirror: <前缀>" 追加，写 "mirror: none" 可整体关掉。 */
static const char *UPD_MIRROR[] = {
    "https://gh-proxy.com/",
    "https://ghfast.top/",
    "https://gh-proxy.net/",
    "https://ghfile.geekertao.top/",
    "https://ghproxy.net/",
};

/* 读 update.txt：
 *   第 1 行 manifest URL
 *   第 2 行 NRO 自身路径（可选）
 *   其余  mirror: <前缀>   追加下载镜像（镜像用法：前缀 + 原 GitHub 地址）
 *         mirror: none     关闭内置镜像
 */
#define UPD_MAX_MIRROR 8

static char g_mirror[UPD_MAX_MIRROR][160];
static int  g_nmirror;
static int  g_mirror_off;

static void upd_paths(char *nro, int nsz, char *manifest, int msz)
{
    snprintf(nro, (size_t)nsz, "%s", UPD_NRO);
    snprintf(manifest, (size_t)msz, "%s", UPD_SRC_DEFAULT);
    g_nmirror = 0;
    g_mirror_off = 0;

    char p[256];
    snprintf(p, sizeof p, UPD_BASE "/switch-ha/update.txt");
    FILE *f = fopen(p, "rb");
    if (!f) return;

    char line[512];
    int got = 0, got_nro = 0;
    while (fgets(line, sizeof line, f)) {
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r' ||
                     line[n - 1] == ' ' || line[n - 1] == '\t'))
            line[--n] = '\0';
        if (!n || line[0] == '#') continue;

        if (strncmp(line, "mirror:", 7) == 0) {
            char *v = line + 7;
            while (*v == ' ' || *v == '\t') v++;
            if (strcmp(v, "none") == 0) {
                g_mirror_off = 1;
            } else if (v[0] && g_nmirror < UPD_MAX_MIRROR) {
                size_t vl = strlen(v);
                if (vl && v[vl - 1] != '/') { v[vl] = '/'; v[vl + 1] = '\0'; }
                snprintf(g_mirror[g_nmirror++], sizeof g_mirror[0], "%s", v);
            }
            continue;
        }

        if (got == 0)      { snprintf(manifest, (size_t)msz, "%s", line); got = 1; }
        else if (!got_nro) { snprintf(nro, (size_t)nsz, "%s", line); got_nro = 1; }
    }
    fclose(f);
    applog("upd: manifest=%s nro=%s mirror_off=%d nmirror=%d",
           manifest, nro, g_mirror_off, g_nmirror);
}

/* ---- 版本比较：逐段数字，忽略 v 前缀和后缀（1.10.0 > 1.9.9 > 1.0.2） ---- */
static long ver_seg(const char **p)
{
    while (**p && (**p < '0' || **p > '9')) (*p)++;
    if (!**p) return -1;
    return strtol(*p, (char **)p, 10);
}

static int ver_cmp(const char *a, const char *b)
{
    const char *pa = a, *pb = b;
    for (;;) {
        long va = ver_seg(&pa), vb = ver_seg(&pb);
        if (va < 0 && vb < 0) return 0;
        if (va < 0) va = 0;
        if (vb < 0) vb = 0;
        if (va != vb) return va < vb ? -1 : 1;
    }
}

/* ---- manifest 解析，0 = 成功 ----
 * alt 是备用下载地址：GitHub 的 browser_download_url 会 302 到
 * objects.githubusercontent.com，个别网络环境会 502；此时改走
 * api.github.com/.../releases/assets/<id>（需要 Accept: octet-stream） */
static int parse_manifest(const char *txt, char *ver, int vsz,
                          char *url, int usz, char *alt, int asz)
{
    ver[0] = url[0] = alt[0] = '\0';

    if (txt[0] == '{') {
        JNode *root = json_parse(txt);
        if (!root) return -1;

        const char *tag = json_str_or(root, "tag_name", NULL);
        if (tag && tag[0]) {
            /* GitHub Releases：在 assets 里找第一个 .nro */
            const JNode *assets = json_get(root, "assets");
            const char *dl = NULL, *ap = NULL;
            if (assets) {
                int na = json_len(assets);
                for (int i = 0; i < na && !dl; i++) {
                    const JNode *as = json_at(assets, i);
                    const char *name = json_str_or(as, "name", "");
                    size_t nl = strlen(name);
                    if (nl > 4 && strcmp(name + nl - 4, ".nro") == 0) {
                        dl = json_str_or(as, "browser_download_url", "");
                        ap = json_str_or(as, "url", "");
                    }
                }
            }
            snprintf(ver, (size_t)vsz, "%s", tag);
            if (dl && dl[0]) snprintf(url, (size_t)usz, "%s", dl);
            if (ap && ap[0]) snprintf(alt, (size_t)asz, "%s", ap);
            json_free(root);
            return url[0] ? 0 : -2;
        }

        /* 自托管 JSON：{"version":"..","url":".."} */
        const char *v = json_str_or(root, "version", NULL);
        const char *u = json_str_or(root, "url", NULL);
        if (v && v[0] && u && u[0]) {
            snprintf(ver, (size_t)vsz, "%s", v);
            snprintf(url, (size_t)usz, "%s", u);
            json_free(root);
            return 0;
        }
        json_free(root);
        return -1;
    }

    /* 纯文本：第一行版本，第二行地址 */
    char buf[512];
    const char *p = txt;
    const char *e = strchr(p, '\n');
    if (!e) return -1;
    size_t n = (size_t)(e - p);
    if (n >= sizeof buf) n = sizeof buf - 1;
    memcpy(buf, p, n);
    buf[n] = '\0';
    while (n && (buf[n - 1] == '\r' || buf[n - 1] == ' ')) buf[--n] = '\0';
    snprintf(ver, (size_t)vsz, "%.*s", vsz - 1, buf);

    p = e + 1;
    e = strchr(p, '\n');
    n = e ? (size_t)(e - p) : strlen(p);
    while (n && (p[n - 1] == '\r' || p[n - 1] == ' ')) n--;
    if (n <= 0 || n >= (size_t)usz) return -1;
    memcpy(url, p, n);
    url[n] = '\0';
    return url[0] ? 0 : -1;
}

/* 把 .new 换成正式文件。返回 0=换好了，1=换不动（留给下次启动），-1=失败 */
static int upd_swap(const char *nro, char *msg, int msgsz)
{
    char oldp[256], newp[256];
    snprintf(oldp, sizeof oldp, "%s.old", nro);
    snprintf(newp, sizeof newp, "%s.new", nro);

    remove(oldp);
    struct stat st;
    if (stat(nro, &st) == 0) {
        if (rename(nro, oldp) != 0) {
            /* 个别环境运行中不让改名：留着 .new，下次启动再换 */
            snprintf(msg, (size_t)msgsz, "%s", "已下载，下次启动生效");
            applog("upd: rename running nro failed, defer to next boot");
            return 1;
        }
    }
    if (rename(newp, nro) != 0) {
        rename(oldp, nro);      /* 回滚 */
        snprintf(msg, (size_t)msgsz, "%s", "替换文件失败");
        applog("upd: rename new->nro failed");
        return -1;
    }
    applog("upd: applied %s", nro);
    return 0;
}

void upd_apply_pending(void)
{
    char nro[192], manifest[384];
    upd_paths(nro, sizeof nro, manifest, sizeof manifest);

    char newp[256], oldp[256];
    snprintf(newp, sizeof newp, "%s.new", nro);
    snprintf(oldp, sizeof oldp, "%s.old", nro);

    struct stat st;
    if (stat(newp, &st) == 0) {
        remove(oldp);
        if (stat(nro, &st) == 0 && rename(nro, oldp) != 0) {
            applog("upd: pending apply failed (cannot move old)");
            return;
        }
        if (rename(newp, nro) == 0) {
            remove(oldp);
            applog("upd: applied pending update from last session");
        } else {
            rename(oldp, nro);
            applog("upd: pending apply failed (cannot move new)");
        }
    } else {
        remove(oldp);       /* 清理上次成功替换后留下的备份 */
    }
}

/* ---- 下载进度：阻塞期间直接重绘遮罩 ----
 * 每 1% 或每 512KB 刷一次，避免把 SDL 纹理池刷爆 */
static char g_dl_ver[64];
static int  g_dl_last_pct = -1;
static long long g_dl_next_mark;
static double g_dl_last_draw;

static double g_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* 速度分档：B/s / KB/s / MB/s，读数稳定不留一堆小数 */
static void fmt_speed(char *out, int osz, double bps)
{
    if (bps >= 1048576.0)      snprintf(out, (size_t)osz, "%.2f MB/s", bps / 1048576.0);
    else if (bps >= 1024.0)    snprintf(out, (size_t)osz, "%.0f KB/s", bps / 1024.0);
    else if (bps > 0.5)        snprintf(out, (size_t)osz, "%.0f B/s", bps);
    else                       snprintf(out, (size_t)osz, "--");
}

/* ---- 下载进度：阻塞期间直接重绘遮罩 ----
 * 每 200ms 刷一次（curl 回调本身约 100ms 一次），既不刷爆纹理池，
 * 又能让速度读数看起来是活的。 */
static void dl_prog(void *ud, long long got, long long total, double bps)
{
    (void)ud;
    if (!g_paint) return;

    double t = g_now();
    int pct = total > 0 ? (int)(got * 100 / total) : -1;
    /* 百分比变了立刻画；否则最多 5 次/秒，够看出速度在动 */
    int pct_changed = (pct != g_dl_last_pct);
    if (!pct_changed && t - g_dl_last_draw < 0.20) return;
    g_dl_last_pct = pct;
    g_dl_last_draw = t;

    char title[96], sub[160], spd[32];
    snprintf(title, sizeof title, "正在下载新版本 %s", g_dl_ver);
    fmt_speed(spd, sizeof spd, bps);

    if (total > 0) {
        long long left = total - got;
        int eta = bps > 1.0 ? (int)((double)left / bps) : -1;
        if (eta >= 0)
            snprintf(sub, sizeof sub, "%.1f / %.1f MB   %d%%   %s   剩余 %d:%02d",
                     (double)got / 1048576.0, (double)total / 1048576.0,
                     pct < 0 ? 0 : pct, spd, eta / 60, eta % 60);
        else
            snprintf(sub, sizeof sub, "%.1f / %.1f MB   %d%%   %s",
                     (double)got / 1048576.0, (double)total / 1048576.0,
                     pct < 0 ? 0 : pct, spd);
        paint(title, sub, pct);
    } else {
        snprintf(sub, sizeof sub, "已下载 %.1f MB   %s",
                 (double)got / 1048576.0, spd);
        paint(title, sub, -1);
    }
}

/* ---- 检查：只拉 manifest 比版本，不下载 ----
 * 返回 1 = 有新版本（upd_new_version() / upd_new_size() 有效）
 *      0 = 已是最新
 *     -1 = 失败（msg 说明原因） */
static char g_new_ver[64];
static char g_done_ver[64];
static char g_new_url[512];
static char g_new_alt[512];
static char g_nro[192];
static int  g_has_new;

const char *upd_new_version(void) { return g_new_ver; }

const char *upd_done_version(void) { return g_done_ver; }

/* 去掉 tag 前缀的 v/V，界面上统一自己加「v」 */
static const char *ver_bare(const char *v)
{
    while (*v == 'v' || *v == 'V' || *v == ' ') v++;
    return v;
}

int upd_check(char *msg, int msgsz)
{
    char manifest[384];
    g_has_new = 0;
    g_new_ver[0] = g_new_url[0] = g_new_alt[0] = '\0';

    upd_paths(g_nro, sizeof g_nro, manifest, sizeof manifest);

    time_t t0 = time(NULL);
    paint("正在检查更新…", "正在访问 GitHub，只查版本信息", -1);

    NetResp r;
    memset(&r, 0, sizeof r);
    const char *hdrs[] = { "Accept: application/vnd.github+json" };
    int rc = net_request_ex(manifest, "GET", hdrs, 1, NULL, &r, 20, 5000, 1);
    applog("upd: manifest fetch rc=%d status=%d %lds tries=%d err=%s",
           rc, r.status, (long)(time(NULL) - t0), r.tries, r.err);
    if (rc < 200 || rc >= 300) {
        if (rc == 404)
            snprintf(msg, (size_t)msgsz, "%s", "没有发布（404），检查 update.txt");
        else
            snprintf(msg, (size_t)msgsz, "检查失败：%s",
                     r.err[0] ? r.err : (rc ? "" : "网络错误"));
        if (rc >= 400 && !r.err[0])
            snprintf(msg, (size_t)msgsz, "检查失败：HTTP %d", rc);
        net_resp_free(&r);
        return -1;
    }

    int prc = parse_manifest(r.p, g_new_ver, sizeof g_new_ver,
                             g_new_url, sizeof g_new_url,
                             g_new_alt, sizeof g_new_alt);
    net_resp_free(&r);
    if (prc != 0) {
        snprintf(msg, (size_t)msgsz, "%s", prc == -2
                 ? "发布里没有 .nro 附件" : "无法解析更新信息");
        return -1;
    }
    applog("upd: latest=%s cur=%s", g_new_ver, APP_VERSION_STR);

    /* 已是最新：提示里用「远端的最新号」，而不是本机编译进去的号，
     * 这样即便构建产物偏旧，用户看到的也是发布页真实的版本 */
    if (ver_cmp(g_new_ver, APP_VERSION_STR) <= 0) {
        snprintf(msg, (size_t)msgsz, "已是最新版本 v%s", ver_bare(g_new_ver));
        return 0;
    }
    g_has_new = 1;
    snprintf(msg, (size_t)msgsz, "%s", g_new_ver);
    return 1;
}

/* ---- 安装：用上次 upd_check 的结果下载并替换 ----
 * 返回 0 = 装好了，-1 = 失败 */
int upd_install(char *msg, int msgsz)
{
    if (!g_has_new) {
        snprintf(msg, (size_t)msgsz, "%s", "没有待安装的新版本，请先检查更新");
        return -1;
    }

    snprintf(g_dl_ver, sizeof g_dl_ver, "%s", g_new_ver);
    g_dl_last_pct = -1;
    g_dl_next_mark = 512 * 1024;
    g_dl_last_draw = 0;

    static char mbuf[UPD_MAX_MIRROR][896];
    const char *cand[UPD_MAX_MIRROR + 4];
    int nc = 0;
    cand[nc++] = g_new_url;
    if (g_new_alt[0]) cand[nc++] = g_new_alt;
    if (!g_mirror_off && strncmp(g_new_url, "https://github.com/", 19) == 0) {
        int nm = g_nmirror ? g_nmirror
                           : (int)(sizeof UPD_MIRROR / sizeof UPD_MIRROR[0]);
        if (nm > UPD_MAX_MIRROR) nm = UPD_MAX_MIRROR;
        for (int i = 0; i < nm; i++) {
            const char *pre = g_nmirror ? g_mirror[i] : UPD_MIRROR[i];
            /* 前缀 + 地址可能超长，截断到这里不报错（截断的 URL 只会下载失败，
             * 不会越界）；正常前缀只有二三十字节 */
            int n = snprintf(mbuf[i], sizeof mbuf[i], "%s", pre);
            if (n < 0 || (size_t)n >= sizeof mbuf[i]) continue;
            snprintf(mbuf[i] + n, sizeof mbuf[i] - (size_t)n, "%s", g_new_url);
            cand[nc++] = mbuf[i];
        }
    }

    char title[96];
    snprintf(title, sizeof title, "正在下载新版本 %s", g_new_ver);
    paint(title, "开始下载…", 0);

    /* 先竞速选线：直链 / API / 各镜像同时探 64KB，最快的先下整包。
     * 以前按固定顺序试，第一条直链在墙内动辄卡几十秒才轮到镜像；
     * 竞速能把选线压到 1~3 秒，且哪条快用哪条。 */
    int best = net_race_probe(cand, nc, 64 * 1024, 3000, NULL);
    applog("upd: race winner [%d]/%d %s", best, nc,
           best >= 0 ? cand[best] : "(none)");
    if (best > 0) {
        const char *w = cand[best];
        memmove(&cand[1], &cand[0], (size_t)best * sizeof cand[0]);
        cand[0] = w;
    } else if (best < 0) {
        paint(title, "线路探测都失败，仍按顺序尝试…", 0);
    } else {
        paint(title, "已选定最快的下载线路", 0);
    }

    unsigned char *bin = NULL;
    int blen = 0;
    char err[160] = "";
    int ok = 0;
    for (int i = 0; i < nc && !ok; i++) {
        time_t ts = time(NULL);
        if (i > 0) {
            char sub[96];
            snprintf(sub, sizeof sub, "第 %d 条线路不通，换下一条", i);
            paint(title, sub, 0);
        }
        g_dl_last_pct = -1;
        g_dl_last_draw = 0;
        if (net_get_file_prog(cand[i], NULL, &bin, &blen, 64 * 1024 * 1024,
                              900, dl_prog, NULL, err, sizeof err) == 0) {
            ok = 1;
            applog("upd: dl ok via [%d] %lds %d bytes", i,
                   (long)(time(NULL) - ts), blen);
        } else {
            applog("upd: dl [%d] %s failed after %lds: %s",
                   i, cand[i], (long)(time(NULL) - ts), err);
        }
    }
    if (!ok) {
        snprintf(msg, (size_t)msgsz, "下载失败（%d 条线路都不通）：%s",
                 nc, err[0] ? err : "网络错误");
        return -1;
    }

    paint(title, "写入 SD 卡…", 100);

    /* 校验：NRO 魔数。
     * elf2nro 产出的文件在偏移 0 处是 16 字节头（2000 0014 f000 0000 +
     * "HOMEBREW"），"NRO0" 在 0x10。早期版本在 0 处比对，导致下载成功
     * 也一定判成「不是有效的 NRO」——OTA 永远装不上。两处都认。 */
    if (blen < 0x20 ||
        (memcmp(bin + 0x10, "NRO0", 4) != 0 && memcmp(bin, "NRO0", 4) != 0)) {
        applog("upd: bad nro magic, len=%d head=%02x%02x%02x%02x %02x%02x%02x%02x",
               blen, bin[0], bin[1], bin[2], bin[3], bin[4], bin[5], bin[6], bin[7]);
        free(bin);
        snprintf(msg, (size_t)msgsz, "%s", "下载的文件不是有效的 NRO");
        return -1;
    }

    /* 落盘 + 替换。不做 fsync / stat / 设备提交——这些在 Switch 的
     * FAT 缓存上要把 11MB 全刷进卡，动辄十几秒还白白磨卡；
     * 实测也没换来可靠性（重启后仍是旧版）。关闭应用退回 hbmenu
     * 的几秒钟足够写回完成，用户手动重开即可拿到新版。 */
    char tmp[256];
    snprintf(tmp, sizeof tmp, "%s.new", g_nro);
    FILE *f = fopen(tmp, "wb");
    if (!f) {
        free(bin);
        snprintf(msg, (size_t)msgsz, "%s", "写 SD 卡失败");
        return -1;
    }
    size_t wn = fwrite(bin, 1, (size_t)blen, f);
    fclose(f);
    free(bin);
    if (wn != (size_t)blen) {
        remove(tmp);
        snprintf(msg, (size_t)msgsz, "%s", "写入 SD 卡不完整，请重试");
        return -1;
    }
    applog("upd: downloaded %d bytes -> %s", blen, tmp);

    int src = upd_swap(g_nro, msg, msgsz);
    if (src >= 0) snprintf(g_done_ver, sizeof g_done_ver, "%s", g_new_ver);
    if (src == 0) snprintf(msg, (size_t)msgsz, "已更新到 %s，关闭应用后重新打开生效", g_new_ver);
    else if (src == 1) { /* msg 已是「已下载，重启生效」 */ }
    g_has_new = 0;
    return src < 0 ? -1 : 0;
}

/* 一步到位（检查 + 安装），给没有交互的场合用 */
int upd_run(char *msg, int msgsz)
{
    int c = upd_check(msg, msgsz);
    if (c < 0) return -1;
    if (c == 0) return 1;
    return upd_install(msg, msgsz);
}
