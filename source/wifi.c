/* WiFi 链路状态：nifm 查询连接类型与信号强度（0~3 格）。
 * 状态查询有 IPC 开销，结果缓存 2 秒——顶栏每帧都画，没必要每帧都问。 */
#include <time.h>
#include "wifi.h"
#ifdef __SWITCH__
#include <switch.h>
#else
#include <stdlib.h>
#endif

#ifdef __SWITCH__
static int g_inited;

static void ensure_init(void)
{
    if (!g_inited) {
        nifmInitialize(NifmServiceType_User);
        g_inited = 1;
    }
}
#endif

/* 返回：0 = 未连接，1 = WiFi，2 = 有线；
 * strength 出参 0~3（仅 WiFi 有意义），查询失败时不改动。 */
int wifi_link(int *strength)
{
    static int      s_link = -1, s_str;
    static time_t   s_at;

    time_t now = time(NULL);
    if (s_link >= 0 && now - s_at < 2) {
        if (strength) *strength = s_str;
        return s_link;
    }

#ifdef __SWITCH__
    ensure_init();
    NifmInternetConnectionType type = 0;
    u32 bars = 0;
    NifmInternetConnectionStatus st = 0;
    if (R_SUCCEEDED(nifmGetInternetConnectionStatus(&type, &bars, &st)) &&
        st == NifmInternetConnectionStatus_Connected) {
        s_link = (type == NifmInternetConnectionType_Ethernet) ? 2 : 1;
        s_str  = (int)bars;
        if (s_str > 3) s_str = 3;
    } else {
        s_link = 0;
        s_str  = 0;
    }
#else
    /* 桌面预览：SWHA_WIFI=0~3 控制格数（默认 3），SWHA_LINK=0/1/2 控制类型 */
    const char *l = getenv("SWHA_LINK");
    s_link = l ? atoi(l) : 1;
    if (s_link < 0 || s_link > 2) s_link = 1;
    const char *b = getenv("SWHA_WIFI");
    s_str = b ? atoi(b) : 3;
    if (s_str < 0) s_str = 0;
    if (s_str > 3) s_str = 3;
#endif

    s_at = now;
    if (strength) *strength = s_str;
    return s_link;
}
