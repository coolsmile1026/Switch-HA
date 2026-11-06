#include "ha.h"
#include "json.h"
#include "net.h"
#include "applog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>

#ifdef __SWITCH__
#include <switch.h>
#define HA_DIR  "sdmc:/switch/switch-ha"
#define HA_CFG  "sdmc:/switch/switch-ha/auth.txt"
#define HA_CACHE "sdmc:/switch/switch-ha/ents.txt"
#else
#define HA_DIR  "data"
#define HA_CFG  "data/auth.txt"
#define HA_CACHE "data/ents.txt"
#endif

/* HA 要求 client_id 是一个 URL（OAuth/IndieAuth 规则） */
#define HA_CLIENT_ID "http://switch-ha.local/"

static void cache_save(int force);      /* 状态缓存写盘（实现在文件后部） */

static char  g_base[192];   /* 地址 1：一般填内网，http://192.168.x.x:8123 */
static char  g_alt[192];    /* 地址 2：一般填外网，https://你的域名 */
static int   g_slot;        /* 当前走哪条：0 = 地址1，1 = 地址2 */
static int   g_switching;   /* 正在自动切换（防止回退递归） */
/* 本次运行里内网到底通不通：-1 还没试过，1 通，0 不通。
 * 一旦判定不通，后续请求直接走外网，不必每条都先撞一遍内网超时。
 * 进程重启就回到 -1，所以从外面走回家、重开 App 会自动重新用回内网。 */
static int   g_lan_state = -1;
static char  g_user[64];
static char  g_ip[64];      /* 手动指定的服务器 IP（域名解析不了时用） */
static char  g_access[768];
static char  g_refresh[768];
static long  g_expires;
static char  g_version[24];
static int   g_ready;
static int   g_theme;       /* 0 = 浅色，1 = 深色（存在配置文件里） */

/* 当前生效的地址：地址 2 有值且已切过去就用它 */
static const char *cur(void)
{
    if (!g_base[0] && g_alt[0]) return g_alt;
    return (g_slot == 1 && g_alt[0]) ? g_alt : g_base;
}

static int has_alt(void) { return g_alt[0] != '\0'; }

/* 「内网 / 外网」标签：私有网段和 .local 算内网，其余算外网。
 * 只用于界面和日志措辞，不影响连接逻辑 */
static int is_lan_addr(const char *url)
{
    const char *p = url ? strstr(url, "://") : NULL;
    if (!p) return 1;
    p += 3;
    char host[128];
    int n = 0;
    while (p[n] && p[n] != '/' && p[n] != ':' && n < (int)sizeof host - 1) {
        host[n] = p[n];
        n++;
    }
    host[n] = '\0';

    if (strncmp(host, "192.168.", 8) == 0) return 1;
    if (strncmp(host, "10.", 3) == 0) return 1;
    if (strncmp(host, "127.", 4) == 0) return 1;
    if (strncmp(host, "172.16.", 7) == 0) return 1;
    if (strstr(host, ".local")) return 1;
    if (!strchr(host, '.')) return 1;   /* homeassistant 这类单标签主机名 */
    return 0;
}

static HaEnt  g_ent[HA_MAX_ENT];
static int    g_nent;
static HaArea g_area[HA_MAX_AREA];
static int    g_narea;
static HaEnt  g_scene[HA_MAX_SCENE];
static int    g_nscene;
/* 本次同步被容量挡掉的数量：达到上限时提示用户，而不是静默少显示 */
static int    g_drop_ent, g_drop_scene;

/* ---------------- 配置持久化 ---------------- */

static void cfg_clear(void)
{
    g_base[0] = g_alt[0] = g_user[0] = g_access[0] = g_refresh[0] = '\0';
    g_ip[0] = '\0';
    g_slot = 0;
    g_expires = 0;
    g_ready = 0;
}

void ha_forget(void)
{
    remove(HA_CFG);
    cfg_clear();
    g_nent = g_narea = g_nscene = 0;
    applog("ha: credentials cleared");
}

static void cfg_save(void)
{
    FILE *f = fopen(HA_CFG, "wb");
    if (!f) { applog("ha: cannot write %s", HA_CFG); return; }
    fprintf(f, "base=%s\n", g_base);
    if (g_alt[0]) fprintf(f, "alt=%s\n", g_alt);
    if (g_slot)   fprintf(f, "use=%d\n", g_slot);
    if (g_ip[0]) fprintf(f, "ip=%s\n", g_ip);
    fprintf(f, "user=%s\n", g_user);
    fprintf(f, "access=%s\n", g_access);
    fprintf(f, "refresh=%s\n", g_refresh);
    fprintf(f, "expires=%ld\n", g_expires);
    if (g_theme) fprintf(f, "theme=1\n");
    fclose(f);
}

static void trim_eol(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t'))
        s[--n] = '\0';
}

/* 从 URL 里抠出主机名 */
static int host_of(const char *url, char *out, int outsz)
{
    const char *p = url ? strstr(url, "://") : NULL;
    if (!p) return 0;
    p += 3;
    const char *e = p;
    while (*e && *e != '/' && *e != ':') e++;
    int n = (int)(e - p);
    if (n <= 0 || n >= outsz) return 0;
    memcpy(out, p, (size_t)n);
    out[n] = '\0';
    return 1;
}

static int base_host(char *out, int outsz) { return host_of(g_base, out, outsz); }

/* IP 覆盖只服务于「地址 1 填域名、但解析不到」这一种情况；
 * 走地址 2（外网）时清掉，免得把外网域名指到内网 IP 上 */
static void apply_ip_override(void)
{
    char host[128];
    if (g_slot == 0 && g_ip[0] && base_host(host, sizeof host))
        net_set_host_ip(host, g_ip);
    else
        net_set_host_ip(NULL, NULL);
}

int ha_load(void)
{
    cfg_clear();
    g_version[0] = '\0';

    FILE *f = fopen(HA_CFG, "rb");
    if (!f) return 0;

    char line[1024];
    while (fgets(line, sizeof line, f)) {
        trim_eol(line);
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *k = line, *v = eq + 1;

        if (strcmp(k, "base") == 0)         snprintf(g_base, sizeof g_base, "%s", v);
        else if (strcmp(k, "alt") == 0)     snprintf(g_alt, sizeof g_alt, "%s", v);
        else if (strcmp(k, "use") == 0)     g_slot = (atoi(v) == 1 && g_alt[0]) ? 1 : 0;
        else if (strcmp(k, "ip") == 0)      snprintf(g_ip, sizeof g_ip, "%s", v);
        else if (strcmp(k, "user") == 0)    snprintf(g_user, sizeof g_user, "%s", v);
        else if (strcmp(k, "access") == 0)  snprintf(g_access, sizeof g_access, "%s", v);
        else if (strcmp(k, "refresh") == 0) snprintf(g_refresh, sizeof g_refresh, "%s", v);
        else if (strcmp(k, "expires") == 0) g_expires = atol(v);
        else if (strcmp(k, "theme") == 0)   g_theme = (atoi(v) == 1);
    }
    fclose(f);

    if (!g_base[0] || (!g_access[0] && !g_refresh[0])) {
        applog("ha: config incomplete (base=%d tok=%d)",
               (int)strlen(g_base), (int)strlen(g_access));
        return 0;
    }
    /* 照抄上次成功走的线路（use=）：上次从外网同步成功，这次直接走
     * 外网，不再每次启动都先撞一遍内网超时；反过来同理。
     * 真走不通时 api() 会自动切换并把新线路记回配置。
     * 唯一例外：只填了外网地址时固定走外网。 */
    if (!g_base[0] && g_alt[0]) g_slot = 1;
    g_ready = 1;
    apply_ip_override();
    applog("ha: loaded base=%s alt=%s use=%d user=%s ip=%s",
           g_base, g_alt, g_slot, g_user, g_ip);
    return 1;
}

int ha_ready(void) { return g_ready && cur()[0] && (g_access[0] || g_refresh[0]); }
const char *ha_base(void) { return cur(); }
const char *ha_alt(void) { return g_alt; }
const char *ha_base1(void) { return g_base; }
const char *ha_token(void) { return g_access; }
const char *ha_user(void) { return g_user; }
const char *ha_version(void) { return g_version; }
char *ha_base_mut(void) { return g_base; }

/* 当前走的是哪一条，用来在界面上标「内网 / 外网」 */
const char *ha_route(void)
{
    if (g_slot == 1 && g_alt[0]) return "外网";
    if (!g_base[0] && g_alt[0]) return "外网";
    return is_lan_addr(g_base) ? "内网" : "外网";
}

const char *ha_ip(void) { return g_ip; }

/* 手动指定服务器 IP：域名仍用于 TLS SNI / Host，只绕开 DNS 解析 */
void ha_set_ip(const char *ip)
{
    if (!ip) ip = "";
    while (*ip == ' ') ip++;
    g_ip[0] = '\0';
    if (ip[0]) snprintf(g_ip, sizeof g_ip, "%s", ip);
    apply_ip_override();
}

void ha_save_prefs(void) { cfg_save(); }

/* 外观：0 浅色 / 1 深色。存在配置文件里，注销登录不影响 */
int  ha_theme(void) { return g_theme; }
void ha_set_theme(int dark)
{
    g_theme = dark ? 1 : 0;
    cfg_save();
}

void ha_set_base(const char *base)
{
    char tmp[192];
    if (ha_normalize_url(base, tmp, sizeof tmp))
        snprintf(g_base, sizeof g_base, "%s", tmp);
    else
        snprintf(g_base, sizeof g_base, "%s", base ? base : "");
    g_slot = (!g_base[0] && g_alt[0]) ? 1 : 0;
    g_lan_state = -1;      /* 地址变了，内网通不通得重新判断 */
    apply_ip_override();
}

/* 地址 2（外网）。留空表示只用地址 1，不自动切换 */
void ha_set_alt(const char *base)
{
    char tmp[192];
    g_alt[0] = '\0';
    if (base && base[0]) {
        if (ha_normalize_url(base, tmp, sizeof tmp))
            snprintf(g_alt, sizeof g_alt, "%s", tmp);
        else
            snprintf(g_alt, sizeof g_alt, "%s", base);
    }
    if (!g_alt[0]) g_slot = 0;
    g_lan_state = -1;
    apply_ip_override();
}

void ha_use_slot(int slot)
{
    g_slot = (slot == 1 && g_alt[0]) ? 1 : 0;
    apply_ip_override();
}

int ha_normalize_url(const char *in, char *out, int outsz)
{
    if (!in || !out || outsz < 8) return 0;

    while (*in == ' ' || *in == '\t') in++;
    if (!in[0]) return 0;

    char buf[192];
    if (strncmp(in, "http://", 7) != 0 && strncmp(in, "https://", 8) != 0)
        snprintf(buf, sizeof buf, "http://%s", in);
    else
        snprintf(buf, sizeof buf, "%s", in);

    /* 去掉尾部斜杠 */
    size_t n = strlen(buf);
    while (n > 8 && buf[n - 1] == '/') buf[--n] = '\0';

    /* 至少要有个主机名 */
    const char *host = strstr(buf, "://");
    if (!host || !host[3]) return 0;
    host += 3;
    if (!*host || *host == '/' || *host == ':') return 0;

    snprintf(out, (size_t)outsz, "%s", buf);
    return 1;
}

/* ---------------- HTTP 封装 ---------------- */

/* 单次请求：显式指定走哪条地址 */
static int api_at(const char *base, const char *path, const char *method,
                  const char *body, NetResp *out, int timeout_s)
{
    const char *b = (base && base[0]) ? base : g_base;
    char url[512];
    snprintf(url, sizeof url, "%s%s", b, path);

    const char *hdrs[4];
    int nh = 0;
    static char auth[800];
    if (g_access[0]) {
        snprintf(auth, sizeof auth, "Authorization: Bearer %s", g_access);
        hdrs[nh++] = auth;
    }
    hdrs[nh++] = "Content-Type: application/json";
    hdrs[nh++] = "Accept: application/json";

    /* 局域网地址：1 秒还没把 TCP 连上（含 TLS 握手）就认定这条不通，
     * 立刻交给外网，别让用户干等。局域网里正常是几毫秒连通，
     * 1 秒都连不上基本就是不在这个网段 / HA 没开。
     * 另外 IP 直连不存在 DNS、h2、TLS1.3 的协商问题，4 段回退链
     * 只会在不通时白白多等 4 倍时间，所以这里关掉。
     * 总超时也压到 3 秒：局域网 HA 的 /api/ 响应是毫秒级，
     * 3 秒还没回来说明这条线没意义，先切外网再说。 */
    int lan = b[0] && is_lan_addr(b);
    int t = timeout_s;
    if (lan && t > 3) t = 3;
    return net_request_ex(url, method ? method : "GET", hdrs, nh, body, out,
                          t, lan ? 1000 : 0, lan ? 0 : 1);
}

/* 自动切换：当前链路在网络层就没通（解析不到 / TCP 连不上 / TLS 握手失败），
 * 且填了第二个地址，就换另一条再试一次。
 * 内外网是同一台 HA，令牌通用，所以切过去不用重新登录。 */
static int api(const char *path, const char *method, const char *body,
               NetResp *out, int timeout_s)
{
    /* 本次运行已经确认内网不通 → 直接走外网。
     * 这一步省掉的正是「在家以外每开一个页面都先等内网超时」的体感。 */
    int skip_lan = (g_slot == 0 && g_lan_state == 0 && g_base[0] && has_alt());
    int code = api_at(skip_lan ? g_alt : cur(), path, method, body, out, timeout_s);

    /* 这次走的如果就是内网，顺便把「内网通不通」记下来 */
    if (!skip_lan && g_slot == 0 && g_base[0]) {
        if (code == 0 && out && out->status == 0) {
            if (g_lan_state != 0) applog("ha: 内网不通，本次运行固定走外网");
            g_lan_state = 0;
        } else {
            g_lan_state = 1;
        }
    }

    /* 两条都试过了（skip_lan）还失败，就别再往回切了，纯浪费一趟超时 */
    if (code == 0 && !g_switching && has_alt() && out && out->status == 0) {
        int prev = g_slot;
        int other = prev ? 0 : 1;
        if (other == 1 ? g_alt[0] != '\0' : g_base[0] != '\0') {
            g_switching = 1;
            applog("ha: %s 走%s不通（%s），自动切到%s %s", path,
                   prev ? "外网" : "内网", out->err[0] ? out->err : "网络错误",
                   other ? "外网" : "内网", other ? g_alt : g_base);
            ha_use_slot(other);
            net_resp_free(out);
            code = api_at(cur(), path, method, body, out, timeout_s);
            if (code == 0) {
                ha_use_slot(prev);   /* 两条都不通，别来回抖 */
            } else {
                cfg_save();          /* 记住这条能用，下次启动直接用 */
            }
            g_switching = 0;
        }
    }
    return code;
}

/* 解析错误信息：HA 的错误体通常是 {"message":"..."} */
static void err_from(NetResp *r, char *msg, int msgsz, const char *fallback)
{
    if (!msg || msgsz <= 0) return;

    if (r && r->p && r->p[0]) {
        JNode *j = json_parse(r->p);
        if (j) {
            const char *m = json_str(j, "message");
            if (!m[0]) m = json_str_or(j, "error", "");
            if (m[0]) { snprintf(msg, (size_t)msgsz, "%s", m); json_free(j); return; }
            json_free(j);
        }
    }
    if (r && r->status == 401) { snprintf(msg, (size_t)msgsz, "%s", "未授权（令牌可能已失效）"); return; }
    if (r && r->status)        { snprintf(msg, (size_t)msgsz, "%s HTTP %d", fallback, r->status); return; }
    if (r && r->err[0])        { snprintf(msg, (size_t)msgsz, "%s", r->err); return; }
    snprintf(msg, (size_t)msgsz, "%s", fallback);
}

/* ---------------- 令牌 ---------------- */

static void url_encode(const char *in, char *out, int outsz)
{
    static const char *hex = "0123456789ABCDEF";
    int o = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && o + 4 < outsz; p++) {
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
            (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' || *p == '.' || *p == '~') {
            out[o++] = (char)*p;
        } else {
            out[o++] = '%';
            out[o++] = hex[*p >> 4];
            out[o++] = hex[*p & 0xF];
        }
    }
    out[o] = '\0';
}

/* POST /auth/token（form-encoded） */
static int token_request(const char *form, char *msg, int msgsz)
{
    char url[512];
    snprintf(url, sizeof url, "%s/auth/token", cur());

    const char *hdrs[] = { "Content-Type: application/x-www-form-urlencoded" };
    NetResp r;
    memset(&r, 0, sizeof r);

    int code = net_request(url, "POST", hdrs, 1, form, &r, 15);
    if (code != 200 || !r.p) {
        err_from(&r, msg, msgsz, "登录失败");
        net_resp_free(&r);
        return -1;
    }

    JNode *j = json_parse(r.p);
    if (!j) {
        snprintf(msg, (size_t)msgsz, "%s", "令牌响应无法解析");
        net_resp_free(&r);
        return -1;
    }

    const char *at = json_str(j, "access_token");
    const char *rt = json_str(j, "refresh_token");
    double exp = json_num(j, "expires_in", 1800);

    if (!at[0]) {
        snprintf(msg, (size_t)msgsz, "%s", "服务器没有返回访问令牌");
        json_free(j);
        net_resp_free(&r);
        return -1;
    }

    snprintf(g_access, sizeof g_access, "%s", at);
    if (rt[0]) snprintf(g_refresh, sizeof g_refresh, "%s", rt);
    g_expires = (long)time(NULL) + (long)exp;
    g_ready = 1;

    json_free(j);
    net_resp_free(&r);
    return 0;
}

static int token_refresh(char *msg, int msgsz)
{
    if (!g_refresh[0]) {
        snprintf(msg, (size_t)msgsz, "%s", "登录已失效，请重新登录");
        return -1;
    }

    char e_client[256], e_refresh[1024], form[1400];
    url_encode(HA_CLIENT_ID, e_client, sizeof e_client);
    url_encode(g_refresh, e_refresh, sizeof e_refresh);
    snprintf(form, sizeof form,
             "grant_type=refresh_token&refresh_token=%s&client_id=%s",
             e_refresh, e_client);

    applog("ha: refreshing token");
    if (token_request(form, msg, msgsz) != 0) {
        applog("ha: refresh failed: %s", msg);
        return -1;
    }
    cfg_save();
    applog("ha: token refreshed");
    return 0;
}

/* 到期前 60 秒就提前换新；系统时间明显不对时跳过预判，交给 401 兜底 */
static void token_maybe_refresh(void)
{
    long now = (long)time(NULL);
    if (now < 1600000000L) return;          /* 时间没同步，不做预判 */
    if (g_expires <= 0) return;
    if (now + 60 < g_expires) return;

    char msg[160] = "";
    token_refresh(msg, sizeof msg);
}

static int authed(const char *path, const char *method, const char *body,
                  NetResp *out, int timeout_s, char *msg, int msgsz)
{
    token_maybe_refresh();
    int code = api(path, method, body, out, timeout_s);

    /* access token 被服务器提前作废（重启 HA / 令牌吊销）→ 刷新一次重试 */
    if (code == 401 && g_refresh[0]) {
        applog("ha: 401 on %s, trying refresh", path);
        if (token_refresh(msg, msgsz) == 0) {
            net_resp_free(out);
            code = api(path, method, body, out, timeout_s);
        }
    }
    return code;
}

/* ---------------- 探活 ---------------- */

int ha_probe(char *msg, int msgsz)
{
    if (!cur()[0]) { snprintf(msg, (size_t)msgsz, "%s", "请先填写服务器地址"); return -1; }

    int before = g_slot;
    g_lan_state = -1;      /* 连接性检查 = 强制重探内网，别沿用上次结论 */
    NetResp r;
    memset(&r, 0, sizeof r);
    int code = api("/api/", "GET", NULL, &r, 8);

    const char *via = ha_route();
    int switched = (g_slot != before);

    int ok = 0;
    if (code == 200) {
        ok = 1;
        if (switched) snprintf(msg, (size_t)msgsz, "内网不通，已自动改走%s", via);
        else          snprintf(msg, (size_t)msgsz, "连接成功（走%s）", via);
    } else if (code == 401) {
        ok = 1;
        if (switched) snprintf(msg, (size_t)msgsz, "内网不通，已自动改走%s", via);
        else          snprintf(msg, (size_t)msgsz, "已找到 Home Assistant（走%s）", via);
    } else if (code == 0) {
        snprintf(msg, (size_t)msgsz, "连不上：%s", r.err[0] ? r.err : "网络错误");
    } else if (code == 404) {
        snprintf(msg, (size_t)msgsz, "%s", "这个地址不是 Home Assistant（404）");
    } else {
        snprintf(msg, (size_t)msgsz, "服务器返回 HTTP %d", code);
    }
    net_resp_free(&r);
    return ok ? 0 : -1;
}

/* ---------------- 连接诊断 ---------------- */

static void dadd(char *out, int outsz, int *o, const char *fmt, ...)
{
    if (*o >= outsz - 1) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(out + *o, (size_t)(outsz - *o), fmt, ap);
    va_end(ap);
    if (n > 0) *o += (n < outsz - *o) ? n : (outsz - *o - 1);
}

/* 把响应正文压成一行摘要（去掉引号/换行），最多 keep 字节 */
static void dsnip(char *dst, int dstsz, const char *src, int keep)
{
    dst[0] = '\0';
    if (!src) return;
    int o = 0;
    for (const char *p = src; *p && o < keep && o < dstsz - 1; p++) {
        unsigned char ch = (unsigned char)*p;
        if (ch == '\n' || ch == '\r' || ch == '\t') { dst[o++] = ' '; continue; }
        if (ch == '"') continue;
        if (ch < 0x20) continue;
        dst[o++] = (char)ch;
    }
    dst[o] = '\0';
}

int ha_diag(char *out, int outsz)
{
    if (!out || outsz < 128) return -1;
    out[0] = '\0';
    int o = 0;

    if (!cur()[0]) {
        dadd(out, outsz, &o, "请先填写服务器地址\n");
        return -1;
    }
    dadd(out, outsz, &o, "当前走%s：%s\n", ha_route(), cur());

    /* ⓪a 两条线路各打一次 /api/：一眼看出是内网不通还是外网不通 */
    {
        const char *cand[2] = { g_base, g_alt };
        static const char *nm[2] = { "内网", "外网" };
        int head = 0;
        for (int i = 0; i < 2; i++) {
            if (!cand[i][0]) continue;
            NetResp t;
            memset(&t, 0, sizeof t);
            int c = api_at(cand[i], "/api/", "GET", NULL, &t, 6);
            if (!head) { dadd(out, outsz, &o, "⓪ 线路\n"); head = 1; }
            if (c == 0)
                dadd(out, outsz, &o, "   %s %s\n     → 不通：%s\n", nm[i], cand[i],
                     t.err[0] ? t.err : "网络错误");
            else
                dadd(out, outsz, &o, "   %s %s\n     → HTTP %d %s\n", nm[i], cand[i], c,
                     (c == 200 || c == 401) ? "✓ 是 HA" : "（不是 HA 的响应）");
            net_resp_free(&t);
        }
        apply_ip_override();   /* 探测时 DoH 可能改了全局覆盖，恢复用户设置 */
    }

    /* ⓪b DNS：用外网地址的域名看解析（A / AAAA 分开说） */
    {
        const char *src = g_alt[0] ? g_alt : g_base;
        char host[128];
        char v4[64] = "", v6[64] = "";
        if (host_of(src, host, sizeof host)) {
            int has4 = (net_doh_query(host, 1, v4, sizeof v4) == 0);
            int has6 = (net_doh_query(host, 28, v6, sizeof v6) == 0);
            int literal = (!has4 && !has6);

            if (literal) {
                dadd(out, outsz, &o, "⓪ 解析   %s（本身就是 IP，不需要解析）\n", host);
            } else if (g_ip[0]) {
                dadd(out, outsz, &o, "⓪ 解析   %s → %s（手动指定 IP）\n", host, g_ip);
            } else if (has4 && has6) {
                dadd(out, outsz, &o, "⓪ 解析   %s → %s（IPv4） / %s（IPv6）\n", host, v4, v6);
            } else if (has4) {
                dadd(out, outsz, &o, "⓪ 解析   %s → %s（仅 IPv4）\n", host, v4);
            } else if (has6) {
                dadd(out, outsz, &o,
                     "⓪ 解析   %s → %s（★只有 IPv6，没有 A 记录）\n", host, v6);
                dadd(out, outsz, &o,
                     "   说明：Switch 的 IPv6 出网能力很差，纯 AAAA 域名基本连不上。"
                     "根治办法是让域名有 IPv4 入口——在 NAS 上跑 Cloudflare Tunnel，"
                     "或把域名托管到 Cloudflare 开橙云代理（见 README「外网访问」）。\n");
            } else {
                dadd(out, outsz, &o, "⓪ 解析   %s 查询失败（DoH 三个源都没回应）\n", host);
            }
        }
    }

    NetResp r;
    char snip[80];
    int ok_home = 0, ok_api = 0;

    /* ① 首页：能拿到 200 就说明 DNS、TCP、TLS 三段全通 */
    memset(&r, 0, sizeof r);
    int code = api("/", "GET", NULL, &r, 10);
    if (code > 0) {
        ok_home = 1;
        dadd(out, outsz, &o, "① 首页   HTTP %d  %s%s\n", code,
             r.detail, r.tries > 1 ? "（回退后连通）" : "");
    } else {
        dadd(out, outsz, &o, "① 首页   连不上：%s\n", r.err[0] ? r.err : "无响应");
        dsnip(snip, sizeof snip, r.err, 60);
    }
    net_resp_free(&r);

    /* ② /api/：HA 的探活点，正常返回 {"message":"API running."} */
    if (ok_home) {
        memset(&r, 0, sizeof r);
        code = api("/api/", "GET", NULL, &r, 10);
        if (code > 0) {
            ok_api = (code == 200);
            dsnip(snip, sizeof snip, r.p, 46);
            dadd(out, outsz, &o, "② API    HTTP %d  %s\n", code, snip);
        } else {
            dadd(out, outsz, &o, "② API    连不上：%s\n", r.err[0] ? r.err : "无响应");
        }
        net_resp_free(&r);
    } else {
        dadd(out, outsz, &o, "② API    跳过（首页都不通）\n");
    }

    /* ③ 登录流：验证 POST + JSON 能不能穿过反代 */
    if (ok_home) {
        char body[512];
        snprintf(body, sizeof body,
                 "{\"client_id\":\"%s\",\"handler\":[\"homeassistant\",null],"
                 "\"redirect_uri\":\"%s\"}", HA_CLIENT_ID, HA_CLIENT_ID);
        memset(&r, 0, sizeof r);
        code = api("/auth/login_flow", "POST", body, &r, 12);
        if (code == 200) {
            JNode *j = json_parse(r.p);
            const char *fl = j ? json_str(j, "flow_id") : "";
            dadd(out, outsz, &o, "③ 登录流 HTTP 200  %s\n",
                 fl[0] ? "拿到 flow_id，可以登录" : "返回异常（无 flow_id）");
            if (j) json_free(j);
        } else if (code > 0) {
            dsnip(snip, sizeof snip, r.p, 46);
            dadd(out, outsz, &o, "③ 登录流 HTTP %d  %s\n", code, snip);
        } else {
            dadd(out, outsz, &o, "③ 登录流 连不上：%s\n", r.err[0] ? r.err : "无响应");
        }
        net_resp_free(&r);
    }

    /* 结论 */
    if (ok_home && ok_api)
        dadd(out, outsz, &o, "\n结论：网络与协议层正常，问题在登录凭据或令牌。");
    else if (ok_home)
        dadd(out, outsz, &o, "\n结论：能连上但不是 Home Assistant，或反代把 /api/ 拦了。");
    else
        dadd(out, outsz, &o,
             "\n结论：连不上地址。DNS 解析失败检查 Switch 网络；"
             "握手失败多为反代只开 TLS1.3 或强制 h2。");

    applog("diag: %s", out);
    return o;
}

/* ---------------- 登录流 ---------------- */

static int json_escape(const char *in, char *out, int outsz)
{
    int o = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p; p++) {
        const char *add = NULL;
        if (*p == '"') add = "\\\"";
        else if (*p == '\\') add = "\\\\";
        else if (*p == '\n') add = "\\n";
        else if (*p == '\r') add = "\\r";
        else if (*p == '\t') add = "\\t";
        if (add) {
            int n = (int)strlen(add);
            if (o + n >= outsz) return -1;
            memcpy(out + o, add, (size_t)n);
            o += n;
        } else {
            if (o + 1 >= outsz) return -1;
            out[o++] = (char)*p;
        }
    }
    out[o] = '\0';
    return o;
}

int ha_login(const char *user, const char *pass, char *msg, int msgsz)
{
    if (!cur()[0]) { snprintf(msg, (size_t)msgsz, "%s", "请先填写服务器地址"); return -1; }
    if (!user || !user[0]) { snprintf(msg, (size_t)msgsz, "%s", "请填写用户名"); return -1; }

    /* 1. 开始登录流程 */
    char body[512];
    snprintf(body, sizeof body,
             "{\"client_id\":\"%s\",\"handler\":[\"homeassistant\",null],"
             "\"redirect_uri\":\"%s\"}", HA_CLIENT_ID, HA_CLIENT_ID);

    NetResp r;
    memset(&r, 0, sizeof r);
    int code = api("/auth/login_flow", "POST", body, &r, 12);
    if (code != 200 || !r.p) {
        err_from(&r, msg, msgsz, "无法开始登录");
        net_resp_free(&r);
        return -1;
    }

    JNode *j = json_parse(r.p);
    char flow[96] = "";
    if (j) {
        snprintf(flow, sizeof flow, "%s", json_str(j, "flow_id"));
        json_free(j);
    }
    net_resp_free(&r);

    if (!flow[0]) {
        snprintf(msg, (size_t)msgsz, "%s", "服务器没有返回登录流程 ID");
        return -1;
    }

    /* 2. 提交用户名密码 */
    char eu[120], ep[200];
    if (json_escape(user, eu, sizeof eu) < 0 || json_escape(pass ? pass : "", ep, sizeof ep) < 0) {
        snprintf(msg, (size_t)msgsz, "%s", "账号或密码过长");
        return -1;
    }

    char path[160];
    snprintf(path, sizeof path, "/auth/login_flow/%s", flow);
    char body2[640];
    snprintf(body2, sizeof body2,
             "{\"client_id\":\"%s\",\"username\":\"%s\",\"password\":\"%s\"}",
             HA_CLIENT_ID, eu, ep);

    memset(&r, 0, sizeof r);
    code = api(path, "POST", body2, &r, 15);
    if (code != 200 || !r.p) {
        err_from(&r, msg, msgsz, "登录失败");
        net_resp_free(&r);
        return -1;
    }

    j = json_parse(r.p);
    char auth_code[256] = "";
    const char *type = "";
    const char *step = "";
    const char *errbase = "";

    if (j) {
        type = json_str(j, "type");
        step = json_str(j, "step_id");
        const JNode *e = json_get(j, "errors");
        if (e) {
            errbase = json_str(e, "base");
            if (!errbase[0]) errbase = json_str(e, "password");
        }
        if (strcmp(type, "create_entry") == 0)
            snprintf(auth_code, sizeof auth_code, "%s", json_str(j, "result"));
    }

    int rc = -1;
    if (auth_code[0]) {
        json_free(j);
        net_resp_free(&r);

        /* 3. 授权码换令牌 */
        char e_code[600], e_client[256], form[1200];
        url_encode(auth_code, e_code, sizeof e_code);
        url_encode(HA_CLIENT_ID, e_client, sizeof e_client);
        snprintf(form, sizeof form,
                 "grant_type=authorization_code&code=%s&client_id=%s", e_code, e_client);

        if (token_request(form, msg, msgsz) == 0) {
            snprintf(g_user, sizeof g_user, "%s", user);
            cfg_save();
            snprintf(msg, (size_t)msgsz, "%s", "登录成功");
            applog("ha: login ok as %s", user);
            rc = 0;
        }
        return rc;
    }

    /* 失败分支：把服务器的话翻译成人话 */
    if (strcmp(step, "mfa") == 0 || strcmp(step, "mfa_otp") == 0) {
        snprintf(msg, (size_t)msgsz,
                 "%s", "该账号启用了两步验证：请在 HA 里为 Switch 创建一个长期访问令牌，改用令牌登录");
    } else if (strcmp(errbase, "invalid_auth") == 0) {
        snprintf(msg, (size_t)msgsz, "%s", "用户名或密码错误");
    } else if (strcmp(errbase, "too_many_attempts") == 0) {
        snprintf(msg, (size_t)msgsz, "%s", "尝试次数过多，请稍后再试");
    } else if (strcmp(type, "abort") == 0) {
        snprintf(msg, (size_t)msgsz, "%s", "服务器中止了登录流程");
    } else {
        snprintf(msg, (size_t)msgsz, "登录失败（%.40s / %.40s）", type, step);
    }

    json_free(j);
    net_resp_free(&r);
    return -1;
}

int ha_login_with_token(const char *tok, char *msg, int msgsz)
{
    if (!tok || !tok[0]) { snprintf(msg, (size_t)msgsz, "%s", "请填写令牌"); return -1; }

    snprintf(g_access, sizeof g_access, "%s", tok);
    g_refresh[0] = '\0';
    g_ready = 1;

    NetResp r;
    memset(&r, 0, sizeof r);
    int code = api("/api/", "GET", NULL, &r, 10);
    net_resp_free(&r);

    if (code != 200) {
        cfg_clear();
        snprintf(msg, (size_t)msgsz, "%s", "令牌无效（请检查是否复制完整）");
        return -1;
    }

    if (!g_user[0]) snprintf(g_user, sizeof g_user, "%s", "长期令牌");
    cfg_save();
    snprintf(msg, (size_t)msgsz, "%s", "登录成功");
    applog("ha: login with long-lived token");
    return 0;
}

/* ---------------- 实体同步 ---------------- */

/* 模板一次性拉全量：eid \t 名称 \t 区域 \t 状态 \t device_class \t 亮度 \t 开合 \t 单位
 * 用 attributes.get(...) 而不是 `or -1`——亮度 0 在 Jinja 里是 falsy，会被误判成缺失 */
static const char *TPL =
    "{\"template\":\"{% for s in states if s.domain in "
    "['light','switch','fan','cover','climate','camera','sensor','binary_sensor',"
    "'scene','script','media_player','lock','vacuum','humidifier','input_boolean',"
    "'water_heater'] %}"
    "{{ s.entity_id }}\\t{{ s.name }}\\t{{ area_name(s.entity_id) or '' }}\\t"
    "{{ s.state }}\\t{{ s.attributes.get('device_class','') }}\\t"
    "{{ s.attributes.get('brightness',-1) }}\\t"
    "{{ s.attributes.get('current_position',-1) }}\\t"
    "{{ s.attributes.get('unit_of_measurement','') }}\\n"
    "{% endfor %}\"}";

static int domain_of(const char *eid)
{
    if (!strncmp(eid, "light.", 6))          return DOM_LIGHT;
    if (!strncmp(eid, "switch.", 7))         return DOM_SWITCH;
    if (!strncmp(eid, "fan.", 4))            return DOM_FAN;
    if (!strncmp(eid, "cover.", 6))          return DOM_COVER;
    if (!strncmp(eid, "climate.", 8))        return DOM_CLIMATE;
    if (!strncmp(eid, "camera.", 7))         return DOM_CAMERA;
    if (!strncmp(eid, "binary_sensor.", 14)) return DOM_BINARY;
    if (!strncmp(eid, "sensor.", 7))         return DOM_SENSOR;
    if (!strncmp(eid, "lock.", 5))           return DOM_LOCK;
    if (!strncmp(eid, "vacuum.", 7))         return DOM_VACUUM;
    if (!strncmp(eid, "media_player.", 13))  return DOM_MEDIA;
    if (!strncmp(eid, "humidifier.", 11))    return DOM_HUMIDIFIER;
    if (!strncmp(eid, "scene.", 6))          return DOM_SCENE;
    if (!strncmp(eid, "script.", 7))         return DOM_SCRIPT;
    if (!strncmp(eid, "input_boolean.", 14)) return DOM_INPUT_BOOL;
    if (!strncmp(eid, "water_heater.", 13))  return DOM_WATER_HEATER;
    return DOM_OTHER;
}

/* 状态 → 开/关。返回 -1 表示这类实体没有开关语义 */
static int state_on(const char *dom, const char *st)
{
    if (!st[0]) return -1;
    if (strcmp(st, "unavailable") == 0 || strcmp(st, "unknown") == 0) return -1;

    /* 传感器用状态文本本身展示，没有开关态 */
    if (!strcmp(dom, "sensor") || !strcmp(dom, "binary_sensor")) return -1;

    if (!strcmp(st, "on") || !strcmp(st, "open") || !strcmp(st, "opening") ||
        !strcmp(st, "playing") || !strcmp(st, "cleaning") || !strcmp(st, "unlocked") ||
        !strcmp(st, "heat") || !strcmp(st, "cool") || !strcmp(st, "heat_cool") ||
        !strcmp(st, "auto") || !strcmp(st, "dry") || !strcmp(st, "fan_only") ||
        !strcmp(st, "active") || !strcmp(st, "home") || !strcmp(st, "detected"))
        return 1;

    if (!strcmp(st, "off") || !strcmp(st, "closed") || !strcmp(st, "closing") ||
        !strcmp(st, "idle") || !strcmp(st, "standby") || !strcmp(st, "locked") ||
        !strcmp(st, "docked") || !strcmp(st, "paused"))
        return 0;

    return -1;
}

static int area_index(const char *name)
{
    const char *n = (name && name[0]) ? name : "未分配";
    for (int i = 0; i < g_narea; i++)
        if (strcmp(g_area[i].name, n) == 0) return i;

    if (g_narea >= HA_MAX_AREA) return -1;
    int i = g_narea++;
    snprintf(g_area[i].name, sizeof g_area[i].name, "%s", n);
    g_area[i].nent = 0;
    return i;
}

/* 很多 HA 实例压根没在「设置 → 区域」里建地区，模板只会返回「未分配」一组，
 * 侧栏看起来就是「房间没有显示」。这时按设备类别兜底分组。 */
static const char *dom_group_name(int dom)
{
    switch (dom) {
    case DOM_LIGHT:                        return "灯光";
    case DOM_SWITCH: case DOM_INPUT_BOOL:  return "开关插座";
    case DOM_FAN: case DOM_HUMIDIFIER:
    case DOM_WATER_HEATER:                 return "风扇电器";
    case DOM_COVER:                        return "窗帘";
    case DOM_CLIMATE:                      return "空调";
    case DOM_CAMERA:                       return "摄像头";
    case DOM_SENSOR: case DOM_BINARY:      return "传感器";
    case DOM_LOCK:                         return "门锁";
    case DOM_VACUUM:                       return "扫地机";
    case DOM_MEDIA:                        return "媒体";
    default:                               return "其它设备";
    }
}

static void ent_from_row(char **f, int nf)
{
    const char *eid = f[0];
    if (!eid[0]) return;

    int dom = domain_of(eid);
    const char *name  = (nf > 1 && f[1][0]) ? f[1] : eid;
    const char *area  = (nf > 2) ? f[2] : "";
    const char *st    = (nf > 3) ? f[3] : "";
    const char *dcls  = (nf > 4) ? f[4] : "";
    int bright        = (nf > 5 && f[5][0]) ? atoi(f[5]) : -1;
    int pos           = (nf > 6 && f[6][0]) ? atoi(f[6]) : -1;
    const char *unit  = (nf > 7) ? f[7] : "";

    /* 场景/脚本单独收进「自动化」，不占设备位 */
    if (dom == DOM_SCENE || dom == DOM_SCRIPT) {
        if (g_nscene < HA_MAX_SCENE) {
            HaEnt *s = &g_scene[g_nscene++];
            memset(s, 0, sizeof *s);
            snprintf(s->eid, sizeof s->eid, "%s", eid);
            snprintf(s->name, sizeof s->name, "%s", name);
            snprintf(s->area, sizeof s->area, "%s", area);
            s->dom = dom;
            s->on = -1;
            s->level = -1;
            s->online = 1;
        } else {
            g_drop_scene++;
        }
        return;
    }

    if (g_nent >= HA_MAX_ENT) { g_drop_ent++; return; }

    HaEnt *e = &g_ent[g_nent++];
    memset(e, 0, sizeof *e);
    snprintf(e->eid, sizeof e->eid, "%s", eid);
    snprintf(e->name, sizeof e->name, "%s", name);
    snprintf(e->area, sizeof e->area, "%s", area);
    snprintf(e->state, sizeof e->state, "%s", st);
    snprintf(e->unit, sizeof e->unit, "%s", unit);
    snprintf(e->dev_class, sizeof e->dev_class, "%s", dcls);

    e->dom = dom;
    e->online = (strcmp(st, "unavailable") == 0) ? 0 : 1;
    e->on = state_on(eid, st);
    e->level = (bright >= 0) ? (bright * 100 + 127) / 255
             : (pos >= 0)    ? pos
             : -1;

    int ai = area_index(area);
    if (ai >= 0) g_area[ai].nent++;
}

static void parse_template(const char *txt)
{
    const char *p = txt;
    while (p && *p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);

        if (len > 0 && len < 900) {
            char line[900];
            memcpy(line, p, len);
            line[len] = '\0';

            /* 去掉行首空白 */
            char *s = line;
            while (*s == ' ' || *s == '\t' || *s == '\r') s++;

            if (*s) {
                char *f[8];
                int nf = 0;
                f[nf++] = s;
                for (char *q = s; *q && nf < 8; q++) {
                    if (*q == '\t') { *q = '\0'; f[nf++] = q + 1; }
                }
                if (nf >= 4) ent_from_row(f, nf);
            }
        }

        if (!nl) break;
        p = nl + 1;
    }
}

/* 兜底：/api/states（没有区域信息） */
static void parse_states(const char *txt)
{
    JNode *root = json_parse(txt);
    if (!root || root->t != J_ARR) { json_free(root); return; }

    for (const JNode *it = root->child; it; it = it->next) {
        const char *eid = json_str(it, "entity_id");
        if (!eid[0]) continue;

        const JNode *attrs = json_get(it, "attributes");
        const char *name = attrs ? json_str(attrs, "friendly_name") : "";
        const char *st   = json_str(it, "state");
        const char *dcls = attrs ? json_str(attrs, "device_class") : "";
        const char *unit = attrs ? json_str(attrs, "unit_of_measurement") : "";
        int bright = attrs ? (int)json_num(attrs, "brightness", -1) : -1;
        int pos    = attrs ? (int)json_num(attrs, "current_position", -1) : -1;

        char bs[16], ps[16], brs[16];
        snprintf(bs, sizeof bs, "%d", bright);
        snprintf(ps, sizeof ps, "%d", pos);
        snprintf(brs, sizeof brs, "%d", bright);

        char *f[8];
        f[0] = (char *)eid;
        f[1] = (char *)name;
        f[2] = (char *)"";
        f[3] = (char *)st;
        f[4] = (char *)dcls;
        f[5] = bs;
        f[6] = ps;
        f[7] = (char *)unit;
        (void)brs;

        if (domain_of(eid) == DOM_OTHER) continue;
        ent_from_row(f, 8);
    }
    json_free(root);
}

/* ---------------- 内容签名 ----------------
 * 判断「这次拉回来的和上次是不是一模一样」：一样就不重建界面、不写 SD 卡，
 * 只把「刚刚更新」的时间往前推。设备增减是低频事件，每次同步都做全量重排
 * 加落盘等于白折腾。签名覆盖所有会被画到屏上的字段。 */
static int g_last_changed;

static unsigned sig_mix(unsigned h, const char *s)
{
    if (!s) s = "";
    for (; *s; s++) {
        h ^= (unsigned char)*s;
        h *= 16777619u;
    }
    h ^= 0xffu;             /* 字段分隔：避免 "ab"+"c" 与 "a"+"bc" 撞车 */
    h *= 16777619u;
    return h;
}

static unsigned sig_mix_int(unsigned h, int v)
{
    char b[16];
    snprintf(b, sizeof b, "%d", v);
    return sig_mix(h, b);
}

static unsigned content_sig(void)
{
    unsigned h = 2166136261u;

    h = sig_mix_int(h, g_narea);
    for (int i = 0; i < g_narea; i++) {
        h = sig_mix(h, g_area[i].name);
        h = sig_mix_int(h, g_area[i].nent);
    }

    h = sig_mix_int(h, g_nent);
    for (int i = 0; i < g_nent; i++) {
        const HaEnt *e = &g_ent[i];
        h = sig_mix(h, e->eid);
        h = sig_mix(h, e->name);
        h = sig_mix(h, e->area);
        h = sig_mix(h, e->state);
        h = sig_mix(h, e->unit);
        h = sig_mix(h, e->dev_class);
        h = sig_mix_int(h, e->dom);
        h = sig_mix_int(h, e->on);
        h = sig_mix_int(h, e->level);
        h = sig_mix_int(h, e->online);
    }

    h = sig_mix_int(h, g_nscene);
    for (int i = 0; i < g_nscene; i++) {
        h = sig_mix(h, g_scene[i].eid);
        h = sig_mix(h, g_scene[i].name);
        h = sig_mix(h, g_scene[i].area);
    }
    return h;
}

/* 上一次 ha_sync 拉回来的内容是否和之前不同（1 = 有变化，界面该重建） */
int ha_last_changed(void) { return g_last_changed; }

int ha_sync(char *msg, int msgsz)
{
    /* 失败时不要把屏上已有的列表清空：先记下旧计数，拉不到就恢复。
     * 纯网络失败时实体数组本身没被改写，恢复计数即可原样回到旧数据；
     * 否则启动同步一失败，界面就变成「这里还没有实体」。 */
    int old_ent = g_nent, old_area = g_narea, old_scene = g_nscene;

    /* 清零之前先记一份内容签名，拉回来再比：一样就不重建界面、不写卡 */
    unsigned before = content_sig();
    g_last_changed = 0;

    g_nent = g_narea = g_nscene = 0;
    g_drop_ent = g_drop_scene = 0;
    if (msg && msgsz) msg[0] = '\0';

    if (!ha_ready()) {
        snprintf(msg, (size_t)msgsz, "%s", "尚未登录");
        goto fail;
    }

    token_maybe_refresh();

    NetResp r;
    memset(&r, 0, sizeof r);
    int code = authed("/api/template", "POST", TPL, &r, 20, msg, msgsz);

    if (code == 200 && r.p && r.len > 0) {
        parse_template(r.p);
    }
    net_resp_free(&r);

    if (g_nent == 0) {
        memset(&r, 0, sizeof r);
        code = authed("/api/states", "GET", NULL, &r, 25, msg, msgsz);
        if (code != 200) {
            err_from(&r, msg, msgsz, "读取实体失败");
            net_resp_free(&r);
            goto fail;
        }
        parse_states(r.p);
        net_resp_free(&r);
    }

    /* 没在 HA 里配置地区时，按设备类别分组，保证侧栏和筛选仍有内容 */
    if (g_nent > 0 &&
        (g_narea == 0 ||
         (g_narea == 1 && strcmp(g_area[0].name, "未分配") == 0))) {
        g_narea = 0;
        for (int i = 0; i < g_nent; i++) {
            const char *grp = dom_group_name(g_ent[i].dom);
            snprintf(g_ent[i].area, sizeof g_ent[i].area, "%s", grp);
            int ai = area_index(grp);
            if (ai >= 0) g_area[ai].nent++;
        }
        applog("ha: no HA areas, grouped by domain -> %d groups", g_narea);
    }

    /* 区域按实体数降序（「未分配」永远放最后） */
    for (int i = 0; i < g_narea; i++)
        for (int j = i + 1; j < g_narea; j++) {
            int pi = !strcmp(g_area[i].name, "未分配");
            int pj = !strcmp(g_area[j].name, "未分配");
            if ((g_area[j].nent > g_area[i].nent && !pj) || (pi && !pj)) {
                HaArea t = g_area[i];
                g_area[i] = g_area[j];
                g_area[j] = t;
            }
        }

    /* 内容没变就什么都不做：不写 SD 卡，也不让调用方重建列表。
     * 界面上的「刚刚更新」由调用方按 sync_ms 自己推进。 */
    g_last_changed = (content_sig() != before);
    if (g_last_changed) cache_save(0);

    applog("ha: sync ent=%d area=%d scene=%d drop=%d/%d changed=%d",
           g_nent, g_narea, g_nscene, g_drop_ent, g_drop_scene, g_last_changed);
    if (msg && msgsz) {
        /* 达到内置容量上限时明确说出来：实体特别多的用户不至于
         * 以为「同步完了就这些」，实际是超出部分被丢弃了 */
        const char *cap = "";
        if (g_drop_ent && g_drop_scene)   cap = "（设备和场景已达上限）";
        else if (g_drop_ent)              cap = "（设备已达上限，超出未显示）";
        else if (g_drop_scene)            cap = "（场景已达上限，超出未显示）";
        snprintf(msg, (size_t)msgsz, "%d 个设备 · %d 个区域%s",
                 g_nent, g_narea, cap);
    }
    return g_nent;

fail:
    if (old_ent > 0) {
        g_nent = old_ent;
        g_narea = old_area;
        g_nscene = old_scene;
    }
    return -1;
}

/* ---------------- 状态缓存 ----------------
 * 已登录时重开应用，不该先卡在「正在同步」的遮罩上等网络：先把上次的
 * 实体列表画出来，后台再静默同步，有变化才刷新。缓存只是界面快照，
 * 所有控制动作仍以服务器返回为准；同步失败时保留旧快照继续可看。
 * 文件格式（TAB 分隔，\t \\ \n 转义）：
 *   T <unix时间>                    缓存写入时刻，用来显示「N 分钟前」
 *   A <地区名> <实体数>
 *   E <实体 11 个字段> / S <场景 11 个字段> */
static long g_cache_age = -1;
static time_t g_cache_saved_at;

static void esc_write(FILE *f, const char *s)
{
    for (; s && *s; s++) {
        if (*s == '\\')       fputs("\\\\", f);
        else if (*s == '\t')  fputs("\\t", f);
        else if (*s == '\n')  fputs("\\n", f);
        else if (*s == '\r')  continue;
        else                  fputc(*s, f);
    }
}

static void esc_read(char *dst, size_t dsz, const char *src)
{
    size_t o = 0;
    while (src && *src && o + 1 < dsz) {
        if (*src == '\\' && src[1]) {
            src++;
            if (*src == 't')      dst[o++] = '\t';
            else if (*src == 'n') dst[o++] = '\n';
            else                  dst[o++] = *src;
            src++;
        } else {
            dst[o++] = *src++;
        }
    }
    dst[o] = '\0';
}

static int split_tab(char *s, char **out, int max)
{
    int n = 1;
    out[0] = s;
    while (*s) {
        if (*s == '\t') {
            if (n >= max) break;
            *s = '\0';
            out[n++] = s + 1;
        }
        s++;
    }
    return n;
}

static void ent_dump(FILE *f, char tag, const HaEnt *e)
{
    fputc(tag, f);
    fputc('\t', f);
    esc_write(f, e->eid);   fputc('\t', f);
    esc_write(f, e->name);  fputc('\t', f);
    esc_write(f, e->area);  fputc('\t', f);
    esc_write(f, e->state); fputc('\t', f);
    esc_write(f, e->unit);  fputc('\t', f);
    esc_write(f, e->dev_class);
    fprintf(f, "\t%d\t%d\t%d\t%d\n", e->dom, e->on, e->level, e->online);
}

static void cache_save(int force)
{
    if (g_nent <= 0) return;
    /* 虽然已经取消定时刷新，但连按 X 反复同步也没必要每次都写 100KB：
     * 节流到 2 分钟，退出时再强制写一次（force=1）保证下次启动是最新状态 */
    time_t now = time(NULL);
    if (!force && g_cache_saved_at && now - g_cache_saved_at < 120) return;
    g_cache_saved_at = now;

    char tmp[256];
    snprintf(tmp, sizeof tmp, "%s.tmp", HA_CACHE);
    FILE *f = fopen(tmp, "wb");
    if (!f) { applog("cache: cannot write %s", tmp); return; }

    fprintf(f, "T\t%ld\n", (long)now);
    for (int i = 0; i < g_narea; i++) {
        fputc('A', f); fputc('\t', f);
        esc_write(f, g_area[i].name);
        fprintf(f, "\t%d\n", g_area[i].nent);
    }
    for (int i = 0; i < g_nent; i++)   ent_dump(f, 'E', &g_ent[i]);
    for (int i = 0; i < g_nscene; i++) ent_dump(f, 'S', &g_scene[i]);

    fflush(f);
#ifdef __SWITCH__
    fsync(fileno(f));
#endif
    fclose(f);

    if (rename(tmp, HA_CACHE) != 0) {
        remove(HA_CACHE);
        if (rename(tmp, HA_CACHE) != 0) { applog("cache: rename failed"); return; }
    }
#ifdef __SWITCH__
    fsdevCommitDevice("sdmc");
#endif
    g_cache_age = 0;
    applog("cache: saved ent=%d area=%d scene=%d", g_nent, g_narea, g_nscene);
}

void ha_save_cache(void) { cache_save(1); }

int ha_load_cache(void)
{
    FILE *f = fopen(HA_CACHE, "rb");
    if (!f) return 0;

    g_nent = g_narea = g_nscene = 0;
    g_cache_age = -1;

    long ts = 0;
    char line[768];
    while (fgets(line, sizeof line, f)) {
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
        if (!line[0]) continue;

        char *fl[12];
        int nf = split_tab(line, fl, 12);
        if (fl[0][0] == 'T' && !fl[0][1]) {
            if (nf >= 2) ts = atol(fl[1]);
        } else if (fl[0][0] == 'A' && nf >= 3) {
            if (g_narea >= HA_MAX_AREA) continue;
            esc_read(g_area[g_narea].name, sizeof g_area[0].name, fl[1]);
            g_area[g_narea].nent = atoi(fl[2]);
            g_narea++;
        } else if ((fl[0][0] == 'E' || fl[0][0] == 'S') && nf >= 11) {
            int scene = (fl[0][0] == 'S');
            HaEnt *e;
            if (scene) {
                if (g_nscene >= HA_MAX_SCENE) continue;
                e = &g_scene[g_nscene++];
            } else {
                if (g_nent >= HA_MAX_ENT) continue;
                e = &g_ent[g_nent++];
            }
            esc_read(e->eid, sizeof e->eid, fl[1]);
            esc_read(e->name, sizeof e->name, fl[2]);
            esc_read(e->area, sizeof e->area, fl[3]);
            esc_read(e->state, sizeof e->state, fl[4]);
            esc_read(e->unit, sizeof e->unit, fl[5]);
            esc_read(e->dev_class, sizeof e->dev_class, fl[6]);
            e->dom    = atoi(fl[7]);
            e->on     = atoi(fl[8]);
            e->level  = atoi(fl[9]);
            e->online = atoi(fl[10]);
        }
    }
    fclose(f);

    if (g_nent <= 0) { g_narea = g_nscene = 0; return 0; }
    if (ts > 0) {
        long age = (long)(time(NULL) - ts);
        g_cache_age = age > 0 ? age : 0;
    }
    applog("cache: loaded ent=%d area=%d scene=%d age=%ld", g_nent, g_narea, g_nscene, g_cache_age);
    return g_nent;
}

long ha_cache_age(void) { return g_cache_age; }

int ha_nent(void) { return g_nent; }
const HaEnt *ha_ent(int i) { return (i >= 0 && i < g_nent) ? &g_ent[i] : NULL; }
int ha_narea(void) { return g_narea; }
const HaArea *ha_area(int i) { return (i >= 0 && i < g_narea) ? &g_area[i] : NULL; }
int ha_nscene(void) { return g_nscene; }
const HaEnt *ha_scene(int i) { return (i >= 0 && i < g_nscene) ? &g_scene[i] : NULL; }

int ha_ent_area_count(int ai)
{
    return (ai >= 0 && ai < g_narea) ? g_area[ai].nent : 0;
}

/* ---------------- 控制 ---------------- */

static int call_service(const char *domain, const char *service,
                        const char *eid, const char *extra,
                        char *msg, int msgsz)
{
    char path[192], body[480];
    snprintf(path, sizeof path, "/api/services/%s/%s", domain, service);
    snprintf(body, sizeof body, "{\"entity_id\":\"%s\"%s%s}",
             eid, (extra && extra[0]) ? "," : "", extra ? extra : "");

    NetResp r;
    memset(&r, 0, sizeof r);
    int code = authed(path, "POST", body, &r, 15, msg, msgsz);
    net_resp_free(&r);

    if (code < 200 || code >= 300) {
        if (!msg[0]) err_from(NULL, msg, msgsz, "控制失败");
        return -1;
    }
    return 0;
}

static void dom_name(int dom, char *out, int sz)
{
    const char *n = "switch";
    switch (dom) {
    case DOM_LIGHT: n = "light"; break;
    case DOM_SWITCH: n = "switch"; break;
    case DOM_FAN: n = "fan"; break;
    case DOM_COVER: n = "cover"; break;
    case DOM_CLIMATE: n = "climate"; break;
    case DOM_LOCK: n = "lock"; break;
    case DOM_VACUUM: n = "vacuum"; break;
    case DOM_MEDIA: n = "media_player"; break;
    case DOM_HUMIDIFIER: n = "humidifier"; break;
    case DOM_INPUT_BOOL: n = "input_boolean"; break;
    case DOM_WATER_HEATER: n = "water_heater"; break;
    case DOM_SCENE: n = "scene"; break;
    case DOM_SCRIPT: n = "script"; break;
    default: break;
    }
    snprintf(out, (size_t)sz, "%s", n);
}

int ha_toggle(int idx, char *msg, int msgsz)
{
    if (msg && msgsz) msg[0] = '\0';
    const HaEnt *e = ha_ent(idx);
    if (!e) return -1;

    char dom[24];
    dom_name(e->dom, dom, sizeof dom);

    int rc;
    if (e->dom == DOM_COVER) {
        rc = call_service("cover", e->on == 1 ? "close_cover" : "open_cover",
                          e->eid, NULL, msg, msgsz);
    } else if (e->dom == DOM_CLIMATE || e->dom == DOM_WATER_HEATER) {
        rc = call_service(dom, e->on == 1 ? "turn_off" : "turn_on",
                          e->eid, NULL, msg, msgsz);
    } else if (e->dom == DOM_LOCK) {
        rc = call_service("lock", e->on == 1 ? "unlock" : "lock",
                          e->eid, NULL, msg, msgsz);
    } else if (e->dom == DOM_VACUUM) {
        rc = call_service("vacuum", e->on == 1 ? "return_to_base" : "start",
                          e->eid, NULL, msg, msgsz);
    } else if (e->dom == DOM_SCENE || e->dom == DOM_SCRIPT) {
        rc = call_service(dom, "turn_on", e->eid, NULL, msg, msgsz);
    } else if (e->dom == DOM_SENSOR || e->dom == DOM_BINARY || e->dom == DOM_CAMERA) {
        snprintf(msg, (size_t)msgsz, "%s", "该实体不支持开关");
        return -1;
    } else {
        rc = call_service(dom, "toggle", e->eid, NULL, msg, msgsz);
    }

    if (rc != 0) return -1;

    /* 本地先行更新，界面立即有反馈；下次同步以服务器为准 */
    g_ent[idx].on = (g_ent[idx].on == 1) ? 0 : 1;
    snprintf(g_ent[idx].state, sizeof g_ent[idx].state, "%s",
             g_ent[idx].on == 1 ? "on" : "off");
    return 0;
}

int ha_set_level(int idx, int v, char *msg, int msgsz)
{
    if (msg && msgsz) msg[0] = '\0';
    const HaEnt *e = ha_ent(idx);
    if (!e) return -1;

    if (v < 0) v = 0;
    if (v > 100) v = 100;

    char extra[64];
    int rc;

    if (e->dom == DOM_LIGHT) {
        int b = (v * 255 + 50) / 100;
        if (b < 1) b = 1;
        snprintf(extra, sizeof extra, "\"brightness\":%d", b);
        rc = call_service("light", "turn_on", e->eid, extra, msg, msgsz);
    } else if (e->dom == DOM_FAN) {
        rc = call_service("fan", "set_percentage", e->eid,
                          (snprintf(extra, sizeof extra, "\"percentage\":%d", v), extra),
                          msg, msgsz);
    } else {
        snprintf(msg, (size_t)msgsz, "%s", "该实体不支持调节");
        return -1;
    }

    if (rc != 0) return -1;
    g_ent[idx].level = v;
    g_ent[idx].on = 1;
    snprintf(g_ent[idx].state, sizeof g_ent[idx].state, "%s", "on");
    return 0;
}

int ha_set_position(int idx, int v, char *msg, int msgsz)
{
    if (msg && msgsz) msg[0] = '\0';
    const HaEnt *e = ha_ent(idx);
    if (!e || e->dom != DOM_COVER) {
        snprintf(msg, (size_t)msgsz, "%s", "该实体不支持开合调节");
        return -1;
    }
    if (v < 0) v = 0;
    if (v > 100) v = 100;

    char extra[48];
    snprintf(extra, sizeof extra, "\"position\":%d", v);
    if (call_service("cover", "set_cover_position", e->eid, extra, msg, msgsz) != 0) return -1;

    g_ent[idx].level = v;
    return 0;
}

int ha_run_entity(const char *eid, char *msg, int msgsz)
{
    if (!eid || !eid[0]) return -1;
    int dom = domain_of(eid);
    char dom_s[24];
    dom_name(dom == DOM_SCRIPT ? DOM_SCRIPT : DOM_SCENE, dom_s, sizeof dom_s);

    int rc = call_service(dom_s, "turn_on", eid, NULL, msg, msgsz);
    if (rc == 0) snprintf(msg, (size_t)msgsz, "%s", "已执行");
    return rc;
}

/* ---------------- 摄像头 ---------------- */

int ha_cam_url(const char *eid, char *url, int sz)
{
    if (!cur()[0] || !eid || !eid[0]) return -1;
    snprintf(url, (size_t)sz, "%s/api/camera_proxy/%s", cur(), eid);
    return 0;
}
