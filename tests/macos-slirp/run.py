#!/usr/bin/env python3
"""Exercise real NatFeat Ethernet and SLIRP from an owned Atari guest.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import threading

HERE = Path(__file__).resolve().parent
REQUIRED = ("PASS DHCP", "PASS DNS", "PASS ARP UDP TCP HTTP restart", "PASS shutdown with active receiver")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--app", type=Path)
    mode.add_argument("--reference-emulator", type=Path)
    mode.add_argument("--linux-emulator", type=Path)
    mode.add_argument("--windows-emulator", type=Path)
    parser.add_argument("--firmware", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--require-jit", action="store_true", help="require executed guest JIT instructions")
    parser.add_argument('--architecture',choices=('arm64','arm32','x86_64'),default='arm64')
    parser.add_argument('--qemu',type=Path,help='explicitly cross-emulated ARM debug execution')
    parser.add_argument('--qemu-sysroot',type=Path)
    parser.add_argument('--qemu-library-path')
    parser.add_argument('--fpu-jit',action='store_true',help='exercise networking with the guarded FPU JIT profile active')
    args = parser.parse_args()
    if args.qemu and (not args.linux_emulator or not args.qemu_sysroot or not args.qemu_library_path):
        parser.error('--qemu requires standalone Linux emulator, target sysroot and library path')
    prefix=[]
    if args.qemu:
        prefix=[str(args.qemu.resolve(strict=True)),'-L',str(args.qemu_sysroot.resolve(strict=True)),
                '-U','HOME','-E','LD_LIBRARY_PATH='+args.qemu_library_path]
    witness_arch='UPSTREAM' if args.architecture=='x86_64' else 'ARM32' if args.architecture=='arm32' else 'ARM64'
    arm_fpu=args.fpu_jit and args.require_jit and args.architecture!='x86_64'
    if args.fpu_jit and not arm_fpu and args.architecture != "x86_64" and (args.app or args.reference_emulator or args.qemu):
        parser.error('ARM FPU networking requires --require-jit as well as --fpu-jit')
    native = sys.platform == "darwin" and os.uname().machine in ("arm64", "x86_64")
    if args.app and native and os.uname().machine != args.architecture:
        parser.error("Mac runner and --architecture must match")
    if args.app and not native:
        parser.error("Mac acceptance requires native macOS")
    if args.linux_emulator and sys.platform != "linux":
        parser.error("Linux acceptance requires native Linux")
    if args.windows_emulator and sys.platform != "win32":
        parser.error("Windows acceptance requires native Windows")
    if (args.reference_emulator or args.linux_emulator or args.windows_emulator) and not args.firmware:
        parser.error("standalone emulator needs --firmware")
    args.output.mkdir(parents=True, exist_ok=True)
    requests, datagrams = [], []
    stop = threading.Event()

    class HTTP(BaseHTTPRequestHandler):
        def do_GET(self):
            match = re.fullmatch(r"/aranym-slirp/([0-2])", self.path)
            if not match:
                self.send_error(400)
                return
            requests.append(self.path)
            body = ("ARANYM SLIRP HTTP PASS " + match[1]).encode()
            self.send_response(200)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *_):
            pass

    # Only loopback fixtures are exposed. The guest reaches these through
    # SLIRP's virtual-router alias, exercising actual UDP/TCP host sockets.
    http = ThreadingHTTPServer(("127.0.0.1", 0), HTTP)
    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp.bind(("127.0.0.1", 0))
    udp.settimeout(0.2)

    def echo():
        while not stop.is_set():
            try:
                body, peer = udp.recvfrom(4096)
            except socket.timeout:
                continue
            datagrams.append(body.decode("ascii", errors="replace"))
            udp.sendto(body, peer)

    http_thread = threading.Thread(target=http.serve_forever, daemon=True)
    udp_thread = threading.Thread(target=echo, daemon=True)
    http_thread.start()
    udp_thread.start()
    result = {"jit_required": args.require_jit, "fpu_jit":args.fpu_jit, "native_mac_arm64": native and bool(args.app) and args.architecture == "arm64",
              "native_mac_x86_64": native and bool(args.app) and args.architecture == "x86_64",
              "native_linux": bool(args.linux_emulator) and not bool(args.qemu),
              "cross_emulated":bool(args.qemu), "execution_arch":"x86_64" if args.fpu_jit and not arm_fpu else args.architecture,
              "native_windows": bool(args.windows_emulator),
              "reference_only": bool(args.reference_emulator), "profiles": []}
    try:
        # Distinguish unavailable host DNS from a guest SLIRP DNS regression.
        result["host_dns_ipv4"] = sorted({p[4][0] for p in socket.getaddrinfo("example.com", 80, socket.AF_INET)})
        template = bytes.fromhex((HERE / "guest.hex").read_text())
        for label, router, guest in (("default", "10.0.2.2", "10.0.2.15"),
                                     ("custom", "10.42.0.2", "10.42.0.15")):
            with tempfile.TemporaryDirectory(prefix="ARAnyM SLIRP " + label + " ") as directory:
                root = Path(directory)
                if args.app:
                    app = root / "ARAnyM network test.app"
                    shutil.copytree(args.app.resolve(strict=True), app, symlinks=True)
                    emulator = app / "Contents/MacOS/aranym"
                    firmware = app / "Contents/Resources/emutos-aranym.img"
                else:
                    emulator = (args.reference_emulator or args.linux_emulator or args.windows_emulator).resolve(strict=True)
                    firmware = args.firmware.resolve(strict=True)
                code = template
                for old, port in ((0xa55a1234, http.server_port), (0xa55a5678, udp.getsockname()[1])):
                    before = old.to_bytes(4, "big")
                    if code.count(before) != 1:
                        raise RuntimeError("guest fixture-port marker is missing or ambiguous")
                    code = code.replace(before, port.to_bytes(4, "big"))
                program = root / "NETTEST.PRG"
                program.write_bytes(code)
                (root / "Aranym_files").mkdir()
                config = root / "network-test.config"
                config.write_text(
                    f"[GLOBAL]\nFastRAM = 8\nEmuTOS = {firmware}\nBootstrap = {program}\nTOS =\nFloppy =\n"
                    + ("FixedMemoryOffset = 60000000\n" if args.architecture == "x86_64" else "") +
                    f"[ETH0]\nType = slirp\nTunnel =\nHostIP = {router}\nAtariIP = {guest}\n"
                    "Netmask = 255.255.255.0\nMAC = 52:54:00:12:34:56\n")
                if args.require_jit or args.fpu_jit:
                    with config.open("a") as stream:
                        stream.write("[JIT]\nJIT = Yes\nJITFPU = "+("Yes" if args.fpu_jit else "No")+"\nJITCacheSize = 8192\n")
                environment = {key: value for key, value in os.environ.items()
                               if key != "HOME" and not key.startswith(("DYLD_", "ARANYM_", "SDL_"))}
                if args.reference_emulator or args.linux_emulator or args.windows_emulator:
                    environment.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
                if args.require_jit:
                    environment["ARANYM_UPSTREAM_JIT_DIAGNOSTICS" if witness_arch == "UPSTREAM" else "ARANYM_"+witness_arch+"_JIT_DIAGNOSTICS"] = "1"
                if arm_fpu:
                    environment['ARANYM_ARM_FPU_DIAGNOSTICS']='1'
                log = args.output / ("slirp-" + label + ".txt")
                request_start, datagram_start = len(requests), len(datagrams)
                profile = {"name": label, "router": router, "guest": guest, "passed": False,
                           "guest_sha256": hashlib.sha256(code).hexdigest()}
                result["profiles"].append(profile)
                with log.open("w") as stream:
                    process = subprocess.Popen(prefix+[str(emulator), "-c", str(config)], cwd=root, env=environment,
                                               stdout=stream, stderr=subprocess.STDOUT)
                    try:
                        profile["host_status"] = process.wait(timeout=50)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                        profile["failure"] = "networking or shutdown timeout"
                text = log.read_text(errors="replace")
                profile["http_requests"] = requests[request_start:]
                profile["udp_datagrams"] = datagrams[datagram_start:]
                profile["guest_checks"] = {marker: "SLIRP_GUEST " + marker in text for marker in REQUIRED}
                if witness_arch == "UPSTREAM":
                    witness = re.search(r"UPSTREAM-JIT compiled-insns=\d+ cached-dispatches=\d+ executed-translated=\d+ executed-guest=(\d+)", text)
                    profile["executed_guest_jit"] = int(witness[1]) if witness else 0
                else:
                    witness = re.search(witness_arch+r"-JIT executed-native=(\d+) executed-guest=(\d+)", text)
                    profile["executed_guest_jit"] = int(witness[2]) if witness else 0
                profile["passed"] = (profile.get("host_status") == 0 and all(profile["guest_checks"].values())
                                     and "SLIRP_GUEST FAIL" not in text
                                     and all("/aranym-slirp/" + str(i) in profile["http_requests"] for i in range(3))
                                     and all("ARANYM SLIRP UDP " + str(i) in profile["udp_datagrams"] for i in range(3))
                                     and (not args.require_jit or profile["executed_guest_jit"] > 0)
                                     and (not args.fpu_jit or
                                          ('ARM-FPU-JIT available=yes enabled=yes' in text if arm_fpu else
                                           'MODERN040_FPU_JIT available=yes enabled=yes frontend=pinned-softfloat' in text)))
                (args.output / "slirp-network.json").write_text(json.dumps(result, indent=2) + "\n")
                print(text[-10000:])
                if not profile["passed"]:
                    raise RuntimeError("SLIRP " + label + " profile failed; see retained log and manifest")
        result["passed"] = True
        (args.output / "slirp-network.json").write_text(json.dumps(result, indent=2) + "\n")
        print("PASS: DHCP, DNS, UDP echo, TCP/HTTP, receiver restart and active-receiver shutdown on both subnets")
    finally:
        stop.set()
        http.shutdown()
        http.server_close()
        http_thread.join(timeout=2)
        udp_thread.join(timeout=2)
        udp.close()


if __name__ == "__main__":
    main()
