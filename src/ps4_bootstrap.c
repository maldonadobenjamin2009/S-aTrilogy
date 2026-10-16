/*
 * Early PS4 startup diagnostics for Sonic Advance Trilogy.
 *
 * A plain constructor writes a log before main() or SDL_Init(), installs fatal
 * exception logging, loads the system modules used by the OpenOrbis samples,
 * hides the launch splash when possible, and keeps a frame-rate watchdog.
 */
#include "ps4_bootstrap.h"

#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int sceKernelMkdir(const char *path, int mode);
int sceKernelInstallExceptionHandler(int signal, void (*handler)(int, void *));
uint32_t sceKernelLoadStartModule(const char *path, size_t argc, const void *argv,
                                  uint32_t flags, void *opt, void *result);
const char *sceKernelGetFsSandboxRandomWord(void);
int32_t sceSystemServiceHideSplashScreen(void);

#define PS4_LOG_PATH "/data/SonicAdvanceTrilogy/ps4_startup.log"
#define PS4_FALLBACK_LOG_PATH "/data/ps4_startup.log"

static volatile uint64_t s_frame_count;
static int s_bootstrap_initialized;

static void ps4_log_v(const char *fmt, va_list args)
{
    char message[900];
    char line[1200];
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    vsnprintf(message, sizeof(message), fmt, args);
    int n = snprintf(line, sizeof(line), "[%llu.%03ld] %s\n",
                     (unsigned long long)now.tv_sec, now.tv_nsec / 1000000L, message);
    if (n <= 0) return;
    size_t length = (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1;

    const char *paths[] = { PS4_LOG_PATH, PS4_FALLBACK_LOG_PATH };
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        int fd = open(paths[i], O_WRONLY | O_CREAT | O_APPEND, 0666);
        if (fd < 0) continue;
        size_t offset = 0;
        while (offset < length) {
            ssize_t written = write(fd, line + offset, length - offset);
            if (written <= 0) break;
            offset += (size_t)written;
        }
        close(fd);
        return;
    }
}

void ps4_bootstrap_log(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    ps4_log_v(fmt, args);
    va_end(args);
}

static void ps4_fatal_exception(int signal_number, void *context)
{
    /* OpenOrbis uses a FreeBSD amd64 ucontext layout; save RIP if it is present. */
    uintptr_t rip = 0;
    if (context != NULL) {
        memcpy(&rip, (const unsigned char *)context + 224, sizeof(rip));
    }
    ps4_bootstrap_log("FATAL exception signal=%d context=%p rip=0x%llx",
                      signal_number, context, (unsigned long long)rip);
    _exit(128 + signal_number);
}

__attribute__((constructor))
static void ps4_bootstrap_constructor(void)
{
    /* Do not assign a constructor priority: OpenOrbis' linker script keeps the
       normal .init_array, while some priority sections can be dropped. */
    int mkdir_result = sceKernelMkdir("/data/SonicAdvanceTrilogy", 0777);
    ps4_bootstrap_log("constructor ran before main; data-dir mkdir=0x%08x",
                      (unsigned)mkdir_result);
    int segv = sceKernelInstallExceptionHandler(SIGSEGV, ps4_fatal_exception);
    int bus  = sceKernelInstallExceptionHandler(SIGBUS, ps4_fatal_exception);
    int ill  = sceKernelInstallExceptionHandler(SIGILL, ps4_fatal_exception);
    int fpe  = sceKernelInstallExceptionHandler(SIGFPE, ps4_fatal_exception);
    ps4_bootstrap_log("exception handlers: SEGV=%d BUS=%d ILL=%d FPE=%d",
                      segv, bus, ill, fpe);
}

static void *ps4_watchdog(void *unused)
{
    (void)unused;
    uint64_t previous = 0;
    for (;;) {
        sleep(10);
        uint64_t current = __atomic_load_n(&s_frame_count, __ATOMIC_RELAXED);
        ps4_bootstrap_log("watchdog: %.1f fps over 10s; total frames=%llu",
                          (double)(current - previous) / 10.0,
                          (unsigned long long)current);
        if (current == 0) {
            ps4_bootstrap_log("watchdog: no frame has reached the presentation loop yet");
        }
        previous = current;
    }
    return NULL;
}

void ps4_bootstrap_init(void)
{
    if (s_bootstrap_initialized) return;
    s_bootstrap_initialized = 1;

    ps4_bootstrap_log("main entered; loading OpenOrbis runtime modules");
    const char *sandbox = sceKernelGetFsSandboxRandomWord();
    if (sandbox == NULL || sandbox[0] == '\0') sandbox = "system";

    /* This follows the OpenOrbis sample setup. Some modules may already be
       loaded by the process; failures are logged and are not fatal by themselves. */
    static const char *const modules[] = {
        "libSceSysCore", "libSceMbus", "libSceIpmi", "libSceSystemService",
        "libSceUserService", "libSceAudioOut", "libScePad", "libSceVideoOut"
    };
    for (size_t i = 0; i < sizeof(modules) / sizeof(modules[0]); ++i) {
        char path[256];
        snprintf(path, sizeof(path), "/%s/common/lib/%s.sprx", sandbox, modules[i]);
        uint32_t module_result = 0;
        uint32_t result = sceKernelLoadStartModule(path, 0, NULL, 0, NULL, &module_result);
        ps4_bootstrap_log("load %s => 0x%08x (module result=0x%08x)",
                          path, result, module_result);
    }

    int splash_result = sceSystemServiceHideSplashScreen();
    ps4_bootstrap_log("sceSystemServiceHideSplashScreen => 0x%08x",
                      (unsigned)splash_result);

    pthread_t watchdog;
    int thread_result = pthread_create(&watchdog, NULL, ps4_watchdog, NULL);
    if (thread_result == 0) {
        pthread_detach(watchdog);
        ps4_bootstrap_log("startup watchdog thread started");
    } else {
        ps4_bootstrap_log("WARNING: watchdog pthread_create failed: %d", thread_result);
    }
}

void ps4_bootstrap_note_frame(void)
{
    __atomic_add_fetch(&s_frame_count, 1, __ATOMIC_RELAXED);
}
