/*
 * Timestamped host MIDI input/output.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "host_midi.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <mutex>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#include <system_error>
#else
#include <pthread.h>
#include <signal.h>
#endif
#ifdef __linux__
#include <linux/soundcard.h>
#endif
#ifdef HAVE_PORTMIDI
#include <portmidi.h>
#endif

#ifndef O_BINARY
#define O_BINARY 0
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK 0
#endif

namespace {
using Clock = std::chrono::steady_clock;
using U64 = std::uint64_t;
constexpr std::size_t QueueSize = 16384;
constexpr U64 Million = 1000000;

U64 host_time_ns()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now().time_since_epoch()).count();
}

U64 add_time(U64 a, U64 b)
{
    return std::numeric_limits<U64>::max() - a < b ?
        std::numeric_limits<U64>::max() : a + b;
}

bool same_file(int first, int second, const struct stat &a, const struct stat &b)
{
#ifdef _WIN32
    (void)a;
    (void)b;
    BY_HANDLE_FILE_INFORMATION one, two;
    if (!GetFileInformationByHandle(reinterpret_cast<HANDLE>(_get_osfhandle(first)), &one) ||
        !GetFileInformationByHandle(reinterpret_cast<HANDLE>(_get_osfhandle(second)), &two))
        return false;
    return one.dwVolumeSerialNumber == two.dwVolumeSerialNumber &&
           one.nFileIndexHigh == two.nFileIndexHigh && one.nFileIndexLow == two.nFileIndexLow;
#else
    (void)first;
    (void)second;
    return a.st_dev == b.st_dev && a.st_ino == b.st_ino;
#endif
}

struct QueueByte {
    std::uint8_t byte;
    U64 timestamp;
    std::uint32_t generation;
};

template<class T, std::size_t Size> class ByteQueue {
    std::array<T, Size> values;
    std::atomic<std::uint32_t> write_index{0}, read_index{0};
public:
    bool push(const T &value)
    {
        const auto head = write_index.load(std::memory_order_relaxed);
        if (head - read_index.load(std::memory_order_acquire) >= Size)
            return false;
        values[head % Size] = value;
        write_index.store(head + 1, std::memory_order_release);
        return true;
    }
    bool pop(T &value)
    {
        const auto tail = read_index.load(std::memory_order_relaxed);
        if (tail == write_index.load(std::memory_order_acquire))
            return false;
        value = values[tail % Size];
        read_index.store(tail + 1, std::memory_order_release);
        return true;
    }
    std::size_t free() const
    {
        return Size - (write_index.load(std::memory_order_acquire) -
                       read_index.load(std::memory_order_acquire));
    }
};

enum class Backend { Raw, Sequencer, PortMidi };

#ifdef HAVE_PORTMIDI
int midi_length(std::uint8_t status)
{
    if (status < 0x80) return 0;
    if (status < 0xf0)
        return (status & 0xe0) == 0xc0 ? 2 : 3;
    switch (status) {
    case 0xf1: case 0xf3: return 2;
    case 0xf2: return 3;
    case 0xf6: case 0xf7: return 1;
    default: return status >= 0xf8 ? 1 : 0;
    }
}

/* PortMidi calls are serialized; a stalled driver never owns a CPU-side lock. */
struct PortMidiContext {
    std::mutex mutex;
    unsigned references = 0;
    std::thread::id initialization_thread;
};

PortMidiContext &portmidi_context()
{
    /* A driver can outlive a bounded close; retain this synchronization object. */
    static PortMidiContext *context = new PortMidiContext;
    return *context;
}

std::string portmidi_error(PmError error)
{
    if (error == pmHostError) {
        char text[PM_HOST_ERROR_MSG_LEN] = {};
        Pm_GetHostErrorText(text, sizeof(text));
        return text;
    }
    const char *text = Pm_GetErrorText(error);
    return text ? text : "Unknown PortMidi error";
}

bool portmidi_acquire(std::string &error)
{
    auto &context = portmidi_context();
    if (!context.references) {
        const auto thread = std::this_thread::get_id();
        if (context.initialization_thread != std::thread::id() &&
            context.initialization_thread != thread) {
            error = "Reopen MIDI devices from the original MIDI control thread";
            return false;
        }
        const auto result = Pm_Initialize();
        if (result < 0) {
            error = portmidi_error(result);
            return false;
        }
        context.initialization_thread = thread;
    }
    ++context.references;
    return true;
}

void portmidi_release()
{
    auto &context = portmidi_context();
    if (context.references && --context.references == 0)
        Pm_Terminate();
}

PmDeviceID portmidi_device(const std::string &name, bool input,
                           std::string &error)
{
    if (name == "default")
        return input ? Pm_GetDefaultInputDeviceID() : Pm_GetDefaultOutputDeviceID();
    PmDeviceID selected = pmNoDevice;
    for (PmDeviceID id = 0; id < Pm_CountDevices(); ++id) {
        const auto *info = Pm_GetDeviceInfo(id);
        if (!info || !(input ? info->input : info->output)) continue;
        const std::string device_name = info->name ? info->name : "";
        const std::string full_name = std::string(info->interf ? info->interf : "") +
                                      ": " + device_name;
        if (name != device_name && name != full_name) continue;
        if (selected != pmNoDevice) {
            error = "MIDI device name is ambiguous: " + name;
            return pmNoDevice;
        }
        selected = id;
    }
    if (selected == pmNoDevice) error = "MIDI device is unavailable: " + name;
    return selected;
}

struct MidiPacketizer {
    std::uint8_t running = 0, status = 0, count = 0;
    std::uint32_t message = 0, sysex_word = 0;
    unsigned sysex_count = 0;
    bool sysex = false;

    void reset() { *this = MidiPacketizer(); }

    template<class Emit> bool feed(std::uint8_t byte, Emit emit,
                                   bool &normalized, bool &malformed)
    {
        normalized = malformed = false;
        if (byte >= 0xf8) {
            normalized = sysex && sysex_count;
            return emit(byte);
        }
        if (sysex && (byte < 0x80 || byte == 0xf7)) {
            sysex_word |= std::uint32_t(byte) << (8 * sysex_count++);
            if (sysex_count == 4 || byte == 0xf7) {
                const auto word = sysex_word;
                sysex_word = 0;
                sysex_count = 0;
                if (byte == 0xf7) sysex = false;
                return emit(word);
            }
            return true;
        }
        if (byte & 0x80) {
            if (sysex) {
                /* End a truncated message before a new non-realtime status. */
                sysex_word |= std::uint32_t(0xf7) << (8 * sysex_count++);
                if (!emit(sysex_word)) return false;
                malformed = true;
            }
            sysex = false;
            sysex_word = sysex_count = 0;
            running = byte < 0xf0 ? byte : 0;
            status = byte;
            message = byte;
            count = 1;
            if (byte == 0xf0) {
                sysex = true;
                sysex_word = byte;
                sysex_count = 1;
                status = count = 0;
                return true;
            }
            const int length = midi_length(byte);
            if (!length) {
                status = count = 0;
                malformed = true;
                return true;
            }
            if (length == 1) {
                status = count = 0;
                return emit(byte);
            }
            return true;
        }
        if (!status) {
            if (!running) {
                malformed = true;
                return true;
            }
            status = running;
            message = status;
            count = 1;
        }
        message |= std::uint32_t(byte) << (8 * count++);
        if (count == midi_length(status)) {
            const auto word = message;
            status = count = 0;
            return emit(word);
        }
        return true;
    }
};
#endif

struct TransportState {
    Backend backend = Backend::Raw;
    HostMidiOptions options;
    ByteQueue<QueueByte, QueueSize> input, output;
    std::atomic<bool> stopping{false}, active{false};
    std::atomic<std::uint32_t> generation{1}, anchor_sequence{0};
    std::atomic<U64> anchor_guest{0}, anchor_host{0};
    std::atomic<U64> input_dropped{0}, output_dropped{0}, late_output{0};
    std::atomic<U64> io_errors{0}, malformed_output{0}, normalized_realtime{0};
    std::atomic<U64> max_output_lateness{0};
    std::mutex error_mutex, wait_mutex;
    std::condition_variable wake, finished;
    std::string error_text;
    bool done = false;
    int input_fd = -1, output_fd = -1;
#ifdef _WIN32
    HANDLE disk_input = INVALID_HANDLE_VALUE;
#endif
    std::array<unsigned char, 8> oss_input{};
    unsigned oss_input_count = 0;
#ifdef HAVE_PORTMIDI
    bool portmidi_owned = false, input_sysex = false;
    PortMidiStream *pm_input = nullptr, *pm_output = nullptr;
    PmDeviceID pm_input_id = pmNoDevice, pm_output_id = pmNoDevice;
    MidiPacketizer packetizer;
#endif

    void synchronize(U64 now)
    {
        anchor_sequence.fetch_add(1, std::memory_order_acq_rel);
        anchor_host.store(host_time_ns(), std::memory_order_relaxed);
        anchor_guest.store(now, std::memory_order_relaxed);
        anchor_sequence.fetch_add(1, std::memory_order_release);
    }

    void anchor(U64 &guest, U64 &host) const
    {
        std::uint32_t before, after;
        do {
            before = anchor_sequence.load(std::memory_order_acquire);
            guest = anchor_guest.load(std::memory_order_relaxed);
            host = anchor_host.load(std::memory_order_relaxed);
            after = anchor_sequence.load(std::memory_order_acquire);
        } while ((before & 1) || before != after);
    }

    U64 to_guest(U64 host) const
    {
        U64 g, h;
        anchor(g, h);
        return host >= h ? add_time(g, host - h) : (g > h - host ? g - (h - host) : 0);
    }

    U64 to_host(U64 guest) const
    {
        U64 g, h;
        anchor(g, h);
        return guest >= g ? add_time(h, guest - g) : (h > g - guest ? h - (g - guest) : 0);
    }

    void fail(const std::string &text)
    {
        io_errors.fetch_add(1, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(error_mutex);
        error_text = text;
    }

    void received(std::uint8_t byte, U64 timestamp, std::uint32_t epoch)
    {
        if (stopping.load(std::memory_order_acquire)) return;
        if (!input.push(QueueByte{byte, timestamp, epoch})) {
            input_dropped.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (options.wakeup && !stopping.load(std::memory_order_acquire))
            options.wakeup(options.opaque);
    }

    bool open_raw()
    {
        int flags = O_BINARY | O_NONBLOCK;
        if (!options.input.empty() && options.input == options.output) {
            struct stat info;
            if (::stat(options.input.c_str(), &info) == 0 && !S_ISREG(info.st_mode)) {
                input_fd = output_fd = ::open(options.input.c_str(), O_RDWR | flags);
                if (input_fd < 0) {
                    fail("Cannot open full-duplex MIDI device '" + options.input + "': " + std::strerror(errno));
                    return false;
                }
                return true;
            }
        }
        if (!options.input.empty()) {
            input_fd = ::open(options.input.c_str(), O_RDONLY | flags);
            if (input_fd < 0) {
                fail("Cannot open MIDI input '" + options.input + "': " + std::strerror(errno));
                return false;
            }
#ifdef _WIN32
            HANDLE handle = reinterpret_cast<HANDLE>(_get_osfhandle(input_fd));
            if (GetFileType(handle) == FILE_TYPE_DISK) disk_input = handle;
#endif
        }
        if (!options.output.empty()) {
            output_fd = ::open(options.output.c_str(), O_WRONLY |
                (backend == Backend::Sequencer ? 0 : O_CREAT) | flags |
                (options.append_output ? O_APPEND : 0), 0664);
            if (output_fd < 0) {
                fail("Cannot open MIDI output '" + options.output + "': " + std::strerror(errno));
                return false;
            }
            struct stat out_info, in_info;
            if (::fstat(output_fd, &out_info) < 0) {
                fail("Cannot inspect MIDI output: " + std::string(std::strerror(errno)));
                return false;
            }
            if (S_ISREG(out_info.st_mode)) {
                if (input_fd >= 0 && ::fstat(input_fd, &in_info) == 0 &&
                    same_file(input_fd, output_fd, in_info, out_info)) {
                    fail("MIDI input and capture output must be different files");
                    return false;
                }
                if (!options.append_output) {
#ifdef _WIN32
                    const int result = _chsize(output_fd, 0);
#else
                    const int result = ::ftruncate(output_fd, 0);
#endif
                    if (result < 0) {
                        fail("Cannot truncate MIDI capture: " + std::string(std::strerror(errno)));
                        return false;
                    }
                }
            }
        }
        return input_fd >= 0 || output_fd >= 0;
    }

#ifdef HAVE_PORTMIDI
    static PmTimestamp pm_clock(void *)
    {
        return static_cast<PmTimestamp>(static_cast<std::uint32_t>(host_time_ns() / Million));
    }

    bool open_pm_streams()
    {
        if (pm_input_id != pmNoDevice) {
            const auto result = Pm_OpenInput(&pm_input, pm_input_id, nullptr, 4096, pm_clock, nullptr);
            if (result < 0) {
                pm_input = nullptr;
                fail(portmidi_error(result));
                return false;
            }
            const auto filter = Pm_SetFilter(pm_input, 0);
            if (filter < 0) {
                fail(portmidi_error(filter));
                return false;
            }
        }
        if (pm_output_id != pmNoDevice) {
            const auto result = Pm_OpenOutput(&pm_output, pm_output_id, nullptr, 4096,
                                               pm_clock, nullptr, 0);
            if (result < 0) {
                pm_output = nullptr;
                fail(portmidi_error(result));
                return false;
            }
        }
        return pm_input || pm_output;
    }

    bool open_portmidi()
    {
        auto &context = portmidi_context();
        std::unique_lock<std::mutex> lock(context.mutex, std::try_to_lock);
        if (!lock.owns_lock()) {
            fail("A MIDI driver is busy; close the previous MIDI device before reopening");
            return false;
        }
        std::string error;
        if (!portmidi_acquire(error)) {
            fail(error);
            return false;
        }
        portmidi_owned = true;
        if (!options.input.empty()) {
            pm_input_id = portmidi_device(options.input, true, error);
            if (pm_input_id == pmNoDevice) {
                fail(error.empty() ? "No default MIDI input is available" : error);
                return false;
            }
        }
        if (!options.output.empty()) {
            pm_output_id = portmidi_device(options.output, false, error);
            if (pm_output_id == pmNoDevice) {
                fail(error.empty() ? "No default MIDI output is available" : error);
                return false;
            }
        }
        return open_pm_streams();
    }

    void close_pm_streams()
    {
        if (pm_output) {
            Pm_Abort(pm_output);
            Pm_Close(pm_output);
            pm_output = nullptr;
        }
        if (pm_input) {
            Pm_Close(pm_input);
            pm_input = nullptr;
        }
    }

    bool output_pm(std::uint8_t byte)
    {
        if (!pm_output) return true;
        bool normalized, malformed;
        const bool success = packetizer.feed(byte, [this](std::uint32_t word) {
            PmEvent event;
            event.message = word;
            event.timestamp = pm_clock(nullptr);
            const auto result = Pm_Write(pm_output, &event, 1);
            if (result < 0) {
                fail(portmidi_error(result));
                return false;
            }
            return true;
        }, normalized, malformed);
        if (normalized) normalized_realtime.fetch_add(1, std::memory_order_relaxed);
        if (malformed) malformed_output.fetch_add(1, std::memory_order_relaxed);
        return success;
    }

    void input_pm_event(const PmEvent &event, std::uint32_t epoch)
    {
        const auto now = host_time_ns();
        const auto age = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(now / Million) - static_cast<std::uint32_t>(event.timestamp));
        const U64 age_ns = age > 0 ? U64(age) * Million : 0;
        const U64 timestamp = to_guest(now > age_ns ? now - age_ns : 0);
        auto message = static_cast<std::uint32_t>(event.message);
        const auto status = static_cast<std::uint8_t>(message);
        if (status >= 0xf8) {
            received(status, timestamp, epoch);
            return;
        }
        if (status == 0xf0) input_sysex = true;
        else if (status & 0x80) input_sysex = false;
        if (input_sysex) {
            for (int i = 0; i < 4; ++i) {
                const auto byte = static_cast<std::uint8_t>(message >> (8 * i));
                received(byte, timestamp, epoch);
                if (byte == 0xf7) {
                    input_sysex = false;
                    break;
                }
            }
            return;
        }
        const int length = midi_length(status);
        for (int i = 0; i < length; ++i)
            received(static_cast<std::uint8_t>(message >> (8 * i)), timestamp, epoch);
    }
#endif

    void cleanup()
    {
#ifdef HAVE_PORTMIDI
        if (portmidi_owned) {
            std::lock_guard<std::mutex> lock(portmidi_context().mutex);
            close_pm_streams();
            portmidi_release();
            portmidi_owned = false;
        }
#endif
        if (output_fd >= 0) ::close(output_fd);
        if (input_fd >= 0 && input_fd != output_fd) ::close(input_fd);
        input_fd = output_fd = -1;
    }

    ssize_t read_input_bytes(unsigned char *bytes, std::size_t capacity)
    {
#ifdef _WIN32
        if (disk_input != INVALID_HANDLE_VALUE) {
            DWORD count;
            if (ReadFile(disk_input, bytes, static_cast<DWORD>(capacity), &count, nullptr))
                return count;
            const DWORD error = GetLastError();
            if (error != ERROR_HANDLE_EOF) {
                fail("MIDI input: " + std::system_category().message(error));
                stopping.store(true, std::memory_order_release);
            }
            return 0;
        }
#endif
        const auto count = ::read(input_fd, bytes, capacity);
        if (count < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
            fail("MIDI input: " + std::string(std::strerror(errno)));
            stopping.store(true, std::memory_order_release);
        }
        return count;
    }

    void read_raw(std::uint32_t epoch)
    {
        if (input_fd < 0 || !input.free()) return;
        unsigned char bytes[64];
        const auto count = read_input_bytes(bytes, std::min<std::size_t>(sizeof(bytes), input.free()));
        if (count <= 0) return;
        const auto timestamp = to_guest(host_time_ns());
        for (ssize_t i = 0; i < count; ++i) {
            if (backend != Backend::Sequencer) {
                received(bytes[i], timestamp, epoch);
                continue;
            }
            oss_input[oss_input_count++] = bytes[i];
            const unsigned length = (oss_input[0] & 0x80) ? 8 : 4;
            if (oss_input_count == length) {
#ifdef __linux__
                if (oss_input[0] == SEQ_MIDIPUTC && oss_input[2] == options.sequencer_device)
                    received(oss_input[1], timestamp, epoch);
#endif
                oss_input_count = 0;
            }
        }
    }

    void run()
    {
#ifndef _WIN32
        sigset_t signals;
        sigemptyset(&signals);
        sigaddset(&signals, SIGPIPE);
        pthread_sigmask(SIG_BLOCK, &signals, nullptr);
#endif
        std::uint32_t epoch = generation.load(std::memory_order_acquire);
        QueueByte pending{};
        bool have_pending = false;
        std::array<unsigned char, 4> packet{};
        unsigned packet_size = 0, packet_offset = 0;
        while (!stopping.load(std::memory_order_acquire)) {
            const auto current = generation.load(std::memory_order_acquire);
            if (current != epoch) {
                epoch = current;
                have_pending = false;
                packet_size = packet_offset = oss_input_count = 0;
#ifdef HAVE_PORTMIDI
                if (backend == Backend::PortMidi) {
                    std::lock_guard<std::mutex> lock(portmidi_context().mutex);
                    close_pm_streams();
                    packetizer.reset();
                    input_sysex = false;
                    if (!open_pm_streams()) break;
                }
#endif
            }
#ifdef HAVE_PORTMIDI
            if (backend == Backend::PortMidi) {
                std::lock_guard<std::mutex> lock(portmidi_context().mutex);
                if (pm_input && input.free() >= 64) {
                    PmEvent events[16];
                    const int count = Pm_Read(pm_input, events, 16);
                    if (count == pmBufferOverflow) {
                        input_dropped.fetch_add(1, std::memory_order_relaxed);
                        input_sysex = false;
                        fail(portmidi_error(static_cast<PmError>(count)));
                    } else if (count < 0) {
                        fail(portmidi_error(static_cast<PmError>(count)));
                        break;
                    } else {
                        for (int i = 0; i < count; ++i) input_pm_event(events[i], epoch);
                    }
                }
            } else
#endif
                read_raw(epoch);
            if (!have_pending) {
                for (unsigned i = 0; i < QueueSize && output.pop(pending); ++i) {
                    if (pending.generation != epoch) continue;
                    have_pending = true;
                    packet[0] = pending.byte;
                    packet_size = 1;
                    packet_offset = 0;
#ifdef __linux__
                    if (backend == Backend::Sequencer) {
                        packet[0] = SEQ_MIDIPUTC;
                        packet[1] = pending.byte;
                        packet[2] = static_cast<unsigned char>(options.sequencer_device);
                        packet[3] = 0;
                        packet_size = 4;
                    }
#endif
                    break;
                }
            }
            U64 wait_ns = 250000;
            if (have_pending) {
                const U64 due = to_host(pending.timestamp);
                const U64 now = host_time_ns();
                if (pending.generation != generation.load(std::memory_order_acquire)) {
                    have_pending = false;
                    continue;
                }
                if (now >= due) {
                    const U64 late = now - due;
                    bool complete = true;
#ifdef HAVE_PORTMIDI
                    if (backend == Backend::PortMidi) {
                        std::lock_guard<std::mutex> lock(portmidi_context().mutex);
                        if (!output_pm(pending.byte)) break;
                    } else
#endif
                    if (output_fd >= 0) {
                        const auto count = ::write(output_fd, packet.data() + packet_offset,
                                                   packet_size - packet_offset);
                        if (count < 0) {
                            if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
                                fail("MIDI output: " + std::string(std::strerror(errno)));
                                break;
                            }
                            complete = false;
                        } else {
                            packet_offset += static_cast<unsigned>(count);
                            complete = packet_offset == packet_size;
                        }
                    }
                    have_pending = !complete;
                    if (complete) {
                        if (late) {
                            late_output.fetch_add(1, std::memory_order_relaxed);
                            if (late > max_output_lateness.load(std::memory_order_relaxed))
                                max_output_lateness.store(late, std::memory_order_relaxed);
                        }
                        continue;
                    }
                } else wait_ns = std::min(wait_ns, due - now);
            }
            std::unique_lock<std::mutex> lock(wait_mutex);
            wake.wait_for(lock, std::chrono::nanoseconds(wait_ns));
        }
        active.store(false, std::memory_order_release);
        cleanup();
        {
            std::lock_guard<std::mutex> lock(wait_mutex);
            done = true;
        }
        finished.notify_all();
    }
};
}

struct HostMidiTransport::Impl {
    std::shared_ptr<TransportState> state;
    std::thread worker;
};

HostMidiTransport::HostMidiTransport() : impl(new Impl) {}
HostMidiTransport::~HostMidiTransport() { close(); }

bool HostMidiTransport::open_endpoints(const HostMidiOptions &options, U64 now)
{
    close();
    auto state = std::make_shared<TransportState>();
    impl->state = state;
    state->options = options;
    state->synchronize(now);
    if (options.backend == "sequencer") {
#ifdef __linux__
        state->backend = Backend::Sequencer;
        if (options.sequencer_device > 255) {
            state->fail("OSS MIDI device number is out of range");
            return false;
        }
#else
        state->fail("OSS MIDI sequencer devices are only available on Linux");
        return false;
#endif
    } else if (options.backend == "portmidi") state->backend = Backend::PortMidi;
    else if (options.backend != "file" && options.backend != "raw") {
        state->fail("Unknown MIDI backend: " + options.backend);
        return false;
    }
    if (options.input.empty() && options.output.empty()) {
        state->fail("Select a MIDI input or output endpoint");
        return false;
    }
    bool opened = false;
    if (state->backend == Backend::PortMidi) {
#ifdef HAVE_PORTMIDI
        opened = state->open_portmidi();
#else
        state->fail("This build does not include PortMidi device support");
#endif
    } else opened = state->open_raw();
    if (!opened) {
        state->cleanup();
        return false;
    }
    state->active.store(true, std::memory_order_release);
    try {
        impl->worker = std::thread([state] { state->run(); });
    } catch (...) {
        state->active.store(false, std::memory_order_release);
        state->cleanup();
        state->fail("Cannot start the MIDI I/O worker");
        return false;
    }
    return true;
}

void HostMidiTransport::close()
{
    auto state = impl->state;
    if (!state || !impl->worker.joinable()) return;
    state->active.store(false, std::memory_order_release);
    state->stopping.store(true, std::memory_order_release);
    state->wake.notify_all();
    std::unique_lock<std::mutex> lock(state->wait_mutex);
    const bool done = state->finished.wait_for(lock, std::chrono::milliseconds(200),
                                              [state] { return state->done; });
    lock.unlock();
    if (done) impl->worker.join();
    else {
        /* The shared state stays alive until a stalled host driver returns. */
        impl->worker.detach();
        state->fail("MIDI driver did not close promptly; cleanup is deferred");
    }
}

void HostMidiTransport::reset(U64 now)
{
    if (!impl->state) return;
    impl->state->generation.fetch_add(1, std::memory_order_acq_rel);
    impl->state->synchronize(now);
    impl->state->wake.notify_all();
}

void HostMidiTransport::service(U64 now)
{
    if (impl->state) impl->state->synchronize(now);
}

bool HostMidiTransport::submit_output(std::uint8_t byte, U64 timestamp)
{
    auto *state = impl->state.get();
    if (!state || !state->active.load(std::memory_order_acquire)) return false;
    if (!state->output.push(QueueByte{byte, timestamp,
            state->generation.load(std::memory_order_acquire)})) {
        state->output_dropped.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    state->wake.notify_all();
    return true;
}

bool HostMidiTransport::poll_input(HostMidiByte &event)
{
    auto *state = impl->state.get();
    if (!state) return false;
    QueueByte next;
    const auto epoch = state->generation.load(std::memory_order_acquire);
    for (unsigned i = 0; i < QueueSize && state->input.pop(next); ++i) {
        if (next.generation != epoch) continue;
        event = HostMidiByte{next.byte, next.timestamp};
        return true;
    }
    return false;
}

bool HostMidiTransport::active() const
{
    return impl->state && impl->state->active.load(std::memory_order_acquire);
}

bool HostMidiTransport::preserves_raw_bytes() const
{
    return impl->state && impl->state->backend != Backend::PortMidi;
}

HostMidiStats HostMidiTransport::stats() const
{
    if (!impl->state) return HostMidiStats{};
    const auto &state = *impl->state;
    return HostMidiStats{state.input_dropped.load(), state.output_dropped.load(),
        state.late_output.load(), state.io_errors.load(), state.malformed_output.load(),
        state.normalized_realtime.load(), state.max_output_lateness.load()};
}

std::string HostMidiTransport::error() const
{
    if (!impl->state) return {};
    std::lock_guard<std::mutex> lock(impl->state->error_mutex);
    return impl->state->error_text;
}

bool HostMidiTransport::supports_portmidi()
{
#ifdef HAVE_PORTMIDI
    return true;
#else
    return false;
#endif
}

std::vector<HostMidiDevice> HostMidiTransport::devices(std::string &error)
{
    std::vector<HostMidiDevice> devices;
    error.clear();
#ifdef HAVE_PORTMIDI
    auto &context = portmidi_context();
    std::unique_lock<std::mutex> lock(context.mutex, std::try_to_lock);
    if (!lock.owns_lock()) {
        error = "A MIDI driver is busy";
        return devices;
    }
    if (!portmidi_acquire(error)) return devices;
    for (PmDeviceID id = 0; id < Pm_CountDevices(); ++id) {
        const auto *info = Pm_GetDeviceInfo(id);
        if (!info) continue;
        devices.push_back(HostMidiDevice{info->name ? info->name : "",
            info->interf ? info->interf : "", info->input != 0, info->output != 0});
    }
    portmidi_release();
#else
    error = "This build does not include PortMidi device support";
#endif
    return devices;
}
