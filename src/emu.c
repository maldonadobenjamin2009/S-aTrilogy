/* emu.c - Envoltura mínima del núcleo de mGBA (API pública mCore) enlazada dentro del ejecutable.
 *
 * Usa solo la API pública de mGBA (mCore). Este archivo es código propio del launcher;
 * mGBA (MPL 2.0) se compila sin modificaciones desde ../mgba.
 */
#include <mgba/core/core.h>
#include <mgba/core/serialize.h>
#include <mgba/core/version.h>
#include <mgba/gba/core.h>
#include <mgba/gba/interface.h>
#include <mgba-util/audio-buffer.h>
#include <mgba-util/audio-resampler.h>
#include <mgba-util/vfs.h>

#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include "emu.h"
#ifndef EMU_INTERP
#define EMU_INTERP mINTERPOLATOR_SINC   /* igual que el frontend SDL oficial de mGBA */
#endif
#define VIDEO_W EMU_W
#define VIDEO_H EMU_H

struct Emu {
	struct mCore* core;
	mColor video[VIDEO_W * VIDEO_H];
	struct mAudioBuffer out;
	struct mAudioResampler resampler;
	unsigned outRate;
	unsigned srcRate;
	int audioReady;
};

const char* emu_version(void) {
	return projectVersion;
}


/* Devuelve NULL si falla. out_rate = frecuencia de audio de salida (p. ej. 48000).
 * bios_path puede ser NULL o "" (se usa la BIOS HLE incluida en mGBA). */
Emu* emu_open(const char* rom_path, const char* save_path, const char* bios_path, unsigned out_rate) {
	if (!rom_path || !out_rate) {
		return NULL;
	}
	struct VFile* rom = VFileOpen(rom_path, O_RDONLY);
	if (!rom) {
		return NULL;
	}
	struct mCore* core = mCoreFindVF(rom);
	if (!core || core->platform(core) != mPLATFORM_GBA) {
		rom->close(rom);
		if (core) {
			core->deinit(core);
		}
		return NULL;
	}

	Emu* emu = calloc(1, sizeof(Emu));
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

	/* Buffer de audio interno del núcleo (muestras estéreo a su frecuencia nativa). */
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
	/* Sin esto el volumen maestro de mGBA queda en 0 y el audio sale en silencio. 0x100 = GBA_AUDIO_VOLUME_MAX */
	mCoreConfigSetDefaultIntValue(&core->config, "volume", 0x100);
	mCoreConfigSetDefaultIntValue(&core->config, "mute", 0);
	mCoreLoadForeignConfig(core, &core->config);   /* rellena core->opts (volumen, BIOS...) y llama a loadConfig */

	if (!core->loadROM(core, rom)) {
		rom->close(rom);
		emu_close(emu);
		return NULL;
	}

	if (bios_path && bios_path[0]) {
		struct VFile* bios = VFileOpen(bios_path, O_RDONLY);
		if (bios && !core->loadBIOS(core, bios, 0)) {
			bios->close(bios);
		}
	}

	if (save_path && save_path[0]) {
		struct VFile* sav = VFileOpen(save_path, O_CREAT | O_RDWR);
		if (sav && !core->loadSave(core, sav)) {
			sav->close(sav);
		}
	}

	core->reset(core);
	return emu;
}

void emu_close(Emu* emu) {
	if (!emu) {
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
	emu->core->reset(emu->core);
	mAudioBufferClear(&emu->out);
}

void emu_set_keys(Emu* emu, uint32_t keys) {
	emu->core->setKeys(emu->core, keys);
}

void emu_run_frame(Emu* emu) {
	emu->core->runFrame(emu->core);
	/* La tasa del audio que produce el núcleo CAMBIA durante el juego: cuando el juego escribe SOUNDBIAS la
	 * resolución pasa de 32768 Hz a 65536 / 131072 / 262144 Hz (core->audioSampleRate). El frontend oficial de
	 * mGBA la consulta en cada callback; si se lee solo al abrir, el remuestreo es incorrecto (audio duplicado,
	 * sonido roto y fotogramas de más). */
	unsigned rate = emu->core->audioSampleRate(emu->core);
	if (rate != emu->srcRate) {
		emu->srcRate = rate;
		mAudioResamplerSetSource(&emu->resampler, emu->core->getAudioBuffer(emu->core), rate, true);
	}
	mAudioResamplerProcess(&emu->resampler);
}

/* Puntero al framebuffer 240x160 de 32 bits (bytes R,G,B,X). Válido hasta emu_close. */
const uint32_t* emu_video(Emu* emu) {
	return (const uint32_t*) emu->video;
}

/* Copia hasta max_frames muestras estéreo int16 ya remuestreadas. Devuelve cuántas. */
int emu_read_audio(Emu* emu, int16_t* dst, int max_frames) {
	return (int) mAudioBufferRead(&emu->out, dst, (size_t) max_frames);
}

void emu_set_audio_rate(Emu* emu, double rate) {
	mAudioResamplerSetDestination(&emu->resampler, &emu->out, rate);
}

void emu_clear_audio(Emu* emu) {
	mAudioBufferClear(&emu->out);
	mAudioBufferClear(emu->core->getAudioBuffer(emu->core));
}

/* Estados de guardado compatibles con mGBA. Devuelven 1 si OK. */
int emu_save_state(Emu* emu, const char* path) {
	struct VFile* vf = VFileOpen(path, O_CREAT | O_TRUNC | O_RDWR);
	if (!vf) {
		return 0;
	}
	bool ok = mCoreSaveStateNamed(emu->core, vf, SAVESTATE_SAVEDATA | SAVESTATE_SCREENSHOT | SAVESTATE_RTC | SAVESTATE_METADATA);
	vf->close(vf);
	return ok ? 1 : 0;
}

int emu_load_state(Emu* emu, const char* path) {
	struct VFile* vf = VFileOpen(path, O_RDONLY);
	if (!vf) {
		return 0;
	}
	bool ok = mCoreLoadStateNamed(emu->core, vf, SAVESTATE_SAVEDATA | SAVESTATE_RTC);
	vf->close(vf);
	if (ok) {
		mAudioBufferClear(&emu->out);
	}
	return ok ? 1 : 0;
}
