/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "sysdeps.h"
#include "peripheral_clock.h"
#include "hardware.h"
#include "cpu_emulation.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <thread>

namespace {
using Clock = std::chrono::steady_clock;
constexpr auto never = std::numeric_limits<std::uint64_t>::max();
std::uint64_t host_time(void *)
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now().time_since_epoch()).count();
}
PeripheralClockSource clock_source = host_time;
void *clock_opaque;
std::uint64_t origin = 0, elapsed = 0, source_base = 0;
std::uint64_t service_time = 0, last_service_time = 0;
bool initialized = false, servicing = false;
std::mutex wake_mutex;
std::condition_variable wake_condition;
std::thread wake_thread;
Clock::time_point wake_deadline = Clock::time_point::max();
std::uint64_t wake_generation = 0;
bool stopping = false;
std::uint64_t scheduled_guest_deadline = never;
bool schedule_valid = false;
bool cleanup_registered = false;

void schedule_wakeup(std::uint64_t deadline, std::uint64_t now)
{
    if (schedule_valid && deadline == scheduled_guest_deadline && deadline > now)
        return;
    scheduled_guest_deadline = deadline;
    schedule_valid = true;
    const auto host_deadline = deadline == never || clock_source != host_time ?
        Clock::time_point::max() : Clock::time_point(std::chrono::duration_cast<Clock::duration>(
        std::chrono::nanoseconds(origin + (deadline > source_base ? deadline - source_base : 0))));
    std::lock_guard<std::mutex> lock(wake_mutex);
    wake_deadline = host_deadline;
    ++wake_generation;
    wake_condition.notify_one();
}

void wake_loop()
{
    std::unique_lock<std::mutex> lock(wake_mutex);
    while (!stopping) {
        const auto generation = wake_generation;
        if (wake_deadline == Clock::time_point::max()) {
            wake_condition.wait(lock, [&] { return stopping || generation != wake_generation; });
        } else if (!wake_condition.wait_until(lock, wake_deadline,
                   [&] { return stopping || generation != wake_generation; })) {
            wake_deadline = Clock::time_point::max();
            lock.unlock();
            TriggerInternalIRQ();
            lock.lock();
        }
    }
}
}

unsigned peripheral_instruction_budget = 64;

std::uint64_t peripheral_clock_now_ns()
{
    if (servicing)
        return service_time;
    const auto sample = clock_source(clock_opaque);
    if (!initialized) {
        origin = sample;
        initialized = true;
    }
    if (sample >= origin)
        elapsed = std::max(elapsed, source_base + sample - origin);
    return elapsed;
}

std::uint64_t peripheral_clock_last_event_ns()
{
    return servicing ? service_time : last_service_time;
}

std::uint64_t peripheral_clock_event_time_ns()
{
    if (servicing) return service_time;
    peripheral_clock_service();
    return last_service_time;
}

void peripheral_clock_reset()
{
    origin = clock_source(clock_opaque);
    source_base = elapsed = 0;
    service_time = last_service_time = 0;
    initialized = true;
    peripheral_instruction_budget = 64;
    schedule_valid = false;
    schedule_wakeup(never, 0);
}

void peripheral_clock_set_source(PeripheralClockSource source, void *opaque)
{
    const auto now = peripheral_clock_now_ns();
    clock_source = source ? source : host_time;
    clock_opaque = opaque;
    origin = clock_source(clock_opaque);
    source_base = elapsed = now;
    initialized = true;
    schedule_valid = false;
    schedule_wakeup(never, now);
}

namespace {
std::uint64_t next_device_deadline()
{
    auto deadline = never;
    if (getMFP()) deadline = std::min(deadline, getMFP()->next_deadline_ns());
    if (getMIDI()) deadline = std::min(deadline, getMIDI()->next_deadline_ns());
    if (getDSP()) deadline = std::min(deadline, getDSP()->next_deadline_ns());
    if (getCROSSBAR()) deadline = std::min(deadline, getCROSSBAR()->next_deadline_ns());
    return deadline;
}
}

void peripheral_clock_service()
{
    if (servicing)
        return;
    const auto target = peripheral_clock_now_ns();
    if (getMIDI()) getMIDI()->poll_host(target);
    servicing = true;
    for (;;) {
        service_time = std::max(last_service_time, std::min(target, next_device_deadline()));
        if (getCROSSBAR()) getCROSSBAR()->service(service_time);
        if (getMFP()) getMFP()->service(service_time);
        if (getMIDI()) getMIDI()->service(service_time, false);
        if (getDSP()) getDSP()->service(service_time);
        last_service_time = service_time;
        if (service_time >= target)
            break;
    }
    auto deadline = never;
    if (getMFP()) deadline = std::min(deadline, getMFP()->next_deadline_ns());
    if (getMIDI()) deadline = std::min(deadline, getMIDI()->next_deadline_ns());
    if (getDSP()) deadline = std::min(deadline, getDSP()->next_deadline_ns());
    if (getCROSSBAR()) {
        const auto batch = target + std::min<std::uint64_t>(500000, never - target);
        deadline = std::min(deadline, std::max(batch, getCROSSBAR()->next_deadline_ns()));
    }
    servicing = false;
    schedule_wakeup(deadline, target);
}

void peripheral_clock_start()
{
    if (wake_thread.joinable())
        return;
    {
        std::lock_guard<std::mutex> lock(wake_mutex);
        stopping = false;
    }
    wake_thread = std::thread(wake_loop);
    if (!cleanup_registered) {
        std::atexit(peripheral_clock_shutdown);
        cleanup_registered = true;
    }
    peripheral_clock_service();
}

void peripheral_clock_shutdown()
{
    {
        std::lock_guard<std::mutex> lock(wake_mutex);
        stopping = true;
        wake_condition.notify_one();
    }
    if (wake_thread.joinable())
        wake_thread.join();
}
