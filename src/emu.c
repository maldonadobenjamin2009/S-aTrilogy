/* emu.c - mGBA integrado y coordinación de dos núcleos GBA por cable link. */
#include <mgba/core/core.h>
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

#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>

#include "emu.h"
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

static Emu* emu_open_internal(const char* rom_path, const char* save_path,
                              const char* bios_path, unsigned out_rate, int reset_now) {
	if (!rom_path || !out_rate) return NULL;
	struct VFile* rom = VFileOpen(rom_path, O_RDONLY);
	if (!rom) return NULL;
	struct mCore* core = mCoreFindVF(rom);
	if (!core || core->platform(core) != mPLATFORM_GBA) {
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

	mCoreInitConfig(core, NULL);
	core->init(core);
	core->setVideoBuffer(core, emu->video, VIDEO_W);
	core->setAudioBufferSize(core, 2048);
	emu->srcRate = core->audioSampleRate(core);
	mAudioBufferInit(&emu->out, 8192, 2);
	mAudioResamplerInit(&emu->resampler, mINTERPOLATOR_COSINE);
	mAudioResamplerSetDestination(&emu->resampler, &emu->out, out_rate);
	mAudioResamplerSetSource(&emu->resampler, core->getAudioBuffer(core), emu->srcRate, true);
	emu->audioReady = 1;

	mCoreConfigSetDefaultIntValue(&core->config, "useBios", (bios_path && bios_path[0]) ? 1 : 0);
	mCoreConfigSetDefaultIntValue(&core->config, "skipBios", 1);
	mCoreConfigSetDefaultIntValue(&core->config, "idleOptimization", 1);
	core->loadConfig(core, &core->config);

	if (!core->loadROM(core, rom)) {
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
	free(primary);
}

Emu* emu_open_linked_pair(const char* rom_path, const char* save_path,
                          const char* save_path2, const char* bios_path,
                          unsigned out_rate, Emu** secondary_out) {
	if (secondary_out) *secondary_out = NULL;
	Emu* first = emu_open_internal(rom_path, save_path, bios_path, out_rate, 0);
	if (!first) return NULL;
	Emu* second = emu_open_internal(rom_path, save_path2, bios_path, out_rate, 0);
	if (!second) {
		emu_close(first);
		return NULL;
	}
	struct LinkSession* link = calloc(1, sizeof(*link));
	if (!link) {
		emu_close(first);
		emu_close(second);
		return NULL;
	}
	GBASIOLockstepCoordinatorInit(&link->coordinator);
	first->peer = second;
	first->link = second->link = link;
	first->playerId = 0;
	second->playerId = 1;

	/* El hilo del núcleo espera al frontend en cada VBlank. Así las dos
	 * instancias avanzan al ritmo de la ventana sin llenar buffers a toda velocidad. */
	first->core->opts.videoSync = true;
	first->core->opts.audioSync = false;
	second->core->opts.videoSync = true;
	second->core->opts.audioSync = false;

	first->thread.core = first->core;
	second->thread.core = second->core;
	first->thread.userData = first;
	second->thread.userData = second;
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
		close_linked(first);
		return NULL;
	}
	first->threadStarted = 1;
	if (!mCoreThreadStart(&second->thread)) {
		close_linked(first);
		return NULL;
	}
	second->threadStarted = 1;
	/* No esperar un VBlank aquí: el frontend debe empezar a esperar el primer
	 * fotograma inmediatamente para no perder la notificación de mCoreSync. */
	if (secondary_out) *secondary_out = second;
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
	free(emu);
}

void emu_reset(Emu* emu) {
	if (!emu) return;
	if (emu->peer && emu->link) {
		mCoreThreadReset(&emu->thread);
		mCoreThreadReset(&emu->peer->thread);
	} else {
		emu->core->reset(emu->core);
	}
	mAudioBufferClear(&emu->out);
	mAudioBufferClear(emu->core->getAudioBuffer(emu->core));
	if (emu->peer) {
		mAudioBufferClear(&emu->peer->out);
		mAudioBufferClear(emu->peer->core->getAudioBuffer(emu->peer->core));
	}
}

void emu_set_keys(Emu* emu, uint32_t keys) {
	if (emu && emu->core) emu->core->setKeys(emu->core, keys);
}

void emu_run_frame(Emu* emu) {
	if (!emu || !emu->core) return;
	if (emu->peer && emu->link) {
		Emu* second = emu->peer;
		struct mCoreSync* sync1 = &emu->thread.impl->sync;
		struct mCoreSync* sync2 = &second->thread.impl->sync;
		/* Consume the players separately, releasing each core immediately after
		 * copying its completed framebuffer. mGBA's lockstep scheduler keeps SIO
		 * clocks aligned; neither core is force-paused by our frame callback. */
		bool ready1 = mCoreSyncWaitFrameStart(sync1);
		if (ready1) {
			memcpy(emu->videoSnapshot, emu->video, sizeof(emu->videoSnapshot));
			mAudioResamplerProcess(&emu->resampler);
		}
		mCoreSyncWaitFrameEnd(sync1);
		bool ready2 = mCoreSyncWaitFrameStart(sync2);
		if (ready2) {
			memcpy(second->videoSnapshot, second->video, sizeof(second->videoSnapshot));
			/* J2 jamás envía audio al dispositivo anfitrión. Descartamos su mezcla
			 * aquí, con el núcleo parado en VBlank, para no competir con el mixer. */
			mAudioBufferClear(second->core->getAudioBuffer(second->core));
		}
		mCoreSyncWaitFrameEnd(sync2);
		return;
	}
	emu->core->runFrame(emu->core);
	memcpy(emu->videoSnapshot, emu->video, sizeof(emu->videoSnapshot));
	mAudioResamplerProcess(&emu->resampler);
}

const uint32_t* emu_video(Emu* emu) {
	return emu ? (const uint32_t*) emu->videoSnapshot : NULL;
}

int emu_read_audio(Emu* emu, int16_t* dst, int max_frames) {
	if (!emu || !dst || max_frames <= 0) return 0;
	return (int) mAudioBufferRead(&emu->out, dst, (size_t) max_frames);
}

void emu_clear_audio(Emu* emu) {
	if (!emu || !emu->core) return;
	mAudioBufferClear(&emu->out);
	/* In linked mode the core-owned source buffer is only cleared while that
	 * player's VBlank is held by emu_run_frame. */
	if (!emu->link) mAudioBufferClear(emu->core->getAudioBuffer(emu->core));
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
