/*
 * Host MIDI transport.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef ARANYM_HOST_MIDI_H
#define ARANYM_HOST_MIDI_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct HostMidiOptions {
    std::string backend;  /* file/raw, sequencer, or portmidi */
    std::string input;
    std::string output;
    bool append_output = false;
    unsigned int sequencer_device = 0;
    void (*wakeup)(void *) = nullptr;
    void *opaque = nullptr;
};

struct HostMidiByte {
    std::uint8_t byte;
    std::uint64_t timestamp_ns;
};

struct HostMidiDevice {
    std::string name;
    std::string interface_name;
    bool input;
    bool output;
};

struct HostMidiStats {
    std::uint64_t input_dropped;
    std::uint64_t output_dropped;
    std::uint64_t late_output;
    std::uint64_t io_errors;
    std::uint64_t malformed_output;
    std::uint64_t normalized_realtime;
    std::uint64_t max_output_lateness_ns;
};

class HostMidiTransport {
public:
    HostMidiTransport();
    ~HostMidiTransport();
    HostMidiTransport(const HostMidiTransport &) = delete;
    HostMidiTransport &operator=(const HostMidiTransport &) = delete;

    bool open_endpoints(const HostMidiOptions &, std::uint64_t guest_now_ns);
    void close();
    void reset(std::uint64_t guest_now_ns);
    void service(std::uint64_t guest_now_ns);
    bool submit_output(std::uint8_t byte, std::uint64_t due_ns);
    bool poll_input(HostMidiByte &);
    bool active() const;
    bool preserves_raw_bytes() const;
    HostMidiStats stats() const;
    std::string error() const;

    static bool supports_portmidi();
    static std::vector<HostMidiDevice> devices(std::string &error);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

#endif
