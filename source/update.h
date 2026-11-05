#pragma once

/* ---- OTA 自更新 ----
 *
 * 程序能自己下载新版本的 .nro 并替换 SD 卡上的自己（hbmenu 启动时
 * 已把整个 NRO 读进内存，运行中改名/替换文件是安全的）。
 *
 * 更新源是一个 JSON 或纯文本 manifest，URL 可以放在
 *   sdmc:/switch/switch-ha/update.txt   （第一行 = manifest URL，
 *                                         第二行 = NRO 自身路径，可选）
 * 不存在时用内置默认（GitHub Releases）。
 *
 * 支持两种 manifest 格式：
 *  1) GitHub Releases API（releases/latest）：
 *     自动取 tag_name 和 assets 里第一个 .nro 附件
 *  2) 自托管 JSON：{"version":"1.0.1","url":"https://.../switch-ha.nro"}
 *  3) 纯文本：第一行版本号，第二行下载地址
 *
 * 流程：设置页「检查更新」→ 拉 manifest → 版本比较 → 下载到 .new →
 *       校验 NRO 魔数 → 原子替换（换不动就留给下次启动处理）→ 提示重启。
 */

#ifndef APP_VERSION_STR
#define APP_VERSION_STR "1.0.0"
#endif

const char *upd_cur_version(void);

/* 进度显示钩子：下载是阻塞的，主循环出不了帧，只能由下载回调
 * 直接调用这个绘制函数（ui.c 注册 gfx_busy_overlay）。
 * pct < 0 = 不确定进度。桌面预览/未注册时什么都不做。 */
typedef void (*UpdPaint)(const char *title, const char *sub, int pct);
void upd_set_paint(UpdPaint fn);

/* 检查 + 下载 + 替换（一步到位，无交互）。
 * 返回 0 = 已装好新版（重启生效），1 = 已是最新，-1 = 失败（msg 说明原因） */
int  upd_run(char *msg, int msgsz);

/* 拆开的两步，供「发现新版本 → 用户确认 → 下载」这种交互使用。
 * upd_check：1 = 有新版（upd_new_version() 取版本号），0 = 已最新，-1 = 失败
 * upd_install：用上次 check 的结果下载安装，0 = 成功，-1 = 失败 */
int  upd_check(char *msg, int msgsz);
int  upd_install(char *msg, int msgsz);
const char *upd_new_version(void);

/* 刚装好的版本号（upd_install 成功后有效） */
const char *upd_done_version(void);

/* 启动时调用：应用上次没换成功的 .new，并清理 .old 备份 */
void upd_apply_pending(void);
