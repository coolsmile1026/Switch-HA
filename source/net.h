#pragma once

#include <stddef.h>

typedef struct {
    char *p;        /* 响应正文（以 \0 结尾） */
    int   len;
    int   cap;
    int   status;   /* HTTP 状态码，0 = 网络层失败 */
    char  err[160];
    char  detail[96];   /* 诊断用：协议版本 + 对端 IP */
    int   tries;        /* 实际尝试次数（>1 说明走了回退策略） */
} NetResp;

int  net_init(void);
void net_done(void);

/* headers 是 "Name: value" 字符串数组，可为 NULL */
int net_request(const char *url, const char *method,
                const char **headers, int nhdr,
                const char *body,
                NetResp *out, int timeout_s);

/* 带连接超时（毫秒）和回退链开关的版本。
 * 连局域网 IP 时建议 conn_ms=1200、use_chain=0：
 * IP 直连没有 DNS / h2 / TLS1.3 协商问题，那 4 段回退链只会在
 * 地址不可达时白白多等好几秒 —— 而内外网切换恰恰最怕等。 */
int net_request_ex(const char *url, const char *method,
                   const char **headers, int nhdr,
                   const char *body,
                   NetResp *out, int timeout_s, int conn_ms, int use_chain);

void net_resp_free(NetResp *r);

/* ---- 主机名解析 ----
 * Switch 上 libnx 自带的 resolver 对某些域名（CNAME 链 / DNSSEC 大响应 /
 * 走非标准 DNS）会直接失败，而系统层没有别的办法。这里提供两条后路：
 *   1) 自动：系统解析失败时改走 DoH（IP 直连，不再依赖本机 DNS）
 *   2) 手动：用户在设置里直接填服务器 IP，域名只用于 TLS SNI / Host */
void net_set_host_ip(const char *host, const char *ip);   /* ip="" 清除 */
int  net_doh_lookup(const char *host, char *ip, int ipsz); /* 0 = 成功（A 优先，无 A 则 AAAA） */
int  net_doh_query(const char *host, int qtype, char *ip, int ipsz); /* qtype 1=A 28=AAAA */

/* 拉二进制（摄像头 JPEG）。成功返回字节数，失败 -1 */
int net_get_bin(const char *url, const char *bearer,
                unsigned char **out, int *outlen, int maxlen,
                char *err, int errsz);

/* 同上，但可以指定总超时（秒）——OTA 下载大文件用 */
int net_get_file(const char *url, const char *bearer,
                 unsigned char **out, int *outlen, int maxlen,
                 int timeout_s, char *err, int errsz);

/* 下载进度回调：got = 已收字节，total = 服务端声明总长（未知时为 -1），
 * bps = 平滑后的实时速度（字节/秒，可能为 0）。
 * 在 curl 传输线程（即调用者线程）里同步回调，可以做轻量 UI 刷新。 */
typedef void (*NetProgFn)(void *ud, long long got, long long total, double bps);

/* 带进度的下载。DNS 解析失败时会走一次 DoH 重试，
 * 主链接失败后由调用方决定换哪条备用地址。 */
int net_get_file_prog(const char *url, const char *bearer,
                      unsigned char **out, int *outlen, int maxlen,
                      int timeout_s, NetProgFn prog, void *ud,
                      char *err, int errsz);

/* 多线路竞速：并行向每条线路请求前 probe_bytes 字节，
 * 返回最快线路的下标（全部不通 = -1），out_bps 带胜者速率（字节/秒）。
 * 用于 OTA 下载前选线，probe_bytes 建议 64KB，max_ms 是探测窗口。 */
int net_race_probe(const char *const *urls, int n, int probe_bytes,
                   int max_ms, double *out_bps);
