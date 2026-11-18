/*
	Falcon VIDEL emulation

	(C) 2001-2008 ARAnyM developer team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "sysdeps.h"
#include "display_diagnostics.h"
#include "SDL_compat.h"

#include "sysdeps.h"
#include "hardware.h"
#include "cpu_emulation.h"
#include "memory-uae.h"
#include "hostscreen.h"
#include "host.h"
#include "icio.h"
#include "host_surface.h"
#include "videl.h"
#include "hardware.h"
#include "parameters.h"
#include "display_performance.h"
#include <algorithm>

#define DEBUG 0
#include "debug.h"

/*--- Defines ---*/

#define HW	getHWoffset()
#define VIDEL_COLOR_REGS_BEGIN	0xff9800UL
#define VIDEL_COLOR_REGS_END	(VIDEL_COLOR_REGS_BEGIN+256*4)

#define F_COLORS(i) handleRead(VIDEL_COLOR_REGS_BEGIN + (i))
#define STE_COLORS(i)	handleReadW(0xff8240 + (i<<1))

/*--- Constructor/destructor ---*/

VIDEL::VIDEL(memptr addr, uint32 size)
	: BASE_IO(addr, size), surface(NULL),
	prevVidelWidth(-1), prevVidelHeight(-1), prevVidelBpp(-1)
{
	reset();
}

VIDEL::~VIDEL(void)
{
	if (surface) {
		host->video->destroySurface(surface);
	}
}

void VIDEL::reset(void)
{
	stShifter = false;
	aspect_x = aspect_y = 1;
	updatePalette = true;
	forceRefresh();
}

bool VIDEL::isMyHWRegister(memptr addr)
{
	// FALCON VIDEL COLOR REGISTERS
	if (addr >= VIDEL_COLOR_REGS_BEGIN && addr < VIDEL_COLOR_REGS_END)
		return true;

	return BASE_IO::isMyHWRegister(addr);
}

/*--- BASE_IO functions ---*/

void VIDEL::handleWrite(uint32 addr, uint8 value)
{
	BASE_IO::handleWrite(addr, value);

	if ((addr >= VIDEL_COLOR_REGS_BEGIN && addr < VIDEL_COLOR_REGS_END) ||
		(addr >= 0xff8240 && addr < 0xff8260)) {
		updatePalette = true;		
		return;
	}

	if (addr == HW + 0x60) {
		setStShiftMode(value & 3);
	} else if ((addr & ~1) == HW + 0x66) {
		stShifter = false;
		updatePalette = true;
	} else if ((addr & ~1) == HW + 0x64 || (addr & ~1) == HW + 0x0e || (addr & ~1) == HW + 0x10) {
		forceRefresh();
	}

	if ((addr & ~1) == HW+0xc2) {
		updateAspect();
	}
}

/*--- Private functions ---*/

void VIDEL::setStShiftMode(uint8 mode)
{
	static const uint8 vgaMode[4] = {5, 9, 8, 0};
	static const uint8 rgbMode[4] = {0, 4, 6, 0};
	const uint8 videoMode = bx_options.video.monitor == 1 ? rgbMode[mode] : vgaMode[mode];

	stShifter = true;
	updatePalette = true;
	BASE_IO::handleWrite(HW + 0x60, mode);
	BASE_IO::handleWriteW(HW + 0x10, mode == 2 ? 40 : 80);
	BASE_IO::handleWriteW(HW + 0xc2, videoMode);
}

void VIDEL::updateAspect(void)
{
	int vco = handleReadW(HW + 0xc2);

	switch((vco>>2) & 3) {
		case 0:
			aspect_x = 4;
			break;
		case 1:
			aspect_x = 2;
			break;
		case 2:
			aspect_x = 1;
			break;
	}

	switch (vco & 3) {
		case 0:
			/* No interlace, no doubling */
			aspect_y = 2;
			break;
		case 1:
			/* No interlace, Doubling */
			aspect_y = 4;
			break;
		case 2:
			/* Interlace, no doubling */
			aspect_y = 1;
			break;
		case 3:
			/* Interlace, Doubling */
			aspect_y = 2;
			break;
	}

	if (bx_options.video.monitor == 1) {
		/* rvb */
		aspect_x >>= 1;
		if (aspect_x == 0) {
			aspect_x = 1;
			aspect_y <<= 1;
		}
	} else {
		/* vga */
		aspect_y >>= 1;
		if (aspect_y == 0) {
			aspect_y = 1;
			aspect_x <<= 1;
		}
	}
}

/*--- Protected functions ---*/

Uint32 VIDEL::getVramAddress(void)
{
	return (handleRead(HW + 1) << 16) | (handleRead(HW + 3) << 8) | handleRead(HW + 0x0d);
}

int VIDEL::getWidth(void)
{
	return (handleReadW(HW + 0x10) & 0x03ff) * 16 / getBpp();
}

int VIDEL::getHeight(void)
{
	int vdb = handleReadW(HW + 0xa8);
	int vde = handleReadW(HW + 0xaa);
	int vmode = handleReadW(HW + 0xc2);

	/* visible y resolution:
	 * Graphics display starts at line VDB and ends at line
	 * VDE. If interlace mode off unit of VC-registers is
	 * half lines, else lines.
	 */
	int yres = vde - vdb;
	if (!(vmode & 0x02))		// interlace
		yres >>= 1;
	if (vmode & 0x01)		// double
		yres >>= 1;

	return yres;
}

int VIDEL::getBpp(void)
{
	const int falcon = handleReadW(HW + 0x66);
	if (falcon & 0x0400) return 1;
	if (falcon & 0x0100) return 16;
	if (falcon & 0x0010) return 8;
	if (!stShifter) return 4;

	const int st = handleRead(HW + 0x60);
	if (st == 1) return 2;
	if (st == 2) return 1;
	return 4;
}

HostSurface *VIDEL::getSurface(void)
{
	int videlBpp = getBpp();
	int bpp = (videlBpp<=8) ? 8 : 16;
	int width = getWidth();
	int height = getHeight();

	if (width<64) {
		width = 64;
	}
	if (height<64) {
		height = 64;
	}
	const bool modeChanged = width != prevVidelWidth || height != prevVidelHeight || videlBpp != prevVidelBpp;
	if (modeChanged)
		aranym_display_trace("videl-mode w=%d h=%d bpp=%d",width,height,videlBpp);

	if (surface && ((prevVidelBpp == 16) != (videlBpp == 16))) {
		host->video->destroySurface(surface);
		surface = NULL;
	}
	if (!surface) {
		surface = host->video->createSurface(width,height,bpp);
	} else if (width != prevVidelWidth || height != prevVidelHeight) {
		surface->resize(width,height);
	}

	if (modeChanged) {
		updatePalette = true;
		forceRefresh();
		host->video->forceRefreshScreen();
	}
	prevVidelWidth = width;
	prevVidelHeight = height;
	prevVidelBpp = videlBpp;

	refreshScreen();

	if (bx_options.opengl.enabled && bx_options.autozoom.enabled) {
		surface->setParam(HostSurface::SURF_DRAW, HostSurface::DRAW_RESCALE);
	}
	
	return surface;
}

static Uint8 steColorComponent(unsigned component)
{
	const unsigned intensity = ((component & 7) << 1) | ((component >> 3) & 1);
	return (intensity << 4) | intensity;
}

void VIDEL::refreshPalette(void)
{
	SDL_Color palette[256];
	const int falcon = handleReadW(HW + 0x66);
	const bool useSt = stShifter && !(falcon & 0x0510);
	const int numColors = 1 << prevVidelBpp;
	int i, c;

	if (useSt) {
		/* STe palette registers, 4 bits/component */
		for (i=0; i<numColors; i++) {
			int color = STE_COLORS(i);

			palette[i].r = steColorComponent(color >> 8);
			palette[i].g = steColorComponent(color >> 4);
			palette[i].b = steColorComponent(color);
#if SDL_VERSION_ATLEAST(2, 0, 0)
			palette[i].a = WINDOW_ALPHA;
#endif
		}
	} else {
		/* Falcon palette registers, 6 bits/component */
		const int first = prevVidelBpp == 4 ? (falcon & 15) * 16 : 0;
		for (i=0; i<numColors; i++) {
			int offset = (first + i) << 2;

			c = F_COLORS(offset) & 0xfc;
			c |= (c>>6) & 3;
			palette[i].r = c;
			c = F_COLORS(offset+1) & 0xfc;
			c |= (c>>6) & 3;
			palette[i].g = c;
			c = F_COLORS(offset+3) & 0xfc;
			c |= (c>>6) & 3;
			palette[i].b = c;
#if SDL_VERSION_ATLEAST(2, 0, 0)
			palette[i].a = WINDOW_ALPHA;
#endif
		}
	}

	surface->setPalette(palette, 0, numColors);
}

void VIDEL::forceRefresh(void)
{
	if (!surface) {
		return;
	}

	surface->setDirtyRect(0,0,
		surface->getWidth(), surface->getHeight());
}

void VIDEL::refreshScreen(void)
{
	SDL_Surface *sdl_surf;

	if (!surface) {
		return;
	}
	sdl_surf = surface->getSdlSurface();
	if (!sdl_surf) {
		return;
	}

	if (updatePalette && prevVidelBpp != 16) {
		refreshPalette();
		updatePalette = false;
		surface->setDirtyRect(0,0,surface->getWidth(),surface->getHeight());
	}

	const int bpp = prevVidelBpp;
	const int width = surface->getWidth();
	const int height = surface->getHeight();
	const int tiles = surface->getDirtyWidth();
	const int hscroll = handleReadW(HW + 0x64) & 15;
	const int linewidth = handleReadW(HW + 0x10) & 0x03ff;
	const int lineoffset = handleReadW(HW + 0x0e) & 0x01ff;
	const int pitch = linewidth + lineoffset + (hscroll ? bpp : 0);
	// A fine-scrolled planar tile also reads the next group of plane words.
	const int overlap = (bpp != 16 && hscroll) ? bpp : 0;
	const int rowWords = bpp == 16 ? width : tiles * bpp + overlap;
	Uint16 *src = (Uint16 *)Atari2HostAddr(getVramAddress());
	Uint8 *dirty = surface->getDirtyRects();
	if (!dirty || !rowWords || !height)
		return;
	previousFrame.resize(size_t(rowWords) * height);

	AranymDisplayPerfScope perf(1,"videl");
	perf.width = width;
	perf.height = height;
	for (int top = 0; top < height; top += 16) {
		const int lines = std::min(16, height - top);
		const int tileRow = top / 16 * tiles;
		Uint16 *saved = &previousFrame[size_t(top) * rowWords];
		unsigned changedRows = 0;
		for (int y = 0; y < lines; y++) {
			if (memcmp(src + y * pitch, saved + y * rowWords, rowWords * sizeof(Uint16)))
				changedRows |= 1u << y;
		}
		if (perf.active) perf.blocks += tiles;
		if (changedRows || (surface->hasDirtyRect() && surface->getMaxDirtY() >= top &&
		                    surface->getMinDirtY() < top + lines)) {
			for (int tile = 0; tile < tiles; tile++) {
				const int left = tile * 16;
				const int cols = std::min(16, width - left);
				const int offset = tile * bpp;
				const int words = bpp == 16 ? cols : bpp + overlap;
				for (int y = 0; changedRows && y < lines; y++) {
					if ((changedRows & (1u << y)) &&
					    memcmp(src + y * pitch + offset, saved + y * rowWords + offset, words * sizeof(Uint16))) {
						if (perf.active) ++perf.changed;
						if (!dirty[tileRow + tile])
							surface->setDirtyRect(left, top, cols, lines);
						break;
					}
				}
				if (!dirty[tileRow + tile])
					continue;
				if (perf.active) ++perf.converted;
				perf.rectangle(left, top, cols, lines);
				for (int y = 0; y < lines; y++) {
					Uint16 *in = src + y * pitch + offset;
					Uint8 *out = (Uint8 *)sdl_surf->pixels + (top + y) * sdl_surf->pitch;
					if (bpp == 16) {
						for (int x = 0; x < cols; x++)
							((Uint16 *)out)[left + x] = SDL_SwapBE16(in[x]);
					} else if (cols == 16) {
						HostScreen::bitplaneToChunky(in, bpp, out + left, hscroll);
					} else {
						Uint8 pixels[16];
						HostScreen::bitplaneToChunky(in, bpp, pixels, hscroll);
						memcpy(out + left, pixels, cols);
					}
				}
			}
		}
		for (int y = 0; changedRows && y < lines; y++) {
			if (changedRows & (1u << y))
				memcpy(saved + y * rowWords, src + y * pitch, rowWords * sizeof(Uint16));
		}
		src += lines * pitch;
	}
}
