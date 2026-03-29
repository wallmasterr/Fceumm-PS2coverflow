/*
 * Cover flow launcher: PNG art in <elf_dir>/images/ matches <stem>.nes in roms/.
 * Background: BG.png / bg.png (or .jpg) in images/ — loaded after cover art so VRAM for it is not
 * overwritten by many tile textures. Optional FG.png / fg.png (PNG only) drawn above BG with alpha.
 * D-pad L/R, X = launch, Triangle = file browser.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <dirent.h>
#include <sys/stat.h>
#include <strings.h>
#include <libpad.h>
#include <gsKit.h>
#include <dmaKit.h>

#include "ps2fceu.h"
#include "build_stamp.h"

extern GSGLOBAL *gsGlobal;
extern skin FCEUSkin;
extern u32 old_pad[4];

#define CF_MAX_ITEMS 48
#define CF_SLOT_SPACING 200.0f
#define CF_SCALE_CENTER 1.14f
#define CF_SCALE_SIDE 0.60f
#define CF_BASE_HEIGHT 208.0f
#define CF_CAROUSEL_Y_OFFSET 44.0f
#define CF_Z_BG 0
#define CF_Z_FG 2
#define CF_Z_SLOT_BASE 10
#define CF_VIEW_LERP 0.20f
#define CF_EDGE_FADE_PX 120.0f
#define CF_SLOT_K_MIN (-3)
#define CF_SLOT_K_MAX 3

typedef struct {
	char stem[96];
	char png_path[512];
	char nes_path[512];
	GSTEXTURE tex;
	int tex_ok;
} CF_Item;

static struct padButtonStatus cf_buttons;

static void cf_join(char *out, size_t outsz, const char *dir, const char *name)
{
	size_t len = strlen(dir);
	if (len > 0 && (dir[len - 1] == '/' || dir[len - 1] == '\\'))
		snprintf(out, outsz, "%s%s", dir, name);
	else
		snprintf(out, outsz, "%s/%s", dir, name);
}

static int cf_has_ext_ci(const char *name, const char *ext)
{
	size_t nl = strlen(name), el = strlen(ext);
	if (nl < el)
		return 0;
	return strcasecmp(name + nl - el, ext) == 0;
}

static int cf_find_nes(const char *elf_dir, const char *stem, char *out, size_t outsz)
{
	static const char *const rels[] = { "roms", "rom", NULL };
	const char *const *rp;
	char trial[512];
	struct stat st;
	size_t el = strlen(elf_dir);
	int trail = el && (elf_dir[el - 1] == '/' || elf_dir[el - 1] == '\\');

	for (rp = rels; *rp; rp++) {
		if (!trail)
			snprintf(trial, sizeof trial, "%s/%s/%s.nes", elf_dir, *rp, stem);
		else
			snprintf(trial, sizeof trial, "%s%s/%s.nes", elf_dir, *rp, stem);
		if (stat(trial, &st) == 0 && S_ISREG(st.st_mode)) {
			strncpy(out, trial, outsz - 1);
			out[outsz - 1] = '\0';
			return 1;
		}
	}
	if (!trail)
		snprintf(trial, sizeof trial, "%s/%s.nes", elf_dir, stem);
	else
		snprintf(trial, sizeof trial, "%s%s.nes", elf_dir, stem);
	if (stat(trial, &st) == 0 && S_ISREG(st.st_mode)) {
		strncpy(out, trial, outsz - 1);
		out[outsz - 1] = '\0';
		return 1;
	}
	return 0;
}

static int cf_cmp_item(const void *a, const void *b)
{
	return strcasecmp(((const CF_Item *)a)->stem, ((const CF_Item *)b)->stem);
}

static void cf_pad_reset(void)
{
	old_pad[0] = 0xFFFF;
}

static inline int cf_wrap_i(int i, int n)
{
	if (n <= 0)
		return 0;
	i %= n;
	if (i < 0)
		i += n;
	return i;
}

/* Shortest delta from `from` to `to` on a ring of length n (for smooth wrap). */
static float cf_wrapped_delta(float from, float to, int n)
{
	float d = to - from;
	float nf = (float)n;
	if (n <= 1)
		return 0.f;
	while (d > nf * 0.5f)
		d -= nf;
	while (d < -nf * 0.5f)
		d += nf;
	return d;
}

/* Fade as the cover moves past the left/right screen margins (wrap feels like off / on). */
static float cf_edge_alpha(float center_x, float half_w, int screen_w)
{
	float m = CF_EDGE_FADE_PX;
	float left = center_x - half_w;
	float right = center_x + half_w;
	float f = 1.f;

	if (right < m)
		f = right / m;
	if (left > (float)screen_w - m)
		f = fminf(f, ((float)screen_w - left) / m);
	if (f < 0.f)
		f = 0.f;
	if (f > 1.f)
		f = 1.f;
	return f;
}

static int cf_pad_update(u32 *new_pad_out)
{
	u32 paddata, new_pad;
	u16 slot = 0;
	int ret;

	ret = padGetState(0, slot);
	if ((ret != PAD_STATE_STABLE) && (ret != PAD_STATE_FINDCTP1))
		ret = padGetState(0, slot);

	ret = padRead(0, slot, &cf_buttons);
	if (ret == 0)
		return 0;

	paddata = 0xFFFF ^ cf_buttons.btns;
	new_pad = paddata & ~old_pad[0];
	old_pad[0] = paddata;
	*new_pad_out = new_pad;
	return 1;
}

/* Load fullscreen backdrop last so earlier cover PNGs do not clobber its VRAM on tight budgets. */
static int cf_load_background(GSGLOBAL *gs, GSTEXTURE *bg, const char *img_dir)
{
	char path[512];
	static const char *const png_names[] = { "BG.png", "bg.png", "Bg.png", NULL };
	static const char *const jpg_names[] = { "BG.jpg", "bg.jpg", "BG.jpeg", "bg.jpeg", NULL };
	const char *const *n;

	for (n = png_names; *n; n++) {
		cf_join(path, sizeof path, img_dir, *n);
		memset(bg, 0, sizeof *bg);
		if (gsKit_texture_png(gs, bg, path) != -1 && bg->Width > 0 && bg->Height > 0)
			return 1;
	}
	for (n = jpg_names; *n; n++) {
		cf_join(path, sizeof path, img_dir, *n);
		memset(bg, 0, sizeof *bg);
		if (gsKit_texture_jpeg(gs, bg, path) != -1 && bg->Width > 0 && bg->Height > 0)
			return 1;
	}
	memset(bg, 0, sizeof *bg);
	return 0;
}

/* PNG only — expect alpha channel for transparency */
static int cf_load_foreground(GSGLOBAL *gs, GSTEXTURE *fg, const char *img_dir)
{
	char path[512];
	static const char *const png_names[] = {
		"FG.png", "fg.png", "Fg.png", "FG.PNG", "foreground.png", "Foreground.png", NULL
	};
	const char *const *n;
	struct stat st;

	memset(fg, 0, sizeof *fg);
	for (n = png_names; *n; n++) {
		cf_join(path, sizeof path, img_dir, *n);
		if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
			continue;
		if (gsKit_texture_png(gs, fg, path) != -1 && fg->Width > 0 && fg->Height > 0)
			return 1;
		memset(fg, 0, sizeof *fg);
	}
	return 0;
}

static void cf_draw_fg_with_alpha(GSTEXTURE *fg)
{
	u64 old_alpha;
	u8 old_pabe;

	old_alpha = gsGlobal->PrimAlpha;
	old_pabe = gsGlobal->PABE;
	gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 1);
	gsKit_prim_sprite_texture(gsGlobal, fg,
		0.0f, 0.0f, 0.0f, 0.0f,
		(float)gsGlobal->Width, (float)gsGlobal->Height,
		(float)fg->Width, (float)fg->Height, CF_Z_FG,
		GS_SETREG_RGBA(0x80, 0x80, 0x80, 0x00));
	gsKit_set_primalpha(gsGlobal, old_alpha, old_pabe);
}

static void cf_release_textures(CF_Item *items, int nitems, GSTEXTURE *bg, GSTEXTURE *fg)
{
	int i;

	if (bg && bg->Mem) {
		free(bg->Mem);
		bg->Mem = NULL;
	}
	if (fg && fg->Mem) {
		free(fg->Mem);
		fg->Mem = NULL;
	}
	for (i = 0; i < nitems; i++) {
		if (items[i].tex.Mem) {
			free(items[i].tex.Mem);
			items[i].tex.Mem = NULL;
		}
	}
}

static void cf_draw_slot(CF_Item *it, float cx, float cy, float scale, int z, float fade)
{
	float draw_h = CF_BASE_HEIGHT * scale;
	float draw_w;
	u8 m = (u8)(0x80 * fade);
	u8 dr = (u8)(0x50 * fade);
	u8 dg = (u8)(0x50 * fade);
	u8 db = (u8)(0x55 * fade);
	u64 dim = GS_SETREG_RGBA(dr, dg, db, 0x00);
	u64 hi = FCEUSkin.textcolor ? FCEUSkin.textcolor : GS_SETREG_RGBA(0xff, 0xff, 0xff, 0xff);

	if (m < 4)
		return;

	if (it->tex_ok && it->tex.Height > 0) {
		draw_w = draw_h * ((float)it->tex.Width / (float)it->tex.Height);
		gsKit_prim_sprite_texture(gsGlobal, &it->tex,
			cx - draw_w * 0.5f, cy - draw_h * 0.5f, 0.0f, 0.0f,
			cx + draw_w * 0.5f, cy + draw_h * 0.5f,
			(float)it->tex.Width, (float)it->tex.Height, z,
			GS_SETREG_RGBA(m, m, m, 0x00));
	} else {
		gsKit_prim_sprite(gsGlobal,
			cx - 80.0f * scale, cy - draw_h * 0.5f,
			cx + 80.0f * scale, cy + draw_h * 0.5f, z, dim);
		printXY(it->stem, (int)(cx - 40), (int)(cy - 8), z + 1, hi, 1, 0);
	}
}

int Coverflow_SelectRom(char *out_path, size_t outsz, const char *elf_dir)
{
	CF_Item items[CF_MAX_ITEMS];
	GSTEXTURE bg;
	GSTEXTURE fg;
	int nitems = 0;
	DIR *d;
	struct dirent *de;
	char img_dir[512];
	int i, sel = 0;
	int bg_ok = 0;
	int fg_ok = 0;
	u32 new_pad;
	int had_pad;

	if (!out_path || outsz == 0 || !elf_dir || !elf_dir[0])
		return 0;

	memset(items, 0, sizeof items);
	memset(&bg, 0, sizeof bg);
	memset(&fg, 0, sizeof fg);

	cf_join(img_dir, sizeof img_dir, elf_dir, "images");

	d = opendir(img_dir);
	if (!d)
		return 0;

	while ((de = readdir(d)) != NULL && nitems < CF_MAX_ITEMS) {
		char *name = de->d_name;
		char stem[96];
		size_t nl;

		if (!cf_has_ext_ci(name, ".png"))
			continue;
		nl = strlen(name);
		if (nl < 5)
			continue;
		memcpy(stem, name, nl - 4);
		stem[nl - 4] = '\0';
		if (strcasecmp(stem, "BG") == 0 || strcasecmp(stem, "FG") == 0)
			continue;
		if (!cf_find_nes(elf_dir, stem, items[nitems].nes_path, sizeof items[nitems].nes_path))
			continue;
		strncpy(items[nitems].stem, stem, sizeof items[nitems].stem - 1);
		cf_join(items[nitems].png_path, sizeof items[nitems].png_path, img_dir, name);
		nitems++;
	}
	closedir(d);

	if (nitems == 0)
		return 0;

	qsort(items, (size_t)nitems, sizeof items[0], cf_cmp_item);

	for (i = 0; i < nitems; i++) {
		memset(&items[i].tex, 0, sizeof items[i].tex);
		if (gsKit_texture_png(gsGlobal, &items[i].tex, items[i].png_path) >= 0)
			items[i].tex_ok = 1;
		else
			items[i].tex_ok = 0;
	}

	/* gsKit_texture_png/jpeg already upload to VRAM — do not call gsKit_texture_upload again
	 * or TBW/stride gets wrong and BG/FG look garbled. */
	bg_ok = cf_load_background(gsGlobal, &bg, img_dir);
	fg_ok = cf_load_foreground(gsGlobal, &fg, img_dir);

	cf_pad_reset();

	gsKit_mode_switch(gsGlobal, GS_ONESHOT);
	gsKit_queue_reset(gsGlobal->Os_Queue);
	gsGlobal->DrawOrder = GS_PER_OS;

	{
		float view_f = (float)sel;

		for (;;) {
			float cx = (float)gsGlobal->Width * 0.5f;
			float cy = (float)gsGlobal->Height * 0.52f + CF_CAROUSEL_Y_OFFSET;
			float d;
			float center_floor;
			float t;
			int ci;
			int k;

			new_pad = 0;
			had_pad = cf_pad_update(&new_pad);

			if (had_pad) {
				if (new_pad & PAD_TRIANGLE)
					break;
				if (new_pad & PAD_CROSS) {
					strncpy(out_path, items[sel].nes_path, outsz - 1);
					out_path[outsz - 1] = '\0';
					cf_release_textures(items, nitems, &bg, &fg);
					return 1;
				}
				if (new_pad & PAD_LEFT) {
					sel--;
					if (sel < 0)
						sel = nitems - 1;
				}
				if (new_pad & PAD_RIGHT) {
					sel++;
					if (sel >= nitems)
						sel = 0;
				}
			}

			d = cf_wrapped_delta(view_f, (float)sel, nitems);
			if (fabsf(d) < 0.004f)
				view_f = (float)sel;
			else
				view_f += d * CF_VIEW_LERP;
			while (view_f >= (float)nitems)
				view_f -= (float)nitems;
			while (view_f < 0.f)
				view_f += (float)nitems;

			gsKit_clear(gsGlobal, GS_SETREG_RGBAQ(0x00, 0x00, 0x00, 0x00, 0x00));

			if (bg_ok) {
				gsKit_prim_sprite_texture(gsGlobal, &bg,
					0.0f, 0.0f, 0.0f, 0.0f,
					(float)gsGlobal->Width, (float)gsGlobal->Height,
					(float)bg.Width, (float)bg.Height, CF_Z_BG,
					GS_SETREG_RGBA(0x80, 0x80, 0x80, 0x00));
			}

			if (fg_ok)
				cf_draw_fg_with_alpha(&fg);

			center_floor = floorf(view_f);
			t = view_f - center_floor;
			ci = (int)center_floor;

			if (nitems <= 1) {
				cf_draw_slot(&items[0], cx, cy, CF_SCALE_CENTER,
					CF_Z_SLOT_BASE + 8, 1.f);
			} else {
				for (k = CF_SLOT_K_MIN; k <= CF_SLOT_K_MAX; k++) {
					float xOff = (float)k - t;
					float slot_cx = cx + xOff * CF_SLOT_SPACING;
					int idx = cf_wrap_i(ci + k, nitems);
					float adist = fabsf(xOff);
					float scale = CF_SCALE_SIDE + (CF_SCALE_CENTER - CF_SCALE_SIDE) *
						fmaxf(0.f, 1.f - adist);
					int z = CF_Z_SLOT_BASE + (int)(10.f - adist * 3.f);
					float hw = CF_BASE_HEIGHT * scale * 0.55f;
					float edge_f = cf_edge_alpha(slot_cx, hw, gsGlobal->Width);
					float depth_f = fmaxf(0.35f, 1.f - adist * 0.22f);
					float fade = edge_f * depth_f;

					if (fade < 0.02f)
						continue;
					if (z < CF_Z_SLOT_BASE)
						z = CF_Z_SLOT_BASE;
					cf_draw_slot(&items[idx], slot_cx, cy, scale, z, fade);
				}
			}

			{
				const char *bid = LOWTEK_BUILD_ID;
				int text_w = (int)strlen(bid) * 8;
				int bx = gsGlobal->Width - text_w - 12;
				int by = gsGlobal->Height - 20;
				if (bx < 8)
					bx = 8;
				printXY(bid, bx, by, 40,
					FCEUSkin.textcolor ? FCEUSkin.textcolor : GS_SETREG_RGBA(0xc0, 0xc0, 0xc0, 0xff), 1, 0);
			}

			DrawScreen(gsGlobal);
		}
	}

	cf_release_textures(items, nitems, &bg, &fg);
	return 0;
}
