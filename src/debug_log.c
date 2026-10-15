#include "debug_log.h"
#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#endif

static FILE* s_log_file;

static void make_log_path(char* out, size_t size) {
#if defined(__ORBIS__) || defined(PS4)
    /* Match the writable path used by PS4-SDL-Template. */
    snprintf(out, size, "/data/SDL_Log.txt");
    return;
#elif defined(_WIN32)
    char exe[MAX_PATH * 4];
    DWORD n = GetModuleFileNameA(NULL, exe, (DWORD) sizeof(exe));
    if (n > 0 && n < sizeof(exe)) {
        exe[n] = '\0';
        char* slash1 = strrchr(exe, '\\');
        char* slash2 = strrchr(exe, '/');
        char* slash = slash1;
        if (!slash || (slash2 && slash2 > slash)) slash = slash2;
        if (slash) slash[1] = '\0';
        else exe[0] = '\0';
        snprintf(out, size, "%smain_errors.log", exe);
        return;
    }
#endif
    snprintf(out, size, "main_errors.log");
}

void debug_log_init(void) {
    if (s_log_file) return;
    char path[4096];
    make_log_path(path, sizeof(path));
    s_log_file = fopen(path, "a");
#if defined(__ORBIS__) || defined(PS4)
    if (!s_log_file) s_log_file = fopen("/data/SonicAdvanceTrilogy/main_errors.log", "a");
#endif
    if (!s_log_file && strcmp(path, "main_errors.log") != 0) s_log_file = fopen("main_errors.log", "a");
    if (s_log_file) {
        time_t now = time(NULL);
        struct tm tm_storage;
        struct tm* tm_now;
#if defined(_WIN32)
        tm_now = localtime_s(&tm_storage, &now) == 0 ? &tm_storage : NULL;
#else
        tm_now = localtime_r(&now, &tm_storage);
#endif
        char stamp[32] = "fecha desconocida";
        if (tm_now) strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", tm_now);
        fprintf(s_log_file, "\n===== Inicio de sesión %s =====\n", stamp);
        fflush(s_log_file);
    }
}

static void vlog(const char* level, const char* fmt, va_list ap) {
    time_t now = time(NULL);
    struct tm tm_storage;
    struct tm* tm_now;
#if defined(_WIN32)
    tm_now = localtime_s(&tm_storage, &now) == 0 ? &tm_storage : NULL;
#else
    tm_now = localtime_r(&now, &tm_storage);
#endif
    char stamp[32] = "00:00:00";
    if (tm_now) strftime(stamp, sizeof(stamp), "%H:%M:%S", tm_now);

    va_list copy;
    va_copy(copy, ap);
    fprintf(stderr, "[%s] [%s] ", stamp, level);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    fflush(stderr);

    if (s_log_file) {
        fprintf(s_log_file, "[%s] [%s] ", stamp, level);
        vfprintf(s_log_file, fmt, copy);
        fputc('\n', s_log_file);
        fflush(s_log_file);
    }
    va_end(copy);
}

void debug_log_info(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); vlog("INFO", fmt, ap); va_end(ap);
}
void debug_log_warn(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); vlog("WARN", fmt, ap); va_end(ap);
}
void debug_log_error(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); vlog("ERROR", fmt, ap); va_end(ap);
}
void debug_log_close(void) {
    if (s_log_file) {
        debug_log_info("Fin de sesión");
        fclose(s_log_file);
        s_log_file = NULL;
    }
}
