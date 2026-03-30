/*
 * UI WAV clips from <elf_dir>/sfx/ (menu click / select).
 * PCM WAVE, 8- or 16-bit, mono or stereo; resampled to SND_GetOutputSampleRate().
 * Stop time follows clip length: n_samples / rate (see PS2_SfxTick).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "ps2fceu.h"

#ifdef SOUND_ON
#include <audsrv.h>
#include <kernel.h>

#define SFX_MAX_FILE_BYTES (384 * 1024)
/* Trim auto-stop slightly before nominal end (DAC/ring latency). */
#define SFX_DURATION_TRIM_SEC 0.002

typedef struct {
	int16_t *pcm;
	int n_samples;
	int loaded_rate;
} SfxBuf;

static SfxBuf sfx_click;
static SfxBuf sfx_select;
static char sfx_base[512];
static int sfx_base_ok;
static int sfx_ui_stop_armed;
static clock_t sfx_ui_stop_when;

static void sfx_join(char *out, size_t outsz, const char *dir, const char *name)
{
	size_t len = strlen(dir);
	if (len > 0 && (dir[len - 1] == '/' || dir[len - 1] == '\\'))
		snprintf(out, outsz, "%s%s", dir, name);
	else
		snprintf(out, outsz, "%s/%s", dir, name);
}

static void sfx_free_buf(SfxBuf *b)
{
	if (b->pcm) {
		free(b->pcm);
		b->pcm = NULL;
	}
	b->n_samples = 0;
	b->loaded_rate = 0;
}

static int sfx_read_u16le(const uint8_t *p)
{
	return (int)p[0] | ((int)p[1] << 8);
}

static int sfx_read_u32le(const uint8_t *p)
{
	return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

/* Decode WAV to s16 mono at src native rate; *out_rate = file rate. Returns sample count or -1. */
static int sfx_load_wav_file(const char *path, int16_t **out_pcm, int *out_rate)
{
	FILE *fp;
	long fsz;
	uint8_t *raw = NULL;
	size_t nread;
	int i;
	unsigned fmt_off = 0, fmt_sz = 0;
	unsigned data_off = 0, data_sz = 0;
	int audio_fmt, ch, br, bpp;
	int sr;
	int n_in;
	const uint8_t *pcm8;
	const int16_t *pcm16s;
	int16_t *mono = NULL;

	fp = fopen(path, "rb");
	if (!fp)
		return -1;
	fseek(fp, 0, SEEK_END);
	fsz = ftell(fp);
	if (fsz <= 44 || fsz > SFX_MAX_FILE_BYTES) {
		fclose(fp);
		return -1;
	}
	fseek(fp, 0, SEEK_SET);
	raw = (uint8_t *)malloc((size_t)fsz + 1);
	if (!raw) {
		fclose(fp);
		return -1;
	}
	nread = fread(raw, 1, (size_t)fsz, fp);
	fclose(fp);
	if (nread < 12) {
		free(raw);
		return -1;
	}

	if (memcmp(raw, "RIFF", 4) != 0 || memcmp(raw + 8, "WAVE", 4) != 0) {
		free(raw);
		return -1;
	}

	i = 12;
	while (i + 8 <= (int)nread) {
		unsigned chsz = sfx_read_u32le(raw + i + 4);
		if (!memcmp(raw + i, "fmt ", 4)) {
			fmt_off = (unsigned)i + 8;
			fmt_sz = chsz;
		}
		if (!memcmp(raw + i, "data", 4)) {
			data_off = (unsigned)i + 8;
			data_sz = chsz;
		}
		i += 8 + (int)chsz + (chsz & 1);
	}

	if (fmt_off == 0 || fmt_sz < 16 || data_off == 0 || data_sz == 0
	    || data_off + data_sz > nread || fmt_off + fmt_sz > nread) {
		free(raw);
		return -1;
	}

	audio_fmt = sfx_read_u16le(raw + fmt_off);
	ch = sfx_read_u16le(raw + fmt_off + 2);
	sr = sfx_read_u32le(raw + fmt_off + 4);
	br = sfx_read_u32le(raw + fmt_off + 8);
	bpp = sfx_read_u16le(raw + fmt_off + 14);

	if (audio_fmt != 1 || ch < 1 || ch > 2 || sr < 4000 || sr > 96000) {
		free(raw);
		return -1;
	}

	(void)br;

	if (bpp == 8) {
		pcm8 = raw + data_off;
		if (ch == 2) {
			n_in = (int)data_sz / 2;
			mono = (int16_t *)malloc((size_t)n_in * sizeof(int16_t));
			if (!mono) {
				free(raw);
				return -1;
			}
			for (i = 0; i < n_in; i++) {
				int a = pcm8[i * 2] - 128;
				int b = pcm8[i * 2 + 1] - 128;
				mono[i] = (int16_t)(((a + b) * 256) / 2);
			}
		} else {
			n_in = (int)data_sz;
			mono = (int16_t *)malloc((size_t)n_in * sizeof(int16_t));
			if (!mono) {
				free(raw);
				return -1;
			}
			for (i = 0; i < n_in; i++)
				mono[i] = (int16_t)((pcm8[i] - 128) * 256);
		}
	} else if (bpp == 16) {
		n_in = (int)data_sz / (2 * ch);
		mono = (int16_t *)malloc((size_t)n_in * sizeof(int16_t));
		if (!mono) {
			free(raw);
			return -1;
		}
		pcm16s = (const int16_t *)(raw + data_off);
		if (ch == 2) {
			for (i = 0; i < n_in; i++) {
				int32_t a = (int32_t)pcm16s[i * 2];
				int32_t b = (int32_t)pcm16s[i * 2 + 1];
				mono[i] = (int16_t)((a + b) / 2);
			}
		} else {
			for (i = 0; i < n_in; i++)
				mono[i] = pcm16s[i];
		}
	} else {
		free(raw);
		return -1;
	}

	free(raw);
	*out_pcm = mono;
	*out_rate = sr;
	return n_in;
}

static int16_t *sfx_resample_mono(const int16_t *in, int n_in, int rate_in, int rate_out, int *n_out)
{
	int16_t *out;
	int j;
	int n;

	if (rate_in <= 0 || rate_out <= 0 || n_in <= 0)
		return NULL;
	if (rate_in == rate_out) {
		out = (int16_t *)malloc((size_t)n_in * sizeof(int16_t));
		if (out)
			memcpy(out, in, (size_t)n_in * sizeof(int16_t));
		*n_out = n_in;
		return out;
	}

	n = (int)((double)n_in * (double)rate_out / (double)rate_in + 0.5);
	if (n < 1)
		n = 1;
	out = (int16_t *)malloc((size_t)n * sizeof(int16_t));
	if (!out) {
		*n_out = 0;
		return NULL;
	}

	for (j = 0; j < n; j++) {
		double pos = (double)j * (double)rate_in / (double)rate_out;
		int i0 = (int)pos;
		double frac = pos - (double)i0;
		int i1 = i0 + 1;
		int32_t s0, s1;
		if (i0 >= n_in)
			i0 = n_in - 1;
		if (i1 >= n_in)
			i1 = n_in - 1;
		s0 = in[i0];
		s1 = in[i1];
		out[j] = (int16_t)(s0 * (1.0 - frac) + s1 * frac);
	}

	*n_out = n;
	return out;
}

static int sfx_load_clip(SfxBuf *dst, const char *names[])
{
	char path[512];
	int ri;
	int16_t *raw = NULL;
	int rate_file = 0;
	int n_in;
	int rate_out = SND_GetOutputSampleRate();
	int16_t *ready = NULL;
	int n_out;

	for (ri = 0; names[ri]; ri++) {
		sfx_join(path, sizeof path, sfx_base, names[ri]);
		n_in = sfx_load_wav_file(path, &raw, &rate_file);
		if (n_in > 0 && raw) {
			ready = sfx_resample_mono(raw, n_in, rate_file, rate_out, &n_out);
			free(raw);
			if (!ready || n_out <= 0)
				return 0;
			sfx_free_buf(dst);
			dst->pcm = ready;
			dst->n_samples = n_out;
			dst->loaded_rate = rate_out;
			return 1;
		}
		if (raw) {
			free(raw);
			raw = NULL;
		}
	}
	return 0;
}

static void sfx_ensure_loaded(SfxBuf *dst, const char *names[])
{
	int need = !dst->pcm || dst->loaded_rate != SND_GetOutputSampleRate();
	if (!need)
		return;
	if (!sfx_base_ok)
		return;
	sfx_free_buf(dst);
	sfx_load_clip(dst, names);
}

static const char *const sfx_click_files[] = {
	"menuClick2.wav",
	"MenuClick2.wav",
	"menu_click2.wav",
	"menuClick2.WAV",
	"menuClick2",
	"menuClick.wav",
	"MenuClick.wav",
	"menu_click.wav",
	NULL
};

static const char *const sfx_select_files[] = {
	"menuSelect.wav",
	"MenuSelect.wav",
	"menu_select.wav",
	"menuSlelect.wav",
	NULL
};

static clock_t sfx_duration_clocks(int n_samples, int sample_rate_hz)
{
	double sec;
	clock_t c;

	if (sample_rate_hz <= 0)
		sample_rate_hz = SND_GetOutputSampleRate();
	if (sample_rate_hz <= 0)
		sample_rate_hz = 48000;
	if (n_samples <= 0)
		return (clock_t)1;
	sec = (double)n_samples / (double)sample_rate_hz;
	sec -= SFX_DURATION_TRIM_SEC;
	if (sec < 0.0)
		sec = 0.0;
	c = (clock_t)(sec * (double)CLOCKS_PER_SEC + 0.5);
	if (c < (clock_t)1)
		c = (clock_t)1;
	return c;
}

/*
 * Stop + reset ring (via format), then push full PCM. Advance by bytes IOP
 * actually queued. On stall, spin briefly instead of audsrv_wait_audio().
 */
static void sfx_ui_play_buf(SfxBuf *b)
{
	uint8_t *p;
	int left, chunk, sent, stalls;

	if (!b->pcm || b->n_samples <= 0)
		return;

	FlushCache(0);
	audsrv_stop_audio();
	SND_ReapplyAudsrvFormat();

	left = (int)(b->n_samples * sizeof(int16_t));
	p = (uint8_t *)b->pcm;
	stalls = 0;

	while (left > 0) {
		int room = audsrv_available();

		if (room < 64) {
			if (++stalls > 300)
				return;
			for (volatile int y = 0; y < 2048; y++);
			continue;
		}

		chunk = left < room ? left : room;
		chunk &= ~1;
		if (chunk < 2)
			break;

		sent = audsrv_play_audio((char *)p, chunk);
		if (sent < 0)
			return;
		if (sent == 0) {
			if (++stalls > 300)
				return;
			for (volatile int y = 0; y < 2048; y++);
			continue;
		}
		stalls = 0;
		p += sent;
		left -= sent;
	}
}

static void sfx_play_ui(SfxBuf *dst, const char *names[])
{
	if (!sfx_base_ok)
		return;
	sfx_ensure_loaded(dst, names);
	if (!dst->pcm || dst->n_samples <= 0)
		return;
	sfx_ui_play_buf(dst);
	sfx_ui_stop_armed = 1;
	sfx_ui_stop_when = clock() + sfx_duration_clocks(dst->n_samples, dst->loaded_rate);
}
#endif

void PS2_SfxInit(const char *elf_dir)
{
#ifdef SOUND_ON
	size_t el;
	sfx_free_buf(&sfx_click);
	sfx_free_buf(&sfx_select);
	sfx_base[0] = '\0';
	sfx_base_ok = 0;
	sfx_ui_stop_armed = 0;
	if (!elf_dir || !elf_dir[0])
		return;
	el = strlen(elf_dir);
	if (el >= sizeof sfx_base - 8)
		return;
	strcpy(sfx_base, elf_dir);
	if (el && sfx_base[el - 1] != '/' && sfx_base[el - 1] != '\\')
		strcat(sfx_base, "/");
	strcat(sfx_base, "sfx");
	sfx_base_ok = 1;
#else
	(void)elf_dir;
#endif
}

void PS2_SfxPreload(void)
{
#ifdef SOUND_ON
	if (!sfx_base_ok)
		return;
	sfx_ensure_loaded(&sfx_click, sfx_click_files);
	sfx_ensure_loaded(&sfx_select, sfx_select_files);
#endif
}

void PS2_SfxCarouselEnter(void)
{
#ifdef SOUND_ON
	sfx_ui_stop_armed = 0;
	FlushCache(0);
	audsrv_stop_audio();
	SND_ReapplyAudsrvFormat();
#endif
}

void PS2_SfxCarouselClick(void)
{
#ifdef SOUND_ON
	sfx_play_ui(&sfx_click, sfx_click_files);
#endif
}

void PS2_SfxMenuClick(void)
{
#ifdef SOUND_ON
	sfx_play_ui(&sfx_click, sfx_click_files);
#endif
}

void PS2_SfxMenuSelect(void)
{
#ifdef SOUND_ON
	sfx_play_ui(&sfx_select, sfx_select_files);
#endif
}

void PS2_SfxTick(void)
{
#ifdef SOUND_ON
	if (!sfx_ui_stop_armed)
		return;
	if (clock() >= sfx_ui_stop_when) {
		audsrv_stop_audio();
		sfx_ui_stop_armed = 0;
	}
#endif
}
