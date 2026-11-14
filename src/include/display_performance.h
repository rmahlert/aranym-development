// SPDX-License-Identifier: GPL-2.0-or-later
// Opt-in host-only timing. Never reads guest memory or changes refresh policy.
#ifndef ARANYM_DISPLAY_PERFORMANCE_H
#define ARANYM_DISPLAY_PERFORMANCE_H
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "SDL_compat.h"
inline bool aranym_display_perf_enabled()
{
    static const bool enabled = []() {
        const char *value = std::getenv("ARANYM_DISPLAY_PERFORMANCE");
        return value && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}
inline Uint64 aranym_display_perf_clock()
{
#if SDL_VERSION_ATLEAST(2, 0, 0)
    static const Uint64 frequency = SDL_GetPerformanceFrequency();
    const Uint64 ticks = SDL_GetPerformanceCounter();
    return (ticks / frequency) * 1000000 + (ticks % frequency) * 1000000 / frequency;
#else
    return Uint64(SDL_GetTicks()) * 1000;
#endif
}
struct AranymDisplayPerfTotals {
    Uint64 calls, elapsed, blocks, changed, converted, rects, pixels, update, render;
    Uint64 last_report, reports, max_rects;
    AranymDisplayPerfTotals() : calls(0), elapsed(0), blocks(0), changed(0),
        converted(0), rects(0), pixels(0), update(0), render(0), last_report(0),
        reports(0), max_rects(0) {}
};
struct AranymDisplayPerfState {
    Uint64 origin;
    unsigned milestones;
    AranymDisplayPerfTotals groups[4];
    AranymDisplayPerfState() : origin(aranym_display_perf_clock()), milestones(0) {}
};
inline AranymDisplayPerfState &aranym_display_perf_state()
{
    static AranymDisplayPerfState state;
    return state;
}
inline void aranym_display_perf_milestone(const char *name, int a=0, int b=0)
{
    if (!aranym_display_perf_enabled()) return;
    AranymDisplayPerfState &s = aranym_display_perf_state();
    if (s.milestones++ >= 32) return;
    std::fprintf(stderr,"DISPLAY_PERF milestone=%s t_us=%llu a=%d b=%d\n",name,
        (unsigned long long)(aranym_display_perf_clock()-s.origin),a,b);
}
inline void aranym_display_perf_finish()
{
    if (!aranym_display_perf_enabled()) return;
    AranymDisplayPerfState &s=aranym_display_perf_state();
    const char *names[]={"frame","videl","compose","present"};
    for (unsigned i=0;i<4;++i) {
        const AranymDisplayPerfTotals &t=s.groups[i];
        std::fprintf(stderr,"DISPLAY_PERF_FINAL group=%s t_us=%llu calls=%llu us=%llu blocks=%llu changed=%llu converted=%llu rects=%llu pixels=%llu update_us=%llu render_us=%llu max_rects=%llu\n",names[i],
            (unsigned long long)(aranym_display_perf_clock()-s.origin),
            (unsigned long long)t.calls,(unsigned long long)t.elapsed,
            (unsigned long long)t.blocks,(unsigned long long)t.changed,
            (unsigned long long)t.converted,(unsigned long long)t.rects,
            (unsigned long long)t.pixels,(unsigned long long)t.update,
            (unsigned long long)t.render,(unsigned long long)t.max_rects);
    }
}
class AranymDisplayPerfScope {
    unsigned group;
    const char *name;
    Uint64 started;
public:
    bool active;
    Uint64 blocks, changed, converted, rects, pixels, update, render;
    int width, height, single, min_x, max_x, min_y, max_y;
    AranymDisplayPerfScope(unsigned g, const char *n) : group(g),name(n),started(0),
        active(aranym_display_perf_enabled()), blocks(0),changed(0),converted(0),
        rects(0),pixels(0),update(0),render(0),width(0),height(0),single(0),
        min_x(-1),max_x(-1),min_y(-1),max_y(-1)
    {
        if (active) { aranym_display_perf_state(); started=aranym_display_perf_clock(); }
    }
    void rectangle(int x,int y,int w,int h)
    {
        if (!active) return;
        ++rects; if (w>0 && h>0) pixels+=Uint64(w)*Uint64(h);
        if (min_x<0 || x<min_x) min_x=x;
        if (min_y<0 || y<min_y) min_y=y;
        if (max_x<x+w) max_x=x+w;
        if (max_y<y+h) max_y=y+h;
    }
    ~AranymDisplayPerfScope()
    {
        if (!active) return;
        const Uint64 now=aranym_display_perf_clock();
        AranymDisplayPerfState &s=aranym_display_perf_state();
        AranymDisplayPerfTotals &t=s.groups[group];
        ++t.calls; t.elapsed+=now-started; t.blocks+=blocks; t.changed+=changed;
        t.converted+=converted; t.rects+=rects; t.pixels+=pixels;
        t.update+=update; t.render+=render;
        if (rects>t.max_rects) t.max_rects=rects;
        if (t.reports>=120 || (t.calls>4 && now-t.last_report<1000000)) return;
        ++t.reports; t.last_report=now;
        std::fprintf(stderr,"DISPLAY_PERF group=%s t_us=%llu calls=%llu us=%llu blocks=%llu changed=%llu converted=%llu rects=%llu pixels=%llu update_us=%llu render_us=%llu max_rects=%llu last_rects=%llu bbox=%d,%d,%d,%d mode=%dx%d single=%d\n",name,
            (unsigned long long)(now-s.origin),(unsigned long long)t.calls,
            (unsigned long long)t.elapsed,(unsigned long long)t.blocks,
            (unsigned long long)t.changed,(unsigned long long)t.converted,
            (unsigned long long)t.rects,(unsigned long long)t.pixels,
            (unsigned long long)t.update,(unsigned long long)t.render,
            (unsigned long long)t.max_rects,(unsigned long long)rects,
            min_x,min_y,max_x,max_y,width,height,single);
    }
};
#endif
