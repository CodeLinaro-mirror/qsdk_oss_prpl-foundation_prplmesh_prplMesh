#define _GNU_SOURCE
/* OpenWrt/musl -Wformat-nonliteral + fortify/stdio.h breaks dynamic vsnprintf */
#undef _FORTIFY_SOURCE

#include "amx_dbg_log.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/syscall.h>
#include <sys/types.h>

#undef snprintf
#undef vsnprintf

static pthread_mutex_t amx_dbg_log_mutex = PTHREAD_MUTEX_INITIALIZER;
static int amx_dbg_fd                  = -1;
static int amx_dbg_pid;
static char amx_dbg_proc[64] = "?";
static int amx_dbg_proc_init;

int amx_dbg_tid(void)
{
    return (int) syscall(SYS_gettid);
}

long amx_dbg_now_us(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long) ts.tv_sec * 1000000L + (long) (ts.tv_nsec / 1000L);
}

static void amx_dbg_init_proc(void)
{
    int cfd;
    int r;

    if(amx_dbg_proc_init != 0) {
        return;
    }
    amx_dbg_pid = (int) getpid();
    cfd         = open("/proc/self/comm", O_RDONLY | O_CLOEXEC);
    if(cfd >= 0) {
        r = (int) read(cfd, amx_dbg_proc, sizeof(amx_dbg_proc) - 1);
        if(r > 0) {
            amx_dbg_proc[r] = '\0';
            if(amx_dbg_proc[r - 1] == '\n') {
                amx_dbg_proc[r - 1] = '\0';
            }
        }
        close(cfd);
    }
    amx_dbg_proc_init = 1;
}

void amx_dbg_log(const char* func, const char* fmt, ...)
{
    char msg[768];
    char line[1024];
    struct timespec ts;
    struct tm tm_buf;
    va_list ap;
    int n;

    pthread_mutex_lock(&amx_dbg_log_mutex);
    if(amx_dbg_fd < 0) {
        amx_dbg_fd = open("/tmp/amx_debug.log", O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
        if(amx_dbg_fd < 0) {
            pthread_mutex_unlock(&amx_dbg_log_mutex);
            return;
        }
        amx_dbg_init_proc();
    }

    clock_gettime(CLOCK_REALTIME, &ts);
    memset(&tm_buf, 0, sizeof(tm_buf));
    localtime_r(&ts.tv_sec, &tm_buf);

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    n = snprintf(line, sizeof(line),
                 "%04d-%02d-%02d %02d:%02d:%02d.%06ld pid=%d tid=%d proc=%s func=%s %s\n",
                 tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday, tm_buf.tm_hour,
                 tm_buf.tm_min, tm_buf.tm_sec, (long) (ts.tv_nsec / 1000L), amx_dbg_pid,
                 amx_dbg_tid(), amx_dbg_proc, (func != NULL) ? func : "?", msg);
    if(n > 0) {
        if(n >= (int) sizeof(line)) {
            n = (int) sizeof(line) - 1;
        }
        if((n == 0) || (line[n - 1] != '\n')) {
            if(n >= (int) sizeof(line) - 1) {
                n = (int) sizeof(line) - 2;
            }
            line[n++] = '\n';
        }
        (void) write(amx_dbg_fd, line, (size_t) n);
    }
    pthread_mutex_unlock(&amx_dbg_log_mutex);
}

int amx_dbg_interesting(const char* sig, const char* path)
{
    if((sig == NULL) || (sig[0] == '\0')) {
        return 1;
    }
    if((strcmp(sig, "dm:instance-added") == 0) || (strcmp(sig, "dm:instance-removed") == 0) ||
       (strcmp(sig, "dm:object-added") == 0) || (strcmp(sig, "dm:object-removed") == 0)) {
        return 1;
    }
    if((path != NULL) && (strstr(path, "AccessPoint") != NULL)) {
        return 1;
    }
    return 0;
}