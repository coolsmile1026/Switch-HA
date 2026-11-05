#pragma once

/* Home Assistant 瘦客户端后端。
 *
 * 与手机版 HA App 一致的接入方式：只填服务器地址，然后在应用内完成 OAuth 登录
 * （HA 的 login_flow 接口），拿到的 access/refresh token 存到 SD 卡，
 * 之后每次启动自动免登录，令牌过期自动刷新。
 *
 * 用到的官方接口：
 *   GET  /api/                       探活（401 也说明是 HA）
 *   POST /auth/login_flow            开始登录，拿 flow_id
 *   POST /auth/login_flow/<id>       提交用户名密码，拿授权码
 *   POST /auth/token                 授权码换令牌 / 刷新令牌
 *   POST /api/template               一次性拉全量实体（含区域、状态）
 *   GET  /api/states                 模板不可用时的兜底
 *   POST /api/services/<d>/<s>       控制
 *   GET  /api/camera_proxy/<eid>     摄像头快照
 */

#define HA_MAX_ENT    400
#define HA_MAX_AREA   40
#define HA_MAX_SCENE  60

enum {
    DOM_OTHER = 0, DOM_LIGHT, DOM_SWITCH, DOM_FAN, DOM_COVER, DOM_CLIMATE,
    DOM_CAMERA, DOM_SENSOR, DOM_BINARY, DOM_LOCK, DOM_VACUUM, DOM_MEDIA,
    DOM_HUMIDIFIER, DOM_SCENE, DOM_SCRIPT, DOM_INPUT_BOOL, DOM_WATER_HEATER
};

typedef struct {
    char eid[64];
    char name[64];
    char area[40];
    char state[32];
    char unit[16];
    char dev_class[28];
    int  dom;
    int  on;        /* 1 开 / 0 关 / -1 不适用 */
    int  level;     /* 0-100，-1 = 无此能力（亮度/开合） */
    int  online;
} HaEnt;

typedef struct {
    char name[40];
    int  nent;
} HaArea;

/* ---- 配置与状态 ---- */
/* ---- 地址 1 / 地址 2（内网 + 外网，自动切换）----
 * 家里和外面都能用：地址 1 填内网 http://192.168.x.x:8123，
 * 地址 2 填外网 https://你的域名。哪条能用走哪条，切换结果会记到配置里。
 * 两边是同一台 HA，令牌通用，切换不需要重新登录。 */
const char *ha_route(void);                 /* "内网" / "外网"，当前生效线路 */
const char *ha_base1(void);                 /* 地址 1 原值 */
const char *ha_alt(void);                   /* 地址 2 原值 */
void ha_set_alt(const char *base);
void ha_use_slot(int slot);                 /* 0 = 地址1，1 = 地址2 */

int  ha_load(void);                         /* 读 SD 卡配置，1=已就绪 */
void ha_forget(void);                       /* 清除登录（删配置文件） */
int  ha_ready(void);
const char *ha_base(void);
const char *ha_token(void);
const char *ha_user(void);
const char *ha_version(void);
char *ha_base_mut(void);                    /* 供设置页编辑 */
void ha_set_base(const char *base);         /* 自动补 http:// 去尾部斜杠 */

/* 服务器 IP：Switch 的 DNS 解析不到域名时，直接填 IP，
 * 域名照旧用于 TLS SNI 与 Host 头（反代按域名分流也不会错） */
void ha_set_ip(const char *ip);
void ha_save_prefs(void);                   /* 只存 base/ip，不需要令牌 */
const char *ha_ip(void);

/* 外观偏好：0 = 浅色，1 = 深色（存配置文件） */
int  ha_theme(void);
void ha_set_theme(int dark);

/* ---- 连接与登录（阻塞，调用方负责显示进度） ---- */
int ha_probe(char *msg, int msgsz);         /* 0 = 是 HA 服务器 */

/* 逐项诊断当前地址：探活 → /api/ → 登录流，结果写成多行文本（\n 分隔）。
 * 用来区分「DNS 不通 / TLS 握手失败 / 证书或反代拦了 / 不是 HA」这些情况 */
int ha_diag(char *out, int outsz);
int ha_login(const char *user, const char *pass, char *msg, int msgsz);
int ha_login_with_token(const char *tok, char *msg, int msgsz);

/* ---- 实体同步 ---- */
int ha_sync(char *msg, int msgsz);          /* 返回实体数，<0 失败 */
/* 上一次 ha_sync 拉回来的内容是否和上次不同：0 = 一模一样（跳过重建/写盘） */
int ha_last_changed(void);

/* ---- 状态缓存（重开秒进）----
 * 同步成功后把实体/地区/场景写一份到 SD 卡；下次启动先画缓存，
 * 后台再静默同步，避免「一进来就卡在同步遮罩上」。
 * 缓存只用于显示，控制动作仍以服务器返回为准。 */
int  ha_load_cache(void);                   /* 返回载入的实体数，0 = 无缓存 */
void ha_save_cache(void);                   /* 立即写盘（退出时调用） */
long ha_cache_age(void);                    /* 缓存写入距今秒数，-1 = 未知 */

/* ---- 读取 ---- */
int  ha_nent(void);
const HaEnt *ha_ent(int i);
int  ha_narea(void);
const HaArea *ha_area(int i);
int  ha_nscene(void);
const HaEnt *ha_scene(int i);
int  ha_ent_area_count(int area_idx);

/* ---- 控制 ---- */
int ha_toggle(int idx, char *msg, int msgsz);
int ha_set_level(int idx, int v, char *msg, int msgsz);
int ha_set_position(int idx, int v, char *msg, int msgsz);
int ha_run_entity(const char *eid, char *msg, int msgsz);

/* ---- 摄像头 ---- */
int ha_cam_url(const char *eid, char *url, int sz);

/* 地址格式规整（不合法返回 0） */
int ha_normalize_url(const char *in, char *out, int outsz);
