#pragma once

#include <SDL.h>

void cam_init(SDL_Renderer *ren);
int  cam_open(const char *url, const char *token);   /* 0=已开始取图 */
void cam_close(void);
void cam_reload(void);                               /* 断开重连 */
void cam_tick(void);                                 /* 主线程：解码最新帧 */
int  cam_draw(SDL_Rect box);                         /* 1=画了图 */
int  cam_ready(void);
const char *cam_status(void);
