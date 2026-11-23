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

#ifndef AUDIODMA_H
#define AUDIODMA_H
#include "icio.h"
#include <stdint.h>

class AUDIODMA : public BASE_IO {
    struct Channel {
        uint32 start, end, current, active_end;
        bool running, repeat, frame_start;
    } play, record;
    uint16 control, mode;
    bool play_line, record_line;
    void changeControl(uint16 value, uint64_t when);
    void soundLine(bool recording, bool high, uint64_t when);
    void finish(Channel &channel, bool recording, uint64_t when);
public:
    AUDIODMA(memptr, uint32);
    ~AUDIODMA();
    void reset();
    virtual uae_u8 handleRead(uaecptr addr);
    virtual void handleWrite(uaecptr addr, uae_u8 value);
    void updateMode();
    void reset_sound(uint64_t when);
    bool playback_active() const { return play.running; }
    bool playback_frame_start() const { return play.frame_start; }
    bool recording_active() const { return record.running; }
    bool recording_frame_start() const { return record.frame_start; }
    unsigned playback_tracks() const { return ((mode >> 8) & 3) + 1; }
    unsigned monitor_track() const { return (mode >> 12) & 3; }
    unsigned legacy_frequency() const { return mode & 3; }
    bool eight_bit() const { return !(mode & 0x40); }
    bool playback_word(unsigned slot, int16_t &sample, uint64_t when);
    void record_word(int16_t sample, unsigned slot, unsigned tracks, uint64_t when);
};
#endif
