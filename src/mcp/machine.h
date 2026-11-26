/*
 * machine.h - what the MCP tools reach in ARAnyM (machine.cpp)
 *
 * The MCP server is C; ARAnyM is C++. These are the few things the tools
 * do to the machine. Call them on the emulator's main thread only: from
 * the VBL (aranym_mcp_vbl) or through mcp_main_call.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef AMCP_MACHINE_H
#define AMCP_MACHINE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "json.h"

#ifdef __cplusplus
extern "C" {
#endif

/* IKBD mouse button bits */
#define MACH_BUTTON_RIGHT	1
#define MACH_BUTTON_LEFT	2

/// An IKBD key event: the Atari scancode, | 0x80 to release.
void mach_key(uint8_t code);
/// Put the pointer at x, y with these buttons down (MACH_BUTTON_*). True
/// when a driver takes absolute positions (fVDI does); then it is there.
bool mach_mouse_abs(int x, int y, int buttons);
/// A relative IKBD mouse packet.
void mach_mouse_rel(int dx, int dy, int buttons);
/// Where the guest's pointer is, when a driver says so.
bool mach_pointer(int *x, int *y);
/// Turn the wheel one notch: up (-1) or down (1).
void mach_wheel(int dir);
/// Whether fVDI takes the mouse as absolute positions.
bool mach_mouse_absolute(void);

void mach_reset(bool cold);
void mach_quit(void);
/// Keep the host window alive while the CPU is stopped; true when the
/// user closed the window (ARAnyM is quitting).
bool mach_service_events(void);

const char *mach_floppy_path(void);
bool mach_floppy_insert(const char *path);
void mach_floppy_eject(void);

/// The host address of a guest byte in RAM, ROM or video RAM, or NULL (I/O
/// space, or nothing there). ROM can't be written.
uint8_t *mach_mem(uint32_t addr, bool write);

typedef struct {
	uint32_t d[8], a[8];
	uint32_t pc, sr, usp, ssp, msp, vbr, sfc, dfc, cacr;
} mach_regs;

void mach_get_regs(mach_regs *r);
/// Set a register by name (d0-d7, a0-a7, sp, pc, sr, usp, ssp, msp, vbr,
/// sfc, dfc, cacr). False: no such register.
bool mach_set_reg(const char *name, uint32_t v);

/// Whether the built-in 68k disassembler is there.
bool mach_disasm_available(void);
/// One instruction at addr as text; returns its length in bytes (0: none).
int mach_disasm(uint32_t addr, char *buf, size_t n);

/// Raise the MFP's receive interrupt when the tools have serial input.
void mach_serial_poll(void);

/// Whether the JIT compiler runs.
bool mach_jit(void);
/// Why debug-run and debug-step can't stop the CPU here, or NULL.
const char *mach_debug_unavailable(void);

/// The machine, for vm-info: RAM, ROM/OS, JIT, video, disks, network.
void mach_info(sbuf *out);

/* the other way, server.c for machine.cpp */
/// Whether a picture is wanted at this VBL (all of them while the tools
/// are busy, fewer while idle).
bool mcp_frame_wanted(void);
/// A picture of the screen, 0x00RRGGBB pixels.
void mcp_frame_submit(const uint32_t *px, int w, int h);

#ifdef __cplusplus
}
#endif

#endif
