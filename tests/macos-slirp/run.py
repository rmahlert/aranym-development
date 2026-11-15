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
    parser.add_argument("--firmware", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    native = sys.platform == "darwin" and os.uname().machine == "arm64"
    if args.app and not native:
        parser.error("Mac acceptance requires native Apple Silicon")
    if args.linux_emulator and sys.platform != "linux":
        parser.error("Linux acceptance requires native Linux")
    if (args.reference_emulator or args.linux_emulator) and not args.firmware:
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
    result = {"native_mac_arm64": native and bool(args.app),
              "native_linux": bool(args.linux_emulator),
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
                    emulator = (args.reference_emulator or args.linux_emulator).resolve(strict=True)
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
                    f"[ETH0]\nType = slirp\nTunnel =\nHostIP = {router}\nAtariIP = {guest}\n"
                    "Netmask = 255.255.255.0\nMAC = 52:54:00:12:34:56\n")
                environment = {key: value for key, value in os.environ.items()
                               if not key.startswith(("DYLD_", "ARANYM_", "SDL_"))}
                if args.reference_emulator or args.linux_emulator:
                    environment.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
                log = args.output / ("slirp-" + label + ".txt")
                request_start, datagram_start = len(requests), len(datagrams)
                profile = {"name": label, "router": router, "guest": guest, "passed": False,
                           "guest_sha256": hashlib.sha256(code).hexdigest()}
                result["profiles"].append(profile)
                with log.open("w") as stream:
                    process = subprocess.Popen([str(emulator), "-c", str(config)], cwd=root, env=environment,
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
                profile["passed"] = (profile.get("host_status") == 0 and all(profile["guest_checks"].values())
                                     and "SLIRP_GUEST FAIL" not in text
                                     and all("/aranym-slirp/" + str(i) in profile["http_requests"] for i in range(3))
                                     and all("ARANYM SLIRP UDP " + str(i) in profile["udp_datagrams"] for i in range(3)))
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
