# Using the ARAnyM MCP server

ARAnyM has a built-in [MCP](https://modelcontextprotocol.io/) (Model
Context Protocol) server. With it, an AI agent (Claude Code, or another
MCP client) can work an ARAnyM machine like a person at the keyboard:

- look at the screen
- type and use the mouse (GEM menus, dialogs, the desktop)
- run shell commands on a serial port and get their output
- change floppies
- look inside the 68040: registers, memory, breakpoints, single steps

It works on macOS and Linux.

## How it works

- **Every ARAnyM built with MCP offers it.** It is on by default. Each
  ARAnyM opens a socket in `/tmp/aranym-$UID/`, a folder only your user
  can open, and writes `<pid>.json` next to it, which says who it is.
- **`aranym -mcp-bridge` is the bridge.** It is the one MCP server your
  agent starts. It finds the running ARAnyMs and passes each tool call to
  the VM you name. It opens no window and starts no VM.
- **VMs can come and go.** The bridge keeps running. `vm-wait` waits for a
  VM to start or to end.

## Setup

1. Build ARAnyM. `configure` turns MCP on where it finds zlib and Unix
   sockets (`--enable-mcp` makes it required, `--disable-mcp` turns it
   off). Add `--enable-disasm=builtin` for the `disassemble` tool.
2. Add the bridge to your agent once.
   - Claude Code:
     `claude mcp add aranym -s user -- /path/to/aranym -mcp-bridge`
     (on macOS: `/path/to/ARAnyM.app/Contents/MacOS/aranym -mcp-bridge`)
   - Other MCP clients: add a stdio server with that command.
3. Start a VM from a shell, in its folder, as usual.
4. In the agent: run `vm-list`, then `vm-info` and `screenshot`.

**Headless works.** With `SDL_VIDEODRIVER=dummy` there is no window, but
the agent still sees the screen.

## Settings

| Setting | What it does |
|---|---|
| `ARANYM_MCP=0` | Turns the MCP server off for this ARAnyM |
| `ARANYM_NAME=x` | The VM's name in the tools (default: the name of the folder ARAnyM runs in) |
| `--option JIT:JIT:No` | The interpreter instead of the JIT: `debug-run` and `debug-step` need it |

## Tools

Every tool takes `vm`: a name or PID from `vm-list`. You may leave it out
while only one VM runs.

| Tool | What it does |
|---|---|
| `vm-list` | List the running VMs: name, PID, folder, who started it |
| `vm-wait` | Wait for a VM to start (`up`) or end (`gone`) |
| `vm-info` | Describe the VM: run state, PC, RAM, JIT, boot OS, disks, floppy, screen size, mouse, serial. Use it first |
| `vm-power` | `pause`, `resume`, `reset` (warm, or `cold`) or `quit` |
| `screenshot` | A picture of the screen (PNG); `region` crops, `scale` zooms, `pointer` marks the mouse |
| `wait-screen` | Wait until the screen changes, settles, or both. Use it instead of sleeping |
| `key` | Press keys and combinations by Atari scancode names (`ctrl-c`, `alt-x`, `undo`, `help`, `kp-enter`...) |
| `type-text` | Type a string (US layout) |
| `mouse-move`, `mouse-click`, `mouse-drag`, `mouse-button`, `mouse-wheel` | Use the mouse |
| `pointer-locate` | Say where the mouse pointer is, with a picture |
| `serial-write`, `serial-read`, `serial-expect` | Write, read and wait for text on the serial port |
| `serial-run` | Run a shell command on the serial port: its output and exit status in one call |
| `media` | Insert or eject a floppy image in drive A |
| `steps` | Several tool calls in one, stopping at the first wait that is not met |
| `debug-registers`, `register-write` | The 68040's registers and the next instructions; set a register |
| `memory-read`, `memory-write` | Read or patch memory: ST-RAM, FastRAM, video RAM, TOS (read only) |
| `disassemble` | 68k code at an address |
| `debug-run` | Run until the CPU gets to an address (up to 16 breakpoints) |
| `debug-step` | Run some instructions, then stop |

Inputs (`key`, `type-text`, the mouse tools) take `then`: wait for the
screen to change or settle, and take a screenshot, in the same call.

## The mouse

- With ARAnyM's fVDI driver taking events, the tools put the pointer
  exactly where they want it.
- Otherwise they send IKBD mouse packets, read where the guest has the
  pointer (Line-A, or what the guest's driver told ARAnyM), and correct
  until it is there. When nothing says where it is, they push it into the
  top-left corner first and count from there.
- The host's mouse needs no grab: the tools work whether the window has
  the mouse or not.

## The serial port

The serial tools reach two ports:

- **Modem 1, the MFP's USART** (BIOS device 6, `u:/dev/modem1` under
  FreeMiNT). ARAnyM does not emulate the USART otherwise; while the MCP
  server runs, its data register carries the tools' bytes, and their input
  raises the MFP's receive interrupt.
- **Modem 2, SCC channel B** (`u:/dev/modem2`). The tools see what the
  guest sends and give it their input before the configured `[SERIAL]`
  port's. While the MCP server runs, the line is always ready.

The input goes to the port the guest last wrote to.

EmuTOS does not drive the SCC on ARAnyM (it maps AUX to Modem 2 but skips
the SCC), so with EmuTOS use Modem 1. Under FreeMiNT, start a shell there
once, from a console window:

    bash -i <u:/dev/modem1 >u:/dev/modem1 2>&1 &

Then `serial-run` runs commands: `{"command": "ls -l /"}` returns the
listing and the exit status.

## The debugger

`debug-run` and `debug-step` stop the CPU between two instructions. That
needs the interpreter: start ARAnyM with `--option JIT:JIT:No` (or set
`JIT = No` in the `[JIT]` section). With the JIT they say so and do
nothing. `debug-registers`, `memory-read`, `memory-write`, `register-write`
and `disassemble` work either way; while the CPU runs, the registers are a
snapshot at a VBL.

Addresses are physical. The I/O space (0xf00000-0xffffff) is not read or
written.

## Safety

- The socket and the registry are in `/tmp/aranym-$UID/`, mode 0700: only
  your user can reach them. Any program of yours can.
- An agent can press keys, write memory and quit the VM, like you can.
  Work on copies of disk images you care about.
- `ARANYM_MCP=0` turns the server off for one run.

## The protocol

The bridge speaks MCP over stdio (JSON-RPC 2.0, protocol versions
2025-11-25 and 2025-06-18). Between the bridge and a VM it is the same
JSON-RPC, one message a line, on the VM's Unix socket. Long calls send
progress notifications when the client asks for them, and stop when it
cancels them.

The code is in `src/mcp/`: `server.c` (the socket, calls, queues and the
VBL hook), `tools.c` (the tools), `keys.c` (key names and the US layout),
`bridge.c`, `machine.cpp` (the glue to ARAnyM), and `tools.json` and
`instructions.txt`, which `embed.sh` turns into `schema_data.h`.
