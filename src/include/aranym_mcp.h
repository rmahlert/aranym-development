/*
 * aranym_mcp.h - the MCP server: what the rest of ARAnyM calls
 *
 * ARAnyM (C) 2026 ARAnyM developer team
 *
 * Every ARAnyM built with --enable-mcp serves MCP (the Model Context
 * Protocol) on a private Unix socket, /tmp/aranym-<uid>/<pid>.sock, and
 * describes itself next to it in <pid>.json. "aranym -mcp-bridge" is the
 * MCP server an agent starts: it finds the running VMs there and passes
 * each tool call to one. See MCP.md.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef ARANYM_MCP_H
#define ARANYM_MCP_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Start the server (after InitAll). False if it is off.
bool aranym_mcp_init(void);
/// Remove the socket and the registry file.
void aranym_mcp_done(void);
/// Whether the server runs.
bool aranym_mcp_on(void);

/// The 50 Hz VBL, on the emulator's main thread: input, calls from the
/// tools, and the pause (it returns when the VM may run again).
void aranym_mcp_vbl(void);

/// The host window's picture, after HostScreen::refresh drew it (an
/// SDL_Surface).
struct SDL_Surface;
void aranym_mcp_frame(struct SDL_Surface *screen);

/// The serial ports the tools reach: the MFP's USART (Modem 1, EmuTOS's
/// AUX) and SCC channel B (Modem 2).
#define ARANYM_MCP_SERIAL_MFP	1
#define ARANYM_MCP_SERIAL_SCC	2
/// The guest sent a byte on a serial port.
void aranym_mcp_serial_tx(int port, uint8_t b);
/// How many bytes the tools have queued for the guest to read on this
/// port: the input goes to the port the guest last wrote to (to both
/// before it wrote to either).
int aranym_mcp_serial_pending(int port);
/// Take one queued byte (call only when aranym_mcp_serial_pending() > 0).
uint8_t aranym_mcp_serial_rx(void);

/// The debugger's per-instruction hook (in the interpreter loop): nonzero
/// stops the CPU before the instruction at pc, until a tool lets it go on.
extern volatile int aranym_mcp_dbg_active;
void aranym_mcp_dbg_check(uint32_t pc);

/// "aranym -mcp-bridge": the MCP server on stdin and stdout.
int aranym_mcp_bridge_main(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* ARANYM_MCP_H */
