/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "mfp_timer.h"
#include <limits>

namespace {
constexpr std::uint64_t clock_hz = 2457600;
constexpr std::uint64_t second_ns = 1000000000;
constexpr unsigned divisors[] = {0, 4, 10, 16, 50, 64, 100, 200};
}

void MFP_Timer::reset(std::uint64_t now_ns)
{
    control_ = reload_ = 0;
    counter_ = 256;
    output_ = input_active_ = false;
    last_ns_ = now_ns;
    fraction_ = prescale_phase_ = 0;
}

unsigned MFP_Timer::divisor() const
{
    return divisors[control_ & 7];
}

void MFP_Timer::setControl(std::uint8_t value)
{
    const auto next = std::uint8_t(value & 15);
    if (next != control_)
        fraction_ = prescale_phase_ = 0;
    control_ = next;
    if (value & 16)
        output_ = false;
}

void MFP_Timer::setData(std::uint8_t value)
{
    reload_ = value;
    if (!isRunning())
        counter_ = value ? value : 256;
}

std::uint64_t MFP_Timer::consume(std::uint64_t ticks)
{
    if (ticks < counter_) {
        counter_ -= unsigned(ticks);
        return 0;
    }
    const unsigned reload = reload_ ? reload_ : 256;
    const auto remaining = ticks - counter_;
    const auto events = 1 + remaining / reload;
    counter_ = std::uint16_t(reload - remaining % reload);
    if (events & 1)
        output_ = !output_;
    return events;
}

std::uint64_t MFP_Timer::advance_to(std::uint64_t now_ns)
{
    if (now_ns <= last_ns_)
        return 0;
    const auto elapsed = now_ns - last_ns_;
    last_ns_ = now_ns;
    const unsigned prescale = divisor();
    if (!prescale || (control_ >= 9 && !input_active_))
        return 0;
    const auto partial = (elapsed % second_ns) * clock_hz + fraction_;
    auto clocks = (elapsed / second_ns) * clock_hz + partial / second_ns;
    fraction_ = partial % second_ns;
    clocks += prescale_phase_;
    prescale_phase_ = unsigned(clocks % prescale);
    return consume(clocks / prescale);
}

std::uint64_t MFP_Timer::next_deadline_ns() const
{
    const auto never = std::numeric_limits<std::uint64_t>::max();
    const unsigned prescale = divisor();
    if (!prescale || (control_ >= 9 && !input_active_))
        return never;
    const auto clocks = std::uint64_t(counter_) * prescale - prescale_phase_;
    const auto numerator = clocks * second_ns - fraction_;
    const auto delay = (numerator + clock_hz - 1) / clock_hz;
    return delay > never - last_ns_ ? never : last_ns_ + delay;
}

std::uint64_t MFP_Timer::set_input(bool active)
{
    const bool rising = active && !input_active_;
    input_active_ = active;
    return control_ == 8 && rising ? consume(1) : 0;
}

int MFP_Timer::compute_timer_freq() const
{
    const unsigned prescale = divisor();
    return prescale ? int(clock_hz / (prescale * (reload_ ? reload_ : 256))) : 0;
}
