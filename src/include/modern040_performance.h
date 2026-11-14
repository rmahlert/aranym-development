// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef ARANYM_MODERN040_PERFORMANCE_H
#define ARANYM_MODERN040_PERFORMANCE_H
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "display_performance.h"
inline bool aranym_modern_perf_enabled(){static const bool enabled=[](){const char *p=std::getenv("ARANYM_MODERN040_PERFORMANCE");return p&&std::strcmp(p,"1")==0;}();return enabled;}
struct AranymModernPerf {
    Uint64 calls[6]{},samples[6]{},elapsed[6]{},memory[3]{},services[6]{},events[10]{},opcodes[65536]{};
    Uint64 interpreter=0,fallback=0,get_state=0,set_state=0;
    unsigned random=0x6d2b79f5;bool sampling=false;
    ~AranymModernPerf(){
        if(!aranym_modern_perf_enabled())return;
        std::fprintf(stderr,"MODERN040_PERF interpreter=%llu fallback=%llu get_state=%llu set_state=%llu fetch=%llu read=%llu write=%llu\n",(unsigned long long)interpreter,(unsigned long long)fallback,(unsigned long long)get_state,(unsigned long long)set_state,(unsigned long long)memory[0],(unsigned long long)memory[1],(unsigned long long)memory[2]);
        const char*names[]={"import","run","export","memory","service","ticks"};
        for(unsigned i=0;i<6;++i)std::fprintf(stderr,"MODERN040_PERF_GROUP group=%s calls=%llu samples=%llu sampled_us=%llu\n",names[i],(unsigned long long)calls[i],(unsigned long long)samples[i],(unsigned long long)elapsed[i]);
        const char*eventnames[]={"compile_block","recompile","cache_miss","invalidate_block","flush_hard","flush_lazy","flush_none","service_exception","fallback_end_compile","memory_barrier"};
        for(unsigned i=0;i<10;++i)std::fprintf(stderr,"MODERN040_PERF_EVENT event=%s count=%llu\n",eventnames[i],(unsigned long long)events[i]);
        for(unsigned i=1;i<6;++i)std::fprintf(stderr,"MODERN040_PERF_SERVICE kind=%u count=%llu\n",i,(unsigned long long)services[i]);
        for(unsigned i=0;i<16;++i){unsigned best=0;for(unsigned j=1;j<65536;++j)if(opcodes[j]>opcodes[best])best=j;if(!opcodes[best])break;std::fprintf(stderr,"MODERN040_PERF_OPCODE opcode=%04x count=%llu\n",best,(unsigned long long)opcodes[best]);opcodes[best]=0;}
    }
};
inline AranymModernPerf &aranym_modern_perf(){static AranymModernPerf p;return p;}
inline void aranym_modern_perf_event(unsigned event){if(aranym_modern_perf_enabled())++aranym_modern_perf().events[event];}
class AranymModernPerfStep {
    bool active,previous=false;
public:
    explicit AranymModernPerfStep(bool jit,unsigned opcode=0):active(aranym_modern_perf_enabled()){
        if(!active)return;auto &p=aranym_modern_perf();previous=p.sampling;
        p.random=p.random*1664525u+1013904223u;p.sampling=(p.random>>20)==0;
        if(jit){++p.fallback;++p.opcodes[opcode&65535];}else ++p.interpreter;
    }
    ~AranymModernPerfStep(){if(active)aranym_modern_perf().sampling=previous;}
};
class AranymModernPerfScope {
    unsigned group;bool active,sampled=false;Uint64 start=0;
public:
    explicit AranymModernPerfScope(unsigned g):group(g),active(aranym_modern_perf_enabled()){
        if(active){auto &p=aranym_modern_perf();++p.calls[g];sampled=p.sampling;if(sampled)start=aranym_display_perf_clock();}}
    ~AranymModernPerfScope(){if(sampled){auto &p=aranym_modern_perf();++p.samples[group];p.elapsed[group]+=aranym_display_perf_clock()-start;}}
};
#endif
