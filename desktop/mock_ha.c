/* 桌面预览用的假 HA 后端：不联网，提供一份接近真实的实体数据用于调 UI */

#include "ha.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static HaEnt  E[64];
static int    NE;
static HaArea A[16];
static int    NA;
static HaEnt  S[16];
static int    NS;

static char g_base[192] = "http://192.168.1.100:8123";
static char g_alt[192]  = "https://ha.example.com";
static int  g_slot;
static char g_user[64] = "coolsmile";

static void add(const char *eid, const char *name, const char *area,
                const char *state, int dom, int on, int level,
                const char *unit, const char *dcls)
{
    HaEnt *e = &E[NE++];
    memset(e, 0, sizeof *e);
    snprintf(e->eid, sizeof e->eid, "%s", eid);
    snprintf(e->name, sizeof e->name, "%s", name);
    snprintf(e->area, sizeof e->area, "%s", area);
    snprintf(e->state, sizeof e->state, "%s", state);
    snprintf(e->unit, sizeof e->unit, "%s", unit ? unit : "");
    snprintf(e->dev_class, sizeof e->dev_class, "%s", dcls ? dcls : "");
    e->dom = dom;
    e->on = on;
    e->level = level;
    e->online = 1;
}

static void add_scene(const char *eid, const char *name, const char *area, int dom)
{
    HaEnt *s = &S[NS++];
    memset(s, 0, sizeof *s);
    snprintf(s->eid, sizeof s->eid, "%s", eid);
    snprintf(s->name, sizeof s->name, "%s", name);
    snprintf(s->area, sizeof s->area, "%s", area);
    s->dom = dom;
    s->on = -1;
    s->level = -1;
    s->online = 1;
}

static void seed(void)
{
    if (NE) return;

    add("light.keting_main", "客厅主灯", "客厅", "on", DOM_LIGHT, 1, 80, "", "");
    add("switch.tv", "电视插座", "客厅", "off", DOM_SWITCH, 0, -1, "", "");
    add("climate.keting_ac", "客厅空调", "客厅", "cool", DOM_CLIMATE, 1, -1, "", "");
    add("cover.keting_curtain", "客厅窗帘", "客厅", "open", DOM_COVER, 1, 62, "", "");
    add("camera.doorbell", "门口摄像头", "客厅", "idle", DOM_CAMERA, -1, -1, "", "");
    add("light.keting_floor", "落地灯", "客厅", "off", DOM_LIGHT, 0, 0, "", "");

    add("light.bedside", "床头灯", "卧室", "on", DOM_LIGHT, 1, 30, "", "");
    add("humidifier.bedroom", "卧室加湿器", "卧室", "off", DOM_HUMIDIFIER, 0, -1, "", "");
    add("sensor.bedroom_temp", "卧室温度", "卧室", "26.5", DOM_SENSOR, -1, -1, "°C", "temperature");
    add("sensor.bedroom_hum", "卧室湿度", "卧室", "48", DOM_SENSOR, -1, -1, "%", "humidity");

    add("light.kitchen", "厨房吊灯", "厨房", "off", DOM_LIGHT, 0, 0, "", "");
    add("switch.kettle", "电热水壶", "厨房", "on", DOM_SWITCH, 1, -1, "", "");
    add("binary_sensor.smoke", "烟雾传感器", "厨房", "off", DOM_BINARY, -1, -1, "", "smoke");

    add("light.desk", "书桌台灯", "书房", "on", DOM_LIGHT, 1, 100, "", "");
    add("fan.desk", "桌面风扇", "书房", "on", DOM_FAN, 1, 40, "", "");
    add("switch.pc", "电脑主机", "书房", "on", DOM_SWITCH, 1, -1, "", "");

    add("vacuum.robot", "扫地机器人", "", "docked", DOM_VACUUM, 0, -1, "", "");
    add("lock.front", "大门锁", "", "locked", DOM_LOCK, 0, -1, "", "");
    add("fan.air_purifier", "空气净化器", "", "off", DOM_FAN, 0, 0, "", "");

    add_scene("scene.home", "回家模式", "客厅", DOM_SCENE);
    add_scene("script.movie", "影院模式", "客厅", DOM_SCRIPT);
    add_scene("scene.away", "离家模式", "", DOM_SCENE);
    add_scene("script.goodnight", "晚安", "卧室", DOM_SCRIPT);

    /* 区域统计 */
    const char *order[] = { "客厅", "卧室", "厨房", "书房", "未分配" };
    for (int i = 0; i < 5; i++) {
        int n = 0;
        for (int k = 0; k < NE; k++) {
            const char *ar = E[k].area[0] ? E[k].area : "未分配";
            if (strcmp(ar, order[i]) == 0) n++;
        }
        if (!n) continue;
        snprintf(A[NA].name, sizeof A[NA].name, "%s", order[i]);
        A[NA].nent = n;
        NA++;
    }
}

int  ha_load(void) { seed(); return 1; }
void ha_forget(void) { g_base[0] = '\0'; }
int  ha_ready(void) { return g_base[0] ? 1 : 0; }
const char *ha_base(void) { return (g_slot && g_alt[0]) ? g_alt : g_base; }
const char *ha_alt(void) { return g_alt; }
const char *ha_base1(void) { return g_base; }
const char *ha_route(void) { return (g_slot && g_alt[0]) ? "外网" : "内网"; }
void ha_set_alt(const char *b) { snprintf(g_alt, sizeof g_alt, "%s", b ? b : ""); if (!g_alt[0]) g_slot = 0; }
void ha_use_slot(int s) { g_slot = (s == 1 && g_alt[0]) ? 1 : 0; }
const char *ha_token(void) { return "mock-token"; }
const char *ha_user(void) { return g_user; }
const char *ha_version(void) { return "2026.10.1"; }
char *ha_base_mut(void) { return g_base; }
void ha_set_base(const char *b) { snprintf(g_base, sizeof g_base, "%s", b); }

int ha_probe(char *msg, int msgsz)
{
    seed();
    snprintf(msg, (size_t)msgsz, "%s", "已找到 Home Assistant（需要登录）");
    return 0;
}

const char *ha_ip(void) { return ""; }
void ha_save_prefs(void) {}

/* 桌面预览：主题由 --dark 参数控制（preview.c 直接调 ui_theme_set），
 * 这里只做持久化的空实现 */
static int mock_theme;
int  ha_theme(void) { return mock_theme; }
void ha_set_theme(int dark) { mock_theme = dark; }
void ha_set_ip(const char *ip) { (void)ip; }

/* 预览用：编一段典型的「内网通、外网只有 AAAA」诊断输出 */
int ha_diag(char *out, int outsz)
{
    return snprintf(out, (size_t)outsz,
                    "当前走内网：%s\n"
                    "⓪ 线路\n"
                    "   内网 %s\n"
                    "     → HTTP 200 ✓ 是 HA\n"
                    "   外网 %s\n"
                    "     → 不通：Couldn't resolve host name\n"
                    "⓪ 解析   ha.example.com → 2001:db8::10"
                    "（★只有 IPv6，没有 A 记录）\n"
                    "   说明：Switch 的 IPv6 出网能力很差，纯 AAAA 域名基本连不上。"
                    "根治办法是让域名有 IPv4 入口——在 NAS 上跑 Cloudflare Tunnel，"
                    "或把域名托管到 Cloudflare 开橙云代理。\n"
                    "① 首页   HTTP 200  HTTP/1.1 · 192.168.1.100\n"
                    "② API    HTTP 200  {message:API running.}\n"
                    "③ 登录流 HTTP 200  拿到 flow_id，可以登录\n"
                    "\n结论：内网这条链路完全正常；外网要等域名拿到 IPv4 才能用。",
                    ha_base(), ha_base1(), g_alt[0] ? g_alt : "（未填）");
}

int ha_login(const char *user, const char *pass, char *msg, int msgsz)
{
    (void)pass;
    seed();
    snprintf(g_user, sizeof g_user, "%s", user);
    snprintf(msg, (size_t)msgsz, "%s", "登录成功");
    return 0;
}

int ha_login_with_token(const char *tok, char *msg, int msgsz)
{
    (void)tok;
    seed();
    snprintf(msg, (size_t)msgsz, "%s", "登录成功");
    return 0;
}

int ha_sync(char *msg, int msgsz)
{
    seed();
    if (msg && msgsz) snprintf(msg, (size_t)msgsz, "%d 个设备 · %d 个区域", NE, NA);
    return NE;
}

/* 桌面预览没有真实同步，永远按「有变化」处理（等价于旧行为） */
int ha_last_changed(void) { return 1; }

int ha_nent(void) { return NE; }
const HaEnt *ha_ent(int i) { return (i >= 0 && i < NE) ? &E[i] : NULL; }
int ha_narea(void) { return NA; }
const HaArea *ha_area(int i) { return (i >= 0 && i < NA) ? &A[i] : NULL; }
int ha_nscene(void) { return NS; }
const HaEnt *ha_scene(int i) { return (i >= 0 && i < NS) ? &S[i] : NULL; }
int ha_ent_area_count(int i) { return (i >= 0 && i < NA) ? A[i].nent : 0; }

int ha_toggle(int idx, char *msg, int msgsz)
{
    if (idx < 0 || idx >= NE) return -1;
    E[idx].on = E[idx].on == 1 ? 0 : 1;
    snprintf(E[idx].state, sizeof E[idx].state, "%s", E[idx].on ? "on" : "off");
    snprintf(msg, (size_t)msgsz, "%s", "已下发");
    return 0;
}

int ha_set_level(int idx, int v, char *msg, int msgsz)
{
    (void)msg; (void)msgsz;
    if (idx < 0 || idx >= NE) return -1;
    E[idx].level = v;
    E[idx].on = 1;
    return 0;
}

int ha_set_position(int idx, int v, char *msg, int msgsz)
{
    (void)msg; (void)msgsz;
    if (idx < 0 || idx >= NE) return -1;
    E[idx].level = v;
    return 0;
}

int ha_run_entity(const char *eid, char *msg, int msgsz)
{
    (void)eid;
    snprintf(msg, (size_t)msgsz, "%s", "已执行");
    return 0;
}

int ha_cam_url(const char *eid, char *url, int sz)
{
    snprintf(url, (size_t)sz, "%s/api/camera_proxy/%s", g_base, eid);
    return 0;
}

int ha_normalize_url(const char *in, char *out, int outsz)
{
    snprintf(out, (size_t)outsz, "http://%s", in);
    return 1;
}
