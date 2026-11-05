#include "net.h"
#include "json.h"
#include "applog.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NET_MAX_BODY (2 * 1024 * 1024)

static int g_inited;

/* 用户/自动解析得到的「主机名 → IP」覆盖表（只维护一条，够用） */
static char g_ovr_host[128];
static char g_ovr_ip[64];

int net_init(void)
{
    if (g_inited) return 0;
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) {
        applog("net: curl_global_init failed");
        return -1;
    }
    g_inited = 1;
    return 0;
}

void net_done(void)
{
    if (g_inited) { curl_global_cleanup(); g_inited = 0; }
}

static size_t write_cb(char *ptr, size_t sz, size_t n, void *ud)
{
    NetResp *b = (NetResp *)ud;
    int add = (int)(sz * n);
    if (add <= 0) return 0;
    if (b->len + add > NET_MAX_BODY) return 0;   /* 超限直接中断 */

    if (b->len + add + 1 > b->cap) {
        int nc = b->cap ? b->cap : 8192;
        while (nc < b->len + add + 1) nc *= 2;
        char *np = (char *)realloc(b->p, (size_t)nc);
        if (!np) return 0;
        b->p = np;
        b->cap = nc;
    }
    memcpy(b->p + b->len, ptr, (size_t)add);
    b->len += add;
    b->p[b->len] = '\0';
    return (size_t)add;
}

void net_resp_free(NetResp *r)
{
    if (!r) return;
    free(r->p);
    r->p = NULL;
    r->len = r->cap = 0;
}

/* ---------------- 主机名工具 ---------------- */

static int url_host(const char *url, char *host, int hostsz, int *port)
{
    const char *p = strstr(url, "://");
    if (!p) return 0;
    int https = (p - url == 5);
    p += 3;

    const char *e = p;
    while (*e && *e != '/' && *e != '?' && *e != '#') e++;
    const char *colon = NULL;
    for (const char *q = p; q < e; q++) if (*q == ':') colon = q;

    int n = (int)((colon ? colon : e) - p);
    if (n <= 0 || n >= hostsz) return 0;
    memcpy(host, p, (size_t)n);
    host[n] = '\0';
    *port = colon ? atoi(colon + 1) : (https ? 443 : 80);
    return 1;
}

static int is_ipv6(const char *s)
{
    int colons = 0;
    if (!s || !strchr(s, ':')) return 0;
    for (const char *p = s; *p; p++) {
        char c = *p;
        if (c == ':') { colons++; continue; }
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
            (c >= 'A' && c <= 'F') || c == '.') continue;
        return 0;
    }
    return colons >= 2;
}

static int is_ipv4(const char *s)
{
    int dots = 0, seg = 0, val = 0;
    for (const char *p = s; *p; p++) {
        if (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); seg++; if (val > 255 || seg > 3) return 0; }
        else if (*p == '.') { if (!seg) return 0; dots++; seg = 0; val = 0; if (dots > 3) return 0; }
        else return 0;
    }
    return dots == 3 && seg > 0;
}

void net_set_host_ip(const char *host, const char *ip)
{
    g_ovr_host[0] = '\0';
    g_ovr_ip[0] = '\0';
    if (!host || !host[0] || !ip || !ip[0]) return;
    snprintf(g_ovr_host, sizeof g_ovr_host, "%s", host);
    snprintf(g_ovr_ip, sizeof g_ovr_ip, "%s", ip);
}

/* ---------------- 单次请求 ---------------- */

typedef struct {
    int force_v4;      /* 只走 IPv4 */
    int force_http11;  /* 只用 HTTP/1.1（不协商 h2） */
    int cap_tls12;     /* TLS 最高 1.2，不试 1.3 套件 */
} NetOpt;

static CURLcode perform(const char *url, const char *method,
                        const char **headers, int nhdr,
                        const char *body, NetResp *out,
                        int timeout_s, int conn_ms, const NetOpt *o)
{
    CURL *c = curl_easy_init();
    if (!c) return CURLE_FAILED_INIT;

    struct curl_slist *hs = NULL;
    for (int i = 0; i < nhdr; i++) hs = curl_slist_append(hs, headers[i]);

    /* 有 IP 覆盖就绕开系统 DNS：域名只用来做 TLS SNI 和 Host 头 */
    struct curl_slist *res = NULL;
    {
        char h[128];
        int p = 0;
        if (url_host(url, h, sizeof h, &p) && g_ovr_ip[0] && strcmp(h, g_ovr_host) == 0) {
            char line[224];
            if (strchr(g_ovr_ip, ':'))
                snprintf(line, sizeof line, "%s:%d:[%s]", h, p, g_ovr_ip);
            else
                snprintf(line, sizeof line, "%s:%d:%s", h, p, g_ovr_ip);
            res = curl_slist_append(NULL, line);
            if (res) curl_easy_setopt(c, CURLOPT_RESOLVE, res);
        }
    }

    curl_easy_setopt(c, CURLOPT_URL, url);
    if (hs) curl_easy_setopt(c, CURLOPT_HTTPHEADER, hs);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, out);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 3L);
    if (conn_ms <= 0) conn_ms = (timeout_s > 5 ? 5 : timeout_s) * 1000;
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, (long)conn_ms);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, (long)timeout_s);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    /* 局域网里多数是自签证书（https://homeassistant.local:8123），
     * 外网反代的证书链在 Switch 上也没地方校验，统一跳过 */
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "switch-ha/1.0");
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");

    int ovr_v6 = (g_ovr_ip[0] && strchr(g_ovr_ip, ':'));

    /* 回退策略开关 */
    if (o && o->force_v4 && !ovr_v6)
        curl_easy_setopt(c, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    if (o && o->force_http11)
        curl_easy_setopt(c, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
#ifdef CURL_SSLVERSION_TLSv1_2
    if (o && o->cap_tls12)
        curl_easy_setopt(c, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
#endif

    if (body) {
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, body);
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
    }
    if (method && strcmp(method, "POST") == 0) {
        curl_easy_setopt(c, CURLOPT_POST, 1L);
    } else if (method && strcmp(method, "GET") != 0) {
        curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, method);
    }

    CURLcode rc = curl_easy_perform(c);

    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    if (out) {
        out->status = (int)code;
        out->detail[0] = '\0';

        const char *hv = "";
#if LIBCURL_VERSION_NUM >= 0x073200
        long v = 0;
        if (curl_easy_getinfo(c, CURLINFO_HTTP_VERSION, &v) == CURLE_OK) {
            if (v == CURL_HTTP_VERSION_1_0) hv = "HTTP/1.0";
            else if (v == CURL_HTTP_VERSION_1_1) hv = "HTTP/1.1";
            else if (v == CURL_HTTP_VERSION_2_0) hv = "HTTP/2";
        }
#endif
        char *ip = NULL;
        curl_easy_getinfo(c, CURLINFO_PRIMARY_IP, &ip);
        snprintf(out->detail, sizeof out->detail, "%s%s%s",
                 hv[0] ? hv : "—", (ip && ip[0]) ? " · " : "", (ip && ip[0]) ? ip : "");
    }

    curl_easy_cleanup(c);
    if (hs) curl_slist_free_all(hs);
    if (res) curl_slist_free_all(res);
    return rc;
}

/* ---------------- DoH 解析 ---------------- */

int net_doh_query(const char *host, int qtype, char *ip, int ipsz)
{
    if (!host || !host[0] || !ip || ipsz < 8) return -1;
    ip[0] = '\0';

    static const char *eps[] = {
        "https://223.5.5.5/resolve?type=%s&name=%s",     /* AliDNS */
        "https://1.1.1.1/dns-query?type=%s&name=%s",     /* Cloudflare */
        "https://1.12.12.12/resolve?type=%s&name=%s",    /* DNSPod */
    };
    const char *qt = (qtype == 28) ? "AAAA" : "A";

    const char *hdr[] = { "Accept: application/dns-json" };

    for (int i = 0; i < 3; i++) {
        char url[256];
        snprintf(url, sizeof url, eps[i], qt, host);

        NetResp r;
        memset(&r, 0, sizeof r);
        CURLcode rc = perform(url, "GET", hdr, 1, NULL, &r, 6, 0, NULL);
        if (rc != CURLE_OK || r.status != 200 || !r.p) {
            applog("doh: %s -> %s", url, rc == CURLE_OK ? "非 200" : curl_easy_strerror(rc));
            net_resp_free(&r);
            continue;
        }

        JNode *j = json_parse(r.p);
        int got = 0;
        if (j) {
            const JNode *ans = json_get(j, "Answer");
            int n = ans ? json_len(ans) : 0;
            for (int k = 0; k < n && !got; k++) {
                const JNode *it = json_at(ans, k);
                if (!it) continue;
                if ((int)json_num(it, "type", 0) != qtype) continue;
                const char *d = json_str(it, "data");
                if (!d || !d[0]) continue;
                if (qtype == 1 && !is_ipv4(d)) continue;
                if (qtype == 28 && !is_ipv6(d)) continue;
                snprintf(ip, (size_t)ipsz, "%s", d);
                got = 1;
            }
            json_free(j);
        }
        if (!got) {
            /* 把响应体前几十字节记下来，方便事后判断是 NODATA 还是解析不了 */
            char snip[96];
            int o = 0;
            for (const char *q = r.p; *q && o < 90 && o < (int)sizeof snip - 1; q++)
                if ((unsigned char)*q >= 0x20) snip[o++] = *q;
            snip[o] = '\0';
            applog("doh: %s 无 %s 记录 %s", host, qt, snip);
        }
        net_resp_free(&r);
        if (got) { applog("doh: %s -> %s (%s)", host, ip, qt); return 0; }
    }
    return -1;
}

int net_doh_lookup(const char *host, char *ip, int ipsz)
{
    if (!host || !host[0] || !ip || ipsz < 8) return -1;
    ip[0] = '\0';

    /* 已经是 IP 就不用查 */
    if (is_ipv4(host) || is_ipv6(host)) { snprintf(ip, (size_t)ipsz, "%s", host); return 0; }

    /* 先 A，再 AAAA —— 有的域名（例如只暴露 IPv6 的家宽）根本没有 A 记录 */
    if (net_doh_query(host, 1, ip, ipsz) == 0) return 0;
    if (net_doh_query(host, 28, ip, ipsz) == 0) return 0;
    return -1;
}


int net_request_ex(const char *url, const char *method,
                   const char **headers, int nhdr,
                   const char *body,
                   NetResp *out, int timeout_s, int conn_ms, int use_chain)
{
    if (out) {
        out->status = 0;
        out->err[0] = '\0';
        out->detail[0] = '\0';
        out->tries = 0;
        if (!out->p) { out->len = 0; out->cap = 0; }
        else out->len = 0;
    }
    if (!g_inited && net_init() != 0) {
        if (out) snprintf(out->err, sizeof out->err, "%s", "网络初始化失败");
        return 0;
    }

    /* 回退链：默认 → IPv4+HTTP1.1 → 只 TLS1.2 → 三者叠加。
     * Switch 的 libcurl 在部分公网反代（只开 TLS1.3/只认 h2/只有 AAAA）上
     * 会握手失败，局域网则第一次就通，所以前一次成功就不会走后面的。 */
    static const NetOpt chain[] = {
        { 0, 0, 0 },
        { 1, 1, 0 },
        { 0, 0, 1 },
        { 1, 1, 1 },
    };
    const int nchain = use_chain ? (int)(sizeof chain / sizeof chain[0]) : 1;

    CURLcode rc = CURLE_OK;
    int tries = 0;

    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < nchain; i++) {
            if (out && out->p) out->len = 0;   /* 每次重置正文 */
            tries++;
            rc = perform(url, method, headers, nhdr, body, out, timeout_s,
                         conn_ms, &chain[i]);
            if (out) out->tries = tries;
            if (rc == CURLE_OK) break;
            if (rc == CURLE_URL_MALFORMAT || rc == CURLE_UNSUPPORTED_PROTOCOL) break;

            /* DNS 解析失败：换 IPv4/HTTP1.1 也没用，直接走 DoH 后重来 */
            if (rc == CURLE_COULDNT_RESOLVE_HOST) break;

            applog("net: try%d %s %s -> %s (%s)", tries, method ? method : "GET",
                   url, curl_easy_strerror(rc), out ? out->detail : "");
        }
        if (rc == CURLE_OK) break;
        if (rc != CURLE_COULDNT_RESOLVE_HOST) break;
        if (g_ovr_ip[0]) break;               /* 已经用过覆盖 IP，不再折腾 */

        char host[128];
        int port = 0;
        char ip[64];
        if (!url_host(url, host, sizeof host, &port)) break;
        if (net_doh_lookup(host, ip, sizeof ip) != 0) {
            applog("net: DoH 也没解析出 %s", host);
            break;
        }
        net_set_host_ip(host, ip);
        applog("net: DoH 解析 %s -> %s，重试", host, ip);
    }

    if (rc != CURLE_OK) {
        if (out) snprintf(out->err, sizeof out->err, "%s", curl_easy_strerror(rc));
        return 0;
    }
    return out ? out->status : 0;
}

int net_request(const char *url, const char *method,
                const char **headers, int nhdr,
                const char *body,
                NetResp *out, int timeout_s)
{
    return net_request_ex(url, method, headers, nhdr, body, out, timeout_s, 0, 1);
}

/* ---------------- 二进制下载（摄像头） ---------------- */

typedef struct {
    unsigned char *p;
    int len, cap, max;
    int over;
} BinBuf;

static size_t bin_cb(char *ptr, size_t sz, size_t n, void *ud)
{
    BinBuf *b = (BinBuf *)ud;
    int add = (int)(sz * n);
    if (add <= 0) return 0;
    if (b->len + add > b->max) { b->over = 1; return 0; }

    if (b->len + add > b->cap) {
        int nc = b->cap ? b->cap : 65536;
        while (nc < b->len + add) nc *= 2;
        unsigned char *np = (unsigned char *)realloc(b->p, (size_t)nc);
        if (!np) return 0;
        b->p = np;
        b->cap = nc;
    }
    memcpy(b->p + b->len, ptr, (size_t)add);
    b->len += add;
    return (size_t)add;
}

/* 下载进度：已收字节 + 总字节 + 实时速度（字节/秒）。
 * 速度在这里算而不是在 UI 层算：curl 的回调频率由 libcurl 控制
 * （大约每 100ms 一次），正好可以当采样点，UI 只负责格式化。 */
typedef struct {
    NetProgFn fn;
    void     *ud;
    BinBuf   *b;
    double    t_start;   /* 传输开始时间 */
    double    t_last;    /* 上次采样时间（秒，单调钟） */
    long long bytes_last;
    double    bps;       /* 平滑后的速度 */
} ProgCtx;

static double mono_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* libcurl 的签名是 (clientp, dltotal, dlnow, ultotal, ulnow)：
 * 第一个是**总长**，第二个才是已下载 —— 弄反会得到「永远 100%」。 */
static int xfer_cb(void *p, curl_off_t dltotal, curl_off_t dlnow,
                   curl_off_t ultotal, curl_off_t ulnow)
{
    (void)ultotal; (void)ulnow;
    ProgCtx *pc = (ProgCtx *)p;
    if (!pc->fn) return 0;

    double now = mono_now();
    double dt = now - pc->t_last;
    if (dt >= 0.12) {
        double inst = (double)((long long)dlnow - pc->bytes_last) / dt;
        if (inst < 0) inst = 0;
        /* 指数平滑，避免数字乱跳 */
        pc->bps = pc->bps > 0 ? pc->bps * 0.55 + inst * 0.45 : inst;
        pc->t_last = now;
        pc->bytes_last = (long long)dlnow;
    }
    /* 开头 200ms 内还没有第二个采样点，先用「累计/总耗时」顶上，
     * 免得进度条旁边一直挂着「--」 */
    if (pc->bps <= 0 && dlnow > 0) {
        double el = now - pc->t_start;
        if (el > 0.05) pc->bps = (double)dlnow / el;
    }
    if (pc->bps < 0) pc->bps = 0;

    /* 低速熔断：开跑 10 秒还不足 30KB（约 3KB/s），这条线基本是「假通」，
     * 立刻中断让下一条接手。没有这个，一条挂掉的镜像要白等 60 秒
     * LOW_SPEED_TIME 才判死，用户就以为卡住了。 */
    if (now - pc->t_start > 10.0 && (double)dlnow < 30720.0) {
        applog("net: low-speed abort %.1fs %.1fKB", now - pc->t_start,
               (double)dlnow / 1024.0);
        return 1;       /* 非 0 = 中断本次传输 */
    }

    pc->fn(pc->ud, (long long)dlnow, (long long)(dltotal > 0 ? dltotal : -1),
           pc->bps);
    return 0;
}

/* 单次下载尝试。返回 0 成功；失败时 *rc 是天 curl 错误码（用于判断 DNS 失败） */
static int get_once(const char *url, const char *bearer,
                    unsigned char **out, int *outlen, int maxlen,
                    int timeout_s, NetProgFn prog, void *ud,
                    char *err, int errsz, CURLcode *rcout)
{
    *out = NULL;
    *outlen = 0;
    *rcout = CURLE_OK;

    CURL *c = curl_easy_init();
    if (!c) {
        if (err) snprintf(err, (size_t)errsz, "%s", "curl 初始化失败");
        return -1;
    }

    BinBuf b;
    memset(&b, 0, sizeof b);
    b.max = maxlen > 0 ? maxlen : (4 * 1024 * 1024);

    struct curl_slist *hs = NULL;
    char auth[720];
    if (bearer && bearer[0]) {
        snprintf(auth, sizeof auth, "Authorization: Bearer %s", bearer);
        hs = curl_slist_append(hs, auth);
    }
    /* GitHub 的 release asset API 只有带 octet-stream 才返回二进制，
     * 否则返回一段 JSON 元数据 —— OTA 备用下载通道用得到 */
    if (strstr(url, "api.github.com/repos/") &&
        strstr(url, "/releases/assets/"))
        hs = curl_slist_append(hs, "Accept: application/octet-stream");

    double t_start = mono_now();
    ProgCtx pc = { prog, ud, &b, t_start, t_start, 0, 0 };

    curl_easy_setopt(c, CURLOPT_URL, url);
    if (hs) curl_easy_setopt(c, CURLOPT_HTTPHEADER, hs);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, bin_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &b);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 8L);
    /* 不用固定总超时卡死下载：13.9MB 的 nro 在慢速网络上可能要十几分钟，
     * 卡 300 秒会直接失败。改成「连续 60 秒低于 1KB/s 才算断」，
     * 慢但能通的链路照样下得完；timeout_s 只作为兜底上限。 */
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 60L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, (long)(timeout_s > 0 ? timeout_s : 12L));
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "switch-ha/1.0");
    if (prog) {
        curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, xfer_cb);
        curl_easy_setopt(c, CURLOPT_XFERINFODATA, &pc);
    }

    CURLcode rc = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(c);
    if (hs) curl_slist_free_all(hs);

    *rcout = rc;
    if (rc != CURLE_OK || b.over) {
        if (err) snprintf(err, (size_t)errsz, "%s",
                          b.over ? "文件过大" : curl_easy_strerror(rc));
        free(b.p);
        return -1;
    }
    if (code < 200 || code >= 300) {
        if (err) snprintf(err, (size_t)errsz, "HTTP %ld", code);
        free(b.p);
        return -1;
    }

    *out = b.p;
    *outlen = b.len;
    return 0;
}

int net_get_file_prog(const char *url, const char *bearer,
                      unsigned char **out, int *outlen, int maxlen,
                      int timeout_s, NetProgFn prog, void *ud,
                      char *err, int errsz)
{
    *out = NULL;
    *outlen = 0;
    if (err && errsz) err[0] = '\0';

    if (!g_inited && net_init() != 0) {
        if (err) snprintf(err, (size_t)errsz, "%s", "网络初始化失败");
        return -1;
    }

    CURLcode rc = CURLE_OK;
    if (get_once(url, bearer, out, outlen, maxlen, timeout_s, prog, ud,
                 err, errsz, &rc) == 0)
        return 0;

    /* DNS 解析不出来（Switch 自带 resolver 对 CNAME 链 / 大响应会失败）：
     * 走一次 DoH 拿到 IP 再重试，和普通 API 请求同样的后路 */
    if (rc == CURLE_COULDNT_RESOLVE_HOST && !g_ovr_ip[0]) {
        char host[128], ip[64];
        int port = 0;
        if (url_host(url, host, sizeof host, &port) &&
            net_doh_lookup(host, ip, sizeof ip) == 0) {
            net_set_host_ip(host, ip);
            applog("net: dl DoH %s -> %s，重试", host, ip);
            if (get_once(url, bearer, out, outlen, maxlen, timeout_s, prog, ud,
                         err, errsz, &rc) == 0)
                return 0;
        }
    }
    return -1;
}

int net_get_file(const char *url, const char *bearer,
                 unsigned char **out, int *outlen, int maxlen,
                 int timeout_s, char *err, int errsz)
{
    return net_get_file_prog(url, bearer, out, outlen, maxlen, timeout_s,
                             NULL, NULL, err, errsz);
}

int net_get_bin(const char *url, const char *bearer,
                unsigned char **out, int *outlen, int maxlen,
                char *err, int errsz)
{
    return net_get_file(url, bearer, out, outlen, maxlen, 12, err, errsz);
}

/* ---- 多线路竞速：并行探测每条线路，选最快的一条 ----
 * 镜像站质量参差且波动大，固定顺序经常落在慢线上；同时向所有线路
 * 请求前 probe_bytes 字节，谁先到谁赢。全部不通返回 -1。
 * probe_bytes 建议 64KB：够区分快慢，开销只占整包的 1% 左右。 */

typedef struct {
    CURL     *eh;
    long long got;      /* 已收到的字节数（丢弃，不保存） */
    long      code;
    int       ok;       /* 传输结束且 HTTP 2xx */
} RaceLine;

static size_t race_cb(char *ptr, size_t sz, size_t n, void *ud)
{
    RaceLine *l = (RaceLine *)ud;
    size_t add = sz * n;
    l->got += (long long)add;
    return add;
}

int net_race_probe(const char *const *urls, int n, int probe_bytes,
                   int max_ms, double *out_bps)
{
    if (out_bps) *out_bps = 0;
    if (n <= 0) return -1;
    if (n == 1) return 0;           /* 只有一条，没什么好赛的 */
    if (n > 6) n = 6;
    if (!g_inited && net_init() != 0) return -1;

    CURLM *m = curl_multi_init();
    if (!m) return -1;

    RaceLine L[6];
    memset(L, 0, sizeof L);
    char range[32];
    snprintf(range, sizeof range, "0-%d", probe_bytes - 1);

    for (int i = 0; i < n; i++) {
        L[i].eh = curl_easy_init();
        if (!L[i].eh) continue;
        curl_easy_setopt(L[i].eh, CURLOPT_URL, urls[i]);
        curl_easy_setopt(L[i].eh, CURLOPT_RANGE, range);
        curl_easy_setopt(L[i].eh, CURLOPT_WRITEFUNCTION, race_cb);
        curl_easy_setopt(L[i].eh, CURLOPT_WRITEDATA, &L[i]);
        curl_easy_setopt(L[i].eh, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(L[i].eh, CURLOPT_CONNECTTIMEOUT_MS, 3500L);
        curl_easy_setopt(L[i].eh, CURLOPT_TIMEOUT_MS, (long)max_ms);
        curl_easy_setopt(L[i].eh, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(L[i].eh, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(L[i].eh, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(L[i].eh, CURLOPT_USERAGENT, "switch-ha/1.0");
        curl_multi_add_handle(m, L[i].eh);
    }

    double t0 = mono_now();
    int winner = -1, still = 0;
    for (;;) {
        curl_multi_perform(m, &still);

        CURLMsg *msg;
        int left;
        while ((msg = curl_multi_info_read(m, &left)) != NULL) {
            if (msg->msg != CURLMSG_DONE) continue;
            for (int i = 0; i < n; i++) {
                if (L[i].eh != msg->easy_handle) continue;
                curl_easy_getinfo(L[i].eh, CURLINFO_RESPONSE_CODE, &L[i].code);
                L[i].ok = (L[i].code >= 200 && L[i].code < 300 &&
                           msg->data.result == CURLE_OK);
                break;
            }
        }

        double el = mono_now() - t0;
        if (el < 0.05) el = 0.05;

        /* 有线路在窗口内完整下满 probe_bytes：第一个到线的直接赢 */
        for (int i = 0; i < n && winner < 0; i++)
            if (L[i].ok && L[i].got >= probe_bytes) winner = i;

        if (winner < 0 && (el * 1000.0 >= (double)max_ms || !still)) {
            /* 窗口结束：选成功线路里平均速率最高的（慢但通的也能选中） */
            double best = 0;
            for (int i = 0; i < n; i++) {
                if (!L[i].ok) continue;
                double bps = (double)L[i].got / el;
                if (bps > best) { best = bps; winner = i; }
            }
            if (winner >= 0 && out_bps) *out_bps = best;
            break;
        }
        if (winner >= 0) {
            if (out_bps) *out_bps = (double)L[winner].got / el;
            break;
        }

        curl_multi_wait(m, NULL, 0, 40, NULL);
    }

    for (int i = 0; i < n; i++) {
        if (!L[i].eh) continue;
        applog("net: race [%d] %s ok=%d got=%lld",
               i, urls[i], L[i].ok, L[i].got);
        curl_multi_remove_handle(m, L[i].eh);
        curl_easy_cleanup(L[i].eh);
    }
    curl_multi_cleanup(m);
    return winner;
}
