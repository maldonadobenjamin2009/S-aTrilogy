/* emu.c - mGBA integrado y coordinación de dos núcleos GBA por cable link. */
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/core/serialize.h>
#include <mgba/core/version.h>
#include <mgba/core/thread.h>
#include <mgba/core/sync.h>
#include <mgba/core/lockstep.h>
#include <mgba/gba/core.h>
#include <mgba/gba/interface.h>
#include <mgba/internal/gba/sio/lockstep.h>
#include <mgba-util/audio-buffer.h>
#include <mgba-util/audio-resampler.h>
#include <mgba-util/vfs.h>
#include <mgba-util/threading.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>

#include "emu.h"
#include "debug_log.h"
#ifndef EMU_INTERP
#define EMU_INTERP mINTERPOLATOR_SINC
#endif
#define VIDEO_W EMU_W
#define VIDEO_H EMU_H

struct LinkSession;
struct Emu {
	struct mCore* core;
	mColor video[VIDEO_W * VIDEO_H];
	mColor videoSnapshot[VIDEO_W * VIDEO_H];
	struct mAudioBuffer out;
	struct mAudioResampler resampler;
	unsigned outRate;
	unsigned srcRate;
	int audioReady;
	Mutex frameMutex;
	Mutex audioMutex;
	int frameMutexReady;
	int audioMutexReady;
	uint64_t nextFrameDeadlineUs;
	uint64_t fpsReportStartUs;
	unsigned fpsReportFrames;

	/* Solo se usa en modo enlazado. La instancia primaria es propietaria del peer. */
	struct Emu* peer;
	struct LinkSession* link;
	struct mCoreThread thread;
	struct mLockstepThreadUser lockstepUser;
	struct GBASIOLockstepDriver linkDriver;
	int playerId;
	int threadStarted;
	int driverAttached;
};

struct LinkSession {
	struct GBASIOLockstepCoordinator coordinator;
};

const char* emu_version(void) {
	return projectVersion;
}

static void mgba_log_sink(struct mLogger* logger, int category, enum mLogLevel level,
                          const char* format, va_list args) {
	(void) logger;
	/* The linked lockstep driver can emit frequent DEBUG events. Discard them
	 * before formatting, and especially never forward them to the Windows CMD;
	 * synchronous console output was a major source of multiplayer slowdown. */
	if (!(level & (mLOG_FATAL | mLOG_ERROR | mLOG_GAME_ERROR | mLOG_WARN))) return;
	char message[2048];
	vsnprintf(message, sizeof(message), format, args);
	const char* name = mLogCategoryName(category);
	if (level & (mLOG_FATAL | mLOG_ERROR | mLOG_GAME_ERROR)) {
		debug_log_error("mGBA [%s]: %s", name ? name : "general", message);
	} else {
		debug_log_warn("mGBA [%s]: %s", name ? name : "general", message);
	}
}

static struct mLogger s_mgba_logger = { mgba_log_sink, NULL };

void emu_enable_debug_logging(void) {
	mLogSetDefaultLogger(&s_mgba_logger);
	debug_log_info("Registro de errores y avisos internos de mGBA activado");
}

static void emu_process_audio(Emu* emu);

/* Reloj monotónico y pacing independiente por núcleo. No sincroniza los FPS de J1/J2;
 * el coordinador mGBA conserva únicamente la coordinación temporal necesaria para SIO. */
static uint64_t emu_monotonic_us(void) {
#ifdef _WIN32
	LARGE_INTEGER frequency;
	LARGE_INTEGER counter;
	/* Evitar inicialización estática compartida entre los dos hilos GBA. */
	if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) return GetTickCount64() * 1000ULL;
	QueryPerformanceCounter(&counter);
	return (uint64_t) (counter.QuadPart / frequency.QuadPart) * 1000000ULL +
	       (uint64_t) (counter.QuadPart % frequency.QuadPart) * 1000000ULL / (uint64_t) frequency.QuadPart;
#else
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return (uint64_t) time(NULL) * 1000000ULL;
	return (uint64_t) ts.tv_sec * 1000000ULL + (uint64_t) ts.tv_nsec / 1000ULL;
#endif
}

static void emu_sleep_us(uint64_t usec) {
	if (!usec) return;
#ifdef _WIN32
	/* Sleep tiene resolución de milisegundos; deja margen pequeño para no sobrepasar mucho. */
	if (usec >= 1500) Sleep((DWORD) ((usec - 500) / 1000));
	else Sleep(0);
#else
	struct timespec req;
	req.tv_sec = (time_t) (usec / 1000000ULL);
	req.tv_nsec = (long) ((usec % 1000000ULL) * 1000ULL);
	while (nanosleep(&req, &req) != 0 && errno == EINTR) { }
#endif
}

static void emu_pace_linked_core(Emu* emu) {
	const uint64_t frame_us = 1000000ULL / 60ULL;
	uint64_t now = emu_monotonic_us();
	if (!emu->nextFrameDeadlineUs) {
		emu->nextFrameDeadlineUs = now + frame_us;
	} else if (emu->nextFrameDeadlineUs + frame_us * 3 < now) {
		/* Si el cable retuvo un núcleo, dejarlo recuperar un fotograma sin acumular retraso. */
		emu->nextFrameDeadlineUs = now;
	} else {
		emu->nextFrameDeadlineUs += frame_us;
	}
	while ((now = emu_monotonic_us()) < emu->nextFrameDeadlineUs) {
		emu_sleep_us(emu->nextFrameDeadlineUs - now);
	}
}

static void linked_frame_callback(struct mCoreThread* thread) {
	Emu* emu = thread ? (Emu*) thread->userData : NULL;
	if (!emu || !emu->core) return;
	if (emu->frameMutexReady) MutexLock(&emu->frameMutex);
	memcpy(emu->videoSnapshot, emu->video, sizeof(emu->videoSnapshot));
	if (emu->frameMutexReady) MutexUnlock(&emu->frameMutex);

	uint64_t measuredAt = emu_monotonic_us();
	if (!emu->fpsReportStartUs) emu->fpsReportStartUs = measuredAt;
	++emu->fpsReportFrames;
	if (measuredAt - emu->fpsReportStartUs >= 5000000ULL) {
		double fps = (double) emu->fpsReportFrames * 1000000.0 /
		             (double) (measuredAt - emu->fpsReportStartUs);
		debug_log_info("Cable link J%d: %.1f FPS medidos en su hilo independiente", emu->playerId + 1, fps);
		emu->fpsReportStartUs = measuredAt;
		emu->fpsReportFrames = 0;
	}
	if (emu->playerId == 0) {
		emu_process_audio(emu);
	} else if (emu->audioMutexReady) {
		/* J2 descarta su audio desde su propio hilo; nunca compite con la cola audible de J1. */
		MutexLock(&emu->audioMutex);
		mAudioBufferClear(emu->core->getAudioBuffer(emu->core));
		MutexUnlock(&emu->audioMutex);
	}
	emu_pace_linked_core(emu);
}


static Emu* emu_open_internal(const char* rom_path, const char* save_path,
                              const char* bios_path, unsigned out_rate, int reset_now) {
	if (!rom_path || !out_rate) return NULL;
	struct VFile* rom = VFileOpen(rom_path, O_RDONLY);
	if (!rom) { debug_log_error("No se pudo abrir la ROM: %s", rom_path); return NULL; }
	struct mCore* core = mCoreFindVF(rom);
	if (!core || core->platform(core) != mPLATFORM_GBA) {
		debug_log_error("La ROM no es una ROM GBA válida: %s", rom_path);
		rom->close(rom);
		if (core) core->deinit(core);
		return NULL;
	}

	Emu* emu = calloc(1, sizeof(*emu));
	if (!emu) {
		rom->close(rom);
		core->deinit(core);
		return NULL;
	}
	emu->core = core;
	emu->outRate = out_rate;
	if (MutexInit(&emu->frameMutex) != 0) {
		free(emu); rom->close(rom); core->deinit(core); return NULL;
	}
	emu->frameMutexReady = 1;
	if (MutexInit(&emu->audioMutex) != 0) {
		MutexDeinit(&emu->frameMutex); free(emu); rom->close(rom); core->deinit(core); return NULL;
	}
	emu->audioMutexReady = 1;

	mCoreInitConfig(core, NULL);
	core->init(core);
	core->setVideoBuffer(core, emu->video, VIDEO_W);
	core->setAudioBufferSize(core, 2048);
	emu->srcRate = core->audioSampleRate(core);
	mAudioBufferInit(&emu->out, 8192, 2);
	mAudioResamplerInit(&emu->resampler, EMU_INTERP);
	mAudioResamplerSetDestination(&emu->resampler, &emu->out, out_rate);
	mAudioResamplerSetSource(&emu->resampler, core->getAudioBuffer(core), emu->srcRate, true);
	emu->audioReady = 1;

	mCoreConfigSetDefaultIntValue(&core->config, "useBios", (bios_path && bios_path[0]) ? 1 : 0);
	mCoreConfigSetDefaultIntValue(&core->config, "skipBios", 1);
	mCoreConfigSetDefaultIntValue(&core->config, "idleOptimization", 1);
	/* Restaurar el volumen de salida de mGBA: el valor por defecto del núcleo puede ser 0. */
	mCoreConfigSetDefaultIntValue(&core->config, "volume", 0x100);
	mCoreConfigSetDefaultIntValue(&core->config, "mute", 0);
	mCoreLoadForeignConfig(core, &core->config);

	if (!core->loadROM(core, rom)) {
		debug_log_error("mGBA no pudo cargar la ROM: %s", rom_path);
		rom->close(rom);
		emu_close(emu);
		return NULL;
	}
	if (bios_path && bios_path[0]) {
		struct VFile* bios = VFileOpen(bios_path, O_RDONLY);
		if (bios && !core->loadBIOS(core, bios, 0)) bios->close(bios);
	}
	if (save_path && save_path[0]) {
		struct VFile* sav = VFileOpen(save_path, O_CREAT | O_RDWR);
		if (sav && !core->loadSave(core, sav)) sav->close(sav);
	}
	if (reset_now) core->reset(core);
	return emu;
}

Emu* emu_open(const char* rom_path, const char* save_path, const char* bios_path, unsigned out_rate) {
	return emu_open_internal(rom_path, save_path, bios_path, out_rate, 1);
}

static void safe_lockstep_sleep(struct mLockstepUser* user) {
	struct mLockstepThreadUser* tu = (struct mLockstepThreadUser*) user;
	if (tu->thread && tu->thread->impl) mCoreThreadWaitFromThread(tu->thread);
}
static void safe_lockstep_wake(struct mLockstepUser* user) {
	struct mLockstepThreadUser* tu = (struct mLockstepThreadUser*) user;
	if (tu->thread && tu->thread->impl) mCoreThreadStopWaiting(tu->thread);
}
static int link_requested_id(struct mLockstepUser* user) {
	struct mLockstepThreadUser* tu = (struct mLockstepThreadUser*) user;
	Emu* emu = tu->thread ? (Emu*) tu->thread->userData : NULL;
	return emu ? emu->playerId : -1;
}
static void emu_process_audio(Emu* emu) {
	if (!emu || !emu->core || !emu->audioReady) return;
	if (emu->audioMutexReady) MutexLock(&emu->audioMutex);
	unsigned rate = emu->core->audioSampleRate(emu->core);
	if (rate != emu->srcRate) {
		emu->srcRate = rate;
		mAudioResamplerSetSource(&emu->resampler, emu->core->getAudioBuffer(emu->core), rate, true);
	}
	mAudioResamplerProcess(&emu->resampler);
	if (emu->audioMutexReady) MutexUnlock(&emu->audioMutex);
}

static void close_linked(Emu* primary) {
	if (!primary) return;
	Emu* secondary = primary->peer;
	struct LinkSession* link = primary->link;
	if (secondary && link) {
		/* End también libera a los hilos que estén esperando al frontend en VBlank.
		 * Es importante finalizar ambos antes de quitar drivers de cable link. */
		if (primary->threadStarted) mCoreThreadEnd(&primary->thread);
		if (secondary->threadStarted) mCoreThreadEnd(&secondary->thread);
		if (primary->threadStarted) mCoreThreadJoin(&primary->thread);
		if (secondary->threadStarted) mCoreThreadJoin(&secondary->thread);
		if (primary->driverAttached) {
			primary->core->setPeripheral(primary->core, mPERIPH_GBA_LINK_PORT, NULL);
			GBASIOLockstepCoordinatorDetach(&link->coordinator, &primary->linkDriver);
			primary->driverAttached = 0;
		}
		if (secondary->driverAttached) {
			secondary->core->setPeripheral(secondary->core, mPERIPH_GBA_LINK_PORT, NULL);
			GBASIOLockstepCoordinatorDetach(&link->coordinator, &secondary->linkDriver);
			secondary->driverAttached = 0;
		}
		GBASIOLockstepCoordinatorDeinit(&link->coordinator);
		free(link);
		primary->peer = NULL;
		secondary->peer = NULL;
		primary->link = NULL;
		secondary->link = NULL;
		if (secondary->audioReady) {
			mAudioResamplerDeinit(&secondary->resampler);
			mAudioBufferDeinit(&secondary->out);
			secondary->audioReady = 0;
		}
		if (secondary->core) {
			mCoreConfigDeinit(&secondary->core->config);
			secondary->core->unloadROM(secondary->core);
			secondary->core->deinit(secondary->core);
			secondary->core = NULL;
		}
		if (secondary->audioMutexReady) MutexDeinit(&secondary->audioMutex);
		if (secondary->frameMutexReady) MutexDeinit(&secondary->frameMutex);
		free(secondary);
	}
	if (primary->audioReady) {
		mAudioResamplerDeinit(&primary->resampler);
		mAudioBufferDeinit(&primary->out);
		primary->audioReady = 0;
	}
	if (primary->core) {
		mCoreConfigDeinit(&primary->core->config);
		primary->core->unloadROM(primary->core);
		primary->core->deinit(primary->core);
		primary->core = NULL;
	}
	if (primary->audioMutexReady) MutexDeinit(&primary->audioMutex);
	if (primary->frameMutexReady) MutexDeinit(&primary->frameMutex);
	free(primary);
}
Emu* emu_open_linked_pair(const char* rom_path, const char* save_path,
                          const char* save_path2, const char* bios_path,
                          unsigned out_rate, Emu** secondary_out) {
	if (secondary_out) *secondary_out = NULL;
	debug_log_info("Creando pareja mGBA con cable link para ROM: %s", rom_path ? rom_path : "(nula)");
	Emu* first = emu_open_internal(rom_path, save_path, bios_path, out_rate, 0);
	if (!first) { debug_log_error("Falló la inicialización de J1/cable link"); return NULL; }
	Emu* second = emu_open_internal(rom_path, save_path2, bios_path, out_rate, 0);
	if (!second) {
		debug_log_error("Falló la inicialización de J2/cable link");
		emu_close(first);
		return NULL;
	}
	struct LinkSession* link = calloc(1, sizeof(*link));
	if (!link) {
		debug_log_error("No hay memoria para crear la sesión de cable link");
		emu_close(first);
		emu_close(second);
		return NULL;
	}
	GBASIOLockstepCoordinatorInit(&link->coordinator);
	first->peer = second;
	first->link = second->link = link;
	first->playerId = 0;
	second->playerId = 1;

	/* Cada núcleo administra sus propios fotogramas en su hilo. Se desactiva la
	 * barrera de vídeo del frontend (que serializaba J1 y J2); el callback copia
	 * cada imagen y limita cada emulador a ~60 FPS de forma independiente.
	 * El cable link conserva la sincronización de ciclos/eventos de SIO requerida
	 * por mGBA, pero ya no sincroniza los fotogramas con la ventana. */
	first->core->opts.videoSync = false;
	first->core->opts.audioSync = false;
	second->core->opts.videoSync = false;
	second->core->opts.audioSync = false;

	first->thread.core = first->core;
	second->thread.core = second->core;
	first->thread.userData = first;
	second->thread.userData = second;
	first->thread.frameCallback = linked_frame_callback;
	second->thread.frameCallback = linked_frame_callback;
	/* Route both worker loggers through the frontend sink. Without this, mGBA's
	 * thread wrapper falls back to printf() for every lockstep DEBUG event. */
	first->thread.logger.logger = &s_mgba_logger;
	second->thread.logger.logger = &s_mgba_logger;
	mLockstepThreadUserInit(&first->lockstepUser, &first->thread);
	mLockstepThreadUserInit(&second->lockstepUser, &second->thread);
	first->lockstepUser.d.sleep = safe_lockstep_sleep;
	first->lockstepUser.d.wake = safe_lockstep_wake;
	second->lockstepUser.d.sleep = safe_lockstep_sleep;
	second->lockstepUser.d.wake = safe_lockstep_wake;
	first->lockstepUser.d.requestedId = link_requested_id;
	second->lockstepUser.d.requestedId = link_requested_id;
	GBASIOLockstepDriverCreate(&first->linkDriver, &first->lockstepUser.d);
	GBASIOLockstepDriverCreate(&second->linkDriver, &second->lockstepUser.d);
	GBASIOLockstepCoordinatorAttach(&link->coordinator, &first->linkDriver);
	GBASIOLockstepCoordinatorAttach(&link->coordinator, &second->linkDriver);
	first->core->setPeripheral(first->core, mPERIPH_GBA_LINK_PORT, &first->linkDriver.d);
	second->core->setPeripheral(second->core, mPERIPH_GBA_LINK_PORT, &second->linkDriver.d);
	first->driverAttached = second->driverAttached = 1;

	if (!mCoreThreadStart(&first->thread)) {
		debug_log_error("mGBA no pudo iniciar el hilo de J1");
		close_linked(first);
		return NULL;
	}
	first->threadStarted = 1;
	if (!mCoreThreadStart(&second->thread)) {
		debug_log_error("mGBA no pudo iniciar el hilo de J2");
		close_linked(first);
		return NULL;
	}
	second->threadStarted = 1;
	/* Los snapshots se actualizan desde los callbacks de cada hilo; el frontend no
	 * debe establecer una barrera por VBlank entre los dos núcleos. */
	if (secondary_out) *secondary_out = second;
	debug_log_info("Cable link listo: J1 izquierda, J2 derecha; pacing de vídeo independiente; audio de J1 prioritario");
	return first;
}


void emu_close(Emu* emu) {
	if (!emu) return;
	if (emu->peer) {
		close_linked(emu);
		return;
	}
	/* La instancia secundaria es propietaria del enlace solo de manera no dueña.
	 * La aplicación cierra siempre el par desde J1. */
	if (emu->link) {
		/* No liberar una sesión desde J2 por accidente; desacoplarla del enlace. */
		return;
	}
	if (emu->audioReady) {
		mAudioResamplerDeinit(&emu->resampler);
		mAudioBufferDeinit(&emu->out);
	}
	if (emu->core) {
		mCoreConfigDeinit(&emu->core->config);
		emu->core->unloadROM(emu->core);
		emu->core->deinit(emu->core);
	}
	if (emu->audioMutexReady) MutexDeinit(&emu->audioMutex);
	if (emu->frameMutexReady) MutexDeinit(&emu->frameMutex);
	free(emu);
}

void emu_set_paused(Emu* emu, int paused) {
	if (!emu || !emu->peer || !emu->link || !emu->thread.impl || !emu->peer->thread.impl) return;
	Emu* second = emu->peer;
	if (paused) {
		mCoreThreadPauseFromThread(&emu->thread);
		mCoreThreadPauseFromThread(&second->thread);
		mCoreThreadPause(&emu->thread);
		mCoreThreadPause(&second->thread);
	} else {
		mCoreThreadUnpause(&emu->thread);
		mCoreThreadUnpause(&second->thread);
	}
}

void emu_reset(Emu* emu) {
	if (!emu) return;
	int linked = emu->peer && emu->link;
	if (linked) {
		mCoreThreadReset(&emu->thread);
		mCoreThreadReset(&emu->peer->thread);
	} else {
		emu->core->reset(emu->core);
	}
	if (emu->audioMutexReady) MutexLock(&emu->audioMutex);
	mAudioBufferClear(&emu->out);
	/* En pareja el buffer fuente pertenece al worker: no tocarlo desde el hilo UI. */
	if (!linked) mAudioBufferClear(emu->core->getAudioBuffer(emu->core));
	if (emu->audioMutexReady) MutexUnlock(&emu->audioMutex);
	if (emu->peer) {
		if (emu->peer->audioMutexReady) MutexLock(&emu->peer->audioMutex);
		mAudioBufferClear(&emu->peer->out);
		if (emu->peer->audioMutexReady) MutexUnlock(&emu->peer->audioMutex);
	}
}

void emu_set_keys(Emu* emu, uint32_t keys) {
	if (emu && emu->core) emu->core->setKeys(emu->core, keys);
}

void emu_run_frame(Emu* emu) {
	if (!emu || !emu->core) return;
	if (emu->peer && emu->link) {
		Emu* second = emu->peer;
		/* Poll no bloqueante: el callback de cada hilo ya actualizó su snapshot
		 * al terminar el fotograma. Limpiamos el contador interno de mGBA, sin
		 * esperar a J1 antes de permitir que J2 siga avanzando (o viceversa). */
		if (emu->thread.impl) {
			struct mCoreSync* sync = &emu->thread.impl->sync;
			(void) mCoreSyncWaitFrameStart(sync);
			mCoreSyncWaitFrameEnd(sync);
		}
		if (second->thread.impl) {
			struct mCoreSync* sync = &second->thread.impl->sync;
			(void) mCoreSyncWaitFrameStart(sync);
			mCoreSyncWaitFrameEnd(sync);
		}
		return;
	}
	emu->core->runFrame(emu->core);
	memcpy(emu->videoSnapshot, emu->video, sizeof(emu->videoSnapshot));
	emu_process_audio(emu);
}

const uint32_t* emu_video(Emu* emu) {
	/* Compatibilidad con llamadas antiguas; el frontend concurrente debe usar emu_copy_video. */
	return emu ? (const uint32_t*) emu->videoSnapshot : NULL;
}

void emu_copy_video(Emu* emu, uint32_t* dst) {
	if (!emu || !dst) return;
	if (emu->frameMutexReady) MutexLock(&emu->frameMutex);
	memcpy(dst, emu->videoSnapshot, sizeof(emu->videoSnapshot));
	if (emu->frameMutexReady) MutexUnlock(&emu->frameMutex);
}

int emu_read_audio(Emu* emu, int16_t* dst, int max_frames) {
	if (!emu || !dst || max_frames <= 0) return 0;
	if (emu->audioMutexReady) MutexLock(&emu->audioMutex);
	int n = (int) mAudioBufferRead(&emu->out, dst, (size_t) max_frames);
	if (emu->audioMutexReady) MutexUnlock(&emu->audioMutex);
	return n;
}

void emu_set_audio_rate(Emu* emu, double rate) {
	if (!emu || !emu->audioReady || rate <= 0.0) return;
	if (emu->audioMutexReady) MutexLock(&emu->audioMutex);
	mAudioResamplerSetDestination(&emu->resampler, &emu->out, rate);
	if (emu->audioMutexReady) MutexUnlock(&emu->audioMutex);
}

void emu_clear_audio(Emu* emu) {
	if (!emu || !emu->core) return;
	if (emu->audioMutexReady) MutexLock(&emu->audioMutex);
	mAudioBufferClear(&emu->out);
	/* In linked mode the core-owned source buffer is cleared by that core's callback. */
	if (!emu->link) mAudioBufferClear(emu->core->getAudioBuffer(emu->core));
	if (emu->audioMutexReady) MutexUnlock(&emu->audioMutex);
}

static int save_state_one(Emu* emu, const char* path) {
	struct VFile* vf = VFileOpen(path, O_CREAT | O_TRUNC | O_RDWR);
	if (!vf) return 0;
	bool ok = mCoreSaveStateNamed(emu->core, vf, SAVESTATE_SAVEDATA | SAVESTATE_SCREENSHOT | SAVESTATE_RTC | SAVESTATE_METADATA);
	vf->close(vf);
	return ok ? 1 : 0;
}
static int load_state_one(Emu* emu, const char* path) {
	struct VFile* vf = VFileOpen(path, O_RDONLY);
	if (!vf) return 0;
	bool ok = mCoreLoadStateNamed(emu->core, vf, SAVESTATE_SAVEDATA | SAVESTATE_RTC);
	vf->close(vf);
	if (ok) mAudioBufferClear(&emu->out);
	return ok ? 1 : 0;
}
static void peer_state_path(const char* path, char* out, size_t size) {
	snprintf(out, size, "%s.p2", path);
}
int emu_save_state(Emu* emu, const char* path) {
	if (!emu || !path) return 0;
	if (!save_state_one(emu, path)) return 0;
	if (emu->peer) {
		char p2[1200]; peer_state_path(path, p2, sizeof(p2));
		return save_state_one(emu->peer, p2);
	}
	return 1;
}
int emu_load_state(Emu* emu, const char* path) {
	if (!emu || !path) return 0;
	if (emu->peer) {
		char p2[1200]; peer_state_path(path, p2, sizeof(p2));
		/* Comprobar los dos archivos antes de mutar el estado de ninguna CPU. */
		struct VFile* vf2 = VFileOpen(p2, O_RDONLY);
		if (!vf2) return 0;
		vf2->close(vf2);
		if (!load_state_one(emu, path)) return 0;
		if (!load_state_one(emu->peer, p2)) return 0;
		mAudioBufferClear(&emu->peer->out);
		mAudioBufferClear(emu->peer->core->getAudioBuffer(emu->peer->core));
		return 1;
	}
	return load_state_one(emu, path);
}
