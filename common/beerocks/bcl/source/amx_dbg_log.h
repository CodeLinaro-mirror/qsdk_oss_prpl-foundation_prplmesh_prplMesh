#ifndef AMX_DBG_LOG_H
#define AMX_DBG_LOG_H

/*
 * Debug trace for AccessPoint _add / _set timing (see amx_dbg_log.c).
 * Log file: /tmp/amx_debug.log
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int amx_dbg_tid(void);
long amx_dbg_now_us(void);
void amx_dbg_log(const char* func, const char* fmt, ...)
    __attribute__((format(printf, 2, 3)));
int amx_dbg_interesting(const char* sig, const char* path);

#define AMX_DBG(...) amx_dbg_log(__func__, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif