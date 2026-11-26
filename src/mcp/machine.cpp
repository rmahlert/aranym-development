/*
 * machine.cpp - the MCP tools' way into ARAnyM: keys, mouse, the screen,
 * reset and quit, the floppy, memory and the CPU's registers.
 *
 * Everything here runs on the emulator's main thread: from the VBL, from
 * HostScreen::refresh, or through mcp_main_call.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "sysdeps.h"
#include "cpu_emulation.h"
#include "memory-uae.h"
#include "newcpu.h"
#include "main.h"
#include "input.h"
#include "hardware.h"
#include "ikbd.h"
#include "mfp.h"
#include "acsifdc.h"
#include "aradata.h"
#include "parameters.h"
#include "host.h"
#include "hostscreen.h"
#include "version.h"
#include "disasm-glue.h"
#include "SDL_compat.h"

#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <vector>

#include "aranym_mcp.h"
#include "machine.h"

// input.cpp
bool input_mcp_fvdi_events(int *x, int *y);
bool input_mcp_mouse_abs(int x, int y, int buttons);
void input_mcp_wheel(int dir);

/* ---- the screen ---- */

static std::vector<uint32_t> frame_buf;

void aranym_mcp_frame(SDL_Surface *s)
{
	if (!s || !s->format || !mcp_frame_wanted())
		return;
	int w = s->w, h = s->h;
	if (w <= 0 || h <= 0)
		return;
	frame_buf.resize((size_t)w * h);
	uint32_t *out = frame_buf.data();
	bool locked = SDL_MUSTLOCK(s) && SDL_LockSurface(s) == 0;
	if (SDL_MUSTLOCK(s) && !locked)
		return;
	const SDL_PixelFormat *f = s->format;
	bool ok = true;
	if (f->BytesPerPixel == 1 && f->palette) {
		const SDL_Color *pal = f->palette->colors;
		int ncol = f->palette->ncolors;
		for (int y = 0; y < h; y++) {
			const uint8_t *row = (const uint8_t *)s->pixels + (size_t)y * s->pitch;
			for (int x = 0; x < w; x++) {
				int i = row[x];
				out[(size_t)y * w + x] = i < ncol ?
					(uint32_t)pal[i].r << 16 | (uint32_t)pal[i].g << 8 | pal[i].b : 0;
			}
		}
	} else {
#if SDL_VERSION_ATLEAST(2, 0, 0)
		ok = SDL_ConvertPixels(w, h, f->format, s->pixels, s->pitch,
			SDL_PIXELFORMAT_RGB888, out, w * 4) == 0;
#else
		for (int y = 0; y < h; y++) {
			const uint8_t *row = (const uint8_t *)s->pixels + (size_t)y * s->pitch;
			for (int x = 0; x < w; x++) {
				uint32_t v = 0;
				const uint8_t *p = row + x * f->BytesPerPixel;
				memcpy(&v, p, f->BytesPerPixel);
				uint8_t r, g, b;
				SDL_GetRGB(v, (SDL_PixelFormat *)f, &r, &g, &b);
				out[(size_t)y * w + x] = (uint32_t)r << 16 | (uint32_t)g << 8 | b;
			}
		}
#endif
	}
	if (locked)
		SDL_UnlockSurface(s);
	if (ok)
		mcp_frame_submit(out, w, h);
}

/* ---- keys and the mouse ---- */

void mach_key(uint8_t code)
{
	getIKBD()->SendKey(code);
}

bool mach_mouse_abs(int x, int y, int buttons)
{
	return input_mcp_mouse_abs(x, y, buttons);
}

void mach_mouse_rel(int dx, int dy, int buttons)
{
	getIKBD()->SendMouseMotion(dx, dy, buttons & 3, false);
}

bool mach_mouse_absolute(void)
{
	return input_mcp_fvdi_events(NULL, NULL);
}

bool mach_pointer(int *x, int *y)
{
	if (getARADATA()->isAtariMouseDriver()) {
		*x = getARADATA()->getAtariMouseX();
		*y = getARADATA()->getAtariMouseY();
		return true;
	}
	return input_mcp_fvdi_events(x, y);
}

void mach_wheel(int dir)
{
	input_mcp_wheel(dir);
}

void mach_serial_poll(void)
{
	getMFP()->mcpSerialPoll();
}

/* ---- power ---- */

void mach_reset(bool cold)
{
	RestartAll(cold);
}

void mach_quit(void)
{
	Quit680x0();
}

bool mach_service_events(void)
{
	check_event();
	if (host && host->video)
		host->video->refresh();
	return quit_program == 1;
}

/* ---- the floppy ---- */

const char *mach_floppy_path(void)
{
	if (!getFDC()->is_floppy_inserted())
		return NULL;
	return bx_options.floppy.path;
}

bool mach_floppy_insert(const char *path)
{
	if (strlen(path) >= sizeof(bx_options.floppy.path))
		return false;
	char old[sizeof(bx_options.floppy.path)];
	strcpy(old, bx_options.floppy.path);
	strcpy(bx_options.floppy.path, path);
	if (getFDC()->insert_floppy())
		return true;
	strcpy(bx_options.floppy.path, old);
	getFDC()->insert_floppy();
	return false;
}

void mach_floppy_eject(void)
{
	getFDC()->remove_floppy();
	bx_options.floppy.path[0] = '\0';
}

/* ---- memory ---- */

uint8_t *mach_mem(uint32_t addr, bool write)
{
	// ST-RAM
	if (RAMBaseHost && addr < RAMSize && addr < 0xe00000)
		return RAMBaseHost + addr;
	// TOS
	if (ROMBaseHost && addr >= ROMBase && addr - ROMBase < ROMSize)
		return write ? NULL : ROMBaseHost + (addr - ROMBase);
	// FastRAM
	if (FastRAMBaseHost && FastRAMSize && addr >= FastRAMBase && addr - FastRAMBase < FastRAMSize)
		return FastRAMBaseHost + (addr - FastRAMBase);
	// ARAnyM's video RAM
	if (VideoRAMBaseHost && addr >= VideoRAMBase && addr - VideoRAMBase < ARANYMVRAMSIZE)
		return VideoRAMBaseHost + (addr - VideoRAMBase);
	return NULL;
}

/* ---- registers ---- */

void mach_get_regs(mach_regs *r)
{
	MakeSR();
	for (int i = 0; i < 8; i++) {
		r->d[i] = m68k_dreg(regs, i);
		r->a[i] = m68k_areg(regs, i);
	}
	r->pc = m68k_getpc();
	r->sr = regs.sr;
	uint32_t sp = m68k_areg(regs, 7);
	r->usp = regs.s ? regs.usp : sp;
	r->ssp = (regs.s && !regs.m) ? sp : regs.isp;
	r->msp = (regs.s && regs.m) ? sp : regs.msp;
	r->vbr = regs.vbr;
	r->sfc = regs.sfc;
	r->dfc = regs.dfc;
	r->cacr = regs.cacr;
}

bool mach_set_reg(const char *name, uint32_t v)
{
	if ((name[0] == 'd' || name[0] == 'D' || name[0] == 'a' || name[0] == 'A') &&
	    name[1] >= '0' && name[1] <= '7' && !name[2]) {
		if (name[0] == 'd' || name[0] == 'D')
			m68k_dreg(regs, name[1] - '0') = v;
		else
			m68k_areg(regs, name[1] - '0') = v;
		return true;
	}
	if (!strcasecmp(name, "sp")) {
		m68k_areg(regs, 7) = v;
	} else if (!strcasecmp(name, "pc")) {
		m68k_setpc(v);
	} else if (!strcasecmp(name, "sr")) {
		MakeSR();
		regs.sr = v;
		MakeFromSR();
	} else if (!strcasecmp(name, "usp")) {
		if (regs.s)
			regs.usp = v;
		else
			m68k_areg(regs, 7) = v;
	} else if (!strcasecmp(name, "ssp") || !strcasecmp(name, "isp")) {
		if (regs.s && !regs.m)
			m68k_areg(regs, 7) = v;
		else
			regs.isp = v;
	} else if (!strcasecmp(name, "msp")) {
		if (regs.s && regs.m)
			m68k_areg(regs, 7) = v;
		else
			regs.msp = v;
	} else if (!strcasecmp(name, "vbr")) {
		regs.vbr = v;
	} else if (!strcasecmp(name, "sfc")) {
		regs.sfc = v & 7;
	} else if (!strcasecmp(name, "dfc")) {
		regs.dfc = v & 7;
	} else if (!strcasecmp(name, "cacr")) {
		regs.cacr = v;
	} else {
		return false;
	}
	return true;
}

/* ---- the disassembler ---- */

bool mach_disasm_available(void)
{
#ifdef HAVE_DISASM_M68K
	return true;
#else
	return false;
#endif
}

int mach_disasm(uint32_t addr, char *buf, size_t n)
{
#ifdef HAVE_DISASM_M68K
	// only where there is memory: the disassembler reads it directly
	if (!mach_mem(addr, false) || !mach_mem(addr + 21, false))
		return 0;
	char line[256];
	disasm_info.memory_vma = addr;
	int len = m68k_disasm_to_buf(&disasm_info, line, 1);
	if (len <= 0)
		return 0;
	// the line starts with the address, [0000abcd]: leave it out
	const char *p = line;
	if (*p == '[') {
		const char *e = strchr(p, ']');
		if (e)
			p = e + 1;
	}
	while (*p == ' ' || *p == '\t')
		p++;
	snprintf(buf, n, "%s", p);
	size_t l = strlen(buf);
	while (l && (buf[l - 1] == '\n' || buf[l - 1] == ' '))
		buf[--l] = 0;
	return len;
#else
	(void)addr;
	(void)buf;
	(void)n;
	return 0;
#endif
}

bool mach_jit(void)
{
#ifdef USE_JIT
	return bx_options.jit.jit;
#else
	return false;
#endif
}

const char *mach_debug_unavailable(void)
{
#if defined(EXPERIMENTAL_MODERN040) || defined(EXPERIMENTAL_UPSTREAM_MODERN040)
	return "this ARAnyM runs the modern040 CPU core, which has no hook between"
		" instructions: build it without --enable-modern040-core for debug-run and debug-step";
#else
	if (mach_jit())
		return "the JIT compiler runs, and it does not stop at single instructions. Start"
			" ARAnyM with the interpreter for debug-run and debug-step: --option JIT:JIT:No"
			" (or JIT = No in [JIT] of its config)";
	return NULL;
#endif
}

/* ---- vm-info ---- */

void mach_info(sbuf *out)
{
	sb_printf(out, "Machine: %s, 68040, ST-RAM %u MB, FastRAM %u MB, CPU %s\n", version_string,
		(unsigned)(RAMSize >> 20), (unsigned)(FastRAMSize >> 20),
		mach_jit() ? "JIT (debug-run and debug-step need --option JIT:JIT:No)" : "interpreter");
#if defined(EXPERIMENTAL_MODERN040) || defined(EXPERIMENTAL_UPSTREAM_MODERN040)
	sb_puts(out, "CPU core: modern040 (debug-run and debug-step are not available)\n");
#endif
	if (bx_options.bootstrap_path[0])
		sb_printf(out, "Boots: %s %s\n", bx_options.bootstrap_path, bx_options.bootstrap_args);
	else if (bx_options.tos.tos_path[0])
		sb_printf(out, "ROM: %s\n", bx_options.tos.tos_path);
	else if (bx_options.tos.emutos_path[0])
		sb_printf(out, "ROM: EmuTOS %s\n", bx_options.tos.emutos_path);
	for (int i = 0; i < DISKS; i++)
		if (bx_options.disks[i].present)
			sb_printf(out, "SCSI %d: %s%s\n", i, bx_options.disks[i].path,
				bx_options.disks[i].readonly ? " (read-only)" : "");
	for (int c = 0; c < BX_MAX_ATA_CHANNEL; c++)
		for (int d = 0; d < 2; d++)
			if (bx_options.atadevice[c][d].present)
				sb_printf(out, "IDE %d: %s%s\n", c * 2 + d, bx_options.atadevice[c][d].path,
					bx_options.atadevice[c][d].isCDROM ? " (CD-ROM)" : "");
	for (int i = 0; i < HOSTFS_MAX_DRIVES; i++)
		if (bx_options.aranymfs.drive[i].rootPath[0])
			sb_printf(out, "HostFS %c: %s\n", 'A' + i, bx_options.aranymfs.drive[i].rootPath);
	sb_printf(out, "Video RAM: 0x%08x (%u KB)\n", (unsigned)VideoRAMBase, (unsigned)(ARANYMVRAMSIZE >> 10));
}
