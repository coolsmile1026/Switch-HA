#pragma once

/* 轻量日志：先进内存缓冲，最多每秒落一次 SD 卡。
 * 崩溃最多丢 1 秒；关键节点可主动调 applog_flush() 立即落盘。 */
void applog(const char *fmt, ...);
void applog_flush(void);
void applog_close(void);
