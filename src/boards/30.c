/* FCE Ultra - NES/Famicom Emulator
 * Mapper 30 — UNROM 512 (homebrew / retroUSB), ported from FCEUX unrom512.cpp
 * (CaitSith2, Cluster) under GPLv2+.
 */

#include "mapinc.h"
#include "../ines.h"
#include <string.h>

extern uint8 *VROM;
extern iNES_HEADER head;

#define M30_ROM    0
#define M30_CFI    0x10
#define M30_FLASH  0x11

#define FLASH_SECTOR_SIZE (4 * 1024)

static uint8 flash_save, flash_state, flash_id_mode, latche, bus_conflict;
static uint16 latcha;
static uint8 *flash_data;
static uint16 flash_buffer_a[10];
static uint8 flash_buffer_v[10];
static uint8 flash_id[2];

static void UNROM512_Sync(void) {
	int chip;

	if (flash_save)
		chip = !flash_id_mode ? M30_FLASH : M30_CFI;
	else
		chip = M30_ROM;
	setprg16r(chip, 0x8000, latche & 0x1F);
	setprg16r(chip, 0xC000, ~0);
	setchr8((latche >> 5) & 3);
	setmirror(MI_0 + ((latche >> 7) & 1));
}

static void StateRestore(int version) {
	UNROM512_Sync();
}

static DECLFW(UNROM512FlashWrite) {
	if (flash_state < (int)(sizeof(flash_buffer_a) / sizeof(flash_buffer_a[0]))) {
		flash_buffer_a[flash_state] = (uint16)((A & 0x3FFF) | ((latche & 1) << 14));
		flash_buffer_v[flash_state] = V;
		flash_state++;

		/* Enter flash ID mode (3-command sequence; FCEUX 2.6.6 used a broken 2-write check) */
		if ((flash_state == 3) &&
		    (flash_buffer_a[0] == 0x5555) && (flash_buffer_v[0] == 0xAA) &&
		    (flash_buffer_a[1] == 0x2AAA) && (flash_buffer_v[1] == 0x55) &&
		    (flash_buffer_a[2] == 0x5555) && (flash_buffer_v[2] == 0x90)) {
			flash_id_mode = 0;
			flash_state = 0;
		}

		/* Sector erase */
		if ((flash_state == 6) &&
		    (flash_buffer_a[0] == 0x5555) && (flash_buffer_v[0] == 0xAA) &&
		    (flash_buffer_a[1] == 0x2AAA) && (flash_buffer_v[1] == 0x55) &&
		    (flash_buffer_a[2] == 0x5555) && (flash_buffer_v[2] == 0x80) &&
		    (flash_buffer_a[3] == 0x5555) && (flash_buffer_v[3] == 0xAA) &&
		    (flash_buffer_a[4] == 0x2AAA) && (flash_buffer_v[4] == 0x55) &&
		    (flash_buffer_v[5] == 0x30)) {
			int offset = (int)(Page[A >> 11] + A - flash_data);
			int sector = offset / FLASH_SECTOR_SIZE;
			int i;

			for (i = sector * FLASH_SECTOR_SIZE; i < (sector + 1) * FLASH_SECTOR_SIZE; i++)
				flash_data[i % (int)PRGsize[M30_ROM]] = 0xFF;
			FCEU_printf("Flash sector #%d erased.\n", sector);
		}

		/* Chip erase */
		if ((flash_state == 6) &&
		    (flash_buffer_a[0] == 0x5555) && (flash_buffer_v[0] == 0xAA) &&
		    (flash_buffer_a[1] == 0x2AAA) && (flash_buffer_v[1] == 0x55) &&
		    (flash_buffer_a[2] == 0x5555) && (flash_buffer_v[2] == 0x80) &&
		    (flash_buffer_a[3] == 0x5555) && (flash_buffer_v[3] == 0xAA) &&
		    (flash_buffer_a[4] == 0x2AAA) && (flash_buffer_v[4] == 0x55) &&
		    (flash_buffer_a[5] == 0x5555) && (flash_buffer_v[5] == 0x10)) {
			memset(flash_data, 0xFF, PRGsize[M30_ROM]);
			FCEU_printf("Flash chip erased.\n");
			flash_state = 0;
		}

		/* Program byte */
		if ((flash_state == 4) &&
		    (flash_buffer_a[0] == 0x5555) && (flash_buffer_v[0] == 0xAA) &&
		    (flash_buffer_a[1] == 0x2AAA) && (flash_buffer_v[1] == 0x55) &&
		    (flash_buffer_a[2] == 0x5555) && (flash_buffer_v[2] == 0xA0)) {
			if (CartBR(A) != 0xFF)
				FCEU_PrintError("UNROM512: cannot program flash; sector not erased.");
			else
				CartBW(A, V);
			flash_state = 0;
		}
	}

	if (((A & 0xFFF) != 0x0AAA) && ((A & 0xFFF) != 0x0555))
		flash_state = 0;

	if (V == 0xF0) {
		flash_state = 0;
		flash_id_mode = 0;
	}

	UNROM512_Sync();
}

static DECLFW(UNROM512HLatchWrite) {
	if (bus_conflict)
		latche = V & CartBR(A);
	else
		latche = V;
	latcha = A;
	UNROM512_Sync();
}

static void UNROM512LatchPower(void) {
	latche = 0;
	UNROM512_Sync();
	SetReadHandler(0x8000, 0xFFFF, CartBR);
	if (!flash_save)
		SetWriteHandler(0x8000, 0xFFFF, UNROM512HLatchWrite);
	else {
		SetWriteHandler(0x8000, 0xBFFF, UNROM512FlashWrite);
		SetWriteHandler(0xC000, 0xFFFF, UNROM512HLatchWrite);
	}
}

static void UNROM512LatchClose(void) {
	if (flash_data)
		FCEU_gfree(flash_data);
	flash_data = NULL;
}

void Mapper30_Init(CartInfo *info) {
	size_t flash_size = 0;
	int mirror;

	info->Power = UNROM512LatchPower;
	info->Close = UNROM512LatchClose;
	GameStateRestore = StateRestore;

	flash_state = 0;
	flash_id_mode = 0;
	flash_save = (uint8)info->battery;
	bus_conflict = flash_save ? 0 : 1;

	mirror = (head.ROM_type & 1) | ((head.ROM_type & 8) >> 2);
	switch (mirror) {
	case 0:
		SetupCartMirroring(MI_H, 1, NULL);
		break;
	case 1:
		SetupCartMirroring(MI_V, 1, NULL);
		break;
	case 2:
		SetupCartMirroring(MI_0, 0, NULL);
		break;
	case 3:
		/* Four-screen: last 8 KiB of CHR RAM (CHRsize[0] set by loader) */
		if (VROM && CHRsize[0] >= 8192)
			SetupCartMirroring(4, 1, VROM + (CHRsize[0] - 8192));
		else
			SetupCartMirroring(MI_V, 1, NULL);
		break;
	}

	if (flash_save) {
		size_t i;

		flash_size = PRGsize[M30_ROM];
		flash_data = (uint8 *)FCEU_gmalloc((uint32)flash_size);
		for (i = 0; i < flash_size; i++)
			flash_data[i] = PRGptr[M30_ROM][i];

		SetupCartPRGMapping(M30_FLASH, flash_data, (uint32)flash_size, 1);
		info->SaveGame[0] = flash_data;
		info->SaveGameLen[0] = (uint32)flash_size;

		flash_id[0] = 0xBF;
		flash_id[1] = (uint8)(0xB5 + (head.ROM_size >> 4));
		SetupCartPRGMapping(M30_CFI, flash_id, sizeof(flash_id), 0);

		AddExState(flash_data, (uint32)flash_size, 0, "FLSH");
		AddExState(&flash_state, sizeof(flash_state), 0, "FLST");
		AddExState(&flash_id_mode, sizeof(flash_id_mode), 0, "FLMD");
		AddExState(flash_buffer_a, sizeof(flash_buffer_a), 0, "FLBA");
		AddExState(flash_buffer_v, sizeof(flash_buffer_v), 0, "FLBV");
	}
	AddExState(&latcha, sizeof(latcha), 0, "LATA");
	AddExState(&latche, sizeof(latche), 0, "LATC");
	AddExState(&bus_conflict, sizeof(bus_conflict), 0, "BUSC");
}
