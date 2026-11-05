/* 桌面预览没有真实图传，给 cam 接口一套空实现 */

#include "cam.h"

void cam_init(SDL_Renderer *ren) { (void)ren; }
int  cam_open(const char *url, const char *token) { (void)url; (void)token; return -1; }
void cam_close(void) {}
void cam_reload(void) {}
void cam_tick(void) {}
int  cam_draw(SDL_Rect box) { (void)box; return 0; }
int  cam_ready(void) { return 0; }
const char *cam_status(void) { return "预览模式：没有摄像头画面"; }
