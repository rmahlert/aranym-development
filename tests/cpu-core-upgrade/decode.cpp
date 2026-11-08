/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "sysconfig.h"
#include "sysdeps.h"
#include "readcpu.h"
void write_log(const TCHAR *, ...) {}
int main() {
    init_table68k();
    for (unsigned opcode = 0; opcode < 65536; ++opcode) {
        const auto &entry = table68k[opcode];
        if (entry.mnemo == i_ILLG || entry.clev > 4 ||
            (entry.unimpclev && entry.unimpclev <= 4)) continue;
        const auto canonical = entry.handler < 0 ? opcode : unsigned(entry.handler);
        std::printf("%04x %04x\n", opcode, canonical);
    }
    exit_table68k();
}
