/* WiFi 链路状态查询（source/wifi.c） */
#ifndef WIFI_H
#define WIFI_H

/* 返回：0 = 未连接，1 = WiFi，2 = 有线。
 * strength 可为 NULL；WiFi 时为 0~3 格信号强度。 */
int wifi_link(int *strength);

#endif
