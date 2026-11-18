/*
	Audio DMA emulation

	ARAnyM (C) 2005 Patrice Mandin

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
 
/* Falcon DMA control/loop/interrupt behavior adapted from Hatari - Crossbar.c,
 * by Laurent Sallafranque, Nicolas Pomarede, Thomas Huth and the Hatari team.
 * Original license notice:
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 */
#include "sysdeps.h"
#include "hardware.h"
#include "cpu_emulation.h"
#include "memory-uae.h"
#include "audio_dma.h"
#include "audio_crossbar.h"
#include "peripheral_clock.h"

AUDIODMA::AUDIODMA(memptr addr, uint32 size) : BASE_IO(addr, size) { reset(); }
AUDIODMA::~AUDIODMA() {}

void AUDIODMA::reset()
{
    play = Channel();
    record = Channel();
    control = mode = 0;
    play_line = record_line = true;
}

void AUDIODMA::soundLine(bool recording, bool high, uint64_t when)
{
    (recording ? record_line : play_line) = high;
    if (!getMFP()) return;
    // Selected active sources share the physical, active-low Falcon sound lines.
    const bool gpip_high = !((control & 0x100) && !play_line) && !((control & 0x200) && !record_line);
    const bool timer_high = !((control & 0x400) && !play_line) && !((control & 0x800) && !record_line);
    getMFP()->setGPIPbit_at(0x80, gpip_high ? 0x80 : 0, when);
    getMFP()->setTimerAInput(timer_high, when);
}

void AUDIODMA::changeControl(uint16 value, uint64_t when)
{
    const uint16 old = control;
    control = value & 0x0fb3;
    for (unsigned i = 0; i < 2; ++i) {
        Channel &channel = i ? record : play;
        const unsigned enable = i ? 0x10 : 1;
        if (!(control & enable)) {
            if (channel.running) soundLine(i != 0, true, when);
            channel.running = false;
        } else if (!(old & enable)) {
            channel.current = channel.start;
            channel.active_end = channel.end;
            channel.repeat = (control & (i ? 0x20 : 2)) != 0;
            channel.frame_start = true;
            channel.running = channel.current < channel.active_end;
            if (channel.running) soundLine(i != 0, false, when);
            else control &= ~enable;
        }
    }
    soundLine(false, play_line, when);
    if (getCROSSBAR()) getCROSSBAR()->configuration_changed(when);
}

void AUDIODMA::finish(Channel &channel, bool recording, uint64_t when)
{
    soundLine(recording, true, when);
    const unsigned enable = recording ? 0x10 : 1;
    // Falcon latches loop mode at start; changing the control bit mid-frame
    // only affects the next explicit stop/start.
    if (channel.repeat && channel.start < channel.end) {
        channel.current = channel.start;
        channel.active_end = channel.end;
        channel.frame_start = true;
        soundLine(recording, false, when);
    } else {
        channel.running = false;
        control &= ~enable;
    }
}

uae_u8 AUDIODMA::handleRead(uaecptr addr)
{
    peripheral_clock_event_time_ns();
    const Channel &channel = (control & 0x80) ? record : play;
    const unsigned offset = addr - getHWoffset();
    if (offset < 2) return offset ? control & 255 : control >> 8;
    if (offset == 0x20 || offset == 0x21) return offset & 1 ? mode & 255 : mode >> 8;
    const uint32 value = offset <= 7 ? channel.start : offset <= 13 ? channel.current : channel.end;
    switch (offset) {
    case 3: case 9: case 15: return value >> 16;
    case 5: case 11: case 17: return value >> 8;
    case 7: case 13: case 19: return value;
    default: return 0;
    }
}

void AUDIODMA::handleWrite(uaecptr addr, uae_u8 value)
{
    const uint64_t when = peripheral_clock_event_time_ns();
    const unsigned offset = addr - getHWoffset();
    if (offset < 2) {
        changeControl(offset ? (control & 0xff00) | value : (control & 255) | (uint16(value) << 8), when);
        return;
    }
    if (offset == 0x20 || offset == 0x21) {
        mode = offset & 1 ? (mode & 0xff00) | value : (mode & 255) | (uint16(value) << 8);
        if (getCROSSBAR()) getCROSSBAR()->configuration_changed(when);
        return;
    }
    Channel &channel = (control & 0x80) ? record : play;
    uint32 *target = offset <= 7 ? &channel.start : &channel.end;
    unsigned shift;
    switch (offset) {
    case 3: case 15: shift = 16; break;
    case 5: case 17: shift = 8; break;
    case 7: case 19: shift = 0; break;
    default: return;
    }
    *target = ((*target & ~(255u << shift)) | (uint32(value) << shift)) & 0xfffffe;
}

void AUDIODMA::updateMode()
{
    const uint64_t when = peripheral_clock_event_time_ns();
    if (getCROSSBAR()) getCROSSBAR()->configuration_changed(when);
}

bool AUDIODMA::playback_word(unsigned slot, int16_t &sample, uint64_t when)
{
    if (!play.running) return false;
    play.frame_start = false;
    const bool word = (mode & 0x40) != 0;
    const bool mono = !word && (mode & 0x80);
    const unsigned bytes = word ? 2 : 1;
    if (play.current > STRAM_END - bytes || play.current + bytes > play.active_end) {
        finish(play, false, when);
        return false;
    }
    const uint8 *source = phys_get_real_address(play.current);
    sample = word ? int16_t((uint16(source[0]) << 8) | source[1]) : int8_t(source[0]);
    // In mono mode the same byte is presented on both slots of a stereo pair.
    if (!mono || (slot & 1)) play.current += bytes;
    if (play.current >= play.active_end) finish(play, false, when);
    return true;
}

void AUDIODMA::record_word(int16_t sample, unsigned slot, unsigned tracks, uint64_t when)
{
    if (!record.running || slot >= tracks * 2) return;
    // Falcon record transfers use words except in the eight-bit mono mode.
    const unsigned bytes = !(mode & 0x40) && (mode & 0x80) ? 1 : 2;
    if (record.current > STRAM_END - bytes || record.current + bytes > record.active_end) {
        finish(record, true, when);
        return;
    }
    uint8 *target = phys_get_real_address(record.current);
    if (bytes == 2) { target[0] = uint16(sample) >> 8; target[1] = uint16(sample); }
    else target[0] = uint16(sample);
    record.current += bytes;
    if (record.current >= record.active_end) finish(record, true, when);
}
